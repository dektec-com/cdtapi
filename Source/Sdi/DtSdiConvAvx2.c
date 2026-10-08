// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiConvAvx2.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The conversions of SDI symbols, with AVX2
//
// SPDX-License-Identifier: BSD-3-Clause
//
// This file is built only for x86 processors, with AVX2 enabled for this file alone. The
// library uses these conversions only after CPUID reports AVX2 and the operating system
// saves its registers.
//
// Unpacking, packing and limiting take sixteen symbols in each step. Each half of the
// register holds a block of eight, handled as the SSSE3 version handles it. The pixel
// formats and the links of 2160p use the SSSE3 version.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <immintrin.h>

// CDTAPI includes
#include "DtSdiConv.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A shuffle index that gives zero.
#define Z -128

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Unpack10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Unpacks sixteen symbols from twenty bytes in each step, ten bytes in each half. A step
// reads twenty-six bytes, so a step runs only while 24 symbols or more remain. The SSSE3
// version converts the rest.
//
static void Unpack10(const uint8_t* Bytes, size_t Count, uint16_t* Symbols)
{
    const __m256i Pairs = _mm256_broadcastsi128_si256(
        _mm_set_epi8(9, 8, 8, 7, 7, 6, 6, 5, 4, 3, 3, 2, 2, 1, 1, 0));
    const __m256i Shifts =
        _mm256_broadcastsi128_si256(_mm_set_epi16(1, 4, 16, 64, 1, 4, 16, 64));
    const __m256i Ten = _mm256_set1_epi16(0x3FF);
    size_t i = 0;
    for (; i + 24 <= Count; i += 16)
    {
        const uint8_t* At = Bytes + i * 10 / 8;
        const __m256i In = _mm256_inserti128_si256(
            _mm256_castsi128_si256(_mm_loadu_si128((const __m128i*)At)),
            _mm_loadu_si128((const __m128i*)(At + 10)), 1);
        const __m256i Moved = _mm256_mullo_epi16(_mm256_shuffle_epi8(In, Pairs), Shifts);
        _mm256_storeu_si256((__m256i*)(Symbols + i),
                            _mm256_and_si256(_mm256_srli_epi16(Moved, 6), Ten));
    }
    DtSdiConv_Ssse3Unchecked()->Unpack10(Bytes + i * 10 / 8, Count - i, Symbols + i);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Pack10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Packs sixteen symbols into twenty bytes in each step. Each half of the register holds
// ten of the bytes, and the second half is stored right after the first. Each store
// writes six bytes past its ten, and the next store overwrites them. A step runs only
// while 24 symbols or more remain, and the SSSE3 version converts the rest.
//
static void Pack10(const uint16_t* Symbols, size_t Count, uint8_t* Bytes)
{
    const __m256i Shifts =
        _mm256_broadcastsi128_si256(_mm_set_epi16(64, 16, 4, 1, 64, 16, 4, 1));
    const __m256i EvenMask = _mm256_broadcastsi128_si256(
        _mm_set_epi8(Z, Z, Z, Z, Z, Z, Z, 13, 12, 9, 8, Z, 5, 4, 1, 0));
    const __m256i OddMask = _mm256_broadcastsi128_si256(
        _mm_set_epi8(Z, Z, Z, Z, Z, Z, 15, 14, 11, 10, Z, 7, 6, 3, 2, Z));
    const __m256i Ten = _mm256_set1_epi16(0x3FF);
    size_t i = 0;
    for (; i + 24 <= Count; i += 16)
    {
        const __m256i In =
            _mm256_and_si256(_mm256_loadu_si256((const __m256i*)(Symbols + i)), Ten);
        const __m256i Moved = _mm256_mullo_epi16(In, Shifts);
        const __m256i Packed = _mm256_or_si256(_mm256_shuffle_epi8(Moved, EvenMask),
                                               _mm256_shuffle_epi8(Moved, OddMask));
        uint8_t* At = Bytes + i * 10 / 8;
        _mm_storeu_si128((__m128i*)At, _mm256_castsi256_si128(Packed));
        _mm_storeu_si128((__m128i*)(At + 10), _mm256_extracti128_si256(Packed, 1));
    }
    DtSdiConv_Ssse3Unchecked()->Pack10(Symbols + i, Count - i, Bytes + i * 10 / 8);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Limit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Limit(uint16_t* Symbols, size_t Count)
{
    const __m256i Low = _mm256_set1_epi16(4);
    const __m256i High = _mm256_set1_epi16(1019);
    size_t i = 0;
    for (; i + 16 <= Count; i += 16)
    {
        __m256i* At = (__m256i*)(Symbols + i);
        _mm256_storeu_si256(
            At, _mm256_min_epi16(_mm256_max_epi16(_mm256_loadu_si256(At), Low), High));
    }
    DtSdiConv_Ssse3Unchecked()->Limit(Symbols + i, Count - i);
}

// The conversions below hand the pixel formats and the links of 2160p to the SSSE3
// version.

static void ToPlanar10(const uint16_t* Symbols, size_t Count, uint8_t* Y, uint8_t* Cb,
                       uint8_t* Cr)
{
    DtSdiConv_Ssse3Unchecked()->ToPlanar10(Symbols, Count, Y, Cb, Cr);
}

static void FromPlanar10(const uint8_t* Y, const uint8_t* Cb, const uint8_t* Cr,
                         size_t Count, uint16_t* Symbols)
{
    DtSdiConv_Ssse3Unchecked()->FromPlanar10(Y, Cb, Cr, Count, Symbols);
}

static void ToPlanar8(const uint16_t* Symbols, size_t Count, uint8_t* Y, uint8_t* Cb,
                      uint8_t* Cr)
{
    DtSdiConv_Ssse3Unchecked()->ToPlanar8(Symbols, Count, Y, Cb, Cr);
}

static void FromPlanar8(const uint8_t* Y, const uint8_t* Cb, const uint8_t* Cr,
                        size_t Count, uint16_t* Symbols)
{
    DtSdiConv_Ssse3Unchecked()->FromPlanar8(Y, Cb, Cr, Count, Symbols);
}

static void ToUyvy8(const uint16_t* Symbols, size_t Count, uint8_t* Bytes)
{
    DtSdiConv_Ssse3Unchecked()->ToUyvy8(Symbols, Count, Bytes);
}

static void FromUyvy8(const uint8_t* Bytes, size_t Count, uint16_t* Symbols)
{
    DtSdiConv_Ssse3Unchecked()->FromUyvy8(Bytes, Count, Symbols);
}

static void ToY210(const uint16_t* Symbols, size_t Count, uint8_t* Bytes)
{
    DtSdiConv_Ssse3Unchecked()->ToY210(Symbols, Count, Bytes);
}

static void FromY210(const uint8_t* Bytes, size_t Count, uint16_t* Symbols)
{
    DtSdiConv_Ssse3Unchecked()->FromY210(Bytes, Count, Symbols);
}

static void ToV210(const uint16_t* Symbols, size_t Count, uint8_t* Bytes)
{
    DtSdiConv_Ssse3Unchecked()->ToV210(Symbols, Count, Bytes);
}

static void FromV210(const uint8_t* Bytes, size_t Count, uint16_t* Symbols)
{
    DtSdiConv_Ssse3Unchecked()->FromV210(Bytes, Count, Symbols);
}

static void Split4k(const uint16_t* Raw, size_t Pixels, uint16_t* Upper, uint16_t* Lower)
{
    DtSdiConv_Ssse3Unchecked()->Split4k(Raw, Pixels, Upper, Lower);
}

static void Join4k(const uint16_t* Upper, const uint16_t* Lower, size_t Pixels,
                   uint16_t* Raw)
{
    DtSdiConv_Ssse3Unchecked()->Join4k(Upper, Lower, Pixels, Raw);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Version +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiConv_Avx2Unchecked -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtSdiConv* DtSdiConv_Avx2Unchecked(void)
{
    static const DtSdiConv Avx2 = {Unpack10,     Pack10,    Limit,       ToPlanar10,
                                   FromPlanar10, ToPlanar8, FromPlanar8, ToUyvy8,
                                   FromUyvy8,    ToY210,    FromY210,    ToV210,
                                   FromV210,     Split4k,   Join4k};
    return &Avx2;
}
