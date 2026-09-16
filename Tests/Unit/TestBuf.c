// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# TestBuf.c *#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Unit tests for the reference-counted buffer
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
#include "Core/DtlAlloc.h" // Allocation fault injection.
#include "Core/DtlBuf.h"   // Interface under test.
#include "DtlTest.h"       // Test framework.

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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DTL_TEST(AllocGivesOneReference)
{
    DtlBuf* Buf = DtlBufAlloc(64);

    DTL_ASSERT(Buf != NULL);
    DTL_ASSERT(DtlBufData(Buf) != NULL);
    DTL_ASSERT_EQ(DtlBufSize(Buf), 64);
    DTL_ASSERT_EQ(DtlBufRefCount(Buf), 1);

    DtlBufUnref(&Buf);
    DTL_ASSERT(Buf == NULL);
}

DTL_TEST(AllocRejectsZeroSize)
{
    DTL_ASSERT(DtlBufAlloc(0) == NULL);
}

DTL_TEST(AllocatedBytesAreWritable)
{
    DtlBuf* Buf = DtlBufAlloc(16);
    uint8_t* Data;

    DTL_ASSERT(Buf != NULL);
    Data = DtlBufData(Buf);
    memset(Data, 0xA5, 16);
    DTL_ASSERT_EQ(Data[0], 0xA5);
    DTL_ASSERT_EQ(Data[15], 0xA5);

    DtlBufUnref(&Buf);
}

DTL_TEST(RefCountRisesAndFalls)
{
    DtlBuf* Buf = DtlBufAlloc(8);
    DtlBuf* Second;

    DTL_ASSERT(Buf != NULL);

    Second = DtlBufRef(Buf);
    DTL_ASSERT(Second == Buf);
    DTL_ASSERT_EQ(DtlBufRefCount(Buf), 2);

    DtlBufUnref(&Second);
    DTL_ASSERT(Second == NULL);
    DTL_ASSERT_EQ(DtlBufRefCount(Buf), 1);

    DtlBufUnref(&Buf);
}

// The whole point of the type: the bytes stay alive until the last holder lets go, and
// the release happens exactly once.
DTL_TEST(ReleaseRunsOnceAtLastReference)
{
    uint8_t Bytes[32];
    int Marker = 7;
    DtlBuf* Buf;
    DtlBuf* Extra;

    ResetRecord();
    Buf = DtlBufWrap(Bytes, sizeof(Bytes), RecordingRelease, &Marker);
    DTL_ASSERT(Buf != NULL);

    Extra = DtlBufRef(Buf);
    DtlBufUnref(&Buf);
    DTL_ASSERT_EQ(g_Record.Calls, 0);

    DtlBufUnref(&Extra);
    DTL_ASSERT_EQ(g_Record.Calls, 1);
    DTL_ASSERT(g_Record.Data == Bytes);
    DTL_ASSERT_EQ(g_Record.Size, sizeof(Bytes));
    DTL_ASSERT(g_Record.Opaque == &Marker);
}

DTL_TEST(WrapAcceptsNoReleaseFunction)
{
    uint8_t Bytes[4] = {1, 2, 3, 4};
    DtlBuf* Buf = DtlBufWrap(Bytes, sizeof(Bytes), NULL, NULL);

    DTL_ASSERT(Buf != NULL);
    DTL_ASSERT(DtlBufData(Buf) == Bytes);

    DtlBufUnref(&Buf);
    DTL_ASSERT_EQ(Bytes[0], 1);
}

DTL_TEST(WrapRejectsBadArguments)
{
    uint8_t Bytes[4];

    DTL_ASSERT(DtlBufWrap(NULL, 4, NULL, NULL) == NULL);
    DTL_ASSERT(DtlBufWrap(Bytes, 0, NULL, NULL) == NULL);
}

DTL_TEST(NullIsAcceptedEverywhere)
{
    DtlBuf* Null = NULL;

    DTL_ASSERT(DtlBufRef(NULL) == NULL);
    DTL_ASSERT(DtlBufData(NULL) == NULL);
    DTL_ASSERT_EQ(DtlBufSize(NULL), 0);
    DTL_ASSERT_EQ(DtlBufRefCount(NULL), 0);

    // Neither of these may do anything, and neither may crash.
    DtlBufUnref(NULL);
    DtlBufUnref(&Null);
    DTL_ASSERT(Null == NULL);
}

DTL_TEST(ManyReferencesBalance)
{
    DtlBuf* Buf = DtlBufAlloc(8);
    DtlBuf* Held[16];
    int i;

    DTL_ASSERT(Buf != NULL);

    for (i = 0; i < 16; i++)
        Held[i] = DtlBufRef(Buf);

    DTL_ASSERT_EQ(DtlBufRefCount(Buf), 17);

    for (i = 0; i < 16; i++)
        DtlBufUnref(&Held[i]);

    DTL_ASSERT_EQ(DtlBufRefCount(Buf), 1);
    DtlBufUnref(&Buf);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Out of memory +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// DtlBufAlloc makes two allocations: the bytes and the handle. If the second one fails
// the first has to be given back, or every failed allocation leaks the payload.
DTL_TEST(AllocFailureFreesTheBytes)
{
    DtlAllocResetCount();

    DtlAllocFailAfter(0);
    DTL_ASSERT(DtlBufAlloc(64) == NULL);

    DtlAllocFailAfter(1);
    DTL_ASSERT(DtlBufAlloc(64) == NULL);

    DtlAllocResetCount();
}

DTL_TEST(WrapFailureIsReported)
{
    uint8_t Bytes[8];

    DtlAllocResetCount();
    DtlAllocFailAfter(0);
    DTL_ASSERT(DtlBufWrap(Bytes, sizeof(Bytes), NULL, NULL) == NULL);
    DtlAllocResetCount();
}

DTL_TEST_MAIN("Buf", DTL_RUN(AllocGivesOneReference), DTL_RUN(AllocRejectsZeroSize),
              DTL_RUN(AllocatedBytesAreWritable), DTL_RUN(RefCountRisesAndFalls),
              DTL_RUN(ReleaseRunsOnceAtLastReference),
              DTL_RUN(WrapAcceptsNoReleaseFunction), DTL_RUN(WrapRejectsBadArguments),
              DTL_RUN(NullIsAcceptedEverywhere), DTL_RUN(ManyReferencesBalance),
              DTL_RUN(AllocFailureFreesTheBytes), DTL_RUN(WrapFailureIsReported))
