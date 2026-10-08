// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiSymbols.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Reading and writing runs of SDI symbols in a frame
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtSdiSymbols.h" // Interface being implemented.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiSymbols_Read -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads 16-bit symbols as little-endian words and keeps their low 10 bits. Reads 10-bit
// symbols in three parts:
// 1. One by one, up to the first symbol that starts on a byte boundary. A 10-bit symbol
//    starts at bit 0, 2, 4 or 6 of a byte, and every fourth symbol starts at the same
//    bit. So this part reads at most three symbols.
// 2. Four at a time, with Conv. Four symbols take five bytes, least significant bit
//    first.
// 3. The rest one by one.
//
void DtSdiSymbols_Read(const DtSdiSymbolPtr* Ptr, size_t Count, uint16_t* Out,
                       const DtSdiConv* Conv)
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

    const size_t Aligned = (Count - i) / 4 * 4;
    Conv->Unpack10(Ptr->Byte + ((size_t)Ptr->Bit + 10 * i) / 8, Aligned, Out + i);
    i += Aligned;

    for (; i < Count; i++)
        Out[i] = DtSdiSymbolPtr_Get(Ptr, i);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiSymbolWriter_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiSymbolWriter_Init(DtSdiSymbolWriter* Writer, uint8_t* Frame, int BitsPerSymbol,
                            const DtSdiConv* Conv)
{
    Writer->Conv = Conv;
    Writer->Next = Frame;
    Writer->Bits = 0;
    Writer->NumBits = 0;
    Writer->BitsPerSymbol = BitsPerSymbol;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiSymbolWriter_Put -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSdiSymbolWriter_Put(DtSdiSymbolWriter* Writer, const uint16_t* Symbols,
                           size_t Count)
{
    if (Writer->BitsPerSymbol == 16)
    {
        for (size_t i = 0; i < Count; i++)
        {
            *Writer->Next++ = (uint8_t)Symbols[i];
            *Writer->Next++ = (uint8_t)(Symbols[i] >> 8);
        }
        return;
    }

    uint64_t Bits = Writer->Bits;
    int NumBits = Writer->NumBits;
    uint8_t* Next = Writer->Next;
    size_t i = 0;

    // Write symbols one at a time until a byte boundary, which comes within four symbols.
    for (; i < Count && NumBits != 0; i++)
    {
        Bits |= (uint64_t)(Symbols[i] & 0x3FF) << NumBits;
        NumBits += 10;
        while (NumBits >= 8)
        {
            *Next++ = (uint8_t)Bits;
            Bits >>= 8;
            NumBits -= 8;
        }
    }

    // Then pack each whole group of four symbols into five bytes.
    const size_t Aligned = (Count - i) / 4 * 4;
    Writer->Conv->Pack10(Symbols + i, Aligned, Next);
    Next += Aligned / 4 * 5;
    i += Aligned;

    // Write the rest one at a time.
    for (; i < Count; i++)
    {
        Bits |= (uint64_t)(Symbols[i] & 0x3FF) << NumBits;
        NumBits += 10;
        while (NumBits >= 8)
        {
            *Next++ = (uint8_t)Bits;
            Bits >>= 8;
            NumBits -= 8;
        }
    }
    Writer->Bits = Bits;
    Writer->NumBits = NumBits;
    Writer->Next = Next;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiSymbolWriter_End -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSdiSymbolWriter_End(DtSdiSymbolWriter* Writer, uint8_t* End)
{
    if (Writer->NumBits > 0 && Writer->Next < End)
        *Writer->Next++ = (uint8_t)Writer->Bits;
    Writer->Bits = 0;
    Writer->NumBits = 0;
    while (Writer->Next < End)
        *Writer->Next++ = 0;
}
