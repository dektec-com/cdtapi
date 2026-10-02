// #*#*#*#*#*#*#*#*#*#*#*#*#*#* OsDmaBuffer.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Memory shared with the driver for DMA, and how it is handed over
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DMA buffer +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Memory that the library allocates and gives to the driver, so that the card can read
// or write it directly (DMA). The driver locks the memory page by page, so it starts on
// a page boundary and covers whole pages.
//
// On Linux the memory is also kept out of a child process made with fork(). Otherwise
// the child would share these pages copy-on-write, the parent's next write would move
// the parent to new pages, and the card would go on writing into the old ones.
//

typedef struct OsDmaBuffer
{
    uint8_t* Data;       // The memory, page-aligned; NULL when not allocated
    size_t Size;         // The size of Data in bytes, a whole number of pages
    void* RawAllocation; // The allocation Data lies in; for OsDmaBuffer_Free only
} OsDmaBuffer;

// Returns the operating system's page size, in bytes.
size_t OsDmaBuffer_PageSize(void);

// Allocates a DMA buffer of at least Size bytes, rounded up to whole pages and filled
// with zeros. Where the platform needs it, keeps it out of a child process. Returns 0,
// or -1 when Size is zero or the allocation fails; *Buf is then empty.
int OsDmaBuffer_Alloc(size_t Size, OsDmaBuffer* Buf);

// Frees the buffer and leaves *Buf empty. The driver must have let go of it first. A
// NULL or empty Buf does nothing.
void OsDmaBuffer_Free(OsDmaBuffer* Buf);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Hand-off +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The command that gives a DMA buffer to the driver is the same on both platforms, but
// the buffer goes in a different place:
//
//   Windows  as the IOCTL's output buffer. With METHOD_OUT_DIRECT, the I/O manager
//            locks it. The m_BufferAddr field of the input structure stays zero.
//   Linux    as an address in the m_BufferAddr field of the input structure. The
//            output buffer is the command's own small output structure.
//
// An OsDmaHandOff holds the three values a command needs for either platform, so the
// command only copies them into place.
//

typedef struct OsDmaHandOff
{
    uint64_t BufferAddr; // The value for the command's m_BufferAddr field
    void* Out;           // The output buffer to pass to OsDrv_Ioctl
    size_t OutSize;      // The size of Out, in bytes
} OsDmaHandOff;

// Fills *HandOff for this platform's driver. FixedOut is the command's own output
// structure, of FixedOutSize bytes, which is the output buffer when the DMA buffer is
// not. A NULL or empty Buf leaves *HandOff all zero.
void OsDmaBuffer_DescribeHandOff(const OsDmaBuffer* Buf, void* FixedOut,
                                 size_t FixedOutSize, OsDmaHandOff* HandOff);

// Fills *HandOff as OsDmaBuffer_DescribeHandOff does, for the platform that
// BufferIsOutput picks: true for Windows, false for Linux. The DtPcie command layer
// calls this one, so that the tests check both ways on every platform.
void OsDmaBuffer_DescribeHandOffAs(bool BufferIsOutput, const OsDmaBuffer* Buf,
                                   void* FixedOut, size_t FixedOutSize,
                                   OsDmaHandOff* HandOff);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Platform part +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Each platform implements these. They are for this layer only.
//

// Returns the page size the operating system reports.
size_t OsPlatform_PageSize(void);

// Keeps the Size bytes at Data out of a child process made with fork(). Returns 0, or
// -1 on failure. Does nothing and returns 0 on a platform without fork().
int OsPlatform_DontFork(uint8_t* Data, size_t Size);

// Undoes OsPlatform_DontFork before the memory is freed, so that the allocator does not
// hand out pages that are still kept out of a child process.
void OsPlatform_DoFork(uint8_t* Data, size_t Size);
