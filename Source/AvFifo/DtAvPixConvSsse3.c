// #*#*#*#*#*#*#*#*#*#*#*#*# DtAvPixConvSsse3.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Pixel conversions between ST 2110-20 pixel groups and the frame formats
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Built only for x86 processors, with SSSE3 enabled for this file alone: the library
// calls these conversions only after CPUID reports SSSE3.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>
#include <tmmintrin.h>

// CDtapiLite includes
#include "DtAvPixConv.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= SSSE3 +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The 10-bit conversions take two pixel groups, ten bytes, at a time and turn their eight
// samples into eight 16-bit lanes. Sample k starts at bit 10k of its bit stream, in byte
// 10k / 8 at bit 10k mod 8, which is 0, 2, 4 or 6: the two bytes around it form a 16-bit
// word from which a multiply by 1, 4, 16 or 64 cuts the bits above the sample, and a
// shift right by 6 those below. The reverse puts each sample back into its two bytes;
// samples 0, 2, 4 and 6 share no byte, nor do 1, 3, 5 and 7, so two shuffles and an OR
// assemble the ten bytes.
//
// A 10-bit step loads 16 bytes and stores up to 16, so it runs while four pixel groups,
// 20 bytes, remain on both sides; the portable conversions do the rest.
//

// A shuffle index that gives zero.
#define Z -128

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Pg10Lanes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The eight samples of the two pixel groups at Src, each shifted left so that it fills
// bits 6 to 15 of its lane.
//
static __m128i Pg10Lanes(const uint8_t* Src)
{
    __m128i Bytes = _mm_loadu_si128((const __m128i*)Src);
    __m128i Words = _mm_shuffle_epi8(
        Bytes, _mm_set_epi8(8, 9, 7, 8, 6, 7, 5, 6, 3, 4, 2, 3, 1, 2, 0, 1));
    return _mm_mullo_epi16(Words, _mm_set_epi16(64, 16, 4, 1, 64, 16, 4, 1));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Pg10ToUyvy10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Pg10ToUyvy10(const uint8_t* Src, uint8_t* Dst, size_t NumPgroups)
{
    for (; NumPgroups >= 4; NumPgroups -= 2, Src += 10, Dst += 10)
    {
        __m128i Samples = _mm_srli_epi16(Pg10Lanes(Src), 6);
        __m128i Shifted =
            _mm_mullo_epi16(Samples, _mm_set_epi16(64, 16, 4, 1, 64, 16, 4, 1));
        __m128i Even = _mm_shuffle_epi8(
            Shifted, _mm_set_epi8(Z, Z, Z, Z, Z, Z, Z, 13, 12, 9, 8, Z, 5, 4, 1, 0));
        __m128i Odd = _mm_shuffle_epi8(
            Shifted, _mm_set_epi8(Z, Z, Z, Z, Z, Z, 15, 14, 11, 10, Z, 7, 6, 3, 2, Z));
        _mm_storeu_si128((__m128i*)Dst, _mm_or_si128(Even, Odd));
    }
    DtAvPixConv_C()->Pg10ToUyvy10(Src, Dst, NumPgroups);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Pg10ToUyvy8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The eight bits a lane holds above its six lowest are the sample's eight highest.
//
static void Pg10ToUyvy8(const uint8_t* Src, uint8_t* Dst, size_t NumPgroups)
{
    for (; NumPgroups >= 4; NumPgroups -= 2, Src += 10, Dst += 8)
    {
        __m128i High =
            _mm_shuffle_epi8(Pg10Lanes(Src), _mm_set_epi8(Z, Z, Z, Z, Z, Z, Z, Z, 15, 13,
                                                          11, 9, 7, 5, 3, 1));
        _mm_storel_epi64((__m128i*)Dst, High);
    }
    DtAvPixConv_C()->Pg10ToUyvy8(Src, Dst, NumPgroups);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Uyvy10ToPg10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The same in the other order: a little-endian word per sample, whose multiply by 64, 16,
// 4 or 1 and shift right by 6 leave the sample, then a big-endian word per sample.
//
static void Uyvy10ToPg10(const uint8_t* Src, uint8_t* Dst, size_t NumPgroups)
{
    for (; NumPgroups >= 4; NumPgroups -= 2, Src += 10, Dst += 10)
    {
        __m128i Bytes = _mm_loadu_si128((const __m128i*)Src);
        __m128i Words = _mm_shuffle_epi8(
            Bytes, _mm_set_epi8(9, 8, 8, 7, 7, 6, 6, 5, 4, 3, 3, 2, 2, 1, 1, 0));
        __m128i Samples = _mm_srli_epi16(
            _mm_mullo_epi16(Words, _mm_set_epi16(1, 4, 16, 64, 1, 4, 16, 64)), 6);
        __m128i Shifted =
            _mm_mullo_epi16(Samples, _mm_set_epi16(1, 4, 16, 64, 1, 4, 16, 64));
        __m128i Even = _mm_shuffle_epi8(
            Shifted, _mm_set_epi8(Z, Z, Z, Z, Z, Z, Z, 12, 13, 8, 9, Z, 4, 5, 0, 1));
        __m128i Odd = _mm_shuffle_epi8(
            Shifted, _mm_set_epi8(Z, Z, Z, Z, Z, Z, 14, 15, 10, 11, Z, 6, 7, 2, 3, Z));
        _mm_storeu_si128((__m128i*)Dst, _mm_or_si128(Even, Odd));
    }
    DtAvPixConv_C()->Uyvy10ToPg10(Src, Dst, NumPgroups);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Uyvy8ToYuv422p -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Eight pixel groups, 32 bytes, a step. Read as 16-bit lanes, each pixel group's bytes
// are a chroma sample below a luma sample: a shift right by 8 leaves the lane's luma and
// a mask its chroma, which saturating packs of two blocks, exact for values below 256,
// turn into the 16 luma bytes and the 16 chroma bytes Cb, Cr, Cb, Cr. One shuffle puts
// the eight Cb bytes below the eight Cr bytes.
//
static void Uyvy8ToYuv422p(const uint8_t* Src, size_t NumPgroups, uint8_t* Y, uint8_t* U,
                           uint8_t* V)
{
    const __m128i Low = _mm_set1_epi16(0x00FF);
    const __m128i Split =
        _mm_set_epi8(15, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 0);
    for (; NumPgroups >= 8; NumPgroups -= 8, Src += 32, Y += 16, U += 8, V += 8)
    {
        __m128i First = _mm_loadu_si128((const __m128i*)Src);
        __m128i Second = _mm_loadu_si128((const __m128i*)(Src + 16));
        __m128i Luma =
            _mm_packus_epi16(_mm_srli_epi16(First, 8), _mm_srli_epi16(Second, 8));
        __m128i Chroma =
            _mm_packus_epi16(_mm_and_si128(First, Low), _mm_and_si128(Second, Low));
        __m128i BlueRed = _mm_shuffle_epi8(Chroma, Split);
        _mm_storeu_si128((__m128i*)Y, Luma);
        _mm_storel_epi64((__m128i*)U, BlueRed);
        _mm_storel_epi64((__m128i*)V, _mm_srli_si128(BlueRed, 8));
    }
    DtAvPixConv_C()->Uyvy8ToYuv422p(Src, NumPgroups, Y, U, V);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPixConv_Ssse3Table -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const DtAvPixConv* DtAvPixConv_Ssse3Table(void)
{
    static const DtAvPixConv Table = {Pg10ToUyvy10, Pg10ToUyvy8, Uyvy10ToPg10,
                                      Uyvy8ToYuv422p};
    return &Table;
}
