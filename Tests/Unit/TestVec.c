// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# TestVec.c *#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Unit tests for the growable array
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "Core/DtlAlloc.h" // Allocation fault injection.
#include "Core/DtlVec.h"   // Interface under test.
#include "DtlTest.h"       // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DTL_TEST(StartsEmptyWithoutAllocating)
{
    DtlVec Vec;

    DtlVecInit(&Vec, sizeof(int));
    DTL_ASSERT_EQ(DtlVecCount(&Vec), 0);
    DTL_ASSERT(Vec.Data == NULL);
    DTL_ASSERT_EQ(Vec.Capacity, 0);

    DtlVecFree(&Vec);
}

DTL_TEST(FreeIsIdempotent)
{
    DtlVec Vec;
    int Value = 1;

    DtlVecInit(&Vec, sizeof(int));
    DTL_ASSERT_OK(DtlVecPush(&Vec, &Value));

    DtlVecFree(&Vec);
    DtlVecFree(&Vec);
    DTL_ASSERT_EQ(DtlVecCount(&Vec), 0);
}

// Pushes far past the initial capacity, so that the growth path runs several times and
// the contents have to survive each reallocation.
DTL_TEST(GrowthPreservesContents)
{
    DtlVec Vec;
    int i;

    DtlVecInit(&Vec, sizeof(int));

    for (i = 0; i < 1000; i++)
        DTL_ASSERT_OK(DtlVecPush(&Vec, &i));

    DTL_ASSERT_EQ(DtlVecCount(&Vec), 1000);
    DTL_ASSERT(Vec.Capacity >= 1000);

    for (i = 0; i < 1000; i++)
        DTL_ASSERT_EQ(DTL_VEC_AT(&Vec, int, (size_t)i), i);

    DtlVecFree(&Vec);
}

DTL_TEST(TypedAccessIsAnLvalue)
{
    DtlVec Vec;
    int Value = 10;

    DtlVecInit(&Vec, sizeof(int));
    DTL_ASSERT_OK(DtlVecPush(&Vec, &Value));

    DTL_VEC_AT(&Vec, int, 0) = 42;
    DTL_ASSERT_EQ(DTL_VEC_AT(&Vec, int, 0), 42);

    DtlVecFree(&Vec);
}

DTL_TEST(IndexOutOfRangeGivesNull)
{
    DtlVec Vec;
    int Value = 1;

    DtlVecInit(&Vec, sizeof(int));
    DTL_ASSERT(DtlVecAt(&Vec, 0) == NULL);

    DTL_ASSERT_OK(DtlVecPush(&Vec, &Value));
    DTL_ASSERT(DtlVecAt(&Vec, 0) != NULL);
    DTL_ASSERT(DtlVecAt(&Vec, 1) == NULL);

    DtlVecFree(&Vec);
}

// A rescan refills a vector it has just emptied; that must not allocate again.
DTL_TEST(ClearKeepsCapacity)
{
    DtlVec Vec;
    size_t CapacityBefore;
    int i;

    DtlVecInit(&Vec, sizeof(int));
    for (i = 0; i < 50; i++)
        DTL_ASSERT_OK(DtlVecPush(&Vec, &i));

    CapacityBefore = Vec.Capacity;
    DtlVecClear(&Vec);

    DTL_ASSERT_EQ(DtlVecCount(&Vec), 0);
    DTL_ASSERT_EQ(Vec.Capacity, CapacityBefore);

    DtlVecFree(&Vec);
}

DTL_TEST(ResizeZeroFillsWhenGrowing)
{
    DtlVec Vec;
    int Value = 0x5A5A;
    size_t i;

    DtlVecInit(&Vec, sizeof(int));
    DTL_ASSERT_OK(DtlVecPush(&Vec, &Value));
    DTL_ASSERT_OK(DtlVecResize(&Vec, 10));

    DTL_ASSERT_EQ(DtlVecCount(&Vec), 10);
    DTL_ASSERT_EQ(DTL_VEC_AT(&Vec, int, 0), 0x5A5A);
    for (i = 1; i < 10; i++)
        DTL_ASSERT_EQ(DTL_VEC_AT(&Vec, int, i), 0);

    DtlVecFree(&Vec);
}

DTL_TEST(ResizeDownDropsElements)
{
    DtlVec Vec;
    int i;

    DtlVecInit(&Vec, sizeof(int));
    for (i = 0; i < 20; i++)
        DTL_ASSERT_OK(DtlVecPush(&Vec, &i));

    DTL_ASSERT_OK(DtlVecResize(&Vec, 5));
    DTL_ASSERT_EQ(DtlVecCount(&Vec), 5);
    DTL_ASSERT(DtlVecAt(&Vec, 5) == NULL);
    DTL_ASSERT_EQ(DTL_VEC_AT(&Vec, int, 4), 4);

    DtlVecFree(&Vec);
}

DTL_TEST(ReserveMakesRoomWithoutAddingElements)
{
    DtlVec Vec;

    DtlVecInit(&Vec, sizeof(int));
    DTL_ASSERT_OK(DtlVecReserve(&Vec, 100));

    DTL_ASSERT(Vec.Capacity >= 100);
    DTL_ASSERT_EQ(DtlVecCount(&Vec), 0);

    // Reserving less than is already there must not shrink anything.
    DTL_ASSERT_OK(DtlVecReserve(&Vec, 2));
    DTL_ASSERT(Vec.Capacity >= 100);

    DtlVecFree(&Vec);
}

DTL_TEST(LargeElementsWork)
{
    typedef struct Big
    {
        char Name[128];
        int Port;
    } Big;

    DtlVec Vec;
    Big Item;
    Big* Stored;

    DtlVecInit(&Vec, sizeof(Big));
    memset(Item.Name, 'x', sizeof(Item.Name));
    Item.Name[0] = 'A';
    Item.Port = 3;

    DTL_ASSERT_OK(DtlVecPush(&Vec, &Item));
    Stored = (Big*)DtlVecAt(&Vec, 0);
    DTL_ASSERT(Stored != NULL);
    DTL_ASSERT_EQ(Stored->Port, 3);
    DTL_ASSERT_EQ(Stored->Name[0], 'A');

    DtlVecFree(&Vec);
}

DTL_TEST(BadArgumentsAreRejected)
{
    DtlVec Vec;
    int Value = 1;

    DtlVecInit(&Vec, sizeof(int));

    DTL_ASSERT_EQ(DtlVecPush(&Vec, NULL), -1);
    DTL_ASSERT_EQ(DtlVecPush(NULL, &Value), -1);
    DTL_ASSERT_EQ(DtlVecReserve(NULL, 10), -1);
    DTL_ASSERT_EQ(DtlVecResize(NULL, 10), -1);
    DTL_ASSERT(DtlVecAt(NULL, 0) == NULL);
    DTL_ASSERT_EQ(DtlVecCount(NULL), 0);

    // These take no action but must not crash.
    DtlVecInit(NULL, sizeof(int));
    DtlVecFree(NULL);
    DtlVecClear(NULL);

    DtlVecFree(&Vec);
}

// An element size of zero would make every element occupy nothing and the index
// arithmetic meaningless, so it is refused rather than accepted quietly.
DTL_TEST(ZeroElementSizeIsRefused)
{
    DtlVec Vec;
    int Value = 1;

    DtlVecInit(&Vec, 0);
    DTL_ASSERT_EQ(DtlVecPush(&Vec, &Value), -1);
    DTL_ASSERT_EQ(DtlVecReserve(&Vec, 4), -1);
    DTL_ASSERT_EQ(DtlVecResize(&Vec, 4), -1);

    DtlVecFree(&Vec);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Out of memory +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A failed growth must leave the vector exactly as it was, so that a caller that ignores
// the error does not then read through a dangling pointer.
DTL_TEST(FailedGrowthLeavesTheVectorIntact)
{
    DtlVec Vec;
    int Value = 11;
    int i;

    DtlVecInit(&Vec, sizeof(int));
    for (i = 0; i < 8; i++)
        DTL_ASSERT_OK(DtlVecPush(&Vec, &i));

    DtlAllocResetCount();
    DtlAllocFailAfter(0);

    DTL_ASSERT_EQ(DtlVecPush(&Vec, &Value), -1);
    DTL_ASSERT_EQ(DtlVecCount(&Vec), 8);
    DTL_ASSERT_EQ(DTL_VEC_AT(&Vec, int, 7), 7);

    DtlAllocResetCount();

    // Still usable once the allocator recovers.
    DTL_ASSERT_OK(DtlVecPush(&Vec, &Value));
    DTL_ASSERT_EQ(DTL_VEC_AT(&Vec, int, 8), 11);

    DtlVecFree(&Vec);
}

DTL_TEST(ReserveAndResizeReportFailure)
{
    DtlVec Vec;

    DtlVecInit(&Vec, sizeof(int));

    DtlAllocResetCount();
    DtlAllocFailAfter(0);
    DTL_ASSERT_EQ(DtlVecReserve(&Vec, 100), -1);

    DtlAllocFailAfter(0);
    DTL_ASSERT_EQ(DtlVecResize(&Vec, 100), -1);
    DTL_ASSERT_EQ(DtlVecCount(&Vec), 0);

    DtlAllocResetCount();
    DtlVecFree(&Vec);
}

// An element large enough that even a small count cannot be expressed in bytes. The
// growth policy has to refuse rather than hand back a capacity that wraps.
DTL_TEST(ImpossibleByteCountIsRefused)
{
    DtlVec Vec;

    DtlVecInit(&Vec, (size_t)-1 / 4);

    DTL_ASSERT_EQ(DtlVecReserve(&Vec, 8), -1);
    DTL_ASSERT_EQ(DtlVecCount(&Vec), 0);

    DtlVecFree(&Vec);
}

DTL_TEST_MAIN("Vec", DTL_RUN(StartsEmptyWithoutAllocating), DTL_RUN(FreeIsIdempotent),
              DTL_RUN(GrowthPreservesContents), DTL_RUN(TypedAccessIsAnLvalue),
              DTL_RUN(IndexOutOfRangeGivesNull), DTL_RUN(ClearKeepsCapacity),
              DTL_RUN(ResizeZeroFillsWhenGrowing), DTL_RUN(ResizeDownDropsElements),
              DTL_RUN(ReserveMakesRoomWithoutAddingElements), DTL_RUN(LargeElementsWork),
              DTL_RUN(BadArgumentsAreRejected), DTL_RUN(ZeroElementSizeIsRefused),
              DTL_RUN(FailedGrowthLeavesTheVectorIntact),
              DTL_RUN(ReserveAndResizeReportFailure),
              DTL_RUN(ImpossibleByteCountIsRefused))
