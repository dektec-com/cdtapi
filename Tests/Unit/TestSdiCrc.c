// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestSdiCrc.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Tests the line CRC versions against the definition in SMPTE ST 292
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The tests check that:
// - the portable version equals DtSdiFrame_Crc18 applied word by word;
// - each PCLMULQDQ version, where the processor has it, equals the portable version;
// - the builder makes the same frames with each version.
//
// The words are random. The lengths are every length up to 128 and every 32nd length
// after that, past the longest the PCLMULQDQ versions handle themselves. That includes
// the line lengths of all standards, and lengths that go to the portable version. The
// numbers of streams are 1, 2 (HD), 8 (2160p) and 3, which is packed word by word.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// CDTAPI includes
#include "DtTest.h"           // Test framework.
#include "Sdi/DtSdiCrc.h"     // The CRC under test.
#include "Video/DtSdiFrame.h" // DtSdiFrame_Crc18, the definition.
#include "cdtapi_sdi.h"       // The builder.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Runs +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

#define MAX_STREAMS DT_SDICRC_MAX_STREAMS
#define MAX_WORDS (DT_SDICRC_CLMUL_MAX_WORDS + 64)

static uint32_t g_Seed = 12345;
static uint32_t g_Table[1024];
static uint16_t g_Words[MAX_WORDS * MAX_STREAMS + 4];

static uint16_t RandomWord(void)
{
    g_Seed = g_Seed * 1103515245u + 12345u;
    return (uint16_t)(g_Seed >> 16 & 0x3FF);
}

static void Setup(void)
{
    for (uint32_t i = 0; i < 1024; i++)
        g_Table[i] = DtSdiFrame_Crc18(i, 0);
    for (size_t i = 0; i < sizeof(g_Words) / sizeof(g_Words[0]); i++)
        g_Words[i] = RandomWord();
}

// Returns the CRC of stream s of the Streams interleaved streams in Words, Count words
// each, computed word by word as SMPTE ST 292 defines it.
static uint32_t Definition(const uint16_t* Words, size_t Count, int Streams, int s)
{
    uint32_t Crc = 0;
    for (size_t k = 0; k < Count; k++)
        Crc = DtSdiFrame_Crc18(Crc, Words[k * (size_t)Streams + (size_t)s]);
    return Crc;
}

// The numbers of streams tested.
static const int g_Streams[] = {1, 2, 3, 8};
#define NUM_STREAMS ((int)(sizeof(g_Streams) / sizeof(g_Streams[0])))

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Checks that the portable version equals the definition.
DT_TEST(PortableEqualsTheDefinition)
{
    Setup();
    static const size_t Counts[] = {0, 1, 7, 64, 720, 1280, 1920, 2048};
    for (size_t c = 0; c < sizeof(Counts) / sizeof(Counts[0]); c++)
    {
        for (int n = 0; n < NUM_STREAMS; n++)
        {
            const int Streams = g_Streams[n];
            uint32_t Crcs[MAX_STREAMS];
            DtSdiCrc_Streams(g_Words, Counts[c], Streams, g_Table, Crcs);
            for (int s = 0; s < Streams; s++)
            {
                const uint32_t Want = Definition(g_Words, Counts[c], Streams, s);
                if (Crcs[s] != Want)
                    DT_FAIL("%zu words, stream %d of %d: %05X, not %05X", Counts[c], s,
                            Streams, (unsigned)Crcs[s], (unsigned)Want);
            }
        }
    }
}

// Checks that a PCLMULQDQ version equals the portable version, for every length, both
// those it handles itself and those it passes on.
static void EqualsPortable(DtSdiCrcFunc Clmul, const char* Name, int* DtFailures)
{
    if (Clmul == NULL)
    {
        printf("  (no %s: skipped)\n", Name);
        return;
    }
    for (size_t Count = 0; Count <= MAX_WORDS; Count += Count < 128 ? 1 : 32)
    {
        for (int n = 0; n < NUM_STREAMS; n++)
        {
            const int Streams = g_Streams[n];
            for (size_t Start = 0; Start < 3; Start++)
            {
                const uint16_t* Words = g_Words + Start;
                uint32_t Want[MAX_STREAMS];
                uint32_t Got[MAX_STREAMS];
                DtSdiCrc_Streams(Words, Count, Streams, g_Table, Want);
                Clmul(Words, Count, Streams, g_Table, Got);
                for (int s = 0; s < Streams; s++)
                {
                    if (Got[s] != Want[s])
                        DT_FAIL(
                            "%s, %zu words, stream %d of %d, from %zu: %05X, not %05X",
                            Name, Count, s, Streams, Start, (unsigned)Got[s],
                            (unsigned)Want[s]);
                }
            }
        }
    }
}

DT_TEST(ClmulEqualsPortable)
{
    Setup();
    EqualsPortable(DtSdiCrc_Clmul(), "PCLMULQDQ with SSSE3", DtFailures);
    EqualsPortable(DtSdiCrc_Avx2(), "PCLMULQDQ with AVX2", DtFailures);
}

// Checks that the fastest version is one of the three.
DT_TEST(BestIsAVersion)
{
    const DtSdiCrcFunc Best = DtSdiCrc_Best();
    DT_ASSERT(Best == DtSdiCrc_Streams || Best == DtSdiCrc_Clmul() ||
              Best == DtSdiCrc_Avx2());
}

// Checks that the builder, with line CRCs on, makes the same frames with the table and
// with the fastest version. In 720p, 1080i, 1080p and 2160p, two frames each, so that
// the first line's CRC also covers the last line of the previous frame.
DT_TEST(BuilderAgrees)
{
    const DtSdiCrcFunc Clmul = DtSdiCrc_Best();
    if (Clmul == DtSdiCrc_Streams)
    {
        printf("  (no PCLMULQDQ: skipped)\n");
        return;
    }
    static const int VidStds[] = {DTAPI_VIDSTD_720P50, DTAPI_VIDSTD_1080I50,
                                  DTAPI_VIDSTD_1080P50, DTAPI_VIDSTD_2160P50};
    for (size_t v = 0; v < sizeof(VidStds) / sizeof(VidStds[0]); v++)
    {
        size_t Size = 0;
        DT_ASSERT_OK(DtSdiView_RawFrameSize(VidStds[v], 10, &Size));
        uint8_t* ByTable = (uint8_t*)malloc(Size);
        uint8_t* ByClmul = (uint8_t*)malloc(Size);
        DtSdiView* View = DtSdiView_Alloc();
        DtSdiBuilder* Table = DtSdiBuilder_Alloc();
        DtSdiBuilder* Folding = DtSdiBuilder_Alloc();
        DT_ASSERT(ByTable != NULL && ByClmul != NULL && View != NULL && Table != NULL &&
                  Folding != NULL);
        DT_ASSERT_OK(DtSdiBuilder_SetChecksums(Table, true));
        DT_ASSERT_OK(DtSdiBuilder_SetChecksums(Folding, true));
        DtSdiBuilder_UseCrc(Table, DtSdiCrc_Streams);
        DtSdiBuilder_UseCrc(Folding, Clmul);
        for (int n = 0; n < 2; n++)
        {
            DT_ASSERT_OK(DtSdiView_SetRawFrame(View, ByTable, Size, VidStds[v], 10));
            DT_ASSERT_OK(DtSdiBuilder_Build(Table, View, NULL, NULL, NULL));
            DT_ASSERT_OK(DtSdiView_SetRawFrame(View, ByClmul, Size, VidStds[v], 10));
            DT_ASSERT_OK(DtSdiBuilder_Build(Folding, View, NULL, NULL, NULL));
            if (memcmp(ByTable, ByClmul, Size) != 0)
                DT_FAIL("standard %d, frame %d: the frames differ", VidStds[v], n);
        }
        DtSdiBuilder_Free(Folding);
        DtSdiBuilder_Free(Table);
        DtSdiView_Free(View);
        free(ByClmul);
        free(ByTable);
    }
}

DT_TEST_MAIN("SdiCrc", DT_RUN(PortableEqualsTheDefinition), DT_RUN(ClmulEqualsPortable),
             DT_RUN(BestIsAVersion), DT_RUN(BuilderAgrees))
