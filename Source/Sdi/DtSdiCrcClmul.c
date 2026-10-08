// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiCrcClmul.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Computes the line CRCs of an HD-SDI line with PCLMULQDQ and SSSE3
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

#if defined(_MSC_VER)
    #include <intrin.h>
#else
    #include <tmmintrin.h>
    #include <wmmintrin.h>
#endif

// CDTAPI includes
#include "DtSdiCrc.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Folding +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// How it works, in three steps:
//
// 1. Pack. Each stream's 10-bit words are packed into a continuous run of bits, in the
//    order they go out on the line: bit 0 of the first word first. Eight words become
//    ten bytes.
//
// 2. Fold. The run is reduced 128 bits at a time. A register holding 128 bits of the run
//    is multiplied (carry-less, with PCLMULQDQ) by two constants, which moves its value
//    128 bits further along the run without changing the CRC. The result is added to
//    the next 128 bits. This is the standard "folding" method for computing a CRC with
//    PCLMULQDQ.
//
//    To keep the processor busy, four registers are folded side by side, each moving 512
//    bits per step. At the end the four are folded into one.
//
// 3. Finish. The last 128 bits go through the lookup table, 10 bits at a time. Two zero
//    bits are added at the front to make 130 bits, or 13 words; leading zeros do not
//    change a CRC that starts from 0.
//
// The constants are x^191 and x^127 modulo the CRC polynomial for a 128-bit step, and
// x^575 and x^511 for a 512-bit step, bit-reversed into 64 bits. The exponents are one
// less than the distance (192 and 128, 576 and 512) because a carry-less product of two
// bit-reversed values comes out multiplied by x.

// The constants that move a register 128 bits on: x^191 and x^127 mod P, bit-reversed.
#define FOLD_FIRST_HALF 0xC7F3000000000000ull
#define FOLD_SECOND_HALF 0x4A36C00000000000ull

// The constants that move a register 512 bits on: x^575 and x^511 mod P, bit-reversed.
#define FOLD4_FIRST_HALF 0x43F0000000000000ull
#define FOLD4_SECOND_HALF 0x7CF1800000000000ull

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Fold -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Moves Reg the distance that Constants stands for along the run, and adds Next, the
// bits found there. Returns the new register.
//
static __m128i Fold(__m128i Reg, __m128i Constants, __m128i Next)
{
    const __m128i First = _mm_clmulepi64_si128(Reg, Constants, 0x00);
    const __m128i Second = _mm_clmulepi64_si128(Reg, Constants, 0x11);
    return _mm_xor_si128(_mm_xor_si128(First, Second), Next);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PackEight -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Packs eight 10-bit words into ten bytes at Out. Stores sixteen bytes; the six extra
// bytes are overwritten by the next call or lie past the end of the run.
//
// A multiply-add joins each pair of words into 20 bits, a shift joins two pairs into 40
// bits, and a byte shuffle gathers the five bytes of each 40 bits.
//
static void PackEight(__m128i Words, uint8_t* Out)
{
    Words = _mm_and_si128(Words, _mm_set1_epi16(0x3FF));
    const __m128i Pairs = _mm_madd_epi16(Words, _mm_set1_epi32(0x04000001));
    const __m128i Fours = _mm_or_si128(_mm_and_si128(Pairs, _mm_set_epi32(0, -1, 0, -1)),
                                       _mm_slli_epi64(_mm_srli_epi64(Pairs, 32), 20));
    const __m128i Bytes = _mm_shuffle_epi8(
        Fours, _mm_setr_epi8(0, 1, 2, 3, 4, 8, 9, 10, 11, 12, -1, -1, -1, -1, -1, -1));
    _mm_storeu_si128((__m128i*)Out, Bytes);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PackStreams -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Separates the interleaved streams and packs each into Bits[s], eight words of each
// stream at a time. Count must be a multiple of eight.
//
// - One stream: its words are loaded as they are.
// - Two streams (HD): sixteen words are loaded and split into even and odd words.
// - Eight streams (2160p): 64 words are loaded into eight registers and transposed, so
//   that each register holds eight words of one stream.
// - Any other number: the words are collected one by one.
//
static void PackStreams(const uint16_t* Words, size_t Count, int Streams, uint8_t** Bits)
{
    const __m128i Even =
        _mm_setr_epi8(0, 1, 4, 5, 8, 9, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1);
    const __m128i Odd =
        _mm_setr_epi8(2, 3, 6, 7, 10, 11, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1);
    for (size_t k = 0; k < Count; k += 8)
    {
        const uint16_t* W = Words + k * (size_t)Streams;
        const size_t Out = k / 8 * 10;
        if (Streams == 1)
            PackEight(_mm_loadu_si128((const __m128i*)W), Bits[0] + Out);
        else if (Streams == 2)
        {
            const __m128i A = _mm_loadu_si128((const __m128i*)W);
            const __m128i B = _mm_loadu_si128((const __m128i*)(W + 8));
            PackEight(
                _mm_unpacklo_epi64(_mm_shuffle_epi8(A, Even), _mm_shuffle_epi8(B, Even)),
                Bits[0] + Out);
            PackEight(
                _mm_unpacklo_epi64(_mm_shuffle_epi8(A, Odd), _mm_shuffle_epi8(B, Odd)),
                Bits[1] + Out);
        }
        else if (Streams == 8)
        {
            __m128i R[8];
            for (int r = 0; r < 8; r++)
                R[r] = _mm_loadu_si128((const __m128i*)(W + 8 * r));
            const __m128i A0 = _mm_unpacklo_epi16(R[0], R[1]);
            const __m128i A1 = _mm_unpackhi_epi16(R[0], R[1]);
            const __m128i A2 = _mm_unpacklo_epi16(R[2], R[3]);
            const __m128i A3 = _mm_unpackhi_epi16(R[2], R[3]);
            const __m128i A4 = _mm_unpacklo_epi16(R[4], R[5]);
            const __m128i A5 = _mm_unpackhi_epi16(R[4], R[5]);
            const __m128i A6 = _mm_unpacklo_epi16(R[6], R[7]);
            const __m128i A7 = _mm_unpackhi_epi16(R[6], R[7]);
            const __m128i B0 = _mm_unpacklo_epi32(A0, A2);
            const __m128i B1 = _mm_unpackhi_epi32(A0, A2);
            const __m128i B2 = _mm_unpacklo_epi32(A1, A3);
            const __m128i B3 = _mm_unpackhi_epi32(A1, A3);
            const __m128i B4 = _mm_unpacklo_epi32(A4, A6);
            const __m128i B5 = _mm_unpackhi_epi32(A4, A6);
            const __m128i B6 = _mm_unpacklo_epi32(A5, A7);
            const __m128i B7 = _mm_unpackhi_epi32(A5, A7);
            PackEight(_mm_unpacklo_epi64(B0, B4), Bits[0] + Out);
            PackEight(_mm_unpackhi_epi64(B0, B4), Bits[1] + Out);
            PackEight(_mm_unpacklo_epi64(B1, B5), Bits[2] + Out);
            PackEight(_mm_unpackhi_epi64(B1, B5), Bits[3] + Out);
            PackEight(_mm_unpacklo_epi64(B2, B6), Bits[4] + Out);
            PackEight(_mm_unpackhi_epi64(B2, B6), Bits[5] + Out);
            PackEight(_mm_unpacklo_epi64(B3, B7), Bits[6] + Out);
            PackEight(_mm_unpackhi_epi64(B3, B7), Bits[7] + Out);
        }
        else
        {
            const size_t S = (size_t)Streams;
            for (int s = 0; s < Streams; s++)
            {
                const uint16_t* V = W + s;
                PackEight(_mm_setr_epi16((short)V[0], (short)V[S], (short)V[2 * S],
                                         (short)V[3 * S], (short)V[4 * S],
                                         (short)V[5 * S], (short)V[6 * S],
                                         (short)V[7 * S]),
                          Bits[s] + Out);
            }
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BitsAt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns the 10 bits that start at bit First of the 128-bit value Low, High. Bit
// positions below 0 read as 0.
//
static uint32_t BitsAt(uint64_t Low, uint64_t High, int First)
{
    if (First < 0)
        return (uint32_t)(Low << -First) & 0x3FF;
    if (First >= 64)
        return (uint32_t)(High >> (First - 64)) & 0x3FF;
    if (First + 10 <= 64)
        return (uint32_t)(Low >> First) & 0x3FF;
    return (uint32_t)(Low >> First | High << (64 - First)) & 0x3FF;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiCrc_FoldClmul -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Runs steps 2 and 3 above. A run of fewer than eight blocks is folded with one register.
//
uint32_t DtSdiCrc_FoldClmul(const uint8_t* Bits, size_t Blocks, const uint32_t* Table)
{
    const __m128i Fold1 =
        _mm_set_epi64x((long long)FOLD_SECOND_HALF, (long long)FOLD_FIRST_HALF);
    __m128i Reg = _mm_loadu_si128((const __m128i*)Bits);
    size_t b = 1;
    if (Blocks >= 8)
    {
        const __m128i Fold4 =
            _mm_set_epi64x((long long)FOLD4_SECOND_HALF, (long long)FOLD4_FIRST_HALF);
        __m128i Regs[4];
        for (int r = 0; r < 4; r++)
            Regs[r] = _mm_loadu_si128((const __m128i*)(Bits + 16 * r));
        for (b = 4; b + 4 <= Blocks; b += 4)
        {
            for (int r = 0; r < 4; r++)
                Regs[r] =
                    Fold(Regs[r], Fold4,
                         _mm_loadu_si128((const __m128i*)(Bits + 16 * (b + (size_t)r))));
        }
        Reg = Regs[0];
        for (int r = 1; r < 4; r++)
            Reg = Fold(Reg, Fold1, Regs[r]);
    }
    for (; b < Blocks; b++)
        Reg = Fold(Reg, Fold1, _mm_loadu_si128((const __m128i*)(Bits + 16 * b)));

    uint64_t Left[2];
    _mm_storeu_si128((__m128i*)Left, Reg);
    uint32_t Crc = 0;
    for (int j = 0; j < 13; j++)
        Crc = (Crc >> 10) ^ Table[(Crc ^ BitsAt(Left[0], Left[1], 10 * j - 2)) & 0x3FF];
    return Crc;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiCrc_StreamsClmulUnchecked -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Packs every stream of the line, then folds each one.
//
void DtSdiCrc_StreamsClmulUnchecked(const uint16_t* Words, size_t Count, int Streams,
                                    const uint32_t* Table, uint32_t* Crcs)
{
    if (Count == 0 || Count % 64 != 0 || Count > DT_SDICRC_CLMUL_MAX_WORDS ||
        Streams < 1 || Streams > DT_SDICRC_MAX_STREAMS)
    {
        DtSdiCrc_Streams(Words, Count, Streams, Table, Crcs);
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
