// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# TestAlloc.c *#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Unit tests for the allocation seam and the growth policy
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "Core/DtlAlloc.h" // Interface under test.
#include "DtlTest.h"       // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DTL_TEST(AllocationsAreCounted)
{
    void* First;
    void* Second;

    DtlAllocResetCount();
    DTL_ASSERT_EQ(DtlAllocCount(), 0);

    First = DtlMalloc(16);
    Second = DtlMalloc(16);
    DTL_ASSERT(First != NULL);
    DTL_ASSERT(Second != NULL);
    DTL_ASSERT_EQ(DtlAllocCount(), 2);

    DtlFree(First);
    DtlFree(Second);
    DtlAllocResetCount();
}

DTL_TEST(InjectionFailsTheChosenAllocation)
{
    void* Ptr;

    DtlAllocResetCount();
    DtlAllocFailAfter(1);

    Ptr = DtlMalloc(16);
    DTL_ASSERT(Ptr != NULL);
    DtlFree(Ptr);

    // The second one is the armed one.
    DTL_ASSERT(DtlMalloc(16) == NULL);

    // One-shot: the injection disarms itself so that a recovery path can allocate.
    Ptr = DtlMalloc(16);
    DTL_ASSERT(Ptr != NULL);
    DtlFree(Ptr);

    DtlAllocResetCount();
}

DTL_TEST(ReallocGrowsThroughTheSeam)
{
    unsigned char* Ptr;

    DtlAllocResetCount();
    Ptr = (unsigned char*)DtlMalloc(4);
    DTL_ASSERT(Ptr != NULL);
    Ptr[0] = 0x42;

    Ptr = (unsigned char*)DtlRealloc(Ptr, 64);
    DTL_ASSERT(Ptr != NULL);
    DTL_ASSERT_EQ(Ptr[0], 0x42);
    DTL_ASSERT_EQ(DtlAllocCount(), 2);

    DtlFree(Ptr);
    DtlAllocResetCount();
}

DTL_TEST(InjectionCoversRealloc)
{
    void* Ptr = DtlMalloc(16);

    DTL_ASSERT(Ptr != NULL);

    DtlAllocFailAfter(0);
    DTL_ASSERT(DtlRealloc(Ptr, 64) == NULL);

    // A failed realloc leaves the original block alive, so it still has to be freed.
    DtlFree(Ptr);
    DtlAllocResetCount();
}

DTL_TEST(ResetDisarmsInjection)
{
    DtlAllocFailAfter(0);
    DtlAllocResetCount();

    void* Ptr = DtlMalloc(16);
    DTL_ASSERT(Ptr != NULL);
    DtlFree(Ptr);
    DtlAllocResetCount();
}

DTL_TEST(FreeAcceptsNull)
{
    long Before;

    DtlAllocResetCount();
    Before = DtlAllocCount();

    DtlFree(NULL);

    // Freeing nothing must neither crash nor be counted as an allocation.
    DTL_ASSERT_EQ(DtlAllocCount(), Before);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Growth policy +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DTL_TEST(GrowthStartsAtTheMinimum)
{
    size_t Out = 0;

    DTL_ASSERT_OK(DtlGrowCapacity(0, 1, 4, 8, &Out));
    DTL_ASSERT_EQ(Out, 8);
}

DTL_TEST(GrowthDoubles)
{
    size_t Out = 0;

    DTL_ASSERT_OK(DtlGrowCapacity(8, 9, 4, 8, &Out));
    DTL_ASSERT_EQ(Out, 16);

    DTL_ASSERT_OK(DtlGrowCapacity(8, 33, 4, 8, &Out));
    DTL_ASSERT_EQ(Out, 64);
}

DTL_TEST(GrowthLeavesEnoughRoomAlone)
{
    size_t Out = 0;

    DTL_ASSERT_OK(DtlGrowCapacity(64, 10, 4, 8, &Out));
    DTL_ASSERT_EQ(Out, 64);
}

// Doubling past half of the address space would wrap the capacity back to a small
// number, and the allocation that followed would be far too small for the writes aimed
// at it. The policy stops at exactly what was asked for instead.
DTL_TEST(DoublingStopsInsteadOfWrapping)
{
    size_t Huge = ((size_t)-1 / 2) + 1;
    size_t Out = 0;

    DTL_ASSERT_OK(DtlGrowCapacity(Huge, Huge + 1, 1, 8, &Out));
    DTL_ASSERT_EQ(Out, Huge + 1);
}

// The byte count is what reaches the allocator, so that is what has to fit. Asking for
// half the address space worth of eight-byte elements cannot be represented and has to
// be refused rather than truncated.
DTL_TEST(ByteCountOverflowIsRefused)
{
    size_t TooMany = ((size_t)-1 / 8) + 1;
    size_t Out = 0;

    DTL_ASSERT_EQ(DtlGrowCapacity(0, TooMany, 8, 8, &Out), -1);
}

DTL_TEST(GrowthRejectsBadArguments)
{
    size_t Out = 0;

    DTL_ASSERT_EQ(DtlGrowCapacity(0, 4, 4, 8, NULL), -1);
    DTL_ASSERT_EQ(DtlGrowCapacity(0, 4, 0, 8, &Out), -1);
}

DTL_TEST_MAIN("Alloc", DTL_RUN(AllocationsAreCounted),
              DTL_RUN(InjectionFailsTheChosenAllocation),
              DTL_RUN(ReallocGrowsThroughTheSeam), DTL_RUN(InjectionCoversRealloc),
              DTL_RUN(ResetDisarmsInjection), DTL_RUN(FreeAcceptsNull),
              DTL_RUN(GrowthStartsAtTheMinimum), DTL_RUN(GrowthDoubles),
              DTL_RUN(GrowthLeavesEnoughRoomAlone),
              DTL_RUN(DoublingStopsInsteadOfWrapping),
              DTL_RUN(ByteCountOverflowIsRefused), DTL_RUN(GrowthRejectsBadArguments))
