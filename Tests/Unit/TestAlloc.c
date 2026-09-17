// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# TestAlloc.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Unit tests for the allocation seam and the growth policy
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "Core/DtAlloc.h" // Interface under test.
#include "DtTest.h"       // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(AllocationsAreCounted)
{
    DtAllocResetCount();
    DT_ASSERT_EQ(DtAllocCount(), 0);

    void* First = DtMalloc(16);
    void* Second = DtMalloc(16);
    DT_ASSERT(First != NULL);
    DT_ASSERT(Second != NULL);
    DT_ASSERT_EQ(DtAllocCount(), 2);

    DtFree(First);
    DtFree(Second);
    DtAllocResetCount();
}

DT_TEST(InjectionFailsTheChosenAllocation)
{
    DtAllocResetCount();
    DtAllocFailAfter(1);

    void* Ptr = DtMalloc(16);
    DT_ASSERT(Ptr != NULL);
    DtFree(Ptr);

    // The second one is the armed one.
    DT_ASSERT(DtMalloc(16) == NULL);

    // One-shot: the injection disarms itself so that a recovery path can allocate.
    Ptr = DtMalloc(16);
    DT_ASSERT(Ptr != NULL);
    DtFree(Ptr);

    DtAllocResetCount();
}

DT_TEST(ReallocGrowsThroughTheSeam)
{
    DtAllocResetCount();
    unsigned char* Ptr = (unsigned char*)DtMalloc(4);
    DT_ASSERT(Ptr != NULL);
    Ptr[0] = 0x42;

    Ptr = (unsigned char*)DtRealloc(Ptr, 64);
    DT_ASSERT(Ptr != NULL);
    DT_ASSERT_EQ(Ptr[0], 0x42);
    DT_ASSERT_EQ(DtAllocCount(), 2);

    DtFree(Ptr);
    DtAllocResetCount();
}

DT_TEST(InjectionCoversRealloc)
{
    void* Ptr = DtMalloc(16);

    DT_ASSERT(Ptr != NULL);

    DtAllocFailAfter(0);
    DT_ASSERT(DtRealloc(Ptr, 64) == NULL);

    // A failed realloc leaves the original block alive, so it still has to be freed.
    DtFree(Ptr);
    DtAllocResetCount();
}

DT_TEST(ResetDisarmsInjection)
{
    DtAllocFailAfter(0);
    DtAllocResetCount();

    void* Ptr = DtMalloc(16);
    DT_ASSERT(Ptr != NULL);
    DtFree(Ptr);
    DtAllocResetCount();
}

// Every block made through the seam counts until it is freed; growing a block does not
// make another, and neither does a failed allocation or freeing NULL.
DT_TEST(LiveBlocksAreCounted)
{
    int Before = DtAllocLive();

    void* Block = DtMalloc(16);
    DT_ASSERT_EQ(DtAllocLive(), Before + 1);

    void* Grown = DtRealloc(Block, 64);
    DT_ASSERT(Grown != NULL);
    DT_ASSERT_EQ(DtAllocLive(), Before + 1);

    DtFree(Grown);
    DT_ASSERT_EQ(DtAllocLive(), Before);

    Block = DtRealloc(NULL, 8);
    DT_ASSERT_EQ(DtAllocLive(), Before + 1);
    DtFree(Block);

    DtAllocResetCount();
    DtAllocFailAfter(0);
    DT_ASSERT(DtMalloc(8) == NULL);
    DtAllocFailAfter(0);
    DT_ASSERT(DtRealloc(NULL, 8) == NULL);
    DtFree(NULL);
    DT_ASSERT_EQ(DtAllocLive(), Before);
    DtAllocResetCount();
}

DT_TEST(FreeAcceptsNull)
{
    DtAllocResetCount();
    int Before = DtAllocCount();

    DtFree(NULL);

    // Freeing nothing must neither crash nor be counted as an allocation.
    DT_ASSERT_EQ(DtAllocCount(), Before);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Growth policy +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(GrowthStartsAtTheMinimum)
{
    size_t Out = 0;

    DT_ASSERT_OK(DtGrowCapacity(0, 1, 4, 8, &Out));
    DT_ASSERT_EQ(Out, 8);
}

DT_TEST(GrowthDoubles)
{
    size_t Out = 0;

    DT_ASSERT_OK(DtGrowCapacity(8, 9, 4, 8, &Out));
    DT_ASSERT_EQ(Out, 16);

    DT_ASSERT_OK(DtGrowCapacity(8, 33, 4, 8, &Out));
    DT_ASSERT_EQ(Out, 64);
}

DT_TEST(GrowthLeavesEnoughRoomAlone)
{
    size_t Out = 0;

    DT_ASSERT_OK(DtGrowCapacity(64, 10, 4, 8, &Out));
    DT_ASSERT_EQ(Out, 64);
}

// Doubling past half of the address space would wrap the capacity back to a small
// number, and the allocation that followed would be far too small for the writes aimed
// at it. The policy stops at exactly what was asked for instead.
DT_TEST(DoublingStopsInsteadOfWrapping)
{
    size_t Huge = ((size_t)-1 / 2) + 1;
    size_t Out = 0;

    DT_ASSERT_OK(DtGrowCapacity(Huge, Huge + 1, 1, 8, &Out));
    DT_ASSERT_EQ(Out, Huge + 1);
}

// The byte count is what reaches the allocator, so that is what has to fit. Asking for
// half the address space worth of eight-byte elements cannot be represented and has to
// be refused rather than truncated.
DT_TEST(ByteCountOverflowIsRefused)
{
    size_t TooMany = ((size_t)-1 / 8) + 1;
    size_t Out = 0;

    DT_ASSERT_EQ(DtGrowCapacity(0, TooMany, 8, 8, &Out), -1);
}

DT_TEST(GrowthRejectsBadArguments)
{
    size_t Out = 0;

    DT_ASSERT_EQ(DtGrowCapacity(0, 4, 4, 8, NULL), -1);
    DT_ASSERT_EQ(DtGrowCapacity(0, 4, 0, 8, &Out), -1);
}

DT_TEST_MAIN("Alloc", DT_RUN(AllocationsAreCounted), DT_RUN(LiveBlocksAreCounted),
             DT_RUN(InjectionFailsTheChosenAllocation),
             DT_RUN(ReallocGrowsThroughTheSeam), DT_RUN(InjectionCoversRealloc),
             DT_RUN(ResetDisarmsInjection), DT_RUN(FreeAcceptsNull),
             DT_RUN(GrowthStartsAtTheMinimum), DT_RUN(GrowthDoubles),
             DT_RUN(GrowthLeavesEnoughRoomAlone), DT_RUN(DoublingStopsInsteadOfWrapping),
             DT_RUN(ByteCountOverflowIsRefused), DT_RUN(GrowthRejectsBadArguments))
