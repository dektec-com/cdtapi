// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtRing.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Read side of the shared DMA ring buffer
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DT_RING_H
#define CDTAPILITE_DT_RING_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtRing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A view over the DMA buffer shared with the driver, not a queue that owns anything.
// The hardware writes into that buffer and the driver reports how far it has got; the
// library tracks how far it has read and tells the driver.
//
//   write offset   moves forward in the driver, read with
//                  DT_CDMAC_CMD_GET_RX_WRITE_OFFSET
//   read offset    moves forward here, written back with DT_CDMAC_CMD_SET_RX_READ_OFFSET
//   load           how many bytes are available: (write + size - read) % size
//
// Part of the buffer is always kept free, because a completely full ring and a completely
// empty one both have the two offsets equal and could not be told apart. The hardware
// keeps one data word free, not one byte: for a PCIe data width of 64 bits that is eight
// bytes. The reserve is therefore a parameter, taken from the DMA controller's
// properties, and the maximum load is Size - Reserve.
//
// The wrap is the part that gets written wrong, so it lives here once rather than in
// every caller: a read that crosses the end of the buffer is two copies.
//
// Not thread-safe. The channel that owns the ring serialises access to it.
//

typedef struct DtRing
{
    uint8_t* Base;
    size_t Size;
    size_t MaxLoad;
    size_t ReadOffset;
    size_t WriteOffset;
} DtRing;

// Prepares a ring over Size bytes at Base, with both offsets at zero, keeping Reserve
// bytes permanently free. Returns 0 on success, -1 when Base is NULL, when Reserve is
// zero, or when Reserve leaves no room at all.
int DtRingInit(DtRing* Ring, uint8_t* Base, size_t Size, size_t Reserve);

// Records where the producer has got to. Returns 0 on success, and -1 when Offset is not
// inside the buffer or would put more than Size - Reserve bytes in the ring. Either means
// the driver and the library disagree about the ring, and reading on would read garbage.
int DtRingSetWriteOffset(DtRing* Ring, size_t Offset);

// How many bytes are available to read.
size_t DtRingLoad(const DtRing* Ring);

// How many bytes could still be written before the ring is full: Size - Reserve - Load.
size_t DtRingFree(const DtRing* Ring);

// Copies Length bytes to Dst without consuming them, handling the wrap. Returns 0 on
// success, -1 when fewer than Length bytes are available.
int DtRingPeek(const DtRing* Ring, void* Dst, size_t Length);

// Consumes Length bytes without copying them. Returns 0 on success, -1 when fewer than
// Length bytes are available.
int DtRingSkip(DtRing* Ring, size_t Length);

// Copies Length bytes to Dst and consumes them. Returns 0 on success, -1 when fewer than
// Length bytes are available, in which case nothing is consumed.
int DtRingRead(DtRing* Ring, void* Dst, size_t Length);

// The read offset, to be handed back to the driver.
size_t DtRingReadOffset(const DtRing* Ring);

// Drops everything available, by moving the read offset to the write offset. That is
// what a FIFO clear does.
void DtRingClear(DtRing* Ring);

#endif // CDTAPILITE_DT_RING_H
