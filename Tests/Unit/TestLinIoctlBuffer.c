// #*#*#*#*#*#*#*#*#*#*#*#* TestLinIoctlBuffer.c *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Unit tests for the Linux IOCTL in/out buffer layout
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Runs on every platform. The layout is the part of the Linux backend most likely to be
// wrong, and these tests check it where no Linux driver is at hand, on Windows and in
// the emulator's builds.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h> // SIZE_MAX.

// CDTAPI includes
#include "DtTest.h"                   // Test framework.
#include "OAL/Linux/LinIoctlBuffer.h" // Interface under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Sizing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(SizeWithoutHeaderIsTheLargerSide)
{
    DT_ASSERT_EQ(LinIoctlBuffer_Size(false, 16, 4), 16);
    DT_ASSERT_EQ(LinIoctlBuffer_Size(false, 4, 104), 104);
}

DT_TEST(HeaderCountsAgainstTheInputSide)
{
    DT_ASSERT_EQ(LIN_IOCTL_SIZE_HEADER_BYTES, 8);

    // Input 16 plus header 8 is 24, which beats an output of 16.
    DT_ASSERT_EQ(LinIoctlBuffer_Size(true, 16, 16), 24);

    // Output 104 still beats input 16 plus header 8.
    DT_ASSERT_EQ(LinIoctlBuffer_Size(true, 16, 104), 104);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Packing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The layout the driver expects: InSize, OutSize, then the input. Getting this shifted
// by even one field makes the driver read the wrong command rather than refuse it.
DT_TEST(HeaderPrecedesInput)
{
    uint8_t In[16];

    for (size_t i = 0; i < sizeof(In); i++)
        In[i] = (uint8_t)(0xA0 + i);

    uint8_t Buf[32];
    DT_ASSERT_OK(LinIoctlBuffer_Pack(true, In, sizeof(In), 16, Buf, sizeof(Buf)));

    uint32_t Sizes[2];
    memcpy(Sizes, Buf, sizeof(Sizes));
    DT_ASSERT_EQ(Sizes[0], 16);
    DT_ASSERT_EQ(Sizes[1], 16);
    DT_ASSERT_MEM(Buf + 8, In, sizeof(In));
}

DT_TEST(WithoutHeaderInputStartsAtZero)
{
    uint8_t In[4] = {1, 2, 3, 4};
    uint8_t Buf[8];

    DT_ASSERT_OK(LinIoctlBuffer_Pack(false, In, sizeof(In), 8, Buf, sizeof(Buf)));
    DT_ASSERT_MEM(Buf, In, sizeof(In));
}

// The driver may read past the input it was given, up to the structure size encoded in
// the IOCTL number. Whatever it finds there must be zero, not the previous command.
DT_TEST(RestOfTheBufferIsCleared)
{
    uint8_t In[4] = {9, 9, 9, 9};
    uint8_t Buf[64];

    memset(Buf, 0xEE, sizeof(Buf));
    DT_ASSERT_OK(LinIoctlBuffer_Pack(true, In, sizeof(In), 64, Buf, sizeof(Buf)));

    for (size_t i = 8 + sizeof(In); i < sizeof(Buf); i++)
        DT_ASSERT_EQ(Buf[i], 0);
}

DT_TEST(PackRefusesBadArguments)
{
    uint8_t In[16];

    memset(In, 0, sizeof(In));

    // Too small for header plus input, no buffer, and no input for a size.
    uint8_t Small[8];
    DT_ASSERT_EQ(LinIoctlBuffer_Pack(true, In, sizeof(In), 0, Small, sizeof(Small)), -1);
    DT_ASSERT_EQ(LinIoctlBuffer_Pack(true, In, sizeof(In), 0, NULL, 32), -1);
    uint8_t Buf[32];
    DT_ASSERT_EQ(LinIoctlBuffer_Pack(true, NULL, 4, 0, Buf, sizeof(Buf)), -1);

    // No input at all is allowed.
    DT_ASSERT_OK(LinIoctlBuffer_Pack(false, NULL, 0, 4, Buf, sizeof(Buf)));
}

// The header holds 32-bit sizes. A larger size cannot be represented and must be refused
// rather than silently truncated into a smaller one the driver would then trust.
//
// BufSize is deliberately larger than the real buffer. That is safe only because the
// size check runs before anything is written, and that ordering is what this checks.
DT_TEST(SizeBeyondThirtyTwoBitsIsRefused)
{
#if SIZE_MAX > UINT32_MAX
    uint8_t Buf[16];
    DT_ASSERT_EQ(
        LinIoctlBuffer_Pack(true, NULL, 0, (size_t)UINT32_MAX + 1, Buf, (size_t)-1), -1);
#else
    (void)DtFailures; // A size_t of 32 bits cannot hold such a size.
#endif
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Unpacking +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The driver writes its answer from offset zero, over the size header, not after it.
// Reading from after the header is the natural mistake, so this checks the other way.
DT_TEST(AnswerIsReadFromOffsetZero)
{
    uint8_t In[16];

    memset(In, 0x11, sizeof(In));
    uint8_t Buf[24];
    uint8_t Out[16];
    DT_ASSERT_OK(
        LinIoctlBuffer_Pack(true, In, sizeof(In), sizeof(Out), Buf, sizeof(Buf)));

    // What the driver does: overwrite the start of the block with its answer.
    for (size_t i = 0; i < sizeof(Out); i++)
        Buf[i] = (uint8_t)(0x50 + i);

    DT_ASSERT_OK(LinIoctlBuffer_Unpack(Buf, sizeof(Buf), Out, sizeof(Out)));
    DT_ASSERT_EQ(Out[0], 0x50);
    DT_ASSERT_EQ(Out[15], 0x5F);
}

DT_TEST(UnpackRefusesBadArguments)
{
    uint8_t Buf[8];

    memset(Buf, 0, sizeof(Buf));

    uint8_t Out[16];
    DT_ASSERT_EQ(LinIoctlBuffer_Unpack(Buf, sizeof(Buf), Out, sizeof(Out)), -1);
    DT_ASSERT_EQ(LinIoctlBuffer_Unpack(NULL, 8, Out, 4), -1);
    DT_ASSERT_EQ(LinIoctlBuffer_Unpack(Buf, sizeof(Buf), NULL, 4), -1);

    // Nothing to copy is allowed.
    DT_ASSERT_OK(LinIoctlBuffer_Unpack(Buf, sizeof(Buf), NULL, 0));
}

DT_TEST_MAIN("LinIoctlBuffer", DT_RUN(SizeWithoutHeaderIsTheLargerSide),
             DT_RUN(HeaderCountsAgainstTheInputSide), DT_RUN(HeaderPrecedesInput),
             DT_RUN(WithoutHeaderInputStartsAtZero), DT_RUN(RestOfTheBufferIsCleared),
             DT_RUN(PackRefusesBadArguments), DT_RUN(SizeBeyondThirtyTwoBitsIsRefused),
             DT_RUN(AnswerIsReadFromOffsetZero), DT_RUN(UnpackRefusesBadArguments))
