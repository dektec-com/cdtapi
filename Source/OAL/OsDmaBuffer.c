// #*#*#*#*#*#*#*#*#*#*#*#*#*#* OsDmaBuffer.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Memory shared with the driver for DMA - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
#include "Core/DtlAlloc.h" // Allocation seam.
#include "OsDmaBuffer.h"   // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DMA buffer +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPageSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t OsPageSize(void)
{
    static size_t Cached = 0;

    if (Cached == 0)
        Cached = OsPlatformPageSize();

    return Cached;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsDmaBufferAlloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Allocates one block large enough to contain a page-aligned region of the rounded size
// wherever the allocator happens to place it, then carves that region out. This is the
// scheme DTAPI uses in Utility::MallocAligned, which is known to work with these drivers;
// here the original block is kept in the structure instead of in front of the data.
//
// It goes through the allocation seam, so a test can make it fail.
//
int OsDmaBufferAlloc(size_t Size, OsDmaBuffer* Buf)
{
    size_t Page = OsPageSize();
    size_t Rounded;
    size_t Total;
    uint8_t* Block;
    uint8_t* Data;

    if (Buf == NULL)
        return -1;

    memset(Buf, 0, sizeof(*Buf));

    // The page size must be a power of two for the masks below to align anything. Only an
    // operating system reporting a nonsensical page size fails this, so the tests do
    // not reach it.
    if (Size == 0 || Page == 0 || (Page & (Page - 1)) != 0)
        return -1;

    if (Size > (size_t)-1 - (Page - 1))
        return -1;
    Rounded = (Size + Page - 1) & ~(Page - 1);

    // No second check is needed. Having passed the one above, Rounded is at most the
    // largest page multiple that fits, SIZE_MAX - Page + 1, so adding Page - 1 reaches at
    // most SIZE_MAX.
    Total = Rounded + Page - 1;

    Block = (uint8_t*)DtlMalloc(Total);
    if (Block == NULL)
        return -1;

    Data = (uint8_t*)(((uintptr_t)Block + Page - 1) & ~((uintptr_t)Page - 1));
    memset(Data, 0, Rounded);

    // A buffer that a child process could take over is worse than no buffer: the card
    // would keep writing into pages this process has stopped reading. So failure here
    // is fatal.
    // Only madvise on Linux can fail, so on Windows the tests do not reach this branch.
    if (OsPlatformDontFork(Data, Rounded) != 0)
    {
        DtlFree(Block);
        return -1;
    }

    Buf->Data = Data;
    Buf->Size = Rounded;
    Buf->Block = Block;
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsDmaBufferFree -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void OsDmaBufferFree(OsDmaBuffer* Buf)
{
    if (Buf == NULL || Buf->Block == NULL)
        return;

    OsPlatformDoFork(Buf->Data, Buf->Size);
    DtlFree(Buf->Block);
    memset(Buf, 0, sizeof(*Buf));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Hand-off +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsDmaDescribeHandOffAs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void OsDmaDescribeHandOffAs(bool BufferIsOutput, const OsDmaBuffer* Buf, void* Fixed,
                            size_t FixedSize, OsDmaHandOff* HandOff)
{
    if (HandOff == NULL)
        return;

    memset(HandOff, 0, sizeof(*HandOff));

    if (Buf == NULL || Buf->Data == NULL)
        return;

    if (BufferIsOutput)
    {
        HandOff->BufferAddr = 0;
        HandOff->Out = Buf->Data;
        HandOff->OutSize = Buf->Size;
        return;
    }

    // Converted through uintptr_t, which is unsigned, so a 32-bit address is
    // zero-extended into the 64-bit field. DTAPI masks off the upper half on 32-bit
    // Linux for the same reason; the unsigned conversion makes that mask unnecessary.
    HandOff->BufferAddr = (uint64_t)(uintptr_t)Buf->Data;
    HandOff->Out = Fixed;
    HandOff->OutSize = FixedSize;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsDmaDescribeHandOff -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void OsDmaDescribeHandOff(const OsDmaBuffer* Buf, void* Fixed, size_t FixedSize,
                          OsDmaHandOff* HandOff)
{
#if defined(_WIN32) || defined(_WIN64)
    OsDmaDescribeHandOffAs(true, Buf, Fixed, FixedSize, HandOff);
#else
    OsDmaDescribeHandOffAs(false, Buf, Fixed, FixedSize, HandOff);
#endif
}
