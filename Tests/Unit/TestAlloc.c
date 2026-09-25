// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# TestAlloc.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Unit tests for the allocation seam and the growth policy
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "Core/DtAlloc.h" // Interface under test.
#include "DtTest.h"       // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(AllocationsAreCounted)
{
    DtAlloc_ResetCount();
    DT_ASSERT_EQ(DtAlloc_NumAllocations(), 0);

    void* First = DtAlloc_Malloc(16);
    void* Second = DtAlloc_Malloc(16);
    DT_ASSERT(First != NULL);
    DT_ASSERT(Second != NULL);
    DT_ASSERT_EQ(DtAlloc_NumAllocations(), 2);

    DtAlloc_Free(First);
    DtAlloc_Free(Second);
    DtAlloc_ResetCount();
}

DT_TEST(InjectionFailsTheChosenAllocation)
{
    DtAlloc_ResetCount();
    DtAlloc_FailAfter(1);

    void* Ptr = DtAlloc_Malloc(16);
    DT_ASSERT(Ptr != NULL);
    DtAlloc_Free(Ptr);

    // The second one is the armed one.
    DT_ASSERT(DtAlloc_Malloc(16) == NULL);

    // One-shot: the injection disarms itself so that a recovery path can allocate.
    Ptr = DtAlloc_Malloc(16);
    DT_ASSERT(Ptr != NULL);
    DtAlloc_Free(Ptr);

    DtAlloc_ResetCount();
}

DT_TEST(ReallocGrowsThroughTheSeam)
{
    DtAlloc_ResetCount();
    unsigned char* Ptr = (unsigned char*)DtAlloc_Malloc(4);
    DT_ASSERT(Ptr != NULL);
    Ptr[0] = 0x42;

    Ptr = (unsigned char*)DtAlloc_Realloc(Ptr, 64);
    DT_ASSERT(Ptr != NULL);
    DT_ASSERT_EQ(Ptr[0], 0x42);
    DT_ASSERT_EQ(DtAlloc_NumAllocations(), 2);

    DtAlloc_Free(Ptr);
    DtAlloc_ResetCount();
}

DT_TEST(InjectionCoversRealloc)
{
    void* Ptr = DtAlloc_Malloc(16);

    DT_ASSERT(Ptr != NULL);

    DtAlloc_FailAfter(0);
    DT_ASSERT(DtAlloc_Realloc(Ptr, 64) == NULL);

    // A failed realloc leaves the original block alive, so it still has to be freed.
    DtAlloc_Free(Ptr);
    DtAlloc_ResetCount();
}

DT_TEST(ResetDisarmsInjection)
{
    DtAlloc_FailAfter(0);
    DtAlloc_ResetCount();

    void* Ptr = DtAlloc_Malloc(16);
    DT_ASSERT(Ptr != NULL);
    DtAlloc_Free(Ptr);
    DtAlloc_ResetCount();
}

// Every block made through the seam counts until it is freed; growing a block does not
// make another, and neither does a failed allocation or freeing NULL.
DT_TEST(LiveBlocksAreCounted)
{
    int Before = DtAlloc_NumLive();

    void* Block = DtAlloc_Malloc(16);
    DT_ASSERT_EQ(DtAlloc_NumLive(), Before + 1);

    void* Grown = DtAlloc_Realloc(Block, 64);
    DT_ASSERT(Grown != NULL);
    DT_ASSERT_EQ(DtAlloc_NumLive(), Before + 1);

    DtAlloc_Free(Grown);
    DT_ASSERT_EQ(DtAlloc_NumLive(), Before);

    Block = DtAlloc_Realloc(NULL, 8);
    DT_ASSERT_EQ(DtAlloc_NumLive(), Before + 1);
    DtAlloc_Free(Block);

    DtAlloc_ResetCount();
    DtAlloc_FailAfter(0);
    DT_ASSERT(DtAlloc_Malloc(8) == NULL);
    DtAlloc_FailAfter(0);
    DT_ASSERT(DtAlloc_Realloc(NULL, 8) == NULL);
    DtAlloc_Free(NULL);
    DT_ASSERT_EQ(DtAlloc_NumLive(), Before);
    DtAlloc_ResetCount();
}

DT_TEST(FreeAcceptsNull)
{
    DtAlloc_ResetCount();
    int Before = DtAlloc_NumAllocations();

    DtAlloc_Free(NULL);

    // Freeing nothing must neither crash nor be counted as an allocation.
    DT_ASSERT_EQ(DtAlloc_NumAllocations(), Before);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Growth policy +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(GrowthStartsAtTheMinimum)
{
    size_t Out = 0;

    DT_ASSERT_OK(DtAlloc_GrowCapacity(0, 1, 4, 8, &Out));
    DT_ASSERT_EQ(Out, 8);
}

DT_TEST(GrowthDoubles)
{
    size_t Out = 0;

    DT_ASSERT_OK(DtAlloc_GrowCapacity(8, 9, 4, 8, &Out));
    DT_ASSERT_EQ(Out, 16);

    DT_ASSERT_OK(DtAlloc_GrowCapacity(8, 33, 4, 8, &Out));
    DT_ASSERT_EQ(Out, 64);
}

DT_TEST(GrowthLeavesEnoughRoomAlone)
{
    size_t Out = 0;

    DT_ASSERT_OK(DtAlloc_GrowCapacity(64, 10, 4, 8, &Out));
    DT_ASSERT_EQ(Out, 64);
}

// Doubling past half of the address space would wrap the capacity back to a small
// number, and the allocation that followed would be far too small for the writes aimed
// at it. The policy stops at exactly what was asked for instead.
DT_TEST(DoublingStopsInsteadOfWrapping)
{
    size_t Huge = ((size_t)-1 / 2) + 1;
    size_t Out = 0;

    DT_ASSERT_OK(DtAlloc_GrowCapacity(Huge, Huge + 1, 1, 8, &Out));
    DT_ASSERT_EQ(Out, Huge + 1);
}

// The byte count is what reaches the allocator, so that is what has to fit. Asking for
// half the address space worth of eight-byte elements cannot be represented and has to
// be refused rather than truncated.
DT_TEST(ByteCountOverflowIsRefused)
{
    size_t TooMany = ((size_t)-1 / 8) + 1;
    size_t Out = 0;

    DT_ASSERT_EQ(DtAlloc_GrowCapacity(0, TooMany, 8, 8, &Out), -1);
}

DT_TEST(GrowthRejectsBadArguments)
{
    size_t Out = 0;

    DT_ASSERT_EQ(DtAlloc_GrowCapacity(0, 4, 4, 8, NULL), -1);
    DT_ASSERT_EQ(DtAlloc_GrowCapacity(0, 4, 0, 8, &Out), -1);
}

DT_TEST_MAIN("Alloc", DT_RUN(AllocationsAreCounted), DT_RUN(LiveBlocksAreCounted),
             DT_RUN(InjectionFailsTheChosenAllocation),
             DT_RUN(ReallocGrowsThroughTheSeam), DT_RUN(InjectionCoversRealloc),
             DT_RUN(ResetDisarmsInjection), DT_RUN(FreeAcceptsNull),
             DT_RUN(GrowthStartsAtTheMinimum), DT_RUN(GrowthDoubles),
             DT_RUN(GrowthLeavesEnoughRoomAlone), DT_RUN(DoublingStopsInsteadOfWrapping),
             DT_RUN(ByteCountOverflowIsRefused), DT_RUN(GrowthRejectsBadArguments))
