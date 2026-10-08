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
// SMPTE ST 292's CRC-18, x^18 + x^5 + x^4 + 1, over Count 10-bit words Step words apart,
// least significant bit first, from a register of 0: the CRC a line carries over the
// active part of the line before. Table holds the CRC of each 10-bit word from a register
// of 0, as DtSdiFrame_Crc18 gives it. Every version gives the same result as the
// portable one, which is the reference.

// A version of the CRC.
typedef uint32_t (*DtSdiCrcFunc)(const uint16_t* Words, size_t Count, size_t Step,
                                 const uint32_t* Table);

// The portable version: ten bits a step, through Table.
uint32_t DtSdiCrc_Words(const uint16_t* Words, size_t Count, size_t Step,
                        const uint32_t* Table);

// The version with PCLMULQDQ, or NULL where the processor lacks it or the library was
// built without it. It packs the words into a run of bits and folds that 128 bits a
// step; a run that does not come to whole steps of 128 bits, or of more than
// DT_SDICRC_CLMUL_MAX_WORDS words, it leaves to the portable version.
DtSdiCrcFunc DtSdiCrc_Clmul(void);

// The fastest version the processor has.
DtSdiCrcFunc DtSdiCrc_Best(void);

// The longest run the version with PCLMULQDQ takes itself: a line of 2048 words per
// stream, and room to spare.
#define DT_SDICRC_CLMUL_MAX_WORDS 4096

// The version with PCLMULQDQ itself, without the check of the processor. Only for
// DtSdiCrc.c, which checks first.
uint32_t DtSdiCrc_WordsClmulUnchecked(const uint16_t* Words, size_t Count, size_t Step,
                                      const uint32_t* Table);

// Makes Builder work out its line CRCs with Crc rather than the fastest version, for the
// tests and the benchmark that compare them.
void DtSdiBuilder_UseCrc(DtSdiBuilder* Builder, DtSdiCrcFunc Crc);
