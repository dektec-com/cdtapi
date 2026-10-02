// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAvPixConv.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Pixel conversions between ST 2110-20 pixel groups and the frame formats
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Pixel formats +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Every format here is 4:2:2 YCbCr, in pixel groups of two pixels: Cb, Y0, Cr, Y1.
//
//   pgroup 10    ST 2110-20's 10-bit pixel group, 5 bytes: the four 10-bit samples as one
//                bit stream, most significant bit first
//   UYVY 10      the application's 10-bit format, 5 bytes: the same samples, least
//                significant bit first, Cb in bits 0-9 of the first two bytes
//   UYVY 8       4 bytes, one per sample; also ST 2110-20's 8-bit pixel group
//   YUV 4:2:2p   8-bit planes: two Y bytes, one U and one V per pixel group
//
// A 10-bit sample becomes 8 bits by dropping its two least significant bits. The
// conversions read and write only the pixel groups they are given.
//

// Converts NumPgroups pixel groups from Src to Dst, which must not overlap.
typedef void (*DtAvPixConvFunc)(const uint8_t* Src, uint8_t* Dst, size_t NumPgroups);

// Converts NumPgroups pixel groups of UYVY 8 from Src into the planes: 2 * NumPgroups
// bytes at Y, NumPgroups at U and NumPgroups at V.
typedef void (*DtAvPixConvPlanarFunc)(const uint8_t* Src, size_t NumPgroups, uint8_t* Y,
                                      uint8_t* U, uint8_t* V);

// One implementation of every conversion: portable C, SSSE3 or AVX2.
typedef struct DtAvPixConvTable
{
    DtAvPixConvFunc Pg10ToUyvy10;         // Pgroup 10 to UYVY 10
    DtAvPixConvFunc Pg10ToUyvy8;          // Pgroup 10 to UYVY 8
    DtAvPixConvFunc Uyvy10ToPg10;         // UYVY 10 to pgroup 10
    DtAvPixConvPlanarFunc Uyvy8ToYuv422p; // UYVY 8 to YUV 4:2:2p
} DtAvPixConvTable;

// Returns the conversions written in portable C, which run everywhere.
const DtAvPixConvTable* DtAvPixConv_C(void);

// Returns the conversions that use SSSE3, or NULL when the library was built without
// them or the processor has no SSSE3.
const DtAvPixConvTable* DtAvPixConv_Ssse3(void);

// Returns the conversions that use AVX2 for 10-bit video and SSSE3 for planar video, or
// NULL when the library was built without them, or the processor or the operating system
// does not support AVX2.
const DtAvPixConvTable* DtAvPixConv_Avx2(void);

// Returns the fastest conversions the processor can run: AVX2, SSSE3 or portable C.
const DtAvPixConvTable* DtAvPixConv_Best(void);

// Return the SSSE3 and the AVX2 conversions without checking the processor; they exist
// only in an x86 build, each in a source file compiled for its instruction set. Other
// code calls DtAvPixConv_Ssse3 or DtAvPixConv_Avx2, which check first.
const DtAvPixConvTable* DtAvPixConv_Ssse3Unchecked(void);
const DtAvPixConvTable* DtAvPixConv_Avx2Unchecked(void);
