// #*#*#*#*#*#*#*#*#*#*#*#* TestLinIoctlBuffer.c *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Unit tests for the Linux IOCTL in/out buffer layout
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Runs on every platform. The layout is the part of the Linux backend most likely to be
// wrong, and these tests are the only part of that backend exercised until a Linux
// machine is available.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "DtlTest.h"                  // Test framework.
#include "OAL/Linux/LinIoctlBuffer.h" // Interface under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Sizing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DTL_TEST(SizeWithoutHeaderIsTheLargerSide)
{
    DTL_ASSERT_EQ(LinIoctlBufferSize(false, 16, 4), 16);
    DTL_ASSERT_EQ(LinIoctlBufferSize(false, 4, 104), 104);
}

DTL_TEST(HeaderCountsAgainstTheInputSide)
{
    DTL_ASSERT_EQ(LIN_IOCTL_SIZE_HEADER_BYTES, 8);

    // Input 16 plus header 8 is 24, which beats an output of 16.
    DTL_ASSERT_EQ(LinIoctlBufferSize(true, 16, 16), 24);

    // Output 104 still beats input 16 plus header 8.
    DTL_ASSERT_EQ(LinIoctlBufferSize(true, 16, 104), 104);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Packing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The layout the driver expects: InSize, OutSize, then the input. Getting this shifted
// by even one field makes the driver read the wrong command rather than refuse it.
DTL_TEST(HeaderPrecedesInput)
{
    uint8_t In[16];
    uint8_t Buf[32];
    uint32_t Sizes[2];
    size_t i;

    for (i = 0; i < sizeof(In); i++)
        In[i] = (uint8_t)(0xA0 + i);

    DTL_ASSERT_OK(LinIoctlPack(true, In, sizeof(In), 16, Buf, sizeof(Buf)));

    memcpy(Sizes, Buf, sizeof(Sizes));
    DTL_ASSERT_EQ(Sizes[0], 16);
    DTL_ASSERT_EQ(Sizes[1], 16);
    DTL_ASSERT_MEM(Buf + 8, In, sizeof(In));
}

DTL_TEST(WithoutHeaderInputStartsAtZero)
{
    uint8_t In[4] = {1, 2, 3, 4};
    uint8_t Buf[8];

    DTL_ASSERT_OK(LinIoctlPack(false, In, sizeof(In), 8, Buf, sizeof(Buf)));
    DTL_ASSERT_MEM(Buf, In, sizeof(In));
}

// The driver may read past the input it was given, up to the structure size encoded in
// the IOCTL number. Whatever it finds there must be zero, not the previous command.
DTL_TEST(RestOfTheBufferIsCleared)
{
    uint8_t In[4] = {9, 9, 9, 9};
    uint8_t Buf[64];
    size_t i;

    memset(Buf, 0xEE, sizeof(Buf));
    DTL_ASSERT_OK(LinIoctlPack(true, In, sizeof(In), 64, Buf, sizeof(Buf)));

    for (i = 8 + sizeof(In); i < sizeof(Buf); i++)
        DTL_ASSERT_EQ(Buf[i], 0);
}

DTL_TEST(PackRefusesBadArguments)
{
    uint8_t In[16];
    uint8_t Small[8];
    uint8_t Buf[32];

    memset(In, 0, sizeof(In));

    // Too small for header plus input.
    DTL_ASSERT_EQ(LinIoctlPack(true, In, sizeof(In), 0, Small, sizeof(Small)), -1);
    DTL_ASSERT_EQ(LinIoctlPack(true, In, sizeof(In), 0, NULL, 32), -1);
    DTL_ASSERT_EQ(LinIoctlPack(true, NULL, 4, 0, Buf, sizeof(Buf)), -1);

    // No input at all is allowed.
    DTL_ASSERT_OK(LinIoctlPack(false, NULL, 0, 4, Buf, sizeof(Buf)));
}

// The header holds 32-bit sizes. A larger size cannot be represented and must be refused
// rather than silently truncated into a smaller one the driver would then trust.
//
// BufSize is deliberately larger than the real buffer. That is safe only because the
// size check runs before anything is written, and that ordering is what this checks.
DTL_TEST(SizeBeyondThirtyTwoBitsIsRefused)
{
    uint8_t Buf[16];

    if (sizeof(size_t) <= sizeof(uint32_t))
        return;

    DTL_ASSERT_EQ(LinIoctlPack(true, NULL, 0, (size_t)UINT32_MAX + 1, Buf, (size_t)-1),
                  -1);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Unpacking +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The driver writes its answer from offset zero, over the size header, not after it.
// Reading from after the header is the natural mistake, so this checks the other way.
DTL_TEST(AnswerIsReadFromOffsetZero)
{
    uint8_t In[16];
    uint8_t Buf[24];
    uint8_t Out[16];
    size_t i;

    memset(In, 0x11, sizeof(In));
    DTL_ASSERT_OK(LinIoctlPack(true, In, sizeof(In), sizeof(Out), Buf, sizeof(Buf)));

    // What the driver does: overwrite the start of the block with its answer.
    for (i = 0; i < sizeof(Out); i++)
        Buf[i] = (uint8_t)(0x50 + i);

    DTL_ASSERT_OK(LinIoctlUnpack(Buf, sizeof(Buf), Out, sizeof(Out)));
    DTL_ASSERT_EQ(Out[0], 0x50);
    DTL_ASSERT_EQ(Out[15], 0x5F);
}

DTL_TEST(UnpackRefusesBadArguments)
{
    uint8_t Buf[8];
    uint8_t Out[16];

    memset(Buf, 0, sizeof(Buf));

    DTL_ASSERT_EQ(LinIoctlUnpack(Buf, sizeof(Buf), Out, sizeof(Out)), -1);
    DTL_ASSERT_EQ(LinIoctlUnpack(NULL, 8, Out, 4), -1);
    DTL_ASSERT_EQ(LinIoctlUnpack(Buf, sizeof(Buf), NULL, 4), -1);

    // Nothing to copy is allowed.
    DTL_ASSERT_OK(LinIoctlUnpack(Buf, sizeof(Buf), NULL, 0));
}

DTL_TEST_MAIN("LinIoctlBuffer", DTL_RUN(SizeWithoutHeaderIsTheLargerSide),
              DTL_RUN(HeaderCountsAgainstTheInputSide), DTL_RUN(HeaderPrecedesInput),
              DTL_RUN(WithoutHeaderInputStartsAtZero), DTL_RUN(RestOfTheBufferIsCleared),
              DTL_RUN(PackRefusesBadArguments), DTL_RUN(SizeBeyondThirtyTwoBitsIsRefused),
              DTL_RUN(AnswerIsReadFromOffsetZero), DTL_RUN(UnpackRefusesBadArguments))
