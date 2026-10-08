// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiCrcClmul.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The line CRC of SMPTE ST 292 with PCLMULQDQ
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
// The words are packed into a run of bits as they go on the line, the least significant
// bit of the first word first, which loads into 128-bit registers with the run's first
// bit in bit 0: the reflected order of the CRC. With A the polynomial of a register's
// first 64 bits and B that of its last, the register stands for A x^64 + B, and 128 bits
// later for (A x^64 + B) x^128, which modulo the CRC's polynomial P is A (x^192 mod P) +
// B (x^128 mod P): two carry-less products, of fewer than 128 bits, that take the
// register's place against the next 128 bits of the run. A carry-less product of two
// reflected 64-bit values is the product times x, so the constants are x^191 and x^127
// modulo P, reflected. Four registers take turns, each folded 512 bits on, with x^575
// and x^511, so that no step waits for the one before; at the end they are folded into
// one, 128 bits at a time. What is left after the last step, 128 bits, has the run's
// CRC; the table works it out, two zero bits before it to make thirteen words, which from
// a register of 0 change nothing.
//
// The words are packed eight at a time with SSSE3: the pairs joined by a multiply-add,
// the pairs of pairs by a shift, and the five bytes of each 40 bits gathered by a
// shuffle.

// x^191 mod P and x^127 mod P, each reflected into 64 bits: 128 bits on.
#define FOLD_FIRST_HALF 0xC7F3000000000000ull
#define FOLD_SECOND_HALF 0x4A36C00000000000ull

// x^575 mod P and x^511 mod P, each reflected into 64 bits: 512 bits on.
#define FOLD4_FIRST_HALF 0x43F0000000000000ull
#define FOLD4_SECOND_HALF 0x7CF1800000000000ull

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Fold -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reg moved Fold's distance on, against the bits there.
//
static __m128i Fold(__m128i Reg, __m128i Constants, __m128i Next)
{
    const __m128i First = _mm_clmulepi64_si128(Reg, Constants, 0x00);
    const __m128i Second = _mm_clmulepi64_si128(Reg, Constants, 0x11);
    return _mm_xor_si128(_mm_xor_si128(First, Second), Next);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PackEight -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Packs the eight words of Words, ten bits each, into ten bytes at Out; writes sixteen.
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
// Packs the Count words, a multiple of eight, of each of Streams streams that alternate
// in Words into Bits[s], eight of each at a time: one stream's are loaded as they lie,
// two streams' sixteen at a time and parted into even and odd, eight streams' sixty-four
// at a time and transposed, so that each register holds eight of one stream; any other
// number word by word.
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
// The ten bits of the 128 bits Low, High from bit First on; bits before 0 are 0.
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FoldRun -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The CRC of the Blocks times 128 bits at Bits.
//
static uint32_t FoldRun(const uint8_t* Bits, size_t Blocks, const uint32_t* Table)
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
void DtSdiCrc_StreamsClmulUnchecked(const uint16_t* Words, size_t Count, int Streams,
                                    const uint32_t* Table, uint32_t* Crcs)
{
    if (Count == 0 || Count % 64 != 0 || Count > DT_SDICRC_CLMUL_MAX_WORDS ||
        Streams < 1 || Streams > DT_SDICRC_MAX_STREAMS)
    {
        DtSdiCrc_Streams(Words, Count, Streams, Table, Crcs);
        return;
    }

    // Ten bytes for eight words; each step writes sixteen, the next overwriting the rest.
    enum
    {
        BYTES = DT_SDICRC_CLMUL_MAX_WORDS / 8 * 10 + 16
    };
    uint8_t Bits[DT_SDICRC_MAX_STREAMS][BYTES];
    uint8_t* Runs[DT_SDICRC_MAX_STREAMS];
    for (int s = 0; s < DT_SDICRC_MAX_STREAMS; s++)
        Runs[s] = Bits[s];
    PackStreams(Words, Count, Streams, Runs);
    for (int s = 0; s < Streams; s++)
        Crcs[s] = FoldRun(Bits[s], Count / 64 * 5, Table);
}
