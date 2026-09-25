// #*#*#*#*#*#*#*#*#*#*#*#*#* DtAvPixConvAvx2.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Pixel conversions between ST 2110-20 pixel groups and the frame formats
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Built only for x86 processors, with AVX2 enabled for this file alone: the library calls
// these conversions only after CPUID and the operating system report AVX2. The file
// includes no header with functions of its own, so that no function compiled for AVX2
// can take the place of the same function compiled elsewhere.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <immintrin.h>

// CDTAPI includes
#include "DtAvPixConv.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= AVX2 +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The 10-bit conversions of DtAvPixConvSsse3.c, two lanes at a time: the lower lane holds
// the first two of four pixel groups, the upper lane the next two, and every shuffle,
// multiply and shift acts on both lanes as the SSSE3 conversions act on one. A step
// loads 16 bytes at the first pixel group and 16 at the third, and stores 16 at each,
// or 16 at the first alone for 8-bit output, so it runs while six pixel groups remain
// on both sides; the SSSE3 conversions do the rest.
// The planar conversion waits for memory rather than for the processor, and stays
// SSSE3's.
//

// A shuffle index that gives zero.
#define SHUF_ZERO -128

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BothLanes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The same 128-bit constant in both lanes.
//
static __m256i BothLanes(__m128i Constant)
{
    return _mm256_broadcastsi128_si256(Constant);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LoadGroups -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The 16 bytes at Src in the lower lane and the 16 at Src + 10 in the upper.
//
static __m256i LoadGroups(const uint8_t* Src)
{
    __m128i First = _mm_loadu_si128((const __m128i*)Src);
    __m128i Second = _mm_loadu_si128((const __m128i*)(Src + 10));
    return _mm256_inserti128_si256(_mm256_castsi128_si256(First), Second, 1);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StoreGroups -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The lower lane's ten bytes at Dst and the upper lane's at Dst + 10.
//
static void StoreGroups(uint8_t* Dst, __m256i Groups)
{
    _mm_storeu_si128((__m128i*)Dst, _mm256_castsi256_si128(Groups));
    _mm_storeu_si128((__m128i*)(Dst + 10), _mm256_extracti128_si256(Groups, 1));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Pg10Lanes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The eight samples of each lane's two pixel groups, each shifted left so that it fills
// bits 6 to 15 of its 16-bit element.
//
static __m256i Pg10Lanes(const uint8_t* Src)
{
    __m256i Words = _mm256_shuffle_epi8(
        LoadGroups(Src),
        BothLanes(_mm_set_epi8(8, 9, 7, 8, 6, 7, 5, 6, 3, 4, 2, 3, 1, 2, 0, 1)));
    return _mm256_mullo_epi16(Words,
                              BothLanes(_mm_set_epi16(64, 16, 4, 1, 64, 16, 4, 1)));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Pg10ToUyvy10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Pg10ToUyvy10(const uint8_t* Src, uint8_t* Dst, size_t NumPgroups)
{
    for (; NumPgroups >= 6; NumPgroups -= 4, Src += 20, Dst += 20)
    {
        __m256i Samples = _mm256_srli_epi16(Pg10Lanes(Src), 6);
        __m256i Shifted = _mm256_mullo_epi16(
            Samples, BothLanes(_mm_set_epi16(64, 16, 4, 1, 64, 16, 4, 1)));
        __m256i Even = _mm256_shuffle_epi8(
            Shifted, BothLanes(_mm_set_epi8(SHUF_ZERO, SHUF_ZERO, SHUF_ZERO, SHUF_ZERO,
                                            SHUF_ZERO, SHUF_ZERO, SHUF_ZERO, 13, 12, 9, 8,
                                            SHUF_ZERO, 5, 4, 1, 0)));
        __m256i Odd = _mm256_shuffle_epi8(
            Shifted, BothLanes(_mm_set_epi8(SHUF_ZERO, SHUF_ZERO, SHUF_ZERO, SHUF_ZERO,
                                            SHUF_ZERO, SHUF_ZERO, 15, 14, 11, 10,
                                            SHUF_ZERO, 7, 6, 3, 2, SHUF_ZERO)));
        StoreGroups(Dst, _mm256_or_si256(Even, Odd));
    }
    _mm256_zeroupper();
    DtAvPixConv_Ssse3Unchecked()->Pg10ToUyvy10(Src, Dst, NumPgroups);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Pg10ToUyvy8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Each lane's eight highest sample bits come to its lower eight bytes, and a permutation
// of the 64-bit elements puts the upper lane's next to the lower's.
//
static void Pg10ToUyvy8(const uint8_t* Src, uint8_t* Dst, size_t NumPgroups)
{
    for (; NumPgroups >= 6; NumPgroups -= 4, Src += 20, Dst += 16)
    {
        __m256i High = _mm256_shuffle_epi8(
            Pg10Lanes(Src),
            BothLanes(_mm_set_epi8(SHUF_ZERO, SHUF_ZERO, SHUF_ZERO, SHUF_ZERO, SHUF_ZERO,
                                   SHUF_ZERO, SHUF_ZERO, SHUF_ZERO, 15, 13, 11, 9, 7, 5,
                                   3, 1)));
        __m256i Packed = _mm256_permute4x64_epi64(High, _MM_SHUFFLE(3, 1, 2, 0));
        _mm_storeu_si128((__m128i*)Dst, _mm256_castsi256_si128(Packed));
    }
    _mm256_zeroupper();
    DtAvPixConv_Ssse3Unchecked()->Pg10ToUyvy8(Src, Dst, NumPgroups);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Uyvy10ToPg10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Uyvy10ToPg10(const uint8_t* Src, uint8_t* Dst, size_t NumPgroups)
{
    for (; NumPgroups >= 6; NumPgroups -= 4, Src += 20, Dst += 20)
    {
        __m256i Words = _mm256_shuffle_epi8(
            LoadGroups(Src),
            BothLanes(_mm_set_epi8(9, 8, 8, 7, 7, 6, 6, 5, 4, 3, 3, 2, 2, 1, 1, 0)));
        __m256i Weights = BothLanes(_mm_set_epi16(1, 4, 16, 64, 1, 4, 16, 64));
        __m256i Samples = _mm256_srli_epi16(_mm256_mullo_epi16(Words, Weights), 6);
        __m256i Shifted = _mm256_mullo_epi16(Samples, Weights);
        __m256i Even = _mm256_shuffle_epi8(
            Shifted, BothLanes(_mm_set_epi8(SHUF_ZERO, SHUF_ZERO, SHUF_ZERO, SHUF_ZERO,
                                            SHUF_ZERO, SHUF_ZERO, SHUF_ZERO, 12, 13, 8, 9,
                                            SHUF_ZERO, 4, 5, 0, 1)));
        __m256i Odd = _mm256_shuffle_epi8(
            Shifted, BothLanes(_mm_set_epi8(SHUF_ZERO, SHUF_ZERO, SHUF_ZERO, SHUF_ZERO,
                                            SHUF_ZERO, SHUF_ZERO, 14, 15, 10, 11,
                                            SHUF_ZERO, 6, 7, 2, 3, SHUF_ZERO)));
        StoreGroups(Dst, _mm256_or_si256(Even, Odd));
    }
    _mm256_zeroupper();
    DtAvPixConv_Ssse3Unchecked()->Uyvy10ToPg10(Src, Dst, NumPgroups);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Uyvy8ToYuv422p -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Uyvy8ToYuv422p(const uint8_t* Src, size_t NumPgroups, uint8_t* Y, uint8_t* U,
                           uint8_t* V)
{
    DtAvPixConv_Ssse3Unchecked()->Uyvy8ToYuv422p(Src, NumPgroups, Y, U, V);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPixConv_Avx2Unchecked -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtAvPixConvTable* DtAvPixConv_Avx2Unchecked(void)
{
    static const DtAvPixConvTable Table = {Pg10ToUyvy10, Pg10ToUyvy8, Uyvy10ToPg10,
                                           Uyvy8ToYuv422p};
    return &Table;
}
