// #*#*#*#*#*#*#*#*#*#*#*#*#*#* SimSdiWords.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The words of an SDI line that the emulated card works out itself
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// CDTAPI includes
#include "Video/DtFrameProps.h" // The fields and lines of a frame.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Words of a line +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The emulator makes and converts SDI lines without the library's SDI code, so that the
// tests can check one against the other.

// Returns Crc after one more 10-bit word, Word: SMPTE 292's CRC-18, x^18 + x^5 + x^4 + 1,
// least significant bit first. A line's CRC starts from 0.
uint32_t SimSdiWords_Crc18(uint32_t Crc, uint32_t Word);

// Returns Nine's low nine bits with bit 9 the inverse of bit 8, as line numbers and CRC
// words carry them.
uint32_t SimSdiWords_WithParity(uint32_t Nine);

// Returns the fourth word of the EAV (Eav true) or the SAV of line Line, from 1, of a
// frame of Props: field, vertical blanking, EAV or SAV, and the protection bits over
// those three.
uint32_t SimSdiWords_Xyz(const DtFrameProps* Props, int Line, bool Eav);
