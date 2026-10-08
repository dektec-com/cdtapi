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
// modulo P, reflected. What is left after the last step, 128 bits, has the run's CRC;
// the table works it out, two zero bits before it to make thirteen words, which from a
// register of 0 change nothing.

// x^191 mod P and x^127 mod P, each reflected into 64 bits.
#define FOLD_FIRST_HALF 0xC7F3000000000000ull
#define FOLD_SECOND_HALF 0x4A36C00000000000ull

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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiCrc_WordsClmulUnchecked -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint32_t DtSdiCrc_WordsClmulUnchecked(const uint16_t* Words, size_t Count, size_t Step,
                                      const uint32_t* Table)
{
    if (Count == 0 || Count % 64 != 0 || Count > DT_SDICRC_CLMUL_MAX_WORDS)
        return DtSdiCrc_Words(Words, Count, Step, Table);

    // Four words to five bytes; each step writes eight, the next overwriting the rest.
    uint8_t Bits[DT_SDICRC_CLMUL_MAX_WORDS / 4 * 5 + 8];
    for (size_t k = 0; k < Count; k += 4)
    {
        const uint16_t* W = Words + k * Step;
        const uint64_t Four =
            (uint64_t)(W[0] & 0x3FF) | (uint64_t)(W[Step] & 0x3FF) << 10 |
            (uint64_t)(W[2 * Step] & 0x3FF) << 20 | (uint64_t)(W[3 * Step] & 0x3FF) << 30;
        memcpy(Bits + k / 4 * 5, &Four, sizeof(Four));
    }

    const size_t Bytes = Count / 4 * 5;
    const __m128i Fold =
        _mm_set_epi64x((long long)FOLD_SECOND_HALF, (long long)FOLD_FIRST_HALF);
    __m128i Reg = _mm_loadu_si128((const __m128i*)Bits);
    for (size_t b = 16; b < Bytes; b += 16)
    {
        const __m128i First = _mm_clmulepi64_si128(Reg, Fold, 0x00);
        const __m128i Second = _mm_clmulepi64_si128(Reg, Fold, 0x11);
        Reg = _mm_xor_si128(_mm_xor_si128(First, Second),
                            _mm_loadu_si128((const __m128i*)(Bits + b)));
    }

    uint64_t Left[2];
    _mm_storeu_si128((__m128i*)Left, Reg);
    uint32_t Crc = 0;
    for (int j = 0; j < 13; j++)
        Crc = (Crc >> 10) ^ Table[(Crc ^ BitsAt(Left[0], Left[1], 10 * j - 2)) & 0x3FF];
    return Crc;
}
