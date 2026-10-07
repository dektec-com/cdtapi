// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiVec.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The conversions of SDI symbols, in portable C, and the choice of version
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtSdiVec.h"           // Interface being implemented.
#include "AvFifo/DtAvPixConv.h" // Whether the processor has SSSE3 and AVX2.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Legal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Limits a sample to 4..1019: SDI keeps 0 to 3 and 1020 to 1023 for timing references.
//
static inline uint16_t Legal(unsigned Sample)
{
    const unsigned Low = Sample < 4 ? 4 : Sample;
    return (uint16_t)(Low > 1019 ? 1019 : Low);
}

static inline unsigned GetLe16(const uint8_t* Bytes)
{
    return (unsigned)Bytes[0] | (unsigned)Bytes[1] << 8;
}

static inline void PutLe16(uint8_t* Bytes, unsigned Value)
{
    Bytes[0] = (uint8_t)Value;
    Bytes[1] = (uint8_t)(Value >> 8);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Portable +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Unpack10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Unpack10(const uint8_t* Bytes, size_t Count, uint16_t* Symbols)
{
    for (size_t i = 0; i < Count; i += 4, Bytes += 5)
    {
        const uint64_t Bits = (uint64_t)Bytes[0] | (uint64_t)Bytes[1] << 8 |
                              (uint64_t)Bytes[2] << 16 | (uint64_t)Bytes[3] << 24 |
                              (uint64_t)Bytes[4] << 32;
        Symbols[i] = (uint16_t)(Bits & 0x3FF);
        Symbols[i + 1] = (uint16_t)(Bits >> 10 & 0x3FF);
        Symbols[i + 2] = (uint16_t)(Bits >> 20 & 0x3FF);
        Symbols[i + 3] = (uint16_t)(Bits >> 30 & 0x3FF);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Pack10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Pack10(const uint16_t* Symbols, size_t Count, uint8_t* Bytes)
{
    for (size_t i = 0; i < Count; i += 4, Bytes += 5)
    {
        const uint64_t Bits = (uint64_t)(Symbols[i] & 0x3FF) |
                              (uint64_t)(Symbols[i + 1] & 0x3FF) << 10 |
                              (uint64_t)(Symbols[i + 2] & 0x3FF) << 20 |
                              (uint64_t)(Symbols[i + 3] & 0x3FF) << 30;
        for (int b = 0; b < 5; b++)
            Bytes[b] = (uint8_t)(Bits >> (8 * b));
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Limit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Limit(uint16_t* Symbols, size_t Count)
{
    for (size_t i = 0; i < Count; i++)
        Symbols[i] = Legal(Symbols[i]);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToPlanar10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void ToPlanar10(const uint16_t* Symbols, size_t Count, uint8_t* Y, uint8_t* Cb,
                       uint8_t* Cr)
{
    for (size_t i = 0; i < Count; i += 4)
    {
        PutLe16(Y + i, Symbols[i + 1]);
        PutLe16(Y + i + 2, Symbols[i + 3]);
        PutLe16(Cb + i / 2, Symbols[i]);
        PutLe16(Cr + i / 2, Symbols[i + 2]);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FromPlanar10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void FromPlanar10(const uint8_t* Y, const uint8_t* Cb, const uint8_t* Cr,
                         size_t Count, uint16_t* Symbols)
{
    for (size_t i = 0; i < Count; i += 4)
    {
        Symbols[i] = Legal(GetLe16(Cb + i / 2) & 0x3FF);
        Symbols[i + 1] = Legal(GetLe16(Y + i) & 0x3FF);
        Symbols[i + 2] = Legal(GetLe16(Cr + i / 2) & 0x3FF);
        Symbols[i + 3] = Legal(GetLe16(Y + i + 2) & 0x3FF);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToPlanar8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void ToPlanar8(const uint16_t* Symbols, size_t Count, uint8_t* Y, uint8_t* Cb,
                      uint8_t* Cr)
{
    for (size_t i = 0; i < Count; i += 4)
    {
        Y[i / 2] = (uint8_t)(Symbols[i + 1] >> 2);
        Y[i / 2 + 1] = (uint8_t)(Symbols[i + 3] >> 2);
        Cb[i / 4] = (uint8_t)(Symbols[i] >> 2);
        Cr[i / 4] = (uint8_t)(Symbols[i + 2] >> 2);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FromPlanar8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void FromPlanar8(const uint8_t* Y, const uint8_t* Cb, const uint8_t* Cr,
                        size_t Count, uint16_t* Symbols)
{
    for (size_t i = 0; i < Count; i += 4)
    {
        Symbols[i] = Legal((unsigned)Cb[i / 4] << 2);
        Symbols[i + 1] = Legal((unsigned)Y[i / 2] << 2);
        Symbols[i + 2] = Legal((unsigned)Cr[i / 4] << 2);
        Symbols[i + 3] = Legal((unsigned)Y[i / 2 + 1] << 2);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToUyvy8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void ToUyvy8(const uint16_t* Symbols, size_t Count, uint8_t* Bytes)
{
    for (size_t i = 0; i < Count; i++)
        Bytes[i] = (uint8_t)(Symbols[i] >> 2);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FromUyvy8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void FromUyvy8(const uint8_t* Bytes, size_t Count, uint16_t* Symbols)
{
    for (size_t i = 0; i < Count; i++)
        Symbols[i] = Legal((unsigned)Bytes[i] << 2);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToY210 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Each pair of pixels: Y0, Cb, Y1, Cr, the 10 bits at the top of each word.
//
static void ToY210(const uint16_t* Symbols, size_t Count, uint8_t* Bytes)
{
    for (size_t i = 0; i < Count; i += 4, Bytes += 8)
    {
        PutLe16(Bytes + 0, (unsigned)Symbols[i + 1] << 6 & 0xFFFF);
        PutLe16(Bytes + 2, (unsigned)Symbols[i] << 6 & 0xFFFF);
        PutLe16(Bytes + 4, (unsigned)Symbols[i + 3] << 6 & 0xFFFF);
        PutLe16(Bytes + 6, (unsigned)Symbols[i + 2] << 6 & 0xFFFF);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FromY210 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void FromY210(const uint8_t* Bytes, size_t Count, uint16_t* Symbols)
{
    for (size_t i = 0; i < Count; i += 4, Bytes += 8)
    {
        Symbols[i] = Legal(GetLe16(Bytes + 2) >> 6);
        Symbols[i + 1] = Legal(GetLe16(Bytes + 0) >> 6);
        Symbols[i + 2] = Legal(GetLe16(Bytes + 6) >> 6);
        Symbols[i + 3] = Legal(GetLe16(Bytes + 4) >> 6);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToV210 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void ToV210(const uint16_t* Symbols, size_t Count, uint8_t* Bytes)
{
    for (size_t i = 0; i < Count; i += 3, Bytes += 4)
    {
        uint32_t Word = 0;
        for (size_t k = 0; k < 3 && i + k < Count; k++)
            Word |= (uint32_t)(Symbols[i + k] & 0x3FF) << (10 * k);
        Bytes[0] = (uint8_t)Word;
        Bytes[1] = (uint8_t)(Word >> 8);
        Bytes[2] = (uint8_t)(Word >> 16);
        Bytes[3] = (uint8_t)(Word >> 24);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FromV210 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void FromV210(const uint8_t* Bytes, size_t Count, uint16_t* Symbols)
{
    for (size_t i = 0; i < Count; i += 3, Bytes += 4)
    {
        const uint32_t Word = (uint32_t)Bytes[0] | (uint32_t)Bytes[1] << 8 |
                              (uint32_t)Bytes[2] << 16 | (uint32_t)Bytes[3] << 24;
        for (size_t k = 0; k < 3 && i + k < Count; k++)
            Symbols[i + k] = Legal(Word >> (10 * k) & 0x3FF);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Split4k -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Pixel x of link L (from 0) is pixel X of its image line: its pixel pair x / 2 is the
// image's pair 2 * (x / 2) for links 1 and 3, and that plus one for links 2 and 4. Its C
// word is word 8x + Place[L] of the raw line, its Y word four after.
//
static const int g_LinkPlace[4] = {3, 1, 2, 0};

static void Split4k(const uint16_t* Raw, size_t Pixels, uint16_t* Upper, uint16_t* Lower)
{
    for (int Link = 0; Link < 4; Link++)
    {
        uint16_t* Line = (Link >> 1) != 0 ? Lower : Upper;
        const size_t Place = (size_t)g_LinkPlace[Link];
        for (size_t x = 0; x < Pixels; x++)
        {
            const size_t X = 2 * (2 * (x / 2) + (size_t)(Link & 1)) + (x & 1);
            Line[2 * X] = Raw[8 * x + Place];
            Line[2 * X + 1] = Raw[8 * x + 4 + Place];
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Join4k -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Join4k(const uint16_t* Upper, const uint16_t* Lower, size_t Pixels,
                   uint16_t* Raw)
{
    for (int Link = 0; Link < 4; Link++)
    {
        const uint16_t* Line = (Link >> 1) != 0 ? Lower : Upper;
        const size_t Place = (size_t)g_LinkPlace[Link];
        for (size_t x = 0; x < Pixels; x++)
        {
            const size_t X = 2 * (2 * (x / 2) + (size_t)(Link & 1)) + (x & 1);
            Raw[8 * x + Place] = Line[2 * X];
            Raw[8 * x + 4 + Place] = Line[2 * X + 1];
        }
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Versions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiVec_Avx2 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtSdiVec* DtSdiVec_Avx2(void)
{
#if defined(CDTAPI_HAVE_AVX2)
    return DtAvPixConv_Avx2() != NULL ? DtSdiVec_Avx2Unchecked() : NULL;
#else
    return NULL;
#endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiVec_Best -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtSdiVec* DtSdiVec_Best(void)
{
    const DtSdiVec* Avx2 = DtSdiVec_Avx2();
    if (Avx2 != NULL)
        return Avx2;
    const DtSdiVec* Ssse3 = DtSdiVec_Ssse3();
    return Ssse3 != NULL ? Ssse3 : DtSdiVec_C();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiVec_C -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const DtSdiVec* DtSdiVec_C(void)
{
    static const DtSdiVec Portable = {Unpack10,     Pack10,    Limit,       ToPlanar10,
                                      FromPlanar10, ToPlanar8, FromPlanar8, ToUyvy8,
                                      FromUyvy8,    ToY210,    FromY210,    ToV210,
                                      FromV210,     Split4k,   Join4k};
    return &Portable;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiVec_Ssse3 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// SSSE3 is there when the pixel conversions found it: they ask the processor.
//
const DtSdiVec* DtSdiVec_Ssse3(void)
{
#if defined(CDTAPI_HAVE_SSSE3)
    return DtAvPixConv_Ssse3() != NULL ? DtSdiVec_Ssse3Unchecked() : NULL;
#else
    return NULL;
#endif
}
