// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtBuf.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Reference-counted byte buffer with a release callback
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DT_BUF_H
#define CDTAPILITE_DT_BUF_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtBuf +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A block of bytes with a reference count and an optional release callback, so that a
// frame can be handed to a consumer without copying it. At 12G-SDI a copy per frame is
// about 1.5 GB/s per stream, which is the whole reason this type exists.
//
// Two ways to make one:
//
//   DtBufAlloc  allocates the bytes and frees them when the last reference goes.
//   DtBufWrap   takes bytes that already exist, and calls the supplied release
//                function when the last reference goes. That is how a frame pointing
//                into a DMA ring is handed out: the callback returns the space to the
//                ring rather than freeing it.
//
// The count is atomic, because a buffer produced on the receive thread is routinely
// released on the caller's thread.
//
// Ownership rule: DtBufAlloc and DtBufWrap return a buffer with one reference, which
// belongs to the caller. Every DtBufRef must be matched by a DtBufUnref.
//

typedef struct DtBuf DtBuf;

// Called once, when the last reference to a wrapped buffer is dropped. Opaque is the
// pointer given to DtBufWrap.
typedef void (*DtBufReleaseFunc)(void* Opaque, uint8_t* Data, size_t Size);

// Allocates a buffer of Size bytes with a reference count of one. The contents are
// uninitialised. Returns NULL when out of memory, or when Size is zero.
DtBuf* DtBufAlloc(size_t Size);

// Wraps bytes the caller already owns, with a reference count of one. Release may be
// NULL, which means the bytes outlive the buffer and nothing has to be done for them.
// Returns NULL when out of memory, or when Data is NULL, or when Size is zero.
DtBuf* DtBufWrap(uint8_t* Data, size_t Size, DtBufReleaseFunc Release, void* Opaque);

// Adds a reference and returns Buf, so that it can be used in an assignment. Passing
// NULL returns NULL.
DtBuf* DtBufRef(DtBuf* Buf);

// Drops a reference and clears the caller's pointer. Releases the bytes and the buffer
// itself when this was the last reference. Passing NULL, or a pointer to NULL, does
// nothing. The pointer is cleared so that a stale handle cannot be used by accident.
void DtBufUnref(DtBuf** Buf);

// The bytes. Valid for as long as the caller holds a reference. NULL when Buf is NULL.
uint8_t* DtBufData(const DtBuf* Buf);

// The number of bytes. Zero when Buf is NULL.
size_t DtBufSize(const DtBuf* Buf);

// The current reference count. Intended for tests and diagnostics; in live code the
// answer can be stale the moment it is returned. Zero when Buf is NULL.
long DtBufRefCount(const DtBuf* Buf);

#endif // CDTAPILITE_DT_BUF_H
