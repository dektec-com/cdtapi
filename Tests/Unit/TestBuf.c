// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* TestBuf.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Unit tests for the reference-counted buffer
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
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
    void* Opaque;
} ReleaseRecord;

static ReleaseRecord g_Record;

static void RecordingRelease(void* Opaque, uint8_t* Data, size_t Size)
{
    g_Record.Calls++;
    g_Record.Data = Data;
    g_Record.Size = Size;
    g_Record.Opaque = Opaque;
}

static void ResetRecord(void)
{
    memset(&g_Record, 0, sizeof(g_Record));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(AllocGivesOneReference)
{
    DtBuf* Buf = DtBufAlloc(64);

    DT_ASSERT(Buf != NULL);
    DT_ASSERT(DtBufData(Buf) != NULL);
    DT_ASSERT_EQ(DtBufSize(Buf), 64);
    DT_ASSERT_EQ(DtBufRefCount(Buf), 1);

    DtBufUnref(&Buf);
    DT_ASSERT(Buf == NULL);
}

DT_TEST(AllocRejectsZeroSize)
{
    DT_ASSERT(DtBufAlloc(0) == NULL);
}

DT_TEST(AllocatedBytesAreWritable)
{
    DtBuf* Buf = DtBufAlloc(16);

    DT_ASSERT(Buf != NULL);
    uint8_t* Data = DtBufData(Buf);
    memset(Data, 0xA5, 16);
    DT_ASSERT_EQ(Data[0], 0xA5);
    DT_ASSERT_EQ(Data[15], 0xA5);

    DtBufUnref(&Buf);
}

DT_TEST(RefCountRisesAndFalls)
{
    DtBuf* Buf = DtBufAlloc(8);

    DT_ASSERT(Buf != NULL);

    DtBuf* Second = DtBufRef(Buf);
    DT_ASSERT(Second == Buf);
    DT_ASSERT_EQ(DtBufRefCount(Buf), 2);

    DtBufUnref(&Second);
    DT_ASSERT(Second == NULL);
    DT_ASSERT_EQ(DtBufRefCount(Buf), 1);

    DtBufUnref(&Buf);
}

// The whole point of the type: the bytes stay alive until the last holder lets go, and
// the release happens exactly once.
DT_TEST(ReleaseRunsOnceAtLastReference)
{
    int Marker = 7;

    ResetRecord();
    uint8_t Bytes[32];
    DtBuf* Buf = DtBufWrap(Bytes, sizeof(Bytes), RecordingRelease, &Marker);
    DT_ASSERT(Buf != NULL);

    DtBuf* Extra = DtBufRef(Buf);
    DtBufUnref(&Buf);
    DT_ASSERT_EQ(g_Record.Calls, 0);

    DtBufUnref(&Extra);
    DT_ASSERT_EQ(g_Record.Calls, 1);
    DT_ASSERT(g_Record.Data == Bytes);
    DT_ASSERT_EQ(g_Record.Size, sizeof(Bytes));
    DT_ASSERT(g_Record.Opaque == &Marker);
}

DT_TEST(WrapAcceptsNoReleaseFunction)
{
    uint8_t Bytes[4] = {1, 2, 3, 4};
    DtBuf* Buf = DtBufWrap(Bytes, sizeof(Bytes), NULL, NULL);

    DT_ASSERT(Buf != NULL);
    DT_ASSERT(DtBufData(Buf) == Bytes);

    DtBufUnref(&Buf);
    DT_ASSERT_EQ(Bytes[0], 1);
}

DT_TEST(WrapRejectsBadArguments)
{
    DT_ASSERT(DtBufWrap(NULL, 4, NULL, NULL) == NULL);
    uint8_t Bytes[4];
    DT_ASSERT(DtBufWrap(Bytes, 0, NULL, NULL) == NULL);
}

DT_TEST(NullIsAcceptedEverywhere)
{
    DtBuf* Null = NULL;

    DT_ASSERT(DtBufRef(NULL) == NULL);
    DT_ASSERT(DtBufData(NULL) == NULL);
    DT_ASSERT_EQ(DtBufSize(NULL), 0);
    DT_ASSERT_EQ(DtBufRefCount(NULL), 0);

    // Neither of these may do anything, and neither may crash.
    DtBufUnref(NULL);
    DtBufUnref(&Null);
    DT_ASSERT(Null == NULL);
}

DT_TEST(ManyReferencesBalance)
{
    DtBuf* Buf = DtBufAlloc(8);

    DT_ASSERT(Buf != NULL);

    DtBuf* Held[16];
    int i;
    for (i = 0; i < 16; i++)
        Held[i] = DtBufRef(Buf);

    DT_ASSERT_EQ(DtBufRefCount(Buf), 17);

    for (i = 0; i < 16; i++)
        DtBufUnref(&Held[i]);

    DT_ASSERT_EQ(DtBufRefCount(Buf), 1);
    DtBufUnref(&Buf);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Out of memory +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// DtBufAlloc makes two allocations: the bytes and the handle. If the second one fails
// the first has to be given back, or every failed allocation leaks the payload.
DT_TEST(AllocFailureFreesTheBytes)
{
    DtAllocResetCount();

    DtAllocFailAfter(0);
    DT_ASSERT(DtBufAlloc(64) == NULL);

    DtAllocFailAfter(1);
    DT_ASSERT(DtBufAlloc(64) == NULL);

    DtAllocResetCount();
}

DT_TEST(WrapFailureIsReported)
{
    DtAllocResetCount();
    DtAllocFailAfter(0);
    uint8_t Bytes[8];
    DT_ASSERT(DtBufWrap(Bytes, sizeof(Bytes), NULL, NULL) == NULL);
    DtAllocResetCount();
}

DT_TEST_MAIN("Buf", DT_RUN(AllocGivesOneReference), DT_RUN(AllocRejectsZeroSize),
             DT_RUN(AllocatedBytesAreWritable), DT_RUN(RefCountRisesAndFalls),
             DT_RUN(ReleaseRunsOnceAtLastReference), DT_RUN(WrapAcceptsNoReleaseFunction),
             DT_RUN(WrapRejectsBadArguments), DT_RUN(NullIsAcceptedEverywhere),
             DT_RUN(ManyReferencesBalance), DT_RUN(AllocFailureFreesTheBytes),
             DT_RUN(WrapFailureIsReported))
