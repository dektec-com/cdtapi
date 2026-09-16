// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestDmaBuffer.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Unit tests for DMA buffer allocation and the driver hand-off
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "Core/DtlAlloc.h"   // Allocation fault injection.
#include "DtlTest.h"         // Test framework.
#include "OAL/OsDmaBuffer.h" // Interface under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

static int IsPageAligned(const uint8_t* Ptr)
{
    return ((uintptr_t)Ptr & ((uintptr_t)OsPageSize() - 1)) == 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Allocation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DTL_TEST(PageSizeIsAPowerOfTwo)
{
    size_t Page = OsPageSize();

    DTL_ASSERT(Page >= 4096);
    DTL_ASSERT_EQ(Page & (Page - 1), 0);
}

// The driver locks the buffer page by page, so the start must be on a page boundary and
// the size must cover whole pages.
DTL_TEST(BufferIsPageAlignedAndRounded)
{
    size_t Page = OsPageSize();
    OsDmaBuffer Buf;

    DTL_ASSERT_OK(OsDmaBufferAlloc(1, &Buf));
    DTL_ASSERT(IsPageAligned(Buf.Data));
    DTL_ASSERT_EQ(Buf.Size, Page);
    OsDmaBufferFree(&Buf);

    DTL_ASSERT_OK(OsDmaBufferAlloc(Page + 1, &Buf));
    DTL_ASSERT(IsPageAligned(Buf.Data));
    DTL_ASSERT_EQ(Buf.Size, 2 * Page);
    OsDmaBufferFree(&Buf);

    DTL_ASSERT_OK(OsDmaBufferAlloc(3 * Page, &Buf));
    DTL_ASSERT_EQ(Buf.Size, 3 * Page);
    OsDmaBufferFree(&Buf);
}

// Every byte of the rounded size is inside the allocation. Under AddressSanitizer a
// region carved out one byte too short fails here.
DTL_TEST(WholeBufferIsZeroedAndWritable)
{
    OsDmaBuffer Buf;
    size_t i;

    DTL_ASSERT_OK(OsDmaBufferAlloc(5 * OsPageSize() + 17, &Buf));

    for (i = 0; i < Buf.Size; i++)
    {
        if (Buf.Data[i] != 0)
            DTL_FAIL("byte %zu is %u, not zero", i, (unsigned)Buf.Data[i]);
    }

    memset(Buf.Data, 0xA5, Buf.Size);
    DTL_ASSERT_EQ(Buf.Data[Buf.Size - 1], 0xA5);

    OsDmaBufferFree(&Buf);
}

// Several buffers alive at once land at different places and each stays aligned; the
// allocator's own placement must not affect the alignment.
DTL_TEST(ManyBuffersAreEachAligned)
{
    OsDmaBuffer Bufs[16];
    int i;

    for (i = 0; i < 16; i++)
    {
        DTL_ASSERT_OK(OsDmaBufferAlloc((size_t)(i + 1) * 1000, &Bufs[i]));
        DTL_ASSERT(IsPageAligned(Bufs[i].Data));
    }

    for (i = 0; i < 16; i++)
        OsDmaBufferFree(&Bufs[i]);
}

DTL_TEST(FreeEmptiesTheBufferAndCanRepeat)
{
    OsDmaBuffer Buf;

    DTL_ASSERT_OK(OsDmaBufferAlloc(100, &Buf));
    OsDmaBufferFree(&Buf);

    DTL_ASSERT(Buf.Data == NULL);
    DTL_ASSERT(Buf.Block == NULL);
    DTL_ASSERT_EQ(Buf.Size, 0);

    OsDmaBufferFree(&Buf);
    OsDmaBufferFree(NULL);
}

DTL_TEST(AllocRejectsBadArguments)
{
    OsDmaBuffer Buf;

    DTL_ASSERT_EQ(OsDmaBufferAlloc(100, NULL), -1);

    DTL_ASSERT_EQ(OsDmaBufferAlloc(0, &Buf), -1);
    DTL_ASSERT(Buf.Data == NULL);

    // Rounding this up to a whole page would wrap around to a tiny size.
    DTL_ASSERT_EQ(OsDmaBufferAlloc((size_t)-1, &Buf), -1);
    DTL_ASSERT(Buf.Data == NULL);

    // The exact boundary: one byte more than the largest size whose rounding still fits.
    // It must be refused before any allocation is attempted.
    DTL_ASSERT_EQ(OsDmaBufferAlloc((size_t)-1 - OsPageSize() + 2, &Buf), -1);
    DTL_ASSERT(Buf.Data == NULL);
}

DTL_TEST(AllocationFailureLeavesTheBufferEmpty)
{
    OsDmaBuffer Buf;

    DtlAllocResetCount();
    DtlAllocFailAfter(0);

    DTL_ASSERT_EQ(OsDmaBufferAlloc(100, &Buf), -1);
    DTL_ASSERT(Buf.Data == NULL);
    DTL_ASSERT(Buf.Block == NULL);
    DTL_ASSERT_EQ(Buf.Size, 0);

    DtlAllocResetCount();
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Hand-off +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The Windows convention: the buffer travels as the IOCTL output, and the address field
// stays zero.
DTL_TEST(HandOffAsTheOutputBuffer)
{
    OsDmaBuffer Buf;
    OsDmaHandOff HandOff;
    uint8_t Fixed[4];

    DTL_ASSERT_OK(OsDmaBufferAlloc(100, &Buf));

    OsDmaDescribeHandOffAs(true, &Buf, Fixed, sizeof(Fixed), &HandOff);
    DTL_ASSERT_EQ(HandOff.BufferAddr, 0);
    DTL_ASSERT(HandOff.Out == Buf.Data);
    DTL_ASSERT_EQ(HandOff.OutSize, Buf.Size);

    OsDmaBufferFree(&Buf);
}

// The Linux convention: the address travels in the input structure, and the output is
// the command's own small structure.
DTL_TEST(HandOffAsAnAddress)
{
    OsDmaBuffer Buf;
    OsDmaHandOff HandOff;
    uint8_t Fixed[4];

    DTL_ASSERT_OK(OsDmaBufferAlloc(100, &Buf));

    OsDmaDescribeHandOffAs(false, &Buf, Fixed, sizeof(Fixed), &HandOff);
    DTL_ASSERT(HandOff.BufferAddr == (uint64_t)(uintptr_t)Buf.Data);
    DTL_ASSERT(HandOff.Out == Fixed);
    DTL_ASSERT_EQ(HandOff.OutSize, sizeof(Fixed));

    OsDmaBufferFree(&Buf);
}

// Whichever convention the host uses, the platform entry point must pick that one.
DTL_TEST(HandOffMatchesThisPlatform)
{
    OsDmaBuffer Buf;
    OsDmaHandOff Actual;
    OsDmaHandOff Expected;
    uint8_t Fixed[4];

    DTL_ASSERT_OK(OsDmaBufferAlloc(100, &Buf));

    OsDmaDescribeHandOff(&Buf, Fixed, sizeof(Fixed), &Actual);
#if defined(_WIN32) || defined(_WIN64)
    OsDmaDescribeHandOffAs(true, &Buf, Fixed, sizeof(Fixed), &Expected);
#else
    OsDmaDescribeHandOffAs(false, &Buf, Fixed, sizeof(Fixed), &Expected);
#endif

    DTL_ASSERT(Actual.BufferAddr == Expected.BufferAddr);
    DTL_ASSERT(Actual.Out == Expected.Out);
    DTL_ASSERT_EQ(Actual.OutSize, Expected.OutSize);

    OsDmaBufferFree(&Buf);
}

// An unallocated buffer describes nothing, rather than handing the driver a null address
// that it would try to lock.
DTL_TEST(EmptyBufferDescribesNothing)
{
    OsDmaBuffer Empty;
    OsDmaHandOff HandOff;
    uint8_t Fixed[4];

    memset(&Empty, 0, sizeof(Empty));
    OsDmaDescribeHandOffAs(false, &Empty, Fixed, sizeof(Fixed), &HandOff);
    DTL_ASSERT_EQ(HandOff.BufferAddr, 0);
    DTL_ASSERT(HandOff.Out == NULL);
    DTL_ASSERT_EQ(HandOff.OutSize, 0);

    OsDmaDescribeHandOffAs(true, NULL, Fixed, sizeof(Fixed), &HandOff);
    DTL_ASSERT(HandOff.Out == NULL);

    OsDmaDescribeHandOffAs(true, &Empty, Fixed, sizeof(Fixed), NULL);
}

DTL_TEST_MAIN("DmaBuffer", DTL_RUN(PageSizeIsAPowerOfTwo),
              DTL_RUN(BufferIsPageAlignedAndRounded),
              DTL_RUN(WholeBufferIsZeroedAndWritable), DTL_RUN(ManyBuffersAreEachAligned),
              DTL_RUN(FreeEmptiesTheBufferAndCanRepeat),
              DTL_RUN(AllocRejectsBadArguments),
              DTL_RUN(AllocationFailureLeavesTheBufferEmpty),
              DTL_RUN(HandOffAsTheOutputBuffer), DTL_RUN(HandOffAsAnAddress),
              DTL_RUN(HandOffMatchesThisPlatform), DTL_RUN(EmptyBufferDescribesNothing))
