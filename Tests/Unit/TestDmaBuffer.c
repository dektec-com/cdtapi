// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestDmaBuffer.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Unit tests for DMA buffer allocation and the driver hand-off
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "Core/DtAlloc.h"    // Allocation fault injection.
#include "DtTest.h"          // Test framework.
#include "OAL/OsDmaBuffer.h" // Interface under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

static int IsPageAligned(const uint8_t* Ptr)
{
    return ((uintptr_t)Ptr & ((uintptr_t)OsDmaBuffer_PageSize() - 1)) == 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Allocation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(PageSizeIsAPowerOfTwo)
{
    size_t Page = OsDmaBuffer_PageSize();

    DT_ASSERT(Page >= 4096);
    DT_ASSERT_EQ(Page & (Page - 1), 0);
}

// The driver locks the buffer page by page, so the start must be on a page boundary and
// the size must cover whole pages.
DT_TEST(BufferIsPageAlignedAndRounded)
{
    size_t Page = OsDmaBuffer_PageSize();
    OsDmaBuffer Buf;

    DT_ASSERT_OK(OsDmaBuffer_Alloc(1, &Buf));
    DT_ASSERT(IsPageAligned(Buf.Data));
    DT_ASSERT_EQ(Buf.Size, Page);
    OsDmaBuffer_Free(&Buf);

    DT_ASSERT_OK(OsDmaBuffer_Alloc(Page + 1, &Buf));
    DT_ASSERT(IsPageAligned(Buf.Data));
    DT_ASSERT_EQ(Buf.Size, 2 * Page);
    OsDmaBuffer_Free(&Buf);

    DT_ASSERT_OK(OsDmaBuffer_Alloc(3 * Page, &Buf));
    DT_ASSERT_EQ(Buf.Size, 3 * Page);
    OsDmaBuffer_Free(&Buf);
}

// Every byte of the rounded size is inside the allocation. Under AddressSanitizer a
// region carved out one byte too short fails here.
DT_TEST(WholeBufferIsZeroedAndWritable)
{
    OsDmaBuffer Buf;

    DT_ASSERT_OK(OsDmaBuffer_Alloc(5 * OsDmaBuffer_PageSize() + 17, &Buf));

    for (size_t i = 0; i < Buf.Size; i++)
    {
        if (Buf.Data[i] != 0)
            DT_FAIL("byte %zu is %u, not zero", i, (unsigned)Buf.Data[i]);
    }

    memset(Buf.Data, 0xA5, Buf.Size);
    DT_ASSERT_EQ(Buf.Data[Buf.Size - 1], 0xA5);

    OsDmaBuffer_Free(&Buf);
}

// Several buffers alive at once land at different places and each stays aligned; the
// allocator's own placement must not affect the alignment.
DT_TEST(ManyBuffersAreEachAligned)
{
    OsDmaBuffer Bufs[16];
    int i;

    for (i = 0; i < 16; i++)
    {
        DT_ASSERT_OK(OsDmaBuffer_Alloc((size_t)(i + 1) * 1000, &Bufs[i]));
        DT_ASSERT(IsPageAligned(Bufs[i].Data));
    }

    for (i = 0; i < 16; i++)
        OsDmaBuffer_Free(&Bufs[i]);
}

DT_TEST(FreeEmptiesTheBufferAndCanRepeat)
{
    OsDmaBuffer Buf;

    DT_ASSERT_OK(OsDmaBuffer_Alloc(100, &Buf));
    OsDmaBuffer_Free(&Buf);

    DT_ASSERT(Buf.Data == NULL);
    DT_ASSERT(Buf.Block == NULL);
    DT_ASSERT_EQ(Buf.Size, 0);

    OsDmaBuffer_Free(&Buf);
    OsDmaBuffer_Free(NULL);
}

DT_TEST(AllocRejectsBadArguments)
{
    DT_ASSERT_EQ(OsDmaBuffer_Alloc(100, NULL), -1);

    OsDmaBuffer Buf;
    DT_ASSERT_EQ(OsDmaBuffer_Alloc(0, &Buf), -1);
    DT_ASSERT(Buf.Data == NULL);

    // Rounding this up to a whole page would wrap around to a tiny size.
    DT_ASSERT_EQ(OsDmaBuffer_Alloc((size_t)-1, &Buf), -1);
    DT_ASSERT(Buf.Data == NULL);

    // The exact boundary: one byte more than the largest size whose rounding still fits.
    // It must be refused before any allocation is attempted.
    DT_ASSERT_EQ(OsDmaBuffer_Alloc((size_t)-1 - OsDmaBuffer_PageSize() + 2, &Buf), -1);
    DT_ASSERT(Buf.Data == NULL);
}

DT_TEST(AllocationFailureLeavesTheBufferEmpty)
{
    DtAlloc_ResetCount();
    DtAlloc_FailAfter(0);

    OsDmaBuffer Buf;
    DT_ASSERT_EQ(OsDmaBuffer_Alloc(100, &Buf), -1);
    DT_ASSERT(Buf.Data == NULL);
    DT_ASSERT(Buf.Block == NULL);
    DT_ASSERT_EQ(Buf.Size, 0);

    DtAlloc_ResetCount();
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Hand-off +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The Windows convention: the buffer travels as the IOCTL output, and the address field
// stays zero.
DT_TEST(HandOffAsTheOutputBuffer)
{
    OsDmaBuffer Buf;

    DT_ASSERT_OK(OsDmaBuffer_Alloc(100, &Buf));

    OsDmaHandOff HandOff;
    uint8_t Fixed[4];
    OsDmaBuffer_DescribeHandOffAs(true, &Buf, Fixed, sizeof(Fixed), &HandOff);
    DT_ASSERT_EQ(HandOff.BufferAddr, 0);
    DT_ASSERT(HandOff.Out == Buf.Data);
    DT_ASSERT_EQ(HandOff.OutSize, Buf.Size);

    OsDmaBuffer_Free(&Buf);
}

// The Linux convention: the address travels in the input structure, and the output is
// the command's own small structure.
DT_TEST(HandOffAsAnAddress)
{
    OsDmaBuffer Buf;

    DT_ASSERT_OK(OsDmaBuffer_Alloc(100, &Buf));

    OsDmaHandOff HandOff;
    uint8_t Fixed[4];
    OsDmaBuffer_DescribeHandOffAs(false, &Buf, Fixed, sizeof(Fixed), &HandOff);
    DT_ASSERT(HandOff.BufferAddr == (uint64_t)(uintptr_t)Buf.Data);
    DT_ASSERT(HandOff.Out == Fixed);
    DT_ASSERT_EQ(HandOff.OutSize, sizeof(Fixed));

    OsDmaBuffer_Free(&Buf);
}

// Whichever convention the host uses, the platform entry point must pick that one.
DT_TEST(HandOffMatchesThisPlatform)
{
    OsDmaBuffer Buf;
    DT_ASSERT_OK(OsDmaBuffer_Alloc(100, &Buf));

    uint8_t Fixed[4];
    OsDmaHandOff Actual;
    OsDmaBuffer_DescribeHandOff(&Buf, Fixed, sizeof(Fixed), &Actual);
    OsDmaHandOff Expected;
#if defined(_WIN32) || defined(_WIN64)
    OsDmaBuffer_DescribeHandOffAs(true, &Buf, Fixed, sizeof(Fixed), &Expected);
#else
    OsDmaBuffer_DescribeHandOffAs(false, &Buf, Fixed, sizeof(Fixed), &Expected);
#endif

    DT_ASSERT(Actual.BufferAddr == Expected.BufferAddr);
    DT_ASSERT(Actual.Out == Expected.Out);
    DT_ASSERT_EQ(Actual.OutSize, Expected.OutSize);

    OsDmaBuffer_Free(&Buf);
}

// An unallocated buffer describes nothing, rather than handing the driver a null address
// that it would try to lock.
DT_TEST(EmptyBufferDescribesNothing)
{
    OsDmaBuffer Empty;

    memset(&Empty, 0, sizeof(Empty));
    OsDmaHandOff HandOff;
    uint8_t Fixed[4];
    OsDmaBuffer_DescribeHandOffAs(false, &Empty, Fixed, sizeof(Fixed), &HandOff);
    DT_ASSERT_EQ(HandOff.BufferAddr, 0);
    DT_ASSERT(HandOff.Out == NULL);
    DT_ASSERT_EQ(HandOff.OutSize, 0);

    OsDmaBuffer_DescribeHandOffAs(true, NULL, Fixed, sizeof(Fixed), &HandOff);
    DT_ASSERT(HandOff.Out == NULL);

    OsDmaBuffer_DescribeHandOffAs(true, &Empty, Fixed, sizeof(Fixed), NULL);
}

DT_TEST_MAIN("DmaBuffer", DT_RUN(PageSizeIsAPowerOfTwo),
             DT_RUN(BufferIsPageAlignedAndRounded),
             DT_RUN(WholeBufferIsZeroedAndWritable), DT_RUN(ManyBuffersAreEachAligned),
             DT_RUN(FreeEmptiesTheBufferAndCanRepeat), DT_RUN(AllocRejectsBadArguments),
             DT_RUN(AllocationFailureLeavesTheBufferEmpty),
             DT_RUN(HandOffAsTheOutputBuffer), DT_RUN(HandOffAsAnAddress),
             DT_RUN(HandOffMatchesThisPlatform), DT_RUN(EmptyBufferDescribesNothing))
