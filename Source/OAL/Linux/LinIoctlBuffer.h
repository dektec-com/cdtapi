// #*#*#*#*#*#*#*#*#*#*#*#*#* LinIoctlBuffer.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Layout of the single in/out buffer a Linux DekTec IOCTL uses
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
// Builds and reads the one block of memory that a DekTec driver on Linux takes for an
// IOCTL. On Windows, DeviceIoControl has separate input and output buffers; on Linux,
// ioctl takes one pointer, so the driver reads its input from the block and writes its
// answer into the same block. The block looks like this:
//
//   offset 0            UInt InSize    only for an IOCTL built with DT_IOCTL_MAGIC_SIZE
//   offset 4            UInt OutSize   only for an IOCTL built with DT_IOCTL_MAGIC_SIZE
//   offset HeaderBytes  the input structure
//
// The driver writes its answer from offset 0, over the sizes. So the block must be large
// enough for the sizes and the input, and also for the answer.
//
// The sizes are there because an IOCTL number holds the size of one fixed structure,
// while some commands take input of varying length. DT_IOCTL_MAGIC_SIZE in the IOCTL
// number tells the driver that the sizes are there. Every command CDTAPI sends uses it.
//
// This layout is kept apart from the Linux backend, without Linux headers, so that it
// is tested on every platform. A mistake here fails quietly: with the sizes shifted, the
// driver reads a wrong command rather than refuse it.
//

// The size in bytes of InSize and OutSize together, when they are there: two 32-bit
// unsigned integers.
#define LIN_IOCTL_SIZE_HEADER_BYTES (2 * sizeof(uint32_t))

// Returns the size in bytes the block needs: the larger of the sizes and the input
// together, and the answer.
size_t LinIoctlBuffer_Size(bool HasSizeHeader, size_t InSize, size_t OutSize);

// Fills Buf, of BufSize bytes, for the driver: zeros it, then writes the sizes when
// HasSizeHeader is true, and the input In of InSize bytes. Returns 0, or -1 when:
//   - Buf is NULL, or In is NULL while InSize is not 0
//   - with HasSizeHeader, InSize or OutSize does not fit in 32 bits
//   - BufSize is less than LinIoctlBuffer_Size
int LinIoctlBuffer_Pack(bool HasSizeHeader, const void* In, size_t InSize, size_t OutSize,
                        uint8_t* Buf, size_t BufSize);

// Copies the driver's answer, the first OutSize bytes of Buf, to Out. Returns 0, or -1
// when Buf is NULL, Out is NULL while OutSize is not 0, or BufSize is less than OutSize.
int LinIoctlBuffer_Unpack(const uint8_t* Buf, size_t BufSize, void* Out, size_t OutSize);
