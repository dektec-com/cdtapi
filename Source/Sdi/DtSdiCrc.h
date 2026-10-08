// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiCrc.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Computes the line CRCs of an HD-SDI line, with a lookup table or with
// PCLMULQDQ
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "cdtapi_sdi.h" // DtSdiBuilder.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The CRC +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// These functions compute the CRC that SMPTE ST 292 puts after each line number. It is
// the CRC-18 with polynomial x^18 + x^5 + x^4 + 1, computed over the 10-bit words of one
// stream, least significant bit first, starting from 0. A line carries the CRC of the
// active part of the line before it.
//
// The words of a line's streams are interleaved: word 0 of stream 0, word 0 of stream 1,
// and so on. A CRC function therefore handles all streams of a line in one call:
//   Words    the first word of the interleaved active part
//   Count    the number of words per stream
//   Streams  the number of streams, at most DT_SDICRC_MAX_STREAMS
//   Table    the CRC of each 10-bit value from 0, as DtSdiFrame_Crc18 computes it
//   Crcs     receives one CRC per stream; Crcs[i] is the CRC of the stream that starts
//            at Words[i]
//
// All versions return the same result. The portable version is the reference.

// The most streams a line can have: 2160p over four links, with a C and a Y stream each.
#define DT_SDICRC_MAX_STREAMS 8

// A function that computes the CRCs of a line's streams; see above.
typedef void (*DtSdiCrcFunc)(const uint16_t* Words, size_t Count, int Streams,
                             const uint32_t* Table, uint32_t* Crcs);

// Computes the CRCs in portable C, one 10-bit word at a time, with Table.
void DtSdiCrc_Streams(const uint16_t* Words, size_t Count, int Streams,
                      const uint32_t* Table, uint32_t* Crcs);

// Returns the fastest version: PCLMULQDQ with AVX2. Returns NULL if the processor lacks
// PCLMULQDQ or AVX2, or if the library was built without them.
DtSdiCrcFunc DtSdiCrc_Avx2(void);

// Returns the version with PCLMULQDQ and SSSE3. Returns NULL if the processor lacks them
// or if the library was built without them.
//
// Both PCLMULQDQ versions pack each stream's words into a continuous run of bits and
// reduce that run 128 bits at a time with carry-less multiplications. They hand a line to
// the portable version if its streams are longer than DT_SDICRC_CLMUL_MAX_WORDS words or
// are not a multiple of 64 words long (64 words are 640 bits, five steps of 128).
DtSdiCrcFunc DtSdiCrc_Clmul(void);

// Returns the fastest version this processor can run.
DtSdiCrcFunc DtSdiCrc_Best(void);

// The longest stream the PCLMULQDQ versions handle themselves. A line has at most 2048
// words per stream, so this leaves room to spare.
#define DT_SDICRC_CLMUL_MAX_WORDS 4096

// The buffer size for one packed stream: 10 bytes per 8 words, plus 16 bytes because
// the packing code stores 16 bytes at a time.
#define DT_SDICRC_CLMUL_RUN_BYTES (DT_SDICRC_CLMUL_MAX_WORDS / 8 * 10 + 16)

// The PCLMULQDQ versions without the processor check. Call them only through
// DtSdiCrc_Avx2 or DtSdiCrc_Clmul, which check first.
void DtSdiCrc_StreamsAvx2Unchecked(const uint16_t* Words, size_t Count, int Streams,
                                   const uint32_t* Table, uint32_t* Crcs);
void DtSdiCrc_StreamsClmulUnchecked(const uint16_t* Words, size_t Count, int Streams,
                                    const uint32_t* Table, uint32_t* Crcs);

// Returns the CRC of a packed run of Blocks blocks of 128 bits at Bits. Shared by the
// two PCLMULQDQ versions.
uint32_t DtSdiCrc_FoldClmul(const uint8_t* Bits, size_t Blocks, const uint32_t* Table);

// Makes Builder compute its line CRCs with Crc instead of the fastest version. For the
// tests and the benchmark, which compare the versions.
void DtSdiBuilder_UseCrc(DtSdiBuilder* Builder, DtSdiCrcFunc Crc);
