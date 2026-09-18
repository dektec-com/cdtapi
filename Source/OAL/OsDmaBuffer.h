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
// Memory the library allocates and gives to the driver, which locks it and lets the card
// read or write it directly. It has to start on a page boundary and cover whole pages,
// because the driver locks it page by page.
//
// On Linux it also has to be protected against fork(). Without MADV_DONTFORK a child
// process gets copy-on-write mappings of these pages, the parent's next write moves it to
// fresh pages, and the card keeps writing into the old ones.
//

typedef struct OsDmaBuffer
{
    uint8_t* Data; // Page-aligned; NULL when not allocated
    size_t Size;   // Rounded up to whole pages
    void* Block;   // The allocation Data was carved from; private
} OsDmaBuffer;

// The operating system's page size.
size_t OsDmaBuffer_PageSize(void);

// Allocates at least Size bytes, page-aligned and rounded up to whole pages, and protects
// them against fork() where the platform needs it. The contents are zeroed. Returns 0 on
// success and -1 on failure, leaving Buf empty.
int OsDmaBuffer_Alloc(size_t Size, OsDmaBuffer* Buf);

// Releases the buffer and leaves Buf empty. The driver must already have let go of it.
// Passing NULL, or an empty buffer, does nothing.
void OsDmaBuffer_Free(OsDmaBuffer* Buf);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Hand-off +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The two drivers take the buffer in different places, in the same command:
//
//   Windows  as the IOCTL's output buffer. METHOD_OUT_DIRECT makes the I/O manager lock
//            it, and the address field in the input structure is left at zero.
//   Linux    as a virtual address in the input structure's m_BufferAddr field. The
//            output buffer is the command's small fixed-size output structure.
//
// That is DtProxy.cpp:3417-3434. If callers branched on the platform themselves, the
// difference would leak into every command that registers a buffer, so it is resolved
// here once, into the three values a caller needs.
//

typedef struct OsDmaHandOff
{
    uint64_t BufferAddr; // Value for the command's m_BufferAddr field
    void* Out;           // Output buffer to pass to OsDrv_IoCtl
    size_t OutSize;      // Its size
} OsDmaHandOff;

// Describes the hand-off for this platform's driver. Fixed is the command's own output
// structure and FixedSize its size, used where the buffer does not travel as the output.
void OsDmaBuffer_DescribeHandOff(const OsDmaBuffer* Buf, void* Fixed, size_t FixedSize,
                                 OsDmaHandOff* HandOff);

// The same, with the convention chosen explicitly: true for Windows, false for Linux.
// Exists so that both conventions are tested on every platform.
void OsDmaBuffer_DescribeHandOffAs(bool BufferIsOutput, const OsDmaBuffer* Buf,
                                   void* Fixed, size_t FixedSize, OsDmaHandOff* HandOff);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Platform part +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Implemented per platform. Not for use outside the abstraction layer.
//

// Returns the page size reported by the operating system.
size_t OsPlatform_PageSize(void);

// Excludes [Data, Data + Size) from being copied into a child process. Returns 0 on
// success and -1 on failure. A no-op that succeeds where the platform has no fork().
int OsPlatform_DontFork(uint8_t* Data, size_t Size);

// Undoes OsPlatform_DontFork before the memory goes back to the allocator, so that pages
// the allocator hands out again are not left excluded from a child process.
void OsPlatform_DoFork(uint8_t* Data, size_t Size);
