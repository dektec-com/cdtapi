// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtlBuf.h *#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Reference-counted byte buffer with a release callback
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DTL_BUF_H
#define CDTAPILITE_DTL_BUF_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtlBuf +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A block of bytes with a reference count and an optional release callback, so that a
// frame can be handed to a consumer without copying it. At 12G-SDI a copy per frame is
// about 1.5 GB/s per stream, which is the whole reason this type exists.
//
// Two ways to make one:
//
//   DtlBufAlloc  allocates the bytes and frees them when the last reference goes.
//   DtlBufWrap   takes bytes that already exist, and calls the supplied release
//                function when the last reference goes. That is how a frame pointing
//                into a DMA ring is handed out: the callback returns the space to the
//                ring rather than freeing it.
//
// The count is atomic, because a buffer produced on the receive thread is routinely
// released on the caller's thread.
//
// Ownership rule: DtlBufAlloc and DtlBufWrap return a buffer with one reference, which
// belongs to the caller. Every DtlBufRef must be matched by a DtlBufUnref.
//

typedef struct DtlBuf DtlBuf;

// Called once, when the last reference to a wrapped buffer is dropped. Opaque is the
// pointer given to DtlBufWrap.
typedef void (*DtlBufReleaseFunc)(void* Opaque, uint8_t* Data, size_t Size);

// Allocates a buffer of Size bytes with a reference count of one. The contents are
// uninitialised. Returns NULL when out of memory, or when Size is zero.
DtlBuf* DtlBufAlloc(size_t Size);

// Wraps bytes the caller already owns, with a reference count of one. Release may be
// NULL, which means the bytes outlive the buffer and nothing has to be done for them.
// Returns NULL when out of memory, or when Data is NULL, or when Size is zero.
DtlBuf* DtlBufWrap(uint8_t* Data, size_t Size, DtlBufReleaseFunc Release, void* Opaque);

// Adds a reference and returns Buf, so that it can be used in an assignment. Passing
// NULL returns NULL.
DtlBuf* DtlBufRef(DtlBuf* Buf);

// Drops a reference and clears the caller's pointer. Releases the bytes and the buffer
// itself when this was the last reference. Passing NULL, or a pointer to NULL, does
// nothing. The pointer is cleared so that a stale handle cannot be used by accident.
void DtlBufUnref(DtlBuf** Buf);

// The bytes. Valid for as long as the caller holds a reference. NULL when Buf is NULL.
uint8_t* DtlBufData(const DtlBuf* Buf);

// The number of bytes. Zero when Buf is NULL.
size_t DtlBufSize(const DtlBuf* Buf);

// The current reference count. Intended for tests and diagnostics; in live code the
// answer can be stale the moment it is returned. Zero when Buf is NULL.
long DtlBufRefCount(const DtlBuf* Buf);

#endif // CDTAPILITE_DTL_BUF_H
