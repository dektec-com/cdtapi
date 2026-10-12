// #*#*#*#*#*#*#*#*#*#*#*#*#*#* SimSdiWords.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The words of an SDI line that the emulated card works out itself
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "SimSdiWords.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Words of a line +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The CRC's change for each value of five bits, so that a word takes two steps rather
// than ten.
static const uint32_t g_Crc18Table[32] = {
    0x00000, 0x02300, 0x04600, 0x06500, 0x08C00, 0x0AF00, 0x0CA00, 0x0E900,
    0x11800, 0x13B00, 0x15E00, 0x17D00, 0x19400, 0x1B700, 0x1D200, 0x1F100,
    0x23000, 0x21300, 0x27600, 0x25500, 0x2BC00, 0x29F00, 0x2FA00, 0x2D900,
    0x32800, 0x30B00, 0x36E00, 0x34D00, 0x3A400, 0x38700, 0x3E200, 0x3C100,
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimSdiWords_Crc18 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint32_t SimSdiWords_Crc18(uint32_t Crc, uint32_t Word)
{
    Crc = (Crc >> 5) ^ g_Crc18Table[(Crc ^ Word) & 0x1F];
    return (Crc >> 5) ^ g_Crc18Table[(Crc ^ (Word >> 5)) & 0x1F];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimSdiWords_WithParity -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint32_t SimSdiWords_WithParity(uint32_t Nine)
{
    Nine &= 0x1FF;
    return Nine | ((Nine >> 8) ^ 1) << 9;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimSdiWords_Xyz -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint32_t SimSdiWords_Xyz(const DtFrameProps* Props, int Line, bool Eav)
{
    uint32_t F = Props->NumFields == 2 && Line >= Props->Fields[1].StartLine ? 1 : 0;
    const DtFieldProps* Field = &Props->Fields[F];
    uint32_t V = Line < Field->ActiveStartLine || Line > Field->ActiveEndLine ? 1 : 0;
    uint32_t H = Eav ? 1 : 0;

    return 0x200 | F << 8 | V << 7 | H << 6 | (V ^ H) << 5 | (F ^ H) << 4 | (F ^ V) << 3 |
           (F ^ V ^ H) << 2;
}
