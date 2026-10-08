// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiCrcAvx2.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The line CRC of SMPTE ST 292 with PCLMULQDQ, its words packed with AVX2
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#if defined(_MSC_VER)
    #include <intrin.h>
#else
    #include <immintrin.h>
#endif

// CDTAPI includes
#include "DtSdiCrc.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Packing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// As the version with SSSE3 packs eight words, this packs sixteen: the pairs joined by
// a multiply-add, the pairs of pairs by a shift, and the five bytes of each 40 bits
// gathered by a shuffle, in each 128-bit lane, whose ten bytes are stored one after the
// other. The folding is the same.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PackSixteen -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Packs the sixteen words of Words, ten bits each, eight to a lane, into twenty bytes at
// Out; writes twenty-six.
//
static void PackSixteen(__m256i Words, uint8_t* Out)
{
    Words = _mm256_and_si256(Words, _mm256_set1_epi16(0x3FF));
    const __m256i Pairs = _mm256_madd_epi16(Words, _mm256_set1_epi32(0x04000001));
    const __m256i Fours =
        _mm256_or_si256(_mm256_and_si256(Pairs, _mm256_set1_epi64x(0xFFFFFFFF)),
                        _mm256_slli_epi64(_mm256_srli_epi64(Pairs, 32), 20));
    const __m256i Bytes = _mm256_shuffle_epi8(
        Fours, _mm256_setr_epi8(0, 1, 2, 3, 4, 8, 9, 10, 11, 12, -1, -1, -1, -1, -1, -1,
                                0, 1, 2, 3, 4, 8, 9, 10, 11, 12, -1, -1, -1, -1, -1, -1));
    _mm_storeu_si128((__m128i*)Out, _mm256_castsi256_si128(Bytes));
    _mm_storeu_si128((__m128i*)(Out + 10), _mm256_extracti128_si256(Bytes, 1));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PackStreams -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Packs the Count words, a multiple of sixteen, of each of the one, two or eight streams
// that alternate in Words into Bits[s], sixteen of each at a time: one stream's as they
// lie; two streams' thirty-two at a time, parted into even and odd in each lane and the
// halves brought together across the lanes; eight streams' 128 at a time, a lane taking
// the words of sixteen word places further on, and transposed in each lane, so that the
// two lanes hold sixteen words of one stream in order.
//
static void PackStreams(const uint16_t* Words, size_t Count, int Streams, uint8_t** Bits)
{
    const __m256i Part =
        _mm256_setr_epi8(0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15, 0, 1, 4, 5,
                         8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15);
    for (size_t k = 0; k < Count; k += 16)
    {
        const uint16_t* W = Words + k * (size_t)Streams;
        const size_t Out = k / 8 * 10;
        if (Streams == 1)
            PackSixteen(_mm256_loadu_si256((const __m256i*)W), Bits[0] + Out);
        else if (Streams == 2)
        {
            // Each lane: four even words, then four odd; then the even halves together.
            const __m256i A = _mm256_permute4x64_epi64(
                _mm256_shuffle_epi8(_mm256_loadu_si256((const __m256i*)W), Part), 0xD8);
            const __m256i B = _mm256_permute4x64_epi64(
                _mm256_shuffle_epi8(_mm256_loadu_si256((const __m256i*)(W + 16)), Part),
                0xD8);
            PackSixteen(_mm256_permute2x128_si256(A, B, 0x20), Bits[0] + Out);
            PackSixteen(_mm256_permute2x128_si256(A, B, 0x31), Bits[1] + Out);
        }
        else
        {
            __m256i R[8];
            for (int r = 0; r < 8; r++)
                R[r] = _mm256_inserti128_si256(
                    _mm256_castsi128_si256(_mm_loadu_si128((const __m128i*)(W + 8 * r))),
                    _mm_loadu_si128((const __m128i*)(W + 64 + 8 * r)), 1);
            const __m256i A0 = _mm256_unpacklo_epi16(R[0], R[1]);
            const __m256i A1 = _mm256_unpackhi_epi16(R[0], R[1]);
            const __m256i A2 = _mm256_unpacklo_epi16(R[2], R[3]);
            const __m256i A3 = _mm256_unpackhi_epi16(R[2], R[3]);
            const __m256i A4 = _mm256_unpacklo_epi16(R[4], R[5]);
            const __m256i A5 = _mm256_unpackhi_epi16(R[4], R[5]);
            const __m256i A6 = _mm256_unpacklo_epi16(R[6], R[7]);
            const __m256i A7 = _mm256_unpackhi_epi16(R[6], R[7]);
            const __m256i B0 = _mm256_unpacklo_epi32(A0, A2);
            const __m256i B1 = _mm256_unpackhi_epi32(A0, A2);
            const __m256i B2 = _mm256_unpacklo_epi32(A1, A3);
            const __m256i B3 = _mm256_unpackhi_epi32(A1, A3);
            const __m256i B4 = _mm256_unpacklo_epi32(A4, A6);
            const __m256i B5 = _mm256_unpackhi_epi32(A4, A6);
            const __m256i B6 = _mm256_unpacklo_epi32(A5, A7);
            const __m256i B7 = _mm256_unpackhi_epi32(A5, A7);
            PackSixteen(_mm256_unpacklo_epi64(B0, B4), Bits[0] + Out);
            PackSixteen(_mm256_unpackhi_epi64(B0, B4), Bits[1] + Out);
            PackSixteen(_mm256_unpacklo_epi64(B1, B5), Bits[2] + Out);
            PackSixteen(_mm256_unpackhi_epi64(B1, B5), Bits[3] + Out);
            PackSixteen(_mm256_unpacklo_epi64(B2, B6), Bits[4] + Out);
            PackSixteen(_mm256_unpackhi_epi64(B2, B6), Bits[5] + Out);
            PackSixteen(_mm256_unpacklo_epi64(B3, B7), Bits[6] + Out);
            PackSixteen(_mm256_unpackhi_epi64(B3, B7), Bits[7] + Out);
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiCrc_StreamsAvx2Unchecked -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Other numbers of streams go to the version with SSSE3.
//
void DtSdiCrc_StreamsAvx2Unchecked(const uint16_t* Words, size_t Count, int Streams,
                                   const uint32_t* Table, uint32_t* Crcs)
{
    if (Count == 0 || Count % 64 != 0 || Count > DT_SDICRC_CLMUL_MAX_WORDS ||
        (Streams != 1 && Streams != 2 && Streams != 8))
    {
        DtSdiCrc_StreamsClmulUnchecked(Words, Count, Streams, Table, Crcs);
        return;
    }
    uint8_t Bits[DT_SDICRC_MAX_STREAMS][DT_SDICRC_CLMUL_RUN_BYTES];
    uint8_t* Runs[DT_SDICRC_MAX_STREAMS];
    for (int s = 0; s < DT_SDICRC_MAX_STREAMS; s++)
        Runs[s] = Bits[s];
    PackStreams(Words, Count, Streams, Runs);
    for (int s = 0; s < Streams; s++)
        Crcs[s] = DtSdiCrc_FoldClmul(Bits[s], Count / 64 * 5, Table);
}
