// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtBuf.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Reference-counted byte buffer with a release callback
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtBuf +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A block of bytes with a reference count. It lets the library hand a frame to a
// consumer without copying it: the consumer keeps a reference for as long as it needs
// the frame. At 12G-SDI a copy per frame costs about 1.5 GB/s per stream.
//
// There are two ways to make one:
//
//   DtBuf_Alloc  allocates the bytes, and frees them when the last reference goes.
//   DtBuf_Wrap   takes bytes that already exist, and calls a release function when the
//                last reference goes, for example to return a frame to a pool.
//
// The new buffer has one reference, which belongs to the caller. Each DtBuf_Ref must be
// matched by a DtBuf_Unref. The count is atomic, so a buffer made on one thread can be
// released on another.
//

typedef struct DtBuf DtBuf;

// The function DtBuf_Wrap calls once, when the last reference to the buffer goes.
// Context is the pointer given to DtBuf_Wrap; Data and Size are the wrapped bytes.
typedef void (*DtBufReleaseFunc)(void* Context, uint8_t* Data, size_t Size);

// Allocates a buffer of Size bytes, with one reference. The bytes are not initialised.
// Returns NULL when Size is zero or there is not enough memory.
DtBuf* DtBuf_Alloc(size_t Size);

// Makes a buffer of bytes the caller already has, with one reference. Release is called
// when the last reference goes; it may be NULL when the bytes outlive the buffer and
// nothing needs to be done with them. Returns NULL when Data is NULL, Size is zero, or
// there is not enough memory.
DtBuf* DtBuf_Wrap(uint8_t* Data, size_t Size, DtBufReleaseFunc Release, void* Context);

// Adds a reference to Buf and returns Buf, so that it can be used in an assignment.
// Returns NULL for a NULL Buf.
DtBuf* DtBuf_Ref(DtBuf* Buf);

// Drops the caller's reference and sets *Buf to NULL, so that the caller cannot use it
// again by mistake. When this was the last reference, the bytes and the buffer are
// released. A NULL Buf or *Buf does nothing.
void DtBuf_Unref(DtBuf** Buf);

// Returns the bytes of Buf, which stay valid while the caller holds a reference. Returns
// NULL for a NULL Buf.
uint8_t* DtBuf_Data(const DtBuf* Buf);

// Returns the number of bytes of Buf, or zero for a NULL Buf.
size_t DtBuf_Size(const DtBuf* Buf);

// Returns the number of references to Buf, or zero for a NULL Buf. Meant for tests and
// diagnostics: another thread can change the count straight after.
int DtBuf_RefCount(const DtBuf* Buf);
