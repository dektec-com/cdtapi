// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestSdiFrame.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - The receive channel's frame format and DTAPI's raw SDI frame
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The expected values are computed here bit by bit, independently of the module's own
// packing and its fast paths, from symbols chosen per line and position.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// CDtapiLite includes
#include "CDtapiLite.h"         // DTAPI_VIDSTD_ codes and results.
#include "DtTest.h"             // Test framework.
#include "Video/DtFrameProps.h" // Frame geometry for the expected sizes.
#include "Video/DtSdiFrame.h"   // Module under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Every standard that is not 4K.
static const int g_Standards[] = {
    DTAPI_VIDSTD_525I59_94,    DTAPI_VIDSTD_625I50,     DTAPI_VIDSTD_720P23_98,
    DTAPI_VIDSTD_720P24,       DTAPI_VIDSTD_720P25,     DTAPI_VIDSTD_720P29_97,
    DTAPI_VIDSTD_720P30,       DTAPI_VIDSTD_720P50,     DTAPI_VIDSTD_720P59_94,
    DTAPI_VIDSTD_720P60,       DTAPI_VIDSTD_1080P23_98, DTAPI_VIDSTD_1080P24,
    DTAPI_VIDSTD_1080P25,      DTAPI_VIDSTD_1080P29_97, DTAPI_VIDSTD_1080P30,
    DTAPI_VIDSTD_1080PSF23_98, DTAPI_VIDSTD_1080PSF24,  DTAPI_VIDSTD_1080PSF25,
    DTAPI_VIDSTD_1080PSF29_97, DTAPI_VIDSTD_1080PSF30,  DTAPI_VIDSTD_1080I50,
    DTAPI_VIDSTD_1080I59_94,   DTAPI_VIDSTD_1080I60,    DTAPI_VIDSTD_1080P50,
    DTAPI_VIDSTD_1080P50B,     DTAPI_VIDSTD_1080P59_94, DTAPI_VIDSTD_1080P59_94B,
    DTAPI_VIDSTD_1080P60,      DTAPI_VIDSTD_1080P60B,
};

#define STANDARD_COUNT ((int)(sizeof(g_Standards) / sizeof(g_Standards[0])))

// The symbol a test puts at a position of a line.
static uint32_t Symbol(int Line, int Section, size_t Index)
{
    uint32_t Value = (uint32_t)Line * 2654435761u ^ (uint32_t)Section * 40503u ^
                     (uint32_t)Index * 2246822519u;

    Value ^= Value >> 13;
    return (uint32_t)(Value & 0x3FF);
}

// Sets bit Bit of Bytes.
static void SetBit(uint8_t* Bytes, size_t Bit)
{
    Bytes[Bit / 8] = (uint8_t)(Bytes[Bit / 8] | 1u << (Bit % 8));
}

// Writes a coded line of Layout for line Line: each section packed bit by bit, and its
// padding filled with ones, which must not reach a raw frame.
static void CodeLine(const DtSdiFrameLayout* Layout, int Line, uint8_t* Coded)
{
    const int Syms[2] = {Layout->LineSymsHanc, Layout->LineSymsVideo};
    const int Bytes[2] = {Layout->LineBytesHanc, Layout->LineBytesVideo};
    uint8_t* Section = Coded;
    int s;

    memset(Coded, 0, (size_t)Layout->Stride);
    for (s = 0; s < 2; s++)
    {
        size_t i, b;

        for (i = 0; i < (size_t)Syms[s]; i++)
        {
            uint32_t Value = Symbol(Line, s, i);
            for (b = 0; b < 10; b++)
            {
                if (Value >> b & 1)
                    SetBit(Section, i * 10 + b);
            }
        }
        for (b = (size_t)Syms[s] * 10; b < (size_t)Bytes[s] * 8; b++)
            SetBit(Section, b);
        Section += Bytes[s];
    }
}

// Writes into Raw, bit by bit, what line Line of Layout becomes with SymbolBits.
static void ExpectLine(const DtSdiFrameLayout* Layout, int SymbolBits, int Line,
                       uint8_t* Raw)
{
    const int Syms[2] = {Layout->LineSymsHanc, Layout->LineSymsVideo};
    size_t Symbol0 = (size_t)Line * (size_t)(Syms[0] + Syms[1]);
    int s;

    for (s = 0; s < 2; s++)
    {
        size_t i, b;

        for (i = 0; i < (size_t)Syms[s]; i++)
        {
            uint32_t Value = Symbol(Line, s, i);
            size_t Bit = (Symbol0 + i) * (size_t)SymbolBits;

            if (SymbolBits == 8)
                Value >>= 2;
            for (b = 0; b < (size_t)SymbolBits; b++)
            {
                if (Value >> b & 1)
                    SetBit(Raw, Bit + b);
            }
        }
        Symbol0 += (size_t)Syms[s];
    }
}

// Converts lines Lines of Layout with SymbolBits and compares the raw frame with the
// expected one. Returns false, having reported it, when they differ.
static bool ConvertAndCompare(const DtSdiFrameLayout* Layout, int SymbolBits,
                              const int* Lines, int NumLines, int* DtFailures)
{
    size_t Size = DtSdiFrameRawSize(Layout, SymbolBits);
    uint8_t* Coded = (uint8_t*)malloc((size_t)Layout->Stride);
    uint8_t* Raw = (uint8_t*)calloc(Size, 1);
    uint8_t* Expected = (uint8_t*)calloc(Size, 1);
    bool Same = false;
    int i;

    if (Coded != NULL && Raw != NULL && Expected != NULL)
    {
        for (i = 0; i < NumLines; i++)
        {
            CodeLine(Layout, Lines[i], Coded);
            DtSdiFrameConvertLine(Layout, SymbolBits, Coded, Lines[i], Raw);
            ExpectLine(Layout, SymbolBits, Lines[i], Expected);
        }
        Same = memcmp(Raw, Expected, Size) == 0;
    }
    if (!Same)
    {
        printf("    FAIL: standard %d, %d bits: the raw frame differs\n", Layout->VidStd,
               SymbolBits);
        (*DtFailures)++;
    }
    free(Coded);
    free(Raw);
    free(Expected);
    return Same;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Layout +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// 1080i50 with the 128-bit alignment of a DTA-2178.
DT_TEST(Layout1080I50)
{
    DtSdiFrameLayout Layout;

    DT_ASSERT(DtSdiFrameLayoutInit(&Layout, DTAPI_VIDSTD_1080I50, 128));
    DT_ASSERT_EQ(Layout.VidStd, DTAPI_VIDSTD_1080I50);
    DT_ASSERT_EQ(Layout.Alignment, 16);
    DT_ASSERT_EQ(Layout.HeaderBytes, 16);
    DT_ASSERT_EQ(Layout.NumLines, 1125);
    DT_ASSERT_EQ(Layout.LineSymsHanc, 1440);
    DT_ASSERT_EQ(Layout.LineBytesHanc, 1808);
    DT_ASSERT_EQ(Layout.LineSymsVideo, 3840);
    DT_ASSERT_EQ(Layout.LineBytesVideo, 4800);
    DT_ASSERT_EQ(Layout.Stride, 6608);
    DT_ASSERT_EQ(Layout.Format, DT_SDIFRAME_FORMAT_UNCOMPRESSED);
    DT_ASSERT_EQ(DtSdiFrameCodedSize(&Layout), 16 + 1125 * 6608);
}

// SD, and other alignments: 32 bits, and three bytes, which pads the header too.
DT_TEST(LayoutOtherAlignments)
{
    DtSdiFrameLayout Layout;

    DT_ASSERT(DtSdiFrameLayoutInit(&Layout, DTAPI_VIDSTD_625I50, 32));
    DT_ASSERT_EQ(Layout.LineSymsHanc, 288);
    DT_ASSERT_EQ(Layout.LineBytesHanc, 360);
    DT_ASSERT_EQ(Layout.LineSymsVideo, 1440);
    DT_ASSERT_EQ(Layout.LineBytesVideo, 1800);
    DT_ASSERT_EQ(Layout.NumLines, 625);

    DT_ASSERT(DtSdiFrameLayoutInit(&Layout, DTAPI_VIDSTD_525I59_94, 24));
    DT_ASSERT_EQ(Layout.HeaderBytes, 18);
    DT_ASSERT_EQ(Layout.LineSymsHanc, 276);
    DT_ASSERT_EQ(Layout.LineBytesHanc, 345);
    DT_ASSERT_EQ(Layout.Stride, 345 + 1800);
}

DT_TEST(LayoutRefuses)
{
    DtSdiFrameLayout Layout;

    DT_ASSERT(!DtSdiFrameLayoutInit(&Layout, DTAPI_VIDSTD_2160P50, 128));
    DT_ASSERT_EQ(Layout.VidStd, DTAPI_VIDSTD_UNKNOWN);
    DT_ASSERT(!DtSdiFrameLayoutInit(&Layout, DTAPI_VIDSTD_2160P30, 128));
    DT_ASSERT(!DtSdiFrameLayoutInit(&Layout, DTAPI_VIDSTD_UNKNOWN, 128));
    DT_ASSERT(!DtSdiFrameLayoutInit(&Layout, 12345, 128));
    DT_ASSERT(!DtSdiFrameLayoutInit(&Layout, DTAPI_VIDSTD_1080I50, 0));
    DT_ASSERT(!DtSdiFrameLayoutInit(&Layout, DTAPI_VIDSTD_1080I50, -8));
    DT_ASSERT(!DtSdiFrameLayoutInit(&Layout, DTAPI_VIDSTD_1080I50, 12));
}

// Every standard's sections are its HANC and active symbols, each padded.
DT_TEST(LayoutEveryStandard)
{
    int i;

    for (i = 0; i < STANDARD_COUNT; i++)
    {
        DtSdiFrameLayout Layout;
        DtFrameProps Props;
        int Hanc, Video;

        DT_ASSERT(DtFramePropsInit(&Props, g_Standards[i]));
        DT_ASSERT(DtSdiFrameLayoutInit(&Layout, g_Standards[i], 128));
        Hanc = Props.LineNumSymEav + Props.LineNumSymHanc + Props.LineNumSymSav;
        Video = Props.LineNumSymVanc;
        DT_ASSERT_EQ(Layout.LineSymsHanc, Hanc);
        DT_ASSERT_EQ(Layout.LineSymsVideo, Video);
        DT_ASSERT_EQ(Layout.LineBytesHanc, (Hanc * 10 + 127) / 128 * 16);
        DT_ASSERT_EQ(Layout.LineBytesVideo, (Video * 10 + 127) / 128 * 16);
        DT_ASSERT_EQ(Layout.NumLines, DtFramePropsNumLines(&Props));
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Header +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(HeaderBytes)
{
    static const uint8_t Expected[DT_SDIFRAME_HEADER_BYTES] = {
        0xFE, 0xFB, 0xEF, 0xFF, 0x13, 0x00, 0x34, 0x12,
        0x78, 0x56, 0x34, 0x12, 0x0D, 0x0C, 0x0B, 0x0A,
    };
    DtSdiFrameHeader Header, Decoded;
    uint8_t Bytes[DT_SDIFRAME_HEADER_BYTES];

    Header.SyncWord = DT_SDIFRAME_SYNC_WORD;
    Header.ProtocolVersion = 3;
    Header.Format = 1;
    Header.FrameId = 0x1234;
    Header.PtpSeconds = 0x12345678;
    Header.PtpNanoseconds = 0x0A0B0C0D;
    memset(Bytes, 0xEE, sizeof(Bytes));
    DtSdiFrameEncodeHeader(&Header, Bytes);
    DT_ASSERT_MEM(Bytes, Expected, sizeof(Expected));

    DtSdiFrameDecodeHeader(Expected, &Decoded);
    DT_ASSERT_EQ(Decoded.SyncWord, DT_SDIFRAME_SYNC_WORD);
    DT_ASSERT_EQ(Decoded.ProtocolVersion, 3);
    DT_ASSERT_EQ(Decoded.Format, 1);
    DT_ASSERT_EQ(Decoded.FrameId, 0x1234);
    DT_ASSERT_EQ(Decoded.PtpSeconds, 0x12345678);
    DT_ASSERT_EQ(Decoded.PtpNanoseconds, 0x0A0B0C0D);
}

// The reserved bits are ignored, and the fields keep their widths.
DT_TEST(HeaderFieldWidths)
{
    static const uint8_t Bytes[DT_SDIFRAME_HEADER_BYTES] = {
        0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    DtSdiFrameHeader Header;
    uint8_t Encoded[DT_SDIFRAME_HEADER_BYTES];

    DtSdiFrameDecodeHeader(Bytes, &Header);
    DT_ASSERT_EQ(Header.ProtocolVersion, 15);
    DT_ASSERT_EQ(Header.Format, 15);
    DT_ASSERT_EQ(Header.FrameId, 0xFFFF);

    Header.FrameId = 0x12345;
    Header.ProtocolVersion = 0x11;
    DtSdiFrameEncodeHeader(&Header, Encoded);
    DT_ASSERT_EQ(Encoded[4], 0xF1);
    DT_ASSERT_EQ(Encoded[5], 0x00);
    DT_ASSERT_EQ(Encoded[6], 0x45);
    DT_ASSERT_EQ(Encoded[7], 0x23);
}

DT_TEST(HeaderCheck)
{
    DtSdiFrameLayout Layout;
    DtSdiFrameHeader Header;

    DT_ASSERT(DtSdiFrameLayoutInit(&Layout, DTAPI_VIDSTD_1080I50, 128));
    memset(&Header, 0, sizeof(Header));
    Header.SyncWord = DT_SDIFRAME_SYNC_WORD;
    Header.FrameId = 7;

    DT_ASSERT_OK(DtSdiFrameCheckHeader(&Layout, &Header, -1));
    DT_ASSERT_OK(DtSdiFrameCheckHeader(&Layout, &Header, 7));
    DT_ASSERT_EQ(DtSdiFrameCheckHeader(&Layout, &Header, 8), DTAPI_E_INVALID);

    Header.Format = DT_SDIFRAME_FORMAT_UNCOMPRESSED_4K;
    DT_ASSERT_EQ(DtSdiFrameCheckHeader(&Layout, &Header, 7), DTAPI_E_INVALID_FORMAT);
    DT_ASSERT_EQ(DtSdiFrameCheckHeader(&Layout, &Header, 8), DTAPI_E_INVALID);

    Header.SyncWord = DT_SDIFRAME_SYNC_WORD ^ 1;
    DT_ASSERT_EQ(DtSdiFrameCheckHeader(&Layout, &Header, 8), DTAPI_E_OUT_OF_SYNC);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Raw frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Sizes worked out by hand, and those of every standard from its geometry.
DT_TEST(RawSizes)
{
    DtSdiFrameLayout Layout;
    int i;

    DT_ASSERT(DtSdiFrameLayoutInit(&Layout, DTAPI_VIDSTD_1080I59_94, 128));
    DT_ASSERT_EQ(DtSdiFrameRawSize(&Layout, 10), 6187504);
    DT_ASSERT_EQ(DtSdiFrameRawSize(&Layout, 8), 4950000);
    DT_ASSERT_EQ(DtSdiFrameRawSize(&Layout, 16), 9900000);
    DT_ASSERT_EQ(DtSdiFrameRawSize(&Layout, 12), 0);

    DT_ASSERT(DtSdiFrameLayoutInit(&Layout, DTAPI_VIDSTD_525I59_94, 128));
    DT_ASSERT_EQ(DtSdiFrameRawSize(&Layout, 8), 900904);
    DT_ASSERT_EQ(DtSdiFrameRawSize(&Layout, 10), 1126128);

    for (i = 0; i < STANDARD_COUNT; i++)
    {
        DtFrameProps Props;
        size_t Symbols;

        DT_ASSERT(DtFramePropsInit(&Props, g_Standards[i]));
        DT_ASSERT(DtSdiFrameLayoutInit(&Layout, g_Standards[i], 128));
        Symbols = (size_t)DtFramePropsNumLines(&Props) *
                  (size_t)(DtFramePropsLineSymbolsHanc(&Props) + Props.LineNumSymVanc);
        DT_ASSERT_EQ(DtSdiFrameRawSize(&Layout, 8), (Symbols * 8 + 63) / 64 * 8);
        DT_ASSERT_EQ(DtSdiFrameRawSize(&Layout, 10), (Symbols * 10 + 63) / 64 * 8);
        DT_ASSERT_EQ(DtSdiFrameRawSize(&Layout, 16), (Symbols * 16 + 63) / 64 * 8);
    }
}

// The first, a middle and the last line of every standard, in each format, with the
// card's alignment and with 32 bits.
DT_TEST(ConvertsEveryStandard)
{
    static const int Alignments[] = {128, 32};
    static const int Bits[] = {8, 10, 16};
    size_t a, b;
    int i;

    for (i = 0; i < STANDARD_COUNT; i++)
    {
        for (a = 0; a < sizeof(Alignments) / sizeof(Alignments[0]); a++)
        {
            DtSdiFrameLayout Layout;
            int Lines[3];

            DT_ASSERT(DtSdiFrameLayoutInit(&Layout, g_Standards[i], Alignments[a]));
            Lines[0] = 0;
            Lines[1] = Layout.NumLines / 2;
            Lines[2] = Layout.NumLines - 1;
            for (b = 0; b < sizeof(Bits) / sizeof(Bits[0]); b++)
            {
                if (!ConvertAndCompare(&Layout, Bits[b], Lines, 3, DtFailures))
                    return;
            }
        }
    }
}

// Sections and lines whose bits do not end on a byte, which no standard has, go bit by
// bit, and 8- and 16-bit sections not a multiple of four symbols take the slow path.
DT_TEST(ConvertsOddSections)
{
    static const int Sizes[][2] = {{3, 5}, {1, 2}, {7, 9}, {4, 6}};
    static const int Bits[] = {8, 10, 16};
    size_t s, b;

    for (s = 0; s < sizeof(Sizes) / sizeof(Sizes[0]); s++)
    {
        DtSdiFrameLayout Layout;
        int Lines[4] = {0, 1, 2, 5};

        memset(&Layout, 0, sizeof(Layout));
        Layout.VidStd = DTAPI_VIDSTD_625I50;
        Layout.Alignment = 1;
        Layout.NumLines = 6;
        Layout.LineSymsHanc = Sizes[s][0];
        Layout.LineBytesHanc = (Sizes[s][0] * 10 + 7) / 8;
        Layout.LineSymsVideo = Sizes[s][1];
        Layout.LineBytesVideo = (Sizes[s][1] * 10 + 7) / 8;
        Layout.Stride = Layout.LineBytesHanc + Layout.LineBytesVideo;

        for (b = 0; b < sizeof(Bits) / sizeof(Bits[0]); b++)
        {
            if (!ConvertAndCompare(&Layout, Bits[b], Lines, 4, DtFailures))
                return;
        }
    }
}

// An unknown symbol size writes nothing.
DT_TEST(ConvertsNothingForOtherSizes)
{
    DtSdiFrameLayout Layout;
    uint8_t Coded[8000];
    uint8_t Raw[64] = {0};
    static const uint8_t Zero[64] = {0};

    DT_ASSERT(DtSdiFrameLayoutInit(&Layout, DTAPI_VIDSTD_625I50, 32));
    memset(Coded, 0xFF, sizeof(Coded));
    DtSdiFrameConvertLine(&Layout, 12, Coded, 0, Raw);
    DT_ASSERT_MEM(Raw, Zero, sizeof(Raw));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame sync +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Packs twelve 10-bit symbols into the start of a coded line.
static void PackLineStart(const uint32_t Symbols[12], uint8_t* Bytes)
{
    size_t i, b;

    memset(Bytes, 0, DT_SDIFRAME_LINE_START_BYTES);
    for (i = 0; i < 12; i++)
        for (b = 0; b < 10; b++)
            if ((Symbols[i] >> b & 1) != 0)
                SetBit(Bytes, i * 10 + b);
}

// The start of an HD line numbered Chroma and Luma in its two channels.
static void HdLineStart(int Chroma, int Luma, uint8_t* Bytes)
{
    uint32_t Symbols[12] = {0x3FF, 0x3FF, 0, 0, 0, 0, 0x274, 0x274, 0, 0, 0, 0};

    Symbols[8] = (uint32_t)((Chroma & 0x7F) << 2) | 0x200;
    Symbols[9] = (uint32_t)((Luma & 0x7F) << 2) | 0x200;
    Symbols[10] = (uint32_t)(((Chroma >> 7) & 0xF) << 2) | 0x200;
    Symbols[11] = (uint32_t)(((Luma >> 7) & 0xF) << 2) | 0x200;
    PackLineStart(Symbols, Bytes);
}

// The start of an SD line with this XYZ word in its EAV.
static void SdLineStart(uint32_t Xyz, uint8_t* Bytes)
{
    uint32_t Symbols[12] = {0x3FF, 0,     0,     0,     0x200, 0x200,
                            0x200, 0x200, 0x200, 0x200, 0x200, 0x200};

    Symbols[3] = Xyz;
    PackLineStart(Symbols, Bytes);
}

// HD: the first line is numbered 1 and the last the number of lines, in both channels,
// after a valid EAV; SD: the EAVs of the first and last line, in their upper eight bits.
DT_TEST(ChecksFirstAndLastLine)
{
    DtSdiFrameLayout Layout;
    uint8_t First[DT_SDIFRAME_LINE_START_BYTES];
    uint8_t Last[DT_SDIFRAME_LINE_START_BYTES];

    DT_ASSERT(DtSdiFrameLayoutInit(&Layout, DTAPI_VIDSTD_1080I50, 128));
    HdLineStart(1, 1, First);
    HdLineStart(1125, 1125, Last);
    DT_ASSERT_OK(DtSdiFrameCheckLines(&Layout, First, Last));
    DT_ASSERT_EQ(DtSdiFrameCheckLines(&Layout, Last, First), DTAPI_E_OUT_OF_SYNC);
    HdLineStart(1124, 1124, Last);
    DT_ASSERT_EQ(DtSdiFrameCheckLines(&Layout, First, Last), DTAPI_E_OUT_OF_SYNC);
    HdLineStart(1125, 1, Last);
    DT_ASSERT_EQ(DtSdiFrameCheckLines(&Layout, First, Last), DTAPI_E_OUT_OF_SYNC);
    HdLineStart(1125, 1125, Last);
    Last[2] |= 0x10; // Bit 0 of the third EAV word
    DT_ASSERT_EQ(DtSdiFrameCheckLines(&Layout, First, Last), DTAPI_E_OUT_OF_SYNC);

    DT_ASSERT(DtSdiFrameLayoutInit(&Layout, DTAPI_VIDSTD_720P50, 32));
    HdLineStart(750, 750, Last);
    DT_ASSERT_OK(DtSdiFrameCheckLines(&Layout, First, Last));

    DT_ASSERT(DtSdiFrameLayoutInit(&Layout, DTAPI_VIDSTD_625I50, 128));
    SdLineStart(0x2D8, First);
    SdLineStart(0x3C4, Last);
    DT_ASSERT_OK(DtSdiFrameCheckLines(&Layout, First, Last));
    SdLineStart(0x3C7, Last);
    DT_ASSERT_OK(DtSdiFrameCheckLines(&Layout, First, Last));
    SdLineStart(0x2D8, Last);
    DT_ASSERT_EQ(DtSdiFrameCheckLines(&Layout, First, Last), DTAPI_E_OUT_OF_SYNC);
    SdLineStart(0x3C4, First);
    SdLineStart(0x3C4, Last);
    DT_ASSERT_EQ(DtSdiFrameCheckLines(&Layout, First, Last), DTAPI_E_OUT_OF_SYNC);

    DT_ASSERT(DtSdiFrameLayoutInit(&Layout, DTAPI_VIDSTD_525I59_94, 128));
    SdLineStart(0x2D8, First);
    DT_ASSERT_OK(DtSdiFrameCheckLines(&Layout, First, Last));
}

DT_TEST_MAIN("SdiFrame", DT_RUN(Layout1080I50), DT_RUN(LayoutOtherAlignments),
             DT_RUN(LayoutRefuses), DT_RUN(LayoutEveryStandard), DT_RUN(HeaderBytes),
             DT_RUN(HeaderFieldWidths), DT_RUN(HeaderCheck), DT_RUN(RawSizes),
             DT_RUN(ConvertsEveryStandard), DT_RUN(ConvertsOddSections),
             DT_RUN(ConvertsNothingForOtherSizes), DT_RUN(ChecksFirstAndLastLine))
