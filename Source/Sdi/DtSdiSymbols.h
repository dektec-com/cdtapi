// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiSymbols.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Reading and writing runs of SDI symbols in a frame
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

// Writes symbols one after the other into a frame: 10 bits each, packed least
// significant bit first, or 16 bits each, little endian. A run of 10-bit symbols need
// not end on a byte boundary; the next run goes on where it stopped.
typedef struct DtSdiSymbolWriter
{
    uint8_t* Next;     // The next whole byte to write
    uint64_t Bits;     // Bits written but not yet stored, the first lowest
    int NumBits;       // How many
    int BitsPerSymbol; // 10 or 16
} DtSdiSymbolWriter;

// Starts writing at Frame with BitsPerSymbol bits a symbol.
void DtSdiSymbolWriter_Init(DtSdiSymbolWriter* Writer, uint8_t* Frame, int BitsPerSymbol);

// Writes Count symbols from Symbols, each a value from 0 to 1023.
void DtSdiSymbolWriter_Put(DtSdiSymbolWriter* Writer, const uint16_t* Symbols,
                           size_t Count);

// Stores the bits not yet stored and fills the rest of the frame, up to End, with zeros.
void DtSdiSymbolWriter_End(DtSdiSymbolWriter* Writer, uint8_t* End);
