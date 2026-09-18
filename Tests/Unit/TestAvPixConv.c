// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestAvPixConv.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Unit tests for the pixel conversions, in portable C and in SSSE3
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The reference here reads and writes the samples a bit at a time, most significant bit
// first for pixel groups and least significant bit first for 10-bit UYVY, as the formats
// are defined. The portable conversions are held to it, and the SSSE3 ones to both. A
// source is allocated at exactly its size, so that AddressSanitizer sees a read beyond
// it; a destination has guard bytes behind it, which must stay as they were.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "AvFifo/DtAvPixConv.h" // Functions under test.
#include "DtTest.h"             // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define GUARD 32
#define GUARD_BYTE 0xE7

// The numbers of pixel groups each conversion is tried with.
static const size_t Counts[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 13, 14, 33, 960};

typedef enum Pattern
{
    PATTERN_RANDOM,
    PATTERN_ONES,
    PATTERN_ALTERNATE,
    PATTERN_COUNT
} Pattern;

// Fills Size bytes with a pattern.
static void Fill(uint8_t* Buf, size_t Size, Pattern Kind, uint32_t Seed)
{
    uint32_t State = Seed * 2654435761u + 1;
    for (size_t i = 0; i < Size; i++)
    {
        State ^= State << 13;
        State ^= State >> 17;
        State ^= State << 5;
        Buf[i] = Kind == PATTERN_ONES     ? 0xFF
                 : Kind == PATTERN_RANDOM ? (uint8_t)State
                                          : (i % 2 == 0 ? 0xAA : 0x55);
    }
}

// A destination of Size bytes with guard bytes behind it.
static uint8_t* Destination(size_t Size)
{
    uint8_t* Buf = (uint8_t*)malloc(Size + GUARD);
    if (Buf != NULL)
        memset(Buf, GUARD_BYTE, Size + GUARD);
    return Buf;
}

// Whether the guard bytes behind Size bytes are untouched.
static bool GuardIntact(const uint8_t* Buf, size_t Size)
{
    for (size_t i = Size; i < Size + GUARD; i++)
    {
        if (Buf[i] != GUARD_BYTE)
            return false;
    }
    return true;
}

// Bit Index of Buf, most significant bit first in a byte, or least significant first.
static unsigned GetBit(const uint8_t* Buf, size_t Index, bool MsbFirst)
{
    unsigned Shift = MsbFirst ? 7 - (unsigned)(Index % 8) : (unsigned)(Index % 8);
    return (unsigned)(Buf[Index / 8] >> Shift) & 1;
}

static void PutBit(uint8_t* Buf, size_t Index, bool MsbFirst, unsigned Bit)
{
    unsigned Shift = MsbFirst ? 7 - (unsigned)(Index % 8) : (unsigned)(Index % 8);
    Buf[Index / 8] = (uint8_t)((Buf[Index / 8] & ~(1u << Shift)) | Bit << Shift);
}

// Sample K of pixel group I, of 10 bits.
static unsigned GetSample(const uint8_t* Buf, size_t I, int K, bool MsbFirst)
{
    unsigned Value = 0;
    for (int b = 0; b < 10; b++)
    {
        size_t Index = I * 40 + (size_t)(K * 10 + b);
        unsigned Bit = GetBit(Buf, Index, MsbFirst);
        Value |= MsbFirst ? Bit << (9 - b) : Bit << b;
    }
    return Value;
}

static void PutSample(uint8_t* Buf, size_t I, int K, bool MsbFirst, unsigned Value)
{
    for (int b = 0; b < 10; b++)
    {
        size_t Index = I * 40 + (size_t)(K * 10 + b);
        PutBit(Buf, Index, MsbFirst, MsbFirst ? Value >> (9 - b) & 1 : Value >> b & 1);
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Checks +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Checks one set of conversions against the reference for every count and pattern.
static void CheckAgainstReference(const DtAvPixConv* Conv, int* DtFailures)
{
    for (size_t c = 0; c < sizeof(Counts) / sizeof(Counts[0]); c++)
    {
        for (int p = 0; p < PATTERN_COUNT; p++)
        {
            size_t N = Counts[c];
            uint8_t* Src10 = (uint8_t*)malloc(N * 5);
            uint8_t* Src8 = (uint8_t*)malloc(N * 4);
            uint8_t* Expected = (uint8_t*)calloc(N * 5, 1);
            uint8_t* Dst = Destination(N * 5);
            uint8_t* Planes = Destination(N * 4);
            DT_ASSERT(Src10 != NULL && Src8 != NULL && Expected != NULL && Dst != NULL &&
                      Planes != NULL);
            Fill(Src10, N * 5, (Pattern)p, (uint32_t)(c * 7 + (size_t)p));
            Fill(Src8, N * 4, (Pattern)p, (uint32_t)(c * 11 + (size_t)p));

            // Pixel groups to 10-bit UYVY.
            for (size_t i = 0; i < N; i++)
            {
                for (int k = 0; k < 4; k++)
                    PutSample(Expected, i, k, false, GetSample(Src10, i, k, true));
            }
            Conv->Pg10ToUyvy10(Src10, Dst, N);
            if (memcmp(Dst, Expected, N * 5) != 0 || !GuardIntact(Dst, N * 5))
                DT_FAIL("Pg10ToUyvy10, %zu pixel groups, pattern %d", N, p);

            // And back.
            uint8_t* Back = Destination(N * 5);
            DT_ASSERT(Back != NULL);
            Conv->Uyvy10ToPg10(Expected, Back, N);
            bool BackOk = memcmp(Back, Src10, N * 5) == 0 && GuardIntact(Back, N * 5);
            free(Back);
            if (!BackOk)
                DT_FAIL("Uyvy10ToPg10, %zu pixel groups, pattern %d", N, p);

            // Pixel groups to 8-bit UYVY.
            memset(Dst, GUARD_BYTE, N * 5 + GUARD);
            Conv->Pg10ToUyvy8(Src10, Dst, N);
            for (size_t i = 0; i < N; i++)
            {
                for (int k = 0; k < 4; k++)
                {
                    if (Dst[i * 4 + (size_t)k] !=
                        (uint8_t)(GetSample(Src10, i, k, true) >> 2))
                        DT_FAIL("Pg10ToUyvy8, pixel group %zu of %zu, sample %d", i, N,
                                k);
                }
            }
            if (!GuardIntact(Dst, N * 4))
                DT_FAIL("Pg10ToUyvy8 wrote beyond %zu pixel groups", N);

            // 8-bit UYVY to planes.
            Conv->Uyvy8ToYuv422p(Src8, N, Planes, Planes + 2 * N, Planes + 3 * N);
            for (size_t i = 0; i < N; i++)
            {
                if (Planes[2 * i] != Src8[4 * i + 1] ||
                    Planes[2 * i + 1] != Src8[4 * i + 3] ||
                    Planes[2 * N + i] != Src8[4 * i] ||
                    Planes[3 * N + i] != Src8[4 * i + 2])
                {
                    DT_FAIL("Uyvy8ToYuv422p, pixel group %zu of %zu", i, N);
                }
            }
            if (!GuardIntact(Planes, N * 4))
                DT_FAIL("Uyvy8ToYuv422p wrote beyond %zu pixel groups", N);

            free(Src10);
            free(Src8);
            free(Expected);
            free(Dst);
            free(Planes);
        }
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(PortableMatchesReference)
{
    CheckAgainstReference(DtAvPixConv_C(), DtFailures);
}

DT_TEST(Ssse3MatchesReference)
{
    const DtAvPixConv* Ssse3 = DtAvPixConv_Ssse3();
    if (Ssse3 == NULL)
    {
        printf("    no SSSE3 in this build or on this processor; skipped\n");
        return;
    }
    CheckAgainstReference(Ssse3, DtFailures);
}

DT_TEST(Avx2MatchesReference)
{
    const DtAvPixConv* Avx2 = DtAvPixConv_Avx2();
    if (Avx2 == NULL)
    {
        printf("    no AVX2 in this build or on this processor; skipped\n");
        return;
    }
    CheckAgainstReference(Avx2, DtFailures);
}

// The conversions of Fast give the portable ones' bytes for a frame's worth of random
// data converted in rows of varying lengths, each row into its own place in the frame.
static void CheckAgainstPortable(const DtAvPixConv* Fast, int* DtFailures)
{
    const DtAvPixConv* C = DtAvPixConv_C();
    size_t N = 3840 / 2 * 3;
    uint8_t* Src = (uint8_t*)malloc(N * 5);
    uint8_t* A[4] = {Destination(N * 5), Destination(N * 4), Destination(N * 5),
                     Destination(N * 4)};
    uint8_t* B[4] = {Destination(N * 5), Destination(N * 4), Destination(N * 5),
                     Destination(N * 4)};
    DT_ASSERT(Src != NULL && A[0] != NULL && A[1] != NULL && A[2] != NULL &&
              A[3] != NULL && B[0] != NULL && B[1] != NULL && B[2] != NULL &&
              B[3] != NULL);
    Fill(Src, N * 5, PATTERN_RANDOM, 2110);

    size_t Done = 0;
    for (size_t Row = 0; Done < N; Row++)
    {
        size_t Count = 1 + Row * 37 % 700;
        if (Count > N - Done)
            Count = N - Done;
        const uint8_t* In = Src + Done * 5;
        Fast->Pg10ToUyvy10(In, A[0] + Done * 5, Count);
        C->Pg10ToUyvy10(In, B[0] + Done * 5, Count);
        Fast->Pg10ToUyvy8(In, A[1] + Done * 4, Count);
        C->Pg10ToUyvy8(In, B[1] + Done * 4, Count);
        Fast->Uyvy10ToPg10(In, A[2] + Done * 5, Count);
        C->Uyvy10ToPg10(In, B[2] + Done * 5, Count);
        Fast->Uyvy8ToYuv422p(Src + Done * 4, Count, A[3] + Done * 2, A[3] + N * 2 + Done,
                             A[3] + N * 3 + Done);
        C->Uyvy8ToYuv422p(Src + Done * 4, Count, B[3] + Done * 2, B[3] + N * 2 + Done,
                          B[3] + N * 3 + Done);
        Done += Count;
    }
    static const int Sizes[4] = {5, 4, 5, 4};
    for (int i = 0; i < 4; i++)
    {
        size_t Size = N * (size_t)Sizes[i];
        if (memcmp(A[i], B[i], Size) != 0)
            DT_FAIL("conversion %d differs", i);
        DT_ASSERT(GuardIntact(A[i], Size) && GuardIntact(B[i], Size));
        free(A[i]);
        free(B[i]);
    }
    free(Src);
}

DT_TEST(Ssse3MatchesPortable)
{
    const DtAvPixConv* Ssse3 = DtAvPixConv_Ssse3();
    if (Ssse3 == NULL)
    {
        printf("    no SSSE3 in this build or on this processor; skipped\n");
        return;
    }
    CheckAgainstPortable(Ssse3, DtFailures);
}

DT_TEST(Avx2MatchesPortable)
{
    const DtAvPixConv* Avx2 = DtAvPixConv_Avx2();
    if (Avx2 == NULL)
    {
        printf("    no AVX2 in this build or on this processor; skipped\n");
        return;
    }
    CheckAgainstPortable(Avx2, DtFailures);
}

DT_TEST(BestIsAvailable)
{
    const DtAvPixConv* Best = DtAvPixConv_Best();
    const DtAvPixConv* Avx2 = DtAvPixConv_Avx2();
    const DtAvPixConv* Ssse3 = DtAvPixConv_Ssse3();
    DT_ASSERT(Best != NULL);
    DT_ASSERT(Best == (Avx2 != NULL ? Avx2 : Ssse3 != NULL ? Ssse3 : DtAvPixConv_C()));
    printf("    %s\n", Best == Avx2 ? "AVX2" : Best == Ssse3 ? "SSSE3" : "portable C");
}

DT_TEST_MAIN("AvPixConv", DT_RUN(PortableMatchesReference), DT_RUN(Ssse3MatchesReference),
             DT_RUN(Avx2MatchesReference), DT_RUN(Ssse3MatchesPortable),
             DT_RUN(Avx2MatchesPortable), DT_RUN(BestIsAvailable))
