// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestSdiCrc.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The line CRC by table and with PCLMULQDQ against SMPTE ST 292's definition
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The portable CRC must equal DtSdiFrame_Crc18 applied word by word, and the version
// with PCLMULQDQ, where the processor has it, the portable one: on random words, of
// every length up to 128 and every 32nd beyond, past the longest it takes itself, so
// that the lengths of the lines with a CRC come in and others it leaves to the portable
// version; with the words next to each other and a stream apart. And the builder must
// make the same frames with each.

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

#define MAX_STEP 8
#define MAX_WORDS (DT_SDICRC_CLMUL_MAX_WORDS + 64)

static uint32_t g_Seed = 12345;
static uint32_t g_Table[1024];
static uint16_t g_Words[MAX_WORDS * MAX_STEP];

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

// The CRC of Count words Step apart, word by word as SMPTE ST 292 defines it.
static uint32_t Definition(const uint16_t* Words, size_t Count, size_t Step)
{
    uint32_t Crc = 0;
    for (size_t k = 0; k < Count; k++)
        Crc = DtSdiFrame_Crc18(Crc, Words[k * Step]);
    return Crc;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The table gives the definition's CRC.
DT_TEST(PortableEqualsTheDefinition)
{
    Setup();
    static const size_t Counts[] = {0, 1, 7, 64, 720, 1280, 1920, 2048};
    for (size_t c = 0; c < sizeof(Counts) / sizeof(Counts[0]); c++)
    {
        for (size_t Step = 1; Step <= MAX_STEP; Step *= 2)
        {
            const uint32_t Want = Definition(g_Words, Counts[c], Step);
            const uint32_t Got = DtSdiCrc_Words(g_Words, Counts[c], Step, g_Table);
            if (Got != Want)
                DT_FAIL("%zu words, step %zu: %05X, not %05X", Counts[c], Step,
                        (unsigned)Got, (unsigned)Want);
        }
    }
}

// PCLMULQDQ gives the portable version's CRC, for every length it takes itself and for
// those it leaves.
DT_TEST(ClmulEqualsPortable)
{
    Setup();
    const DtSdiCrcFunc Clmul = DtSdiCrc_Clmul();
    if (Clmul == NULL)
    {
        printf("  (no PCLMULQDQ: skipped)\n");
        return;
    }
    for (size_t Count = 0; Count <= MAX_WORDS; Count += Count < 128 ? 1 : 32)
    {
        for (size_t Step = 1; Step <= MAX_STEP; Step *= 2)
        {
            for (size_t Start = 0; Start < 3; Start++)
            {
                const uint16_t* Words = g_Words + Start;
                const uint32_t Want = DtSdiCrc_Words(Words, Count, Step, g_Table);
                const uint32_t Got = Clmul(Words, Count, Step, g_Table);
                if (Got != Want)
                    DT_FAIL("%zu words, step %zu, from %zu: %05X, not %05X", Count, Step,
                            Start, (unsigned)Got, (unsigned)Want);
            }
        }
    }
}

// The fastest version is one of the two.
DT_TEST(BestIsAVersion)
{
    const DtSdiCrcFunc Best = DtSdiCrc_Best();
    DT_ASSERT(Best == DtSdiCrc_Words || Best == DtSdiCrc_Clmul());
}

// The builder, with the line CRCs on, makes the same frames with either version: in 720p,
// 1080i, 1080p and 2160p, two frames each so that the first line's CRC covers a line of
// the frame before.
DT_TEST(BuilderAgrees)
{
    const DtSdiCrcFunc Clmul = DtSdiCrc_Clmul();
    if (Clmul == NULL)
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
        DtSdiBuilder_UseCrc(Table, DtSdiCrc_Words);
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
