// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* TestBuf.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Unit tests for the reference-counted buffer
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h" // Allocation fault injection.
#include "Core/DtBuf.h"   // Interface under test.
#include "DtTest.h"       // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test double +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Records what the release callback was given, so that a test can check it ran exactly
// once and with the right arguments.
//

typedef struct ReleaseRecord
{
    int Calls;
    uint8_t* Data;
    size_t Size;
    void* Context;
} ReleaseRecord;

static ReleaseRecord g_Record;

static void RecordingRelease(void* Context, uint8_t* Data, size_t Size)
{
    g_Record.Calls++;
    g_Record.Data = Data;
    g_Record.Size = Size;
    g_Record.Context = Context;
}

static void ResetRecord(void)
{
    memset(&g_Record, 0, sizeof(g_Record));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(AllocGivesOneReference)
{
    DtBuf* Buf = DtBuf_Alloc(64);

    DT_ASSERT(Buf != NULL);
    DT_ASSERT(DtBuf_Data(Buf) != NULL);
    DT_ASSERT_EQ(DtBuf_Size(Buf), 64);
    DT_ASSERT_EQ(DtBuf_RefCount(Buf), 1);

    DtBuf_Unref(&Buf);
    DT_ASSERT(Buf == NULL);
}

DT_TEST(AllocRejectsZeroSize)
{
    DT_ASSERT(DtBuf_Alloc(0) == NULL);
}

DT_TEST(AllocatedBytesAreWritable)
{
    DtBuf* Buf = DtBuf_Alloc(16);

    DT_ASSERT(Buf != NULL);
    uint8_t* Data = DtBuf_Data(Buf);
    memset(Data, 0xA5, 16);
    DT_ASSERT_EQ(Data[0], 0xA5);
    DT_ASSERT_EQ(Data[15], 0xA5);

    DtBuf_Unref(&Buf);
}

DT_TEST(RefCountRisesAndFalls)
{
    DtBuf* Buf = DtBuf_Alloc(8);

    DT_ASSERT(Buf != NULL);

    DtBuf* Second = DtBuf_Ref(Buf);
    DT_ASSERT(Second == Buf);
    DT_ASSERT_EQ(DtBuf_RefCount(Buf), 2);

    DtBuf_Unref(&Second);
    DT_ASSERT(Second == NULL);
    DT_ASSERT_EQ(DtBuf_RefCount(Buf), 1);

    DtBuf_Unref(&Buf);
}

// The whole point of the type: the bytes stay alive until the last holder lets go, and
// the release happens exactly once.
DT_TEST(ReleaseRunsOnceAtLastReference)
{
    int Marker = 7;

    ResetRecord();
    uint8_t Bytes[32];
    DtBuf* Buf = DtBuf_Wrap(Bytes, sizeof(Bytes), RecordingRelease, &Marker);
    DT_ASSERT(Buf != NULL);

    DtBuf* Extra = DtBuf_Ref(Buf);
    DtBuf_Unref(&Buf);
    DT_ASSERT_EQ(g_Record.Calls, 0);

    DtBuf_Unref(&Extra);
    DT_ASSERT_EQ(g_Record.Calls, 1);
    DT_ASSERT(g_Record.Data == Bytes);
    DT_ASSERT_EQ(g_Record.Size, sizeof(Bytes));
    DT_ASSERT(g_Record.Context == &Marker);
}

DT_TEST(WrapAcceptsNoReleaseFunction)
{
    uint8_t Bytes[4] = {1, 2, 3, 4};
    DtBuf* Buf = DtBuf_Wrap(Bytes, sizeof(Bytes), NULL, NULL);

    DT_ASSERT(Buf != NULL);
    DT_ASSERT(DtBuf_Data(Buf) == Bytes);

    DtBuf_Unref(&Buf);
    DT_ASSERT_EQ(Bytes[0], 1);
}

DT_TEST(WrapRejectsBadArguments)
{
    DT_ASSERT(DtBuf_Wrap(NULL, 4, NULL, NULL) == NULL);
    uint8_t Bytes[4];
    DT_ASSERT(DtBuf_Wrap(Bytes, 0, NULL, NULL) == NULL);
}

DT_TEST(NullIsAcceptedEverywhere)
{
    DtBuf* Null = NULL;

    DT_ASSERT(DtBuf_Ref(NULL) == NULL);
    DT_ASSERT(DtBuf_Data(NULL) == NULL);
    DT_ASSERT_EQ(DtBuf_Size(NULL), 0);
    DT_ASSERT_EQ(DtBuf_RefCount(NULL), 0);

    // Neither of these may do anything, and neither may crash.
    DtBuf_Unref(NULL);
    DtBuf_Unref(&Null);
    DT_ASSERT(Null == NULL);
}

DT_TEST(ManyReferencesBalance)
{
    DtBuf* Buf = DtBuf_Alloc(8);

    DT_ASSERT(Buf != NULL);

    DtBuf* Held[16];
    int i;
    for (i = 0; i < 16; i++)
        Held[i] = DtBuf_Ref(Buf);

    DT_ASSERT_EQ(DtBuf_RefCount(Buf), 17);

    for (i = 0; i < 16; i++)
        DtBuf_Unref(&Held[i]);

    DT_ASSERT_EQ(DtBuf_RefCount(Buf), 1);
    DtBuf_Unref(&Buf);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Out of memory +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// DtBuf_Alloc makes two allocations: the bytes and the handle. If the second one fails
// the first has to be given back, or every failed allocation leaks the payload. Only a
// leak checker sees that leak; this checks that both failures are reported.
DT_TEST(AllocFailureFreesTheBytes)
{
    DtAlloc_ResetCount();

    DtAlloc_FailAfter(0);
    DT_ASSERT(DtBuf_Alloc(64) == NULL);

    DtAlloc_FailAfter(1);
    DT_ASSERT(DtBuf_Alloc(64) == NULL);

    DtAlloc_ResetCount();
}

DT_TEST(WrapFailureIsReported)
{
    DtAlloc_ResetCount();
    DtAlloc_FailAfter(0);
    uint8_t Bytes[8];
    DT_ASSERT(DtBuf_Wrap(Bytes, sizeof(Bytes), NULL, NULL) == NULL);
    DtAlloc_ResetCount();
}

DT_TEST_MAIN("Buf", DT_RUN(AllocGivesOneReference), DT_RUN(AllocRejectsZeroSize),
             DT_RUN(AllocatedBytesAreWritable), DT_RUN(RefCountRisesAndFalls),
             DT_RUN(ReleaseRunsOnceAtLastReference), DT_RUN(WrapAcceptsNoReleaseFunction),
             DT_RUN(WrapRejectsBadArguments), DT_RUN(NullIsAcceptedEverywhere),
             DT_RUN(ManyReferencesBalance), DT_RUN(AllocFailureFreesTheBytes),
             DT_RUN(WrapFailureIsReported))
