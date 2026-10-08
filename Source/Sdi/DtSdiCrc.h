// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiCrc.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The line CRC of SMPTE ST 292 over a stream's words, by table and with
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
// SMPTE ST 292's CRC-18, x^18 + x^5 + x^4 + 1, over a stream's 10-bit words, least
// significant bit first, from a register of 0: the CRC a line carries over the active
// part of the line before. A line's streams alternate word by word, so a version takes
// them all at once: Count words of each of Streams streams, at most
// DT_SDICRC_MAX_STREAMS, from Words, and sets Crcs[i] to the CRC of the stream whose
// first word is Words[i]. Table holds the CRC of each 10-bit word from a register of 0,
// as DtSdiFrame_Crc18 gives it. Every version gives the same result as the portable one,
// which is the reference.

// The most streams a line has: those of four links of 2160p, C and Y each.
#define DT_SDICRC_MAX_STREAMS 8

// A version of the CRC.
typedef void (*DtSdiCrcFunc)(const uint16_t* Words, size_t Count, int Streams,
                             const uint32_t* Table, uint32_t* Crcs);

// The portable version: ten bits a step, through Table.
void DtSdiCrc_Streams(const uint16_t* Words, size_t Count, int Streams,
                      const uint32_t* Table, uint32_t* Crcs);

// The version with PCLMULQDQ whose words AVX2 packs, sixteen at a time rather than
// eight, or NULL where the processor lacks either or the library was built without them.
DtSdiCrcFunc DtSdiCrc_Avx2(void);

// The version with PCLMULQDQ and SSSE3, or NULL where the processor lacks them or the
// library was built without them. It packs each stream's words into a run of bits and
// folds that 128 bits a step; streams that do not come to whole steps of 128 bits, or of
// more than DT_SDICRC_CLMUL_MAX_WORDS words, it leaves to the portable version.
DtSdiCrcFunc DtSdiCrc_Clmul(void);

// The fastest version the processor has.
DtSdiCrcFunc DtSdiCrc_Best(void);

// The longest run the version with PCLMULQDQ takes itself: a line of 2048 words per
// stream, and room to spare.
#define DT_SDICRC_CLMUL_MAX_WORDS 4096

// The bytes a run of that many words takes packed, and room for the last step's writing.
#define DT_SDICRC_CLMUL_RUN_BYTES (DT_SDICRC_CLMUL_MAX_WORDS / 8 * 10 + 16)

// The versions with PCLMULQDQ themselves, without the check of the processor. Only for
// DtSdiCrc.c, which checks first, and for each other.
void DtSdiCrc_StreamsAvx2Unchecked(const uint16_t* Words, size_t Count, int Streams,
                                   const uint32_t* Table, uint32_t* Crcs);
void DtSdiCrc_StreamsClmulUnchecked(const uint16_t* Words, size_t Count, int Streams,
                                    const uint32_t* Table, uint32_t* Crcs);

// The CRC of the Blocks times 128 bits of a packed run at Bits, folded with PCLMULQDQ.
// Only for the versions with PCLMULQDQ.
uint32_t DtSdiCrc_FoldClmul(const uint8_t* Bits, size_t Blocks, const uint32_t* Table);

// Makes Builder work out its line CRCs with Crc rather than the fastest version, for the
// tests and the benchmark that compare them.
void DtSdiBuilder_UseCrc(DtSdiBuilder* Builder, DtSdiCrcFunc Crc);
