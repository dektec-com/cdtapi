// #*#*#*#*#*#*#*#*#*#*#*#*#* LinIoctlBuffer.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Layout of the single in/out buffer a Linux DekTec IOCTL uses
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Layout +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Windows gives DeviceIoControl separate input and output buffers. Linux gives ioctl one
// pointer, so a DekTec driver on Linux reads its input from and writes its output to the
// same block of memory. This header describes that block.
//
//   offset 0        UInt InSize     only for an IOCTL built with DT_IOCTL_MAGIC_SIZE
//   offset 4        UInt OutSize    only for an IOCTL built with DT_IOCTL_MAGIC_SIZE
//   offset Reserve  input structure
//
// The driver writes its answer starting at offset 0, over the size header. The block
// has to be large enough for whichever is bigger: the header plus the input, or the
// output.
//
// The size header exists because _IOWR encodes the size of one fixed structure into the
// IOCTL number, while several commands take variable-length input. The header tells the
// driver the real sizes. DT_IOCTL_MAGIC_SIZE is the magic number that says it is there,
// and every command CDtapiLite issues is built with it.
//
// This is kept apart from the Linux backend, and free of any Linux header, so that it
// can be tested on every platform. The layout is the part of that backend most likely to
// be wrong and the part that fails most quietly: a shifted header makes the driver read
// the wrong command rather than refuse it.
//

// Bytes the size header occupies when present: two 32-bit unsigned integers.
#define LIN_IOCTL_SIZE_HEADER_BYTES (2 * sizeof(uint32_t))

// The number of bytes the shared block needs.
size_t LinIoctlBuffer_Size(bool SizeHeader, size_t InSize, size_t OutSize);

// Writes the optional size header and the input into Buf. Returns 0 on success, -1 when
// Buf is too small, when In is NULL with a non-zero InSize, or when a size does not fit
// the header's 32 bits.
int LinIoctlBuffer_Pack(bool SizeHeader, const void* In, size_t InSize, size_t OutSize,
                        uint8_t* Buf, size_t BufSize);

// Copies the driver's answer, OutSize bytes from the start of Buf, to Out. Returns 0 on
// success, -1 when Buf is too small or Out is NULL with a non-zero OutSize.
int LinIoctlBuffer_Unpack(const uint8_t* Buf, size_t BufSize, void* Out, size_t OutSize);
