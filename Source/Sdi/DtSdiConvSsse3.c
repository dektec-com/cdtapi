// #*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiConvSsse3.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The conversions of SDI symbols, with SSSE3
//
// SPDX-License-Identifier: BSD-3-Clause
//
// This file is built only for x86 processors, with SSSE3 enabled for this file alone.
// The library uses these conversions only after CPUID reports SSSE3.
//
// Each conversion works a block at a time, as long as the block and every byte its loads
// and stores reach lie within the run. It leaves the rest of the run to the portable
// version.
//
// Ten bytes hold eight 10-bit symbols. Symbol k starts in byte 10k / 8, at bit 10k mod 8,
// which is 0, 2, 4 or 6. Unpacking takes three steps:
// 1. A shuffle puts the two bytes around each symbol in a 16-bit word.
// 2. A multiply by 64, 16, 4 or 1 moves the symbol to the top of its word.
// 3. A shift right by six brings it down.
// Packing reverses these steps. The multiply moves each symbol to its place in its two
// bytes. Symbols 0, 2, 4 and 6 share no byte, and neither do symbols 1, 3, 5 and 7. So
// two shuffles and an OR assemble the ten bytes.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <tmmintrin.h>

// CDTAPI includes
#include "DtSdiConv.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A shuffle index that gives zero.
#define Z -128

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Legal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Limits eight samples to 4..1019.
//
static inline __m128i Legal(__m128i Samples)
{
    return _mm_min_epi16(_mm_max_epi16(Samples, _mm_set1_epi16(4)), _mm_set1_epi16(1019));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Unpack8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Unpacks eight symbols from the ten bytes at Bytes. It loads sixteen bytes, six more
// than it uses.
//
static inline __m128i Unpack8(const uint8_t* Bytes)
{
    const __m128i Pairs = _mm_set_epi8(9, 8, 8, 7, 7, 6, 6, 5, 4, 3, 3, 2, 2, 1, 1, 0);
    const __m128i Words = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)Bytes), Pairs);
    const __m128i Moved =
        _mm_mullo_epi16(Words, _mm_set_epi16(1, 4, 16, 64, 1, 4, 16, 64));
    return _mm_and_si128(_mm_srli_epi16(Moved, 6), _mm_set1_epi16(0x3FF));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Pack8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Packs eight symbols into ten bytes. The result holds them in its low ten bytes, and its
// other six bytes are 0.
//
static inline __m128i Pack8(__m128i Symbols)
{
    const __m128i Masked = _mm_and_si128(Symbols, _mm_set1_epi16(0x3FF));
    const __m128i Moved =
        _mm_mullo_epi16(Masked, _mm_set_epi16(64, 16, 4, 1, 64, 16, 4, 1));
    const __m128i Even = _mm_shuffle_epi8(
        Moved, _mm_set_epi8(Z, Z, Z, Z, Z, Z, Z, 13, 12, 9, 8, Z, 5, 4, 1, 0));
    const __m128i Odd = _mm_shuffle_epi8(
        Moved, _mm_set_epi8(Z, Z, Z, Z, Z, Z, 15, 14, 11, 10, Z, 7, 6, 3, 2, Z));
    return _mm_or_si128(Even, Odd);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Conversions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Unpack10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Unpack10(const uint8_t* Bytes, size_t Count, uint16_t* Symbols)
{
    size_t i = 0;
    for (; i + 16 <= Count; i += 8)
        _mm_storeu_si128((__m128i*)(Symbols + i), Unpack8(Bytes + i * 10 / 8));
    DtSdiConv_C()->Unpack10(Bytes + i * 10 / 8, Count - i, Symbols + i);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Pack10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Pack10(const uint16_t* Symbols, size_t Count, uint8_t* Bytes)
{
    size_t i = 0;
    for (; i + 16 <= Count; i += 8)
        _mm_storeu_si128((__m128i*)(Bytes + i * 10 / 8),
                         Pack8(_mm_loadu_si128((const __m128i*)(Symbols + i))));
    DtSdiConv_C()->Pack10(Symbols + i, Count - i, Bytes + i * 10 / 8);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Limit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Limit(uint16_t* Symbols, size_t Count)
{
    size_t i = 0;
    for (; i + 8 <= Count; i += 8)
    {
        __m128i* At = (__m128i*)(Symbols + i);
        _mm_storeu_si128(At, Legal(_mm_loadu_si128(At)));
    }
    DtSdiConv_C()->Limit(Symbols + i, Count - i);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToPlanar10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Converts sixteen symbols, eight pixels, in each step. Each half of the sixteen is
// shuffled so that its Y words fill the low eight bytes, and its Cb and Cr words four
// bytes each. Unpacks then join the two halves.
//
static void ToPlanar10(const uint16_t* Symbols, size_t Count, uint8_t* Y, uint8_t* Cb,
                       uint8_t* Cr)
{
    const __m128i TakeY =
        _mm_set_epi8(Z, Z, Z, Z, Z, Z, Z, Z, 15, 14, 11, 10, 7, 6, 3, 2);
    const __m128i TakeC = _mm_set_epi8(Z, Z, Z, Z, Z, Z, Z, Z, 13, 12, 5, 4, 9, 8, 1, 0);
    size_t i = 0;
    for (; i + 16 <= Count; i += 16)
    {
        const __m128i A = _mm_loadu_si128((const __m128i*)(Symbols + i));
        const __m128i B = _mm_loadu_si128((const __m128i*)(Symbols + i + 8));
        const __m128i Ys =
            _mm_unpacklo_epi64(_mm_shuffle_epi8(A, TakeY), _mm_shuffle_epi8(B, TakeY));
        const __m128i Cs =
            _mm_unpacklo_epi32(_mm_shuffle_epi8(A, TakeC), _mm_shuffle_epi8(B, TakeC));
        _mm_storeu_si128((__m128i*)(Y + i), Ys);
        _mm_storel_epi64((__m128i*)(Cb + i / 2), Cs);
        _mm_storel_epi64((__m128i*)(Cr + i / 2), _mm_srli_si128(Cs, 8));
    }
    DtSdiConv_C()->ToPlanar10(Symbols + i, Count - i, Y + i, Cb + i / 2, Cr + i / 2);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Interleave -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Interleaves the Y, Cb and Cr words of eight pixels into sixteen symbols, in the order
// Cb, Y, Cr, Y. It limits them to 4..1019 and stores them at Symbols.
//
static inline void Interleave(__m128i Ys, __m128i Cbs, __m128i Crs, uint16_t* Symbols)
{
    const __m128i CbCr = _mm_unpacklo_epi16(Cbs, Crs);
    _mm_storeu_si128((__m128i*)Symbols, Legal(_mm_unpacklo_epi16(CbCr, Ys)));
    _mm_storeu_si128((__m128i*)(Symbols + 8), Legal(_mm_unpackhi_epi16(CbCr, Ys)));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FromPlanar10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void FromPlanar10(const uint8_t* Y, const uint8_t* Cb, const uint8_t* Cr,
                         size_t Count, uint16_t* Symbols)
{
    const __m128i Ten = _mm_set1_epi16(0x3FF);
    size_t i = 0;
    for (; i + 16 <= Count; i += 16)
    {
        const __m128i Ys = _mm_and_si128(_mm_loadu_si128((const __m128i*)(Y + i)), Ten);
        const __m128i Cbs =
            _mm_and_si128(_mm_loadl_epi64((const __m128i*)(Cb + i / 2)), Ten);
        const __m128i Crs =
            _mm_and_si128(_mm_loadl_epi64((const __m128i*)(Cr + i / 2)), Ten);
        Interleave(Ys, Cbs, Crs, Symbols + i);
    }
    DtSdiConv_C()->FromPlanar10(Y + i, Cb + i / 2, Cr + i / 2, Count - i, Symbols + i);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToPlanar8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void ToPlanar8(const uint16_t* Symbols, size_t Count, uint8_t* Y, uint8_t* Cb,
                      uint8_t* Cr)
{
    const __m128i TakeY =
        _mm_set_epi8(Z, Z, Z, Z, Z, Z, Z, Z, 15, 14, 11, 10, 7, 6, 3, 2);
    const __m128i TakeC = _mm_set_epi8(Z, Z, Z, Z, Z, Z, Z, Z, 13, 12, 5, 4, 9, 8, 1, 0);
    size_t i = 0;
    for (; i + 16 <= Count; i += 16)
    {
        const __m128i A =
            _mm_srli_epi16(_mm_loadu_si128((const __m128i*)(Symbols + i)), 2);
        const __m128i B =
            _mm_srli_epi16(_mm_loadu_si128((const __m128i*)(Symbols + i + 8)), 2);
        const __m128i Ys =
            _mm_unpacklo_epi64(_mm_shuffle_epi8(A, TakeY), _mm_shuffle_epi8(B, TakeY));
        const __m128i Cs =
            _mm_unpacklo_epi32(_mm_shuffle_epi8(A, TakeC), _mm_shuffle_epi8(B, TakeC));
        const __m128i Bytes = _mm_packus_epi16(Ys, Cs); // Y 0-7, Cb 8-11, Cr 12-15
        _mm_storel_epi64((__m128i*)(Y + i / 2), Bytes);
        const int CbWord = _mm_cvtsi128_si32(_mm_srli_si128(Bytes, 8));
        const int CrWord = _mm_cvtsi128_si32(_mm_srli_si128(Bytes, 12));
        for (int b = 0; b < 4; b++)
        {
            Cb[i / 4 + (size_t)b] = (uint8_t)((unsigned)CbWord >> (8 * b));
            Cr[i / 4 + (size_t)b] = (uint8_t)((unsigned)CrWord >> (8 * b));
        }
    }
    DtSdiConv_C()->ToPlanar8(Symbols + i, Count - i, Y + i / 2, Cb + i / 4, Cr + i / 4);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FromPlanar8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void FromPlanar8(const uint8_t* Y, const uint8_t* Cb, const uint8_t* Cr,
                        size_t Count, uint16_t* Symbols)
{
    const __m128i Zero = _mm_setzero_si128();
    size_t i = 0;
    for (; i + 16 <= Count; i += 16)
    {
        const __m128i Ys = _mm_slli_epi16(
            _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*)(Y + i / 2)), Zero), 2);
        int CbWord = 0;
        int CrWord = 0;
        for (int b = 3; b >= 0; b--)
        {
            CbWord = (int)((unsigned)CbWord << 8 | Cb[i / 4 + (size_t)b]);
            CrWord = (int)((unsigned)CrWord << 8 | Cr[i / 4 + (size_t)b]);
        }
        const __m128i Cbs =
            _mm_slli_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128(CbWord), Zero), 2);
        const __m128i Crs =
            _mm_slli_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128(CrWord), Zero), 2);
        Interleave(Ys, Cbs, Crs, Symbols + i);
    }
    DtSdiConv_C()->FromPlanar8(Y + i / 2, Cb + i / 4, Cr + i / 4, Count - i, Symbols + i);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToUyvy8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void ToUyvy8(const uint16_t* Symbols, size_t Count, uint8_t* Bytes)
{
    size_t i = 0;
    for (; i + 16 <= Count; i += 16)
    {
        const __m128i A =
            _mm_srli_epi16(_mm_loadu_si128((const __m128i*)(Symbols + i)), 2);
        const __m128i B =
            _mm_srli_epi16(_mm_loadu_si128((const __m128i*)(Symbols + i + 8)), 2);
        _mm_storeu_si128((__m128i*)(Bytes + i), _mm_packus_epi16(A, B));
    }
    DtSdiConv_C()->ToUyvy8(Symbols + i, Count - i, Bytes + i);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FromUyvy8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void FromUyvy8(const uint8_t* Bytes, size_t Count, uint16_t* Symbols)
{
    const __m128i Zero = _mm_setzero_si128();
    size_t i = 0;
    for (; i + 16 <= Count; i += 16)
    {
        const __m128i In = _mm_loadu_si128((const __m128i*)(Bytes + i));
        _mm_storeu_si128((__m128i*)(Symbols + i),
                         Legal(_mm_slli_epi16(_mm_unpacklo_epi8(In, Zero), 2)));
        _mm_storeu_si128((__m128i*)(Symbols + i + 8),
                         Legal(_mm_slli_epi16(_mm_unpackhi_epi8(In, Zero), 2)));
    }
    DtSdiConv_C()->FromUyvy8(Bytes + i, Count - i, Symbols + i);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToY210 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Swaps each pair of words, Cb Y to Y Cb, and shifts each word left by six to put its 10
// bits at the top.
//
static void ToY210(const uint16_t* Symbols, size_t Count, uint8_t* Bytes)
{
    const __m128i Swap =
        _mm_set_epi8(13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2);
    size_t i = 0;
    for (; i + 8 <= Count; i += 8)
    {
        const __m128i In = _mm_loadu_si128((const __m128i*)(Symbols + i));
        _mm_storeu_si128((__m128i*)(Bytes + 2 * i),
                         _mm_slli_epi16(_mm_shuffle_epi8(In, Swap), 6));
    }
    DtSdiConv_C()->ToY210(Symbols + i, Count - i, Bytes + 2 * i);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FromY210 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void FromY210(const uint8_t* Bytes, size_t Count, uint16_t* Symbols)
{
    const __m128i Swap =
        _mm_set_epi8(13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2);
    size_t i = 0;
    for (; i + 8 <= Count; i += 8)
    {
        const __m128i In = _mm_loadu_si128((const __m128i*)(Bytes + 2 * i));
        _mm_storeu_si128((__m128i*)(Symbols + i),
                         Legal(_mm_srli_epi16(_mm_shuffle_epi8(In, Swap), 6)));
    }
    DtSdiConv_C()->FromY210(Bytes + 2 * i, Count - i, Symbols + i);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToV210 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Converts twelve symbols into four words in each step. A multiply-add makes the low
// twenty bits of each word from its first two symbols. The word's third symbol is
// shifted left by 20 and ORed in above them.
//
static void ToV210(const uint16_t* Symbols, size_t Count, uint8_t* Bytes)
{
    const __m128i Ten = _mm_set1_epi16(0x3FF);
    size_t i = 0;
    for (; i + 16 <= Count; i += 12)
    {
        const __m128i A =
            _mm_and_si128(_mm_loadu_si128((const __m128i*)(Symbols + i)), Ten);
        const __m128i B =
            _mm_and_si128(_mm_loadu_si128((const __m128i*)(Symbols + i + 8)), Ten);
        // Pairs holds symbols 0 and 1, 3 and 4, 6 and 7, and 9 and 10, a pair in each
        // 32-bit lane. Symbols 9 and 10 are words 1 and 2 of B.
        const __m128i Pairs =
            _mm_or_si128(_mm_shuffle_epi8(A, _mm_set_epi8(Z, Z, Z, Z, 15, 14, 13, 12, 9,
                                                          8, 7, 6, 3, 2, 1, 0)),
                         _mm_shuffle_epi8(B, _mm_set_epi8(5, 4, 3, 2, Z, Z, Z, Z, Z, Z, Z,
                                                          Z, Z, Z, Z, Z)));
        // Thirds holds symbols 2, 5, 8 and 11, one in the low half of each 32-bit lane.
        // Symbols 8 and 11 are words 0 and 3 of B.
        const __m128i Thirds =
            _mm_or_si128(_mm_shuffle_epi8(A, _mm_set_epi8(Z, Z, Z, Z, Z, Z, Z, Z, Z, Z,
                                                          11, 10, Z, Z, 5, 4)),
                         _mm_shuffle_epi8(B, _mm_set_epi8(Z, Z, 7, 6, Z, Z, 1, 0, Z, Z, Z,
                                                          Z, Z, Z, Z, Z)));
        const __m128i Words =
            _mm_or_si128(_mm_madd_epi16(Pairs, _mm_set1_epi32(0x04000001)),
                         _mm_slli_epi32(Thirds, 20));
        _mm_storeu_si128((__m128i*)(Bytes + i / 3 * 4), Words);
    }
    DtSdiConv_C()->ToV210(Symbols + i, Count - i, Bytes + i / 3 * 4);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FromV210 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Converts four words into twelve symbols in each step. Shifts and masks cut the three
// symbols out of each word, and shuffles then put them in order. Below, F, S and T are
// the first, second and third symbol of a word.
//
static void FromV210(const uint8_t* Bytes, size_t Count, uint16_t* Symbols)
{
    const __m128i Ten = _mm_set1_epi32(0x3FF);
    size_t i = 0;
    for (; i + 16 <= Count; i += 12)
    {
        const __m128i Words = _mm_loadu_si128((const __m128i*)(Bytes + i / 3 * 4));
        const __m128i First = _mm_and_si128(Words, Ten);
        const __m128i Second = _mm_and_si128(_mm_srli_epi32(Words, 10), Ten);
        const __m128i Third = _mm_and_si128(_mm_srli_epi32(Words, 20), Ten);
        // Pairs holds F of word k in 16-bit word 2k, and S of word k in word 2k + 1.
        const __m128i Pairs = _mm_or_si128(First, _mm_slli_epi32(Second, 16));
        // Low holds symbols 0 to 7, which are F0 S0 T0 F1 S1 T1 F2 S2.
        const __m128i Low =
            _mm_or_si128(_mm_shuffle_epi8(Pairs, _mm_set_epi8(11, 10, 9, 8, Z, Z, 7, 6, 5,
                                                              4, Z, Z, 3, 2, 1, 0)),
                         _mm_shuffle_epi8(Third, _mm_set_epi8(Z, Z, Z, Z, 5, 4, Z, Z, Z,
                                                              Z, 1, 0, Z, Z, Z, Z)));
        // High holds symbols 8 to 11, which are T2 F3 S3 T3.
        const __m128i High =
            _mm_or_si128(_mm_shuffle_epi8(Pairs, _mm_set_epi8(Z, Z, Z, Z, Z, Z, Z, Z, Z,
                                                              Z, 15, 14, 13, 12, Z, Z)),
                         _mm_shuffle_epi8(Third, _mm_set_epi8(Z, Z, Z, Z, Z, Z, Z, Z, 13,
                                                              12, Z, Z, Z, Z, 9, 8)));
        _mm_storeu_si128((__m128i*)(Symbols + i), Legal(Low));
        _mm_storel_epi64((__m128i*)(Symbols + i + 8), Legal(High));
    }
    DtSdiConv_C()->FromV210(Bytes + i / 3 * 4, Count - i, Symbols + i);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Split4k -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Converts two pixels of each link, sixteen raw words, in each step. G0 holds pixel 2q
// of every link, and G1 holds pixel 2q + 1. Shuffles pick the words of each image line:
// 1. The upper line takes C and Y of both pixels of link 1, then of link 2. These are
//    words 3 and 7 of G0 and of G1, then words 1 and 5 of each.
// 2. The lower line takes the same of links 3 and 4. These are words 2 and 6 of G0 and
//    of G1, then words 0 and 4 of each.
//
static void Split4k(const uint16_t* Raw, size_t Pixels, uint16_t* Upper, uint16_t* Lower)
{
    const __m128i UpperFrom0 =
        _mm_set_epi8(Z, Z, Z, Z, 11, 10, 3, 2, Z, Z, Z, Z, 15, 14, 7, 6);
    const __m128i UpperFrom1 =
        _mm_set_epi8(11, 10, 3, 2, Z, Z, Z, Z, 15, 14, 7, 6, Z, Z, Z, Z);
    const __m128i LowerFrom0 =
        _mm_set_epi8(Z, Z, Z, Z, 9, 8, 1, 0, Z, Z, Z, Z, 13, 12, 5, 4);
    const __m128i LowerFrom1 =
        _mm_set_epi8(9, 8, 1, 0, Z, Z, Z, Z, 13, 12, 5, 4, Z, Z, Z, Z);
    size_t x = 0;
    for (; x + 2 <= Pixels; x += 2)
    {
        const __m128i G0 = _mm_loadu_si128((const __m128i*)(Raw + 8 * x));
        const __m128i G1 = _mm_loadu_si128((const __m128i*)(Raw + 8 * x + 8));
        _mm_storeu_si128((__m128i*)(Upper + 4 * x),
                         _mm_or_si128(_mm_shuffle_epi8(G0, UpperFrom0),
                                      _mm_shuffle_epi8(G1, UpperFrom1)));
        _mm_storeu_si128((__m128i*)(Lower + 4 * x),
                         _mm_or_si128(_mm_shuffle_epi8(G0, LowerFrom0),
                                      _mm_shuffle_epi8(G1, LowerFrom1)));
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Join4k -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reverses Split4k, two pixels of each link in each step. U and L hold eight words of
// the upper and the lower line. G0 takes, word by word, word 4 of L, word 4 of U, then
// words 0, 5 and 1 of L and U in the same way. G1 takes words 6, 2, 7 and 3 likewise.
//
static void Join4k(const uint16_t* Upper, const uint16_t* Lower, size_t Pixels,
                   uint16_t* Raw)
{
    const __m128i G0FromUpper =
        _mm_set_epi8(3, 2, Z, Z, 11, 10, Z, Z, 1, 0, Z, Z, 9, 8, Z, Z);
    const __m128i G0FromLower =
        _mm_set_epi8(Z, Z, 3, 2, Z, Z, 11, 10, Z, Z, 1, 0, Z, Z, 9, 8);
    const __m128i G1FromUpper =
        _mm_set_epi8(7, 6, Z, Z, 15, 14, Z, Z, 5, 4, Z, Z, 13, 12, Z, Z);
    const __m128i G1FromLower =
        _mm_set_epi8(Z, Z, 7, 6, Z, Z, 15, 14, Z, Z, 5, 4, Z, Z, 13, 12);
    size_t x = 0;
    for (; x + 2 <= Pixels; x += 2)
    {
        const __m128i U = _mm_loadu_si128((const __m128i*)(Upper + 4 * x));
        const __m128i L = _mm_loadu_si128((const __m128i*)(Lower + 4 * x));
        _mm_storeu_si128((__m128i*)(Raw + 8 * x),
                         _mm_or_si128(_mm_shuffle_epi8(U, G0FromUpper),
                                      _mm_shuffle_epi8(L, G0FromLower)));
        _mm_storeu_si128((__m128i*)(Raw + 8 * x + 8),
                         _mm_or_si128(_mm_shuffle_epi8(U, G1FromUpper),
                                      _mm_shuffle_epi8(L, G1FromLower)));
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- JoinLevelB -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Eight words of each link in each step: the low and the high halves interleaved word by
// word. The rest, fewer than eight words, goes word by word.
//
static void JoinLevelB(const uint16_t* LinkB, const uint16_t* LinkA, size_t Count,
                       uint16_t* Line)
{
    size_t k = 0;
    for (; k + 8 <= Count; k += 8)
    {
        const __m128i A = _mm_loadu_si128((const __m128i*)(LinkB + k));
        const __m128i B = _mm_loadu_si128((const __m128i*)(LinkA + k));
        _mm_storeu_si128((__m128i*)(Line + 2 * k), _mm_unpacklo_epi16(A, B));
        _mm_storeu_si128((__m128i*)(Line + 2 * k + 8), _mm_unpackhi_epi16(A, B));
    }
    for (; k < Count; k++)
    {
        Line[2 * k] = LinkB[k];
        Line[2 * k + 1] = LinkA[k];
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SplitLevelB -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sixteen words in each step: a shuffle puts each register's even words in its low half
// and its odd words in its high half, and the halves of the two registers are joined.
//
static void SplitLevelB(const uint16_t* Line, size_t Count, uint16_t* LinkB,
                        uint16_t* LinkA)
{
    const __m128i Split =
        _mm_set_epi8(15, 14, 11, 10, 7, 6, 3, 2, 13, 12, 9, 8, 5, 4, 1, 0);
    size_t k = 0;
    for (; k + 8 <= Count; k += 8)
    {
        const __m128i L =
            _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(Line + 2 * k)), Split);
        const __m128i H =
            _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(Line + 2 * k + 8)), Split);
        _mm_storeu_si128((__m128i*)(LinkB + k), _mm_unpacklo_epi64(L, H));
        _mm_storeu_si128((__m128i*)(LinkA + k), _mm_unpackhi_epi64(L, H));
    }
    for (; k < Count; k++)
    {
        LinkB[k] = Line[2 * k];
        LinkA[k] = Line[2 * k + 1];
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Version +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiConv_Ssse3Unchecked -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const DtSdiConv* DtSdiConv_Ssse3Unchecked(void)
{
    static const DtSdiConv Ssse3 = {
        Unpack10,    Pack10,  Limit,     ToPlanar10, FromPlanar10, ToPlanar8,
        FromPlanar8, ToUyvy8, FromUyvy8, ToY210,     FromY210,     ToV210,
        FromV210,    Split4k, Join4k,    JoinLevelB, SplitLevelB};
    return &Ssse3;
}
