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
#include "DtSdiConv.h"  // The conversions that unpack and pack.
#include "cdtapi_sdi.h" // The symbol pointer.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Symbols +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Reads Count symbols, starting where Ptr points, into Out. Each word of Out receives
// one value from 0 to 1023. Conv unpacks the 10-bit symbols four at a time once the run
// reaches a byte boundary.
void DtSdiSymbols_Read(const DtSdiSymbolPtr* Ptr, size_t Count, uint16_t* Out,
                       const DtSdiConv* Conv);

// Writes symbols one after the other into a frame. A symbol takes either 10 bits, packed
// least significant bit first, or 16 bits, little endian. A run of 10-bit symbols need
// not end on a byte boundary. The next run continues where it stopped.
typedef struct DtSdiSymbolWriter
{
    uint8_t* Next;         // The next whole byte to write
    uint64_t Bits;         // Bits put but not yet stored, the first in the lowest bit
    int NumBits;           // The number of bits in Bits
    int BitsPerSymbol;     // 10 or 16
    const DtSdiConv* Conv; // The conversions that pack the symbols
} DtSdiSymbolWriter;

// Starts writing at Frame, with BitsPerSymbol bits a symbol. Conv packs the 10-bit
// symbols.
void DtSdiSymbolWriter_Init(DtSdiSymbolWriter* Writer, uint8_t* Frame, int BitsPerSymbol,
                            const DtSdiConv* Conv);

// Writes Count symbols from Symbols, each a value from 0 to 1023.
void DtSdiSymbolWriter_Put(DtSdiSymbolWriter* Writer, const uint16_t* Symbols,
                           size_t Count);

// Stores the bits not yet stored, then fills the rest of the frame, up to End, with
// zeros.
void DtSdiSymbolWriter_End(DtSdiSymbolWriter* Writer, uint8_t* End);
