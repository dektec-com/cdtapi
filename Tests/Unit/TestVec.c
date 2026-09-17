// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* TestVec.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Unit tests for the growable array
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "Core/DtAlloc.h" // Allocation fault injection.
#include "Core/DtVec.h"   // Interface under test.
#include "DtTest.h"       // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(StartsEmptyWithoutAllocating)
{
    DtVec Vec;

    DtVecInit(&Vec, sizeof(int));
    DT_ASSERT_EQ(DtVecCount(&Vec), 0);
    DT_ASSERT(Vec.Data == NULL);
    DT_ASSERT_EQ(Vec.Capacity, 0);

    DtVecFree(&Vec);
}

DT_TEST(FreeIsIdempotent)
{
    DtVec Vec;
    int Value = 1;

    DtVecInit(&Vec, sizeof(int));
    DT_ASSERT_OK(DtVecPush(&Vec, &Value));

    DtVecFree(&Vec);
    DtVecFree(&Vec);
    DT_ASSERT_EQ(DtVecCount(&Vec), 0);
}

// Pushes far past the initial capacity, so that the growth path runs several times and
// the contents have to survive each reallocation.
DT_TEST(GrowthPreservesContents)
{
    DtVec Vec;

    DtVecInit(&Vec, sizeof(int));

    int i;
    for (i = 0; i < 1000; i++)
        DT_ASSERT_OK(DtVecPush(&Vec, &i));

    DT_ASSERT_EQ(DtVecCount(&Vec), 1000);
    DT_ASSERT(Vec.Capacity >= 1000);

    for (i = 0; i < 1000; i++)
        DT_ASSERT_EQ(DT_VEC_AT(&Vec, int, (size_t)i), i);

    DtVecFree(&Vec);
}

DT_TEST(TypedAccessIsAnLvalue)
{
    DtVec Vec;
    int Value = 10;

    DtVecInit(&Vec, sizeof(int));
    DT_ASSERT_OK(DtVecPush(&Vec, &Value));

    DT_VEC_AT(&Vec, int, 0) = 42;
    DT_ASSERT_EQ(DT_VEC_AT(&Vec, int, 0), 42);

    DtVecFree(&Vec);
}

DT_TEST(IndexOutOfRangeGivesNull)
{
    DtVec Vec;
    int Value = 1;

    DtVecInit(&Vec, sizeof(int));
    DT_ASSERT(DtVecAt(&Vec, 0) == NULL);

    DT_ASSERT_OK(DtVecPush(&Vec, &Value));
    DT_ASSERT(DtVecAt(&Vec, 0) != NULL);
    DT_ASSERT(DtVecAt(&Vec, 1) == NULL);

    DtVecFree(&Vec);
}

// A rescan refills a vector it has just emptied; that must not allocate again.
DT_TEST(ClearKeepsCapacity)
{
    DtVec Vec;

    DtVecInit(&Vec, sizeof(int));
    for (int i = 0; i < 50; i++)
        DT_ASSERT_OK(DtVecPush(&Vec, &i));

    size_t CapacityBefore = Vec.Capacity;
    DtVecClear(&Vec);

    DT_ASSERT_EQ(DtVecCount(&Vec), 0);
    DT_ASSERT_EQ(Vec.Capacity, CapacityBefore);

    DtVecFree(&Vec);
}

DT_TEST(ResizeZeroFillsWhenGrowing)
{
    DtVec Vec;
    int Value = 0x5A5A;

    DtVecInit(&Vec, sizeof(int));
    DT_ASSERT_OK(DtVecPush(&Vec, &Value));
    DT_ASSERT_OK(DtVecResize(&Vec, 10));

    DT_ASSERT_EQ(DtVecCount(&Vec), 10);
    DT_ASSERT_EQ(DT_VEC_AT(&Vec, int, 0), 0x5A5A);
    for (size_t i = 1; i < 10; i++)
        DT_ASSERT_EQ(DT_VEC_AT(&Vec, int, i), 0);

    DtVecFree(&Vec);
}

DT_TEST(ResizeDownDropsElements)
{
    DtVec Vec;

    DtVecInit(&Vec, sizeof(int));
    for (int i = 0; i < 20; i++)
        DT_ASSERT_OK(DtVecPush(&Vec, &i));

    DT_ASSERT_OK(DtVecResize(&Vec, 5));
    DT_ASSERT_EQ(DtVecCount(&Vec), 5);
    DT_ASSERT(DtVecAt(&Vec, 5) == NULL);
    DT_ASSERT_EQ(DT_VEC_AT(&Vec, int, 4), 4);

    DtVecFree(&Vec);
}

DT_TEST(ReserveMakesRoomWithoutAddingElements)
{
    DtVec Vec;

    DtVecInit(&Vec, sizeof(int));
    DT_ASSERT_OK(DtVecReserve(&Vec, 100));

    DT_ASSERT(Vec.Capacity >= 100);
    DT_ASSERT_EQ(DtVecCount(&Vec), 0);

    // Reserving less than is already there must not shrink anything.
    DT_ASSERT_OK(DtVecReserve(&Vec, 2));
    DT_ASSERT(Vec.Capacity >= 100);

    DtVecFree(&Vec);
}

DT_TEST(LargeElementsWork)
{
    typedef struct Big
    {
        char Name[128];
        int Port;
    } Big;

    DtVec Vec;
    DtVecInit(&Vec, sizeof(Big));
    Big Item;
    memset(Item.Name, 'x', sizeof(Item.Name));
    Item.Name[0] = 'A';
    Item.Port = 3;

    DT_ASSERT_OK(DtVecPush(&Vec, &Item));
    Big* Stored = (Big*)DtVecAt(&Vec, 0);
    DT_ASSERT(Stored != NULL);
    DT_ASSERT_EQ(Stored->Port, 3);
    DT_ASSERT_EQ(Stored->Name[0], 'A');

    DtVecFree(&Vec);
}

DT_TEST(BadArgumentsAreRejected)
{
    DtVec Vec;
    int Value = 1;

    DtVecInit(&Vec, sizeof(int));

    DT_ASSERT_EQ(DtVecPush(&Vec, NULL), -1);
    DT_ASSERT_EQ(DtVecPush(NULL, &Value), -1);
    DT_ASSERT_EQ(DtVecReserve(NULL, 10), -1);
    DT_ASSERT_EQ(DtVecResize(NULL, 10), -1);
    DT_ASSERT(DtVecAt(NULL, 0) == NULL);
    DT_ASSERT_EQ(DtVecCount(NULL), 0);

    // These take no action but must not crash.
    DtVecInit(NULL, sizeof(int));
    DtVecFree(NULL);
    DtVecClear(NULL);

    DtVecFree(&Vec);
}

// An element size of zero would make every element occupy nothing and the index
// arithmetic meaningless, so it is refused rather than accepted quietly.
DT_TEST(ZeroElementSizeIsRefused)
{
    DtVec Vec;
    int Value = 1;

    DtVecInit(&Vec, 0);
    DT_ASSERT_EQ(DtVecPush(&Vec, &Value), -1);
    DT_ASSERT_EQ(DtVecReserve(&Vec, 4), -1);
    DT_ASSERT_EQ(DtVecResize(&Vec, 4), -1);

    DtVecFree(&Vec);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Out of memory +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A failed growth must leave the vector exactly as it was, so that a caller that ignores
// the error does not then read through a dangling pointer.
DT_TEST(FailedGrowthLeavesTheVectorIntact)
{
    DtVec Vec;
    int Value = 11;

    DtVecInit(&Vec, sizeof(int));
    for (int i = 0; i < 8; i++)
        DT_ASSERT_OK(DtVecPush(&Vec, &i));

    DtAllocResetCount();
    DtAllocFailAfter(0);

    DT_ASSERT_EQ(DtVecPush(&Vec, &Value), -1);
    DT_ASSERT_EQ(DtVecCount(&Vec), 8);
    DT_ASSERT_EQ(DT_VEC_AT(&Vec, int, 7), 7);

    DtAllocResetCount();

    // Still usable once the allocator recovers.
    DT_ASSERT_OK(DtVecPush(&Vec, &Value));
    DT_ASSERT_EQ(DT_VEC_AT(&Vec, int, 8), 11);

    DtVecFree(&Vec);
}

DT_TEST(ReserveAndResizeReportFailure)
{
    DtVec Vec;

    DtVecInit(&Vec, sizeof(int));

    DtAllocResetCount();
    DtAllocFailAfter(0);
    DT_ASSERT_EQ(DtVecReserve(&Vec, 100), -1);

    DtAllocFailAfter(0);
    DT_ASSERT_EQ(DtVecResize(&Vec, 100), -1);
    DT_ASSERT_EQ(DtVecCount(&Vec), 0);

    DtAllocResetCount();
    DtVecFree(&Vec);
}

// An element large enough that even a small count cannot be expressed in bytes. The
// growth policy has to refuse rather than hand back a capacity that wraps.
DT_TEST(ImpossibleByteCountIsRefused)
{
    DtVec Vec;

    DtVecInit(&Vec, (size_t)-1 / 4);

    DT_ASSERT_EQ(DtVecReserve(&Vec, 8), -1);
    DT_ASSERT_EQ(DtVecCount(&Vec), 0);

    DtVecFree(&Vec);
}

DT_TEST_MAIN("Vec", DT_RUN(StartsEmptyWithoutAllocating), DT_RUN(FreeIsIdempotent),
             DT_RUN(GrowthPreservesContents), DT_RUN(TypedAccessIsAnLvalue),
             DT_RUN(IndexOutOfRangeGivesNull), DT_RUN(ClearKeepsCapacity),
             DT_RUN(ResizeZeroFillsWhenGrowing), DT_RUN(ResizeDownDropsElements),
             DT_RUN(ReserveMakesRoomWithoutAddingElements), DT_RUN(LargeElementsWork),
             DT_RUN(BadArgumentsAreRejected), DT_RUN(ZeroElementSizeIsRefused),
             DT_RUN(FailedGrowthLeavesTheVectorIntact),
             DT_RUN(ReserveAndResizeReportFailure), DT_RUN(ImpossibleByteCountIsRefused))
