// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiSymbols.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Reading runs of SDI symbols out of a frame
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtSdiSymbols.h" // Interface being implemented.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiSymbols_Read -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A 10-bit symbol starts at bit 0, 2, 4 or 6 of a byte, and every fourth symbol at the
// same one. So at most three symbols are read one by one before the run is on a byte
// boundary; from there four symbols take five bytes, least significant bit first.
//
void DtSdiSymbols_Read(const DtSdiSymbolPtr* Ptr, size_t Count, uint16_t* Out)
{
    if (Ptr->BitsPerSymbol == 16)
    {
        const uint8_t* Words = Ptr->Byte;
        for (size_t i = 0; i < Count; i++)
            Out[i] = (uint16_t)((Words[2 * i] | Words[2 * i + 1] << 8) & 0x3FF);
        return;
    }

    size_t i = 0;
    while (i < Count && ((size_t)Ptr->Bit + 10 * i) % 8 != 0)
    {
        Out[i] = DtSdiSymbolPtr_Get(Ptr, i);
        i++;
    }

    const uint8_t* Bytes = Ptr->Byte + ((size_t)Ptr->Bit + 10 * i) / 8;
    for (; i + 4 <= Count; i += 4, Bytes += 5)
    {
        const uint64_t Bits = (uint64_t)Bytes[0] | (uint64_t)Bytes[1] << 8 |
                              (uint64_t)Bytes[2] << 16 | (uint64_t)Bytes[3] << 24 |
                              (uint64_t)Bytes[4] << 32;
        Out[i] = (uint16_t)(Bits & 0x3FF);
        Out[i + 1] = (uint16_t)(Bits >> 10 & 0x3FF);
        Out[i + 2] = (uint16_t)(Bits >> 20 & 0x3FF);
        Out[i + 3] = (uint16_t)(Bits >> 30 & 0x3FF);
    }

    for (; i < Count; i++)
        Out[i] = DtSdiSymbolPtr_Get(Ptr, i);
}
