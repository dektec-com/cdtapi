// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiSymbols.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Reading runs of SDI symbols out of a frame
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "cdtapi_sdi.h" // The symbol pointer.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Symbols +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Reads Count symbols from where Ptr points into Out, one value from 0 to 1023 a word.
// 10-bit symbols are read four at a time from five bytes once the run reaches a byte
// boundary, which it does within three symbols. This is the portable version.
void DtSdiSymbols_Read(const DtSdiSymbolPtr* Ptr, size_t Count, uint16_t* Out);
