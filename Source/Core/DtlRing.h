// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtlRing.h *#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Read side of the shared DMA ring buffer
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DTL_RING_H
#define CDTAPILITE_DTL_RING_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ DtlRing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
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
// One byte of the buffer is never used, because a completely full ring and a completely
// empty one both have the two offsets equal and could not be told apart. So the maximum
// load is Size - 1.
//
// The wrap is the part that gets written wrong, so it lives here once rather than in
// every caller: a read that crosses the end of the buffer is two copies.
//
// Not thread-safe. The channel that owns the ring serialises access to it.
//

typedef struct DtlRing
{
    uint8_t* Base;
    size_t Size;
    size_t ReadOffset;
    size_t WriteOffset;
} DtlRing;

// Prepares a ring over Size bytes at Base, with both offsets at zero. Returns 0 on
// success, -1 when Base is NULL or Size is less than two.
int DtlRingInit(DtlRing* Ring, uint8_t* Base, size_t Size);

// Records where the producer has got to. Returns 0 on success, -1 when Offset is not
// inside the buffer.
int DtlRingSetWriteOffset(DtlRing* Ring, size_t Offset);

// How many bytes are available to read.
size_t DtlRingLoad(const DtlRing* Ring);

// How many bytes could still be written before the ring is full.
size_t DtlRingFree(const DtlRing* Ring);

// Copies Length bytes to Dst without consuming them, handling the wrap. Returns 0 on
// success, -1 when fewer than Length bytes are available.
int DtlRingPeek(const DtlRing* Ring, void* Dst, size_t Length);

// Consumes Length bytes without copying them. Returns 0 on success, -1 when fewer than
// Length bytes are available.
int DtlRingSkip(DtlRing* Ring, size_t Length);

// Copies Length bytes to Dst and consumes them. Returns 0 on success, -1 when fewer than
// Length bytes are available, in which case nothing is consumed.
int DtlRingRead(DtlRing* Ring, void* Dst, size_t Length);

// The read offset, to be handed back to the driver.
size_t DtlRingReadOffset(const DtlRing* Ring);

// Drops everything available, by moving the read offset to the write offset. That is
// what a FIFO clear does.
void DtlRingClear(DtlRing* Ring);

#endif // CDTAPILITE_DTL_RING_H
