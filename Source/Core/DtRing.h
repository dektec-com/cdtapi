// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtRing.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Read side of the shared DMA ring buffer
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtRing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Reads a ring buffer that the card fills by DMA. The buffer belongs to the driver; a
// DtRing only keeps track of two positions in it:
//
//   write offset   where the card has written up to. The library asks the driver for it,
//                  with a command such as DT_CHSDIRX_CMD_GET_WRITE_OFFSET, and passes it
//                  to DtRing_SetWriteOffset.
//   read offset    where the library has read up to. It moves as the library reads, and
//                  goes back to the driver with a command such as
//                  DT_CHSDIRX_CMD_SET_READ_OFFSET.
//
// The load is the number of bytes between the two: (write + size - read) % size.
//
// A completely full ring and an empty one would both have equal offsets, so part of the
// buffer always stays free. The card keeps one data word free, not one byte: eight
// bytes for a PCIe data width of 64 bits. The driver reports that reserve, and the
// ring holds at most Size - ReservedBytes bytes.
//
// A read that runs past the end of the buffer continues at its start; the functions
// here handle that, so that their callers need not.
//
// A DtRing is not thread-safe: its user makes sure one thread at a time uses it.
//

typedef struct DtRing
{
    uint8_t* Base;      // The start of the buffer
    size_t Size;        // The size of the buffer, in bytes
    size_t MaxLoad;     // The most bytes the ring holds: Size - ReservedBytes
    size_t ReadOffset;  // Where the library reads next, in bytes from Base
    size_t WriteOffset; // Where the card writes next, in bytes from Base
} DtRing;

// Prepares Ring to read the Size bytes at Base, with both offsets at zero.
// ReservedBytes is the part of the buffer that always stays free, as the driver reports
// it. Returns 0, or -1 when Ring or Base is NULL, ReservedBytes is zero, or ReservedBytes
// is not less than Size.
int DtRing_Init(DtRing* Ring, uint8_t* Base, size_t Size, size_t ReservedBytes);

// Sets where the card has written up to, as the driver reports it. Returns 0, or -1
// when Offset is outside the buffer or would make the load more than Size -
// ReservedBytes. Either means the driver and the library disagree about the ring, and
// reading on would read garbage.
int DtRing_SetWriteOffset(DtRing* Ring, size_t Offset);

// Empties the ring and sets both offsets to Offset, where reading and writing start
// again. Returns 0, or -1 when Offset is outside the buffer.
int DtRing_Restart(DtRing* Ring, size_t Offset);

// Returns how many bytes there are to read.
size_t DtRing_Load(const DtRing* Ring);

// Returns how many more bytes the card can write before the ring is full: Size -
// ReservedBytes - Load.
size_t DtRing_Room(const DtRing* Ring);

// Copies the next Length bytes to Dst, without reading them: the read offset stays.
// Returns 0, or -1 when fewer than Length bytes are there to read.
int DtRing_Peek(const DtRing* Ring, void* Dst, size_t Length);

// Copies Length bytes, starting Offset bytes after the read offset, to Dst, without
// reading them. Returns 0, or -1 when those bytes are not all there to read.
int DtRing_PeekAt(const DtRing* Ring, size_t Offset, void* Dst, size_t Length);

// Returns the address of Length bytes starting Offset bytes after the read offset, so
// that the caller can use them without a copy. Returns NULL when they are not all there
// to read, or when they run past the end of the buffer; DtRing_PeekAt copies those.
const uint8_t* DtRing_Span(const DtRing* Ring, size_t Offset, size_t Length);

// Skips the next Length bytes without copying them. Returns 0, or -1 when fewer than
// Length bytes are there to read.
int DtRing_Skip(DtRing* Ring, size_t Length);

// Reads the next Length bytes into Dst. Returns 0, or -1 when fewer than Length bytes
// are there to read; nothing is read then.
int DtRing_Read(DtRing* Ring, void* Dst, size_t Length);

// Returns the read offset, to pass back to the driver.
size_t DtRing_ReadOffset(const DtRing* Ring);

// Throws away all bytes there are to read, by setting the read offset to the write
// offset. This is how a FIFO is cleared.
void DtRing_Clear(DtRing* Ring);
