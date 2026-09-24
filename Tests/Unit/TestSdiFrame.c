// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestSdiFrame.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The firmware's coded SDI frames, DTAPI's raw SDI frame, and black frames
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

// CDTAPI includes
#include "DtTest.h"             // Test framework.
#include "Video/DtFrameProps.h" // Frame geometry for the expected sizes.
#include "Video/DtSdi4k.h"      // The conversions each instruction set has.
#include "Video/DtSdiFrame.h"   // Module under test.
#include "cdtapi.h"             // DTAPI_VIDSTD_ codes and results.

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
// padding filled with ones, which must not reach a raw frame, or with zeros.
static void CodeLine(const DtSdiFrameLayout* Layout, int Line, bool PadOnes,
                     uint8_t* Coded)
{
    const int Syms[2] = {Layout->LineNumSymsHanc, Layout->LineNumSymsVideo};
    const int Bytes[2] = {Layout->SectionBytesHanc, Layout->SectionBytesVideo};
    uint8_t* Section = Coded;

    memset(Coded, 0, (size_t)Layout->Stride);
    for (int s = 0; s < 2; s++)
    {
        size_t b;

        for (size_t i = 0; i < (size_t)Syms[s]; i++)
        {
            uint32_t Value = Symbol(Line, s, i);
            for (b = 0; b < 10; b++)
            {
                if (Value >> b & 1)
                    SetBit(Section, i * 10 + b);
            }
        }
        for (b = (size_t)Syms[s] * 10; PadOnes && b < (size_t)Bytes[s] * 8; b++)
            SetBit(Section, b);
        Section += Bytes[s];
    }
}

// Writes into Raw, bit by bit, what line Line of Layout becomes with BitsPerSymbol.
static void ExpectLine(const DtSdiFrameLayout* Layout, int BitsPerSymbol, int Line,
                       uint8_t* Raw)
{
    const int Syms[2] = {Layout->LineNumSymsHanc, Layout->LineNumSymsVideo};
    size_t Symbol0 = (size_t)Line * (size_t)(Syms[0] + Syms[1]);

    for (int s = 0; s < 2; s++)
    {
        for (size_t i = 0; i < (size_t)Syms[s]; i++)
        {
            uint32_t Value = Symbol(Line, s, i);
            size_t Bit = (Symbol0 + i) * (size_t)BitsPerSymbol;

            if (BitsPerSymbol == 8)
                Value >>= 2;
            size_t b;
            for (b = 0; b < (size_t)BitsPerSymbol; b++)
            {
                if (Value >> b & 1)
                    SetBit(Raw, Bit + b);
            }
        }
        Symbol0 += (size_t)Syms[s];
    }
}

// Converts lines Lines of Layout with BitsPerSymbol and compares the raw frame with the
// expected one. Returns false, having reported it, when they differ.
static bool ConvertAndCompare(const DtSdiFrameLayout* Layout, int BitsPerSymbol,
                              const int* Lines, int NumLines, int* DtFailures)
{
    size_t Size = DtSdiFrame_RawSize(Layout, BitsPerSymbol);
    uint8_t* Coded = (uint8_t*)malloc((size_t)Layout->Stride);
    uint8_t* Raw = (uint8_t*)calloc(Size, 1);
    uint8_t* Expected = (uint8_t*)calloc(Size, 1);
    bool Same = false;

    if (Coded != NULL && Raw != NULL && Expected != NULL)
    {
        for (int i = 0; i < NumLines; i++)
        {
            CodeLine(Layout, Lines[i], true, Coded);
            DtSdiFrame_ConvertLine(Layout, BitsPerSymbol, Coded, Lines[i], Raw);
            ExpectLine(Layout, BitsPerSymbol, Lines[i], Expected);
        }
        Same = memcmp(Raw, Expected, Size) == 0;
    }
    if (!Same)
    {
        printf("    FAIL: standard %d, %d bits: the raw frame differs\n", Layout->VidStd,
               BitsPerSymbol);
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

    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_1080I50, 128));
    DT_ASSERT_EQ(Layout.VidStd, DTAPI_VIDSTD_1080I50);
    DT_ASSERT_EQ(Layout.Alignment, 16);
    DT_ASSERT_EQ(Layout.HeaderNumBytes, 16);
    DT_ASSERT_EQ(Layout.NumLines, 1125);
    DT_ASSERT_EQ(Layout.LineNumSymsHanc, 1440);
    DT_ASSERT_EQ(Layout.SectionBytesHanc, 1808);
    DT_ASSERT_EQ(Layout.LineNumSymsVideo, 3840);
    DT_ASSERT_EQ(Layout.SectionBytesVideo, 4800);
    DT_ASSERT_EQ(Layout.Stride, 6608);
    DT_ASSERT_EQ(Layout.Format, DT_SDIFRAME_FORMAT_UNCOMPRESSED);
    DT_ASSERT_EQ(DtSdiFrame_CodedSize(&Layout), 16 + 1125 * 6608);
}

// SD, and other alignments: 32 bits, and three bytes, which pads the header too.
DT_TEST(LayoutOtherAlignments)
{
    DtSdiFrameLayout Layout;

    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_625I50, 32));
    DT_ASSERT_EQ(Layout.LineNumSymsHanc, 288);
    DT_ASSERT_EQ(Layout.SectionBytesHanc, 360);
    DT_ASSERT_EQ(Layout.LineNumSymsVideo, 1440);
    DT_ASSERT_EQ(Layout.SectionBytesVideo, 1800);
    DT_ASSERT_EQ(Layout.NumLines, 625);

    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_525I59_94, 24));
    DT_ASSERT_EQ(Layout.HeaderNumBytes, 18);
    DT_ASSERT_EQ(Layout.LineNumSymsHanc, 276);
    DT_ASSERT_EQ(Layout.SectionBytesHanc, 345);
    DT_ASSERT_EQ(Layout.Stride, 345 + 1800);
}

DT_TEST(LayoutRefuses)
{
    DtSdiFrameLayout Layout;

    DT_ASSERT(!DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_2160P50B, 128));
    DT_ASSERT_EQ(Layout.VidStd, DTAPI_VIDSTD_UNKNOWN);
    DT_ASSERT(!DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_2160P60B, 128));
    DT_ASSERT(!DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_UNKNOWN, 128));
    DT_ASSERT(!DtSdiFrame_LayoutInit(&Layout, 12345, 128));
    DT_ASSERT(!DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_1080I50, 0));
    DT_ASSERT(!DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_1080I50, -8));
    DT_ASSERT(!DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_1080I50, 12));
}

// Every standard's sections are its HANC and active symbols, each padded.
DT_TEST(LayoutEveryStandard)
{
    for (int i = 0; i < STANDARD_COUNT; i++)
    {
        DtFrameProps Props;

        DT_ASSERT(DtFrameProps_Init(&Props, g_Standards[i]));
        DtSdiFrameLayout Layout;
        DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, g_Standards[i], 128));
        int Hanc = Props.LineNumSymEav + Props.LineNumSymHanc + Props.LineNumSymSav;
        int Video = Props.LineNumSymVanc;
        DT_ASSERT_EQ(Layout.LineNumSymsHanc, Hanc);
        DT_ASSERT_EQ(Layout.LineNumSymsVideo, Video);
        DT_ASSERT_EQ(Layout.SectionBytesHanc, (Hanc * 10 + 127) / 128 * 16);
        DT_ASSERT_EQ(Layout.SectionBytesVideo, (Video * 10 + 127) / 128 * 16);
        DT_ASSERT_EQ(Layout.NumLines, DtFrameProps_NumLines(&Props));
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Header +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(HeaderNumBytes)
{
    static const uint8_t Expected[DT_SDIFRAME_HEADER_BYTES] = {
        0xFE, 0xFB, 0xEF, 0xFF, 0x13, 0x00, 0x34, 0x12,
        0x78, 0x56, 0x34, 0x12, 0x0D, 0x0C, 0x0B, 0x0A,
    };
    DtSdiFrameHeader Header;

    Header.SyncWord = DT_SDIFRAME_SYNC_WORD;
    Header.ProtocolVersion = 3;
    Header.Format = 1;
    Header.FrameId = 0x1234;
    Header.PtpSeconds = 0x12345678;
    Header.PtpNanoseconds = 0x0A0B0C0D;
    uint8_t Bytes[DT_SDIFRAME_HEADER_BYTES];
    memset(Bytes, 0xEE, sizeof(Bytes));
    DtSdiFrame_EncodeHeader(&Header, Bytes);
    DT_ASSERT_MEM(Bytes, Expected, sizeof(Expected));

    DtSdiFrameHeader Decoded;
    DtSdiFrame_DecodeHeader(Expected, &Decoded);
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

    DtSdiFrame_DecodeHeader(Bytes, &Header);
    DT_ASSERT_EQ(Header.ProtocolVersion, 15);
    DT_ASSERT_EQ(Header.Format, 15);
    DT_ASSERT_EQ(Header.FrameId, 0xFFFF);

    Header.FrameId = 0x12345;
    Header.ProtocolVersion = 0x11;
    uint8_t Encoded[DT_SDIFRAME_HEADER_BYTES];
    DtSdiFrame_EncodeHeader(&Header, Encoded);
    DT_ASSERT_EQ(Encoded[4], 0xF1);
    DT_ASSERT_EQ(Encoded[5], 0x00);
    DT_ASSERT_EQ(Encoded[6], 0x45);
    DT_ASSERT_EQ(Encoded[7], 0x23);
}

DT_TEST(HeaderCheck)
{
    DtSdiFrameLayout Layout;

    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_1080I50, 128));
    DtSdiFrameHeader Header;
    memset(&Header, 0, sizeof(Header));
    Header.SyncWord = DT_SDIFRAME_SYNC_WORD;
    Header.FrameId = 7;

    DT_ASSERT_OK(DtSdiFrame_CheckHeader(&Layout, &Header, -1));
    DT_ASSERT_OK(DtSdiFrame_CheckHeader(&Layout, &Header, 7));
    DT_ASSERT_EQ(DtSdiFrame_CheckHeader(&Layout, &Header, 8), DTAPI_E_INVALID);

    Header.Format = DT_SDIFRAME_FORMAT_UNCOMPRESSED_4K;
    DT_ASSERT_EQ(DtSdiFrame_CheckHeader(&Layout, &Header, 7), DTAPI_E_INVALID_FORMAT);
    DT_ASSERT_EQ(DtSdiFrame_CheckHeader(&Layout, &Header, 8), DTAPI_E_INVALID);

    Header.SyncWord = DT_SDIFRAME_SYNC_WORD ^ 1;
    DT_ASSERT_EQ(DtSdiFrame_CheckHeader(&Layout, &Header, 8), DTAPI_E_OUT_OF_SYNC);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Raw frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Sizes worked out by hand, and those of every standard from its geometry.
DT_TEST(RawSizes)
{
    DtSdiFrameLayout Layout;

    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_1080I59_94, 128));
    DT_ASSERT_EQ(DtSdiFrame_RawSize(&Layout, 10), 6187504);
    DT_ASSERT_EQ(DtSdiFrame_RawSize(&Layout, 8), 4950000);
    DT_ASSERT_EQ(DtSdiFrame_RawSize(&Layout, 16), 9900000);
    DT_ASSERT_EQ(DtSdiFrame_RawSize(&Layout, 12), 0);

    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_525I59_94, 128));
    DT_ASSERT_EQ(DtSdiFrame_RawSize(&Layout, 8), 900904);
    DT_ASSERT_EQ(DtSdiFrame_RawSize(&Layout, 10), 1126128);

    for (int i = 0; i < STANDARD_COUNT; i++)
    {
        DtFrameProps Props;

        DT_ASSERT(DtFrameProps_Init(&Props, g_Standards[i]));
        DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, g_Standards[i], 128));
        size_t Symbols =
            (size_t)DtFrameProps_NumLines(&Props) *
            (size_t)(DtFrameProps_LineSymbolsHanc(&Props) + Props.LineNumSymVanc);
        DT_ASSERT_EQ(DtSdiFrame_RawSize(&Layout, 8), (Symbols * 8 + 63) / 64 * 8);
        DT_ASSERT_EQ(DtSdiFrame_RawSize(&Layout, 10), (Symbols * 10 + 63) / 64 * 8);
        DT_ASSERT_EQ(DtSdiFrame_RawSize(&Layout, 16), (Symbols * 16 + 63) / 64 * 8);
    }
}

// The first, a middle and the last line of every standard, in each format, with the
// card's alignment and with 32 bits.
DT_TEST(ConvertsEveryStandard)
{
    static const int Alignments[] = {128, 32};
    static const int Bits[] = {8, 10, 16};

    for (int i = 0; i < STANDARD_COUNT; i++)
    {
        for (size_t a = 0; a < sizeof(Alignments) / sizeof(Alignments[0]); a++)
        {
            DtSdiFrameLayout Layout;

            DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, g_Standards[i], Alignments[a]));
            int Lines[3];
            Lines[0] = 0;
            Lines[1] = Layout.NumLines / 2;
            Lines[2] = Layout.NumLines - 1;
            size_t b;
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
    size_t s;
    size_t b;

    for (s = 0; s < sizeof(Sizes) / sizeof(Sizes[0]); s++)
    {
        DtSdiFrameLayout Layout;
        int Lines[4] = {0, 1, 2, 5};

        memset(&Layout, 0, sizeof(Layout));
        Layout.VidStd = DTAPI_VIDSTD_625I50;
        Layout.Alignment = 1;
        Layout.NumLines = 6;
        Layout.LineNumSymsHanc = Sizes[s][0];
        Layout.SectionBytesHanc = (Sizes[s][0] * 10 + 7) / 8;
        Layout.LineNumSymsVideo = Sizes[s][1];
        Layout.SectionBytesVideo = (Sizes[s][1] * 10 + 7) / 8;
        Layout.Stride = Layout.SectionBytesHanc + Layout.SectionBytesVideo;

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
    uint8_t Raw[64] = {0};
    static const uint8_t Zero[64] = {0};

    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_625I50, 32));
    uint8_t Coded[8000];
    memset(Coded, 0xFF, sizeof(Coded));
    DtSdiFrame_ConvertLine(&Layout, 12, Coded, 0, Raw);
    DT_ASSERT_MEM(Raw, Zero, sizeof(Raw));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame sync +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Packs twelve 10-bit symbols into the start of a coded line.
static void PackLineStart(const uint32_t Symbols[12], uint8_t* Bytes)
{
    memset(Bytes, 0, DT_SDIFRAME_LINE_START_BYTES);
    size_t b;
    for (size_t i = 0; i < 12; i++)
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

    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_1080I50, 128));
    uint8_t First[DT_SDIFRAME_LINE_START_BYTES];
    HdLineStart(1, 1, First);
    uint8_t Last[DT_SDIFRAME_LINE_START_BYTES];
    HdLineStart(1125, 1125, Last);
    DT_ASSERT_OK(DtSdiFrame_CheckLines(&Layout, First, Last));
    DT_ASSERT_EQ(DtSdiFrame_CheckLines(&Layout, Last, First), DTAPI_E_OUT_OF_SYNC);
    HdLineStart(1124, 1124, Last);
    DT_ASSERT_EQ(DtSdiFrame_CheckLines(&Layout, First, Last), DTAPI_E_OUT_OF_SYNC);
    HdLineStart(1125, 1, Last);
    DT_ASSERT_EQ(DtSdiFrame_CheckLines(&Layout, First, Last), DTAPI_E_OUT_OF_SYNC);
    HdLineStart(1125, 1125, Last);
    Last[2] |= 0x10; // Bit 0 of the third EAV word
    DT_ASSERT_EQ(DtSdiFrame_CheckLines(&Layout, First, Last), DTAPI_E_OUT_OF_SYNC);

    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_720P50, 32));
    HdLineStart(750, 750, Last);
    DT_ASSERT_OK(DtSdiFrame_CheckLines(&Layout, First, Last));

    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_625I50, 128));
    SdLineStart(0x2D8, First);
    SdLineStart(0x3C4, Last);
    DT_ASSERT_OK(DtSdiFrame_CheckLines(&Layout, First, Last));
    SdLineStart(0x3C7, Last);
    DT_ASSERT_OK(DtSdiFrame_CheckLines(&Layout, First, Last));
    SdLineStart(0x2D8, Last);
    DT_ASSERT_EQ(DtSdiFrame_CheckLines(&Layout, First, Last), DTAPI_E_OUT_OF_SYNC);
    SdLineStart(0x3C4, First);
    SdLineStart(0x3C4, Last);
    DT_ASSERT_EQ(DtSdiFrame_CheckLines(&Layout, First, Last), DTAPI_E_OUT_OF_SYNC);

    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_525I59_94, 128));
    SdLineStart(0x2D8, First);
    DT_ASSERT_OK(DtSdiFrame_CheckLines(&Layout, First, Last));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmit header +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The transmit header's padded size, and the SDI rate of every standard.
DT_TEST(LayoutTransmit)
{
    DtSdiFrameLayout Layout;

    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_1080I50, 128));
    DT_ASSERT_EQ(Layout.TxHeaderNumBytes, 32);
    DT_ASSERT_EQ(Layout.SdiRate, DT_SDIRATE_HD);
    DT_ASSERT_EQ(DtSdiFrame_TxCodedSize(&Layout), 32 + 1125 * 6608);
    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_625I50, 32));
    DT_ASSERT_EQ(Layout.TxHeaderNumBytes, 20);
    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_525I59_94, 24));
    DT_ASSERT_EQ(Layout.TxHeaderNumBytes, 21);
    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_720P50, 512));
    DT_ASSERT_EQ(Layout.TxHeaderNumBytes, 64);

    for (int i = 0; i < STANDARD_COUNT; i++)
    {
        const int Std = g_Standards[i];
        int Expected = DT_SDIRATE_HD;

        if (Std == DTAPI_VIDSTD_525I59_94 || Std == DTAPI_VIDSTD_625I50)
            Expected = DT_SDIRATE_SD;
        else if (Std == DTAPI_VIDSTD_1080P50 || Std == DTAPI_VIDSTD_1080P50B ||
                 Std == DTAPI_VIDSTD_1080P59_94 || Std == DTAPI_VIDSTD_1080P59_94B ||
                 Std == DTAPI_VIDSTD_1080P60 || Std == DTAPI_VIDSTD_1080P60B)
            Expected = DT_SDIRATE_3G;
        DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, Std, 128));
        DT_ASSERT_EQ(Layout.SdiRate, Expected);
    }
}

// 1080i50 with the card's alignment, as the card took it on port 5.
DT_TEST(TxHeaderNumBytes)
{
    static const uint8_t Expected[DT_SDIFRAME_TX_HEADER_BYTES] = {
        0xFE, 0xFB, 0xEF, 0xFF, 0x00, 0x03, 0x00, 0x00, 0x34, 0x12,
        0x65, 0x04, 0x71, 0x00, 0xA0, 0x05, 0x2C, 0x01, 0x00, 0x0F,
    };
    DtSdiFrameLayout Layout;

    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_1080I50, 128));
    DtSdiFrameTxHeader Header;
    DtSdiFrame_TxHeaderInit(&Layout, 0x1234, &Header);
    uint8_t Bytes[DT_SDIFRAME_TX_HEADER_BYTES];
    memset(Bytes, 0xEE, sizeof(Bytes));
    DtSdiFrame_EncodeTxHeader(&Header, Bytes);
    DT_ASSERT_MEM(Bytes, Expected, sizeof(Expected));

    DtSdiFrameTxHeader Decoded;
    DtSdiFrame_DecodeTxHeader(Expected, &Decoded);
    DT_ASSERT_EQ(Decoded.SyncWord, DT_SDIFRAME_SYNC_WORD);
    DT_ASSERT_EQ(Decoded.ProtocolVersion, 0);
    DT_ASSERT_EQ(Decoded.Format, DT_SDIFRAME_FORMAT_UNCOMPRESSED);
    DT_ASSERT(Decoded.SdiRateValid);
    DT_ASSERT_EQ(Decoded.SdiRate, DT_SDIRATE_HD);
    DT_ASSERT_EQ(Decoded.FrameId, 0x1234);
    DT_ASSERT_EQ(Decoded.NumCodedLines, 1125);
    DT_ASSERT_EQ(Decoded.NumWordsHanc, 113);
    DT_ASSERT_EQ(Decoded.NumSymsHanc, 1440);
    DT_ASSERT_EQ(Decoded.NumWordsVideo, 300);
    DT_ASSERT_EQ(Decoded.NumSymsVideo, 3840);

    // 3G, and a frame ID that keeps its lower 16 bits.
    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_1080P50B, 128));
    DtSdiFrame_TxHeaderInit(&Layout, 0x10002, &Header);
    DT_ASSERT_EQ(Header.FrameId, 2);
    DtSdiFrame_EncodeTxHeader(&Header, Bytes);
    DT_ASSERT_EQ(Bytes[4], 0x00);
    DT_ASSERT_EQ(Bytes[5], 0x05);
    DT_ASSERT_EQ(Bytes[8], 0x02);
    DT_ASSERT_EQ(Bytes[9], 0x00);
    DT_ASSERT_EQ(Bytes[12], 0x71);
    DT_ASSERT_EQ(Bytes[16], 0x2C);
}

// The reserved bits are ignored, and the fields keep their widths.
DT_TEST(TxHeaderFieldWidths)
{
    static const uint8_t Expected[DT_SDIFRAME_TX_HEADER_BYTES] = {
        0x00, 0x00, 0x00, 0x00, 0x2F, 0x0E, 0x00, 0x00, 0x45, 0x23,
        0x01, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x34, 0x12,
    };
    uint8_t Bytes[DT_SDIFRAME_TX_HEADER_BYTES];

    memset(Bytes, 0xFF, sizeof(Bytes));
    DtSdiFrameTxHeader Header;
    DtSdiFrame_DecodeTxHeader(Bytes, &Header);
    DT_ASSERT_EQ(Header.SyncWord, 0xFFFFFFFFu);
    DT_ASSERT_EQ(Header.ProtocolVersion, 15);
    DT_ASSERT_EQ(Header.Format, 15);
    DT_ASSERT(Header.SdiRateValid);
    DT_ASSERT_EQ(Header.SdiRate, 7);
    DT_ASSERT_EQ(Header.FrameId, 0xFFFF);
    DT_ASSERT_EQ(Header.NumCodedLines, 0xFFFF);
    DT_ASSERT_EQ(Header.NumWordsHanc, 0xFFFF);
    DT_ASSERT_EQ(Header.NumSymsHanc, 0xFFFF);
    DT_ASSERT_EQ(Header.NumWordsVideo, 0xFFFF);
    DT_ASSERT_EQ(Header.NumSymsVideo, 0xFFFF);

    Header.SyncWord = 0;
    Header.Format = 0x12;
    Header.SdiRateValid = false;
    Header.SdiRate = 0xF;
    Header.FrameId = 0x12345;
    Header.NumCodedLines = 0x10001;
    Header.NumSymsHanc = 0x10000;
    Header.NumWordsVideo = 0x10000;
    Header.NumSymsVideo = 0x1234;
    DtSdiFrame_EncodeTxHeader(&Header, Bytes);
    DT_ASSERT_MEM(Bytes, Expected, sizeof(Expected));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Coding lines +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(RawLineBits)
{
    DtSdiFrameLayout Layout;

    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_1080I50, 128));
    DT_ASSERT_EQ(DtSdiFrame_RawLineNumBits(&Layout, 8), 42240);
    DT_ASSERT_EQ(DtSdiFrame_RawLineNumBits(&Layout, 10), 52800);
    DT_ASSERT_EQ(DtSdiFrame_RawLineNumBits(&Layout, 16), 84480);
    DT_ASSERT_EQ(DtSdiFrame_RawLineNumBits(&Layout, 12), 0);
    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_720P24, 128));
    DT_ASSERT_EQ(DtSdiFrame_RawLineNumBits(&Layout, 10), 82500);

    // Only 720p23.98 and 720p24 have 10-bit lines that end half-way a byte.
    for (int i = 0; i < STANDARD_COUNT; i++)
    {
        const bool HalfByte = g_Standards[i] == DTAPI_VIDSTD_720P23_98 ||
                              g_Standards[i] == DTAPI_VIDSTD_720P24;

        DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, g_Standards[i], 128));
        DT_ASSERT_EQ(DtSdiFrame_RawLineNumBits(&Layout, 10) % 8, HalfByte ? 4 : 0);
        DT_ASSERT_EQ(DtSdiFrame_RawLineNumBits(&Layout, 16) % 8, 0);
    }
}

// Writes the Count low bits of Value at bit Bit of Bytes, setting and clearing.
static void PutBits(uint8_t* Bytes, size_t Bit, uint32_t Value, int Count)
{
    for (int b = 0; b < Count; b++)
    {
        size_t At = Bit + (size_t)b;
        uint8_t Mask = (uint8_t)(1u << (At % 8));

        if ((Value >> b & 1) != 0)
            Bytes[At / 8] = (uint8_t)(Bytes[At / 8] | Mask);
        else
            Bytes[At / 8] = (uint8_t)(Bytes[At / 8] & ~Mask);
    }
}

// Codes line Line of Layout in BitsPerSymbol from a buffer that holds only the line's
// bytes,
// with the line starting at bit Phase and every bit around it set, and with 16 bits the
// six unused bits of every symbol set too. The coded line must equal the reference
// packer's, and converted back into a raw frame it must give the line's raw frame.
// Returns false, having reported it, when anything differs.
static bool CodeAndCompare(const DtSdiFrameLayout* Layout, int BitsPerSymbol, int Line,
                           int Phase, int* DtFailures)
{
    const int Syms[2] = {Layout->LineNumSymsHanc, Layout->LineNumSymsVideo};
    const size_t LineBits = DtSdiFrame_RawLineNumBits(Layout, BitsPerSymbol);
    const size_t LineBytes = ((size_t)Phase + LineBits + 7) / 8;
    const size_t Size = DtSdiFrame_RawSize(Layout, BitsPerSymbol);
    const size_t Stride = (size_t)Layout->Stride;
    uint8_t* RawLine = (uint8_t*)malloc(LineBytes);
    uint8_t* Coded = (uint8_t*)malloc(Stride);
    uint8_t* Reference = (uint8_t*)malloc(Stride);
    uint8_t* Raw = (uint8_t*)calloc(Size, 1);
    uint8_t* Expected = (uint8_t*)calloc(Size, 1);
    const char* Failure = "out of memory";

    if (RawLine != NULL && Coded != NULL && Reference != NULL && Raw != NULL &&
        Expected != NULL)
    {
        // The raw frame bytes the line touches, and one on either side.
        const size_t First = (size_t)Line * LineBits / 8;
        const size_t From = First > 0 ? First - 1 : 0;
        const size_t To = ((size_t)Line + 1) * LineBits / 8 + 2;
        const size_t Compared = (To < Size ? To : Size) - From;
        size_t Index = 0;
        size_t i;
        int s;

        memset(RawLine, 0xFF, LineBytes);
        for (s = 0; s < 2; s++)
        {
            for (i = 0; i < (size_t)Syms[s]; i++, Index++)
            {
                uint32_t Value =
                    Symbol(Line, s, i) | (BitsPerSymbol == 16 ? 0xFC00u : 0u);

                PutBits(RawLine, (size_t)Phase + Index * (size_t)BitsPerSymbol, Value,
                        BitsPerSymbol);
            }
        }
        CodeLine(Layout, Line, false, Reference);
        ExpectLine(Layout, BitsPerSymbol, Line, Expected);
        memset(Coded, 0xEE, Stride);

        if (!DtSdiFrame_CodeLine(Layout, BitsPerSymbol, RawLine, Phase, Coded))
            Failure = "refused";
        else if (memcmp(Coded, Reference, Stride) != 0)
            Failure = "the coded line differs from the reference";
        else
        {
            DtSdiFrame_ConvertLine(Layout, BitsPerSymbol, Coded, Line, Raw);
            Failure = memcmp(Raw + From, Expected + From, Compared) == 0
                          ? NULL
                          : "converted back, the raw line differs";
        }
    }
    if (Failure != NULL)
    {
        printf("    FAIL: standard %d, %d bits, line %d, phase %d: %s\n", Layout->VidStd,
               BitsPerSymbol, Line, Phase, Failure);
        (*DtFailures)++;
    }
    free(RawLine);
    free(Coded);
    free(Reference);
    free(Raw);
    free(Expected);
    return Failure == NULL;
}

// The first two, a middle and the last line of every standard, in 10 and 16 bits, with
// the card's alignment and with 32 bits, each at the bit it starts at in a frame.
DT_TEST(CodesEveryStandard)
{
    static const int Alignments[] = {128, 32};
    static const int Bits[] = {10, 16};
    int HalfByteStarts = 0;

    for (int i = 0; i < STANDARD_COUNT; i++)
    {
        for (size_t a = 0; a < sizeof(Alignments) / sizeof(Alignments[0]); a++)
        {
            DtSdiFrameLayout Layout;

            DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, g_Standards[i], Alignments[a]));
            int Lines[4];
            Lines[0] = 0;
            Lines[1] = 1;
            Lines[2] = Layout.NumLines / 2;
            Lines[3] = Layout.NumLines - 1;
            size_t b;
            for (b = 0; b < sizeof(Bits) / sizeof(Bits[0]); b++)
            {
                const size_t LineBits = DtSdiFrame_RawLineNumBits(&Layout, Bits[b]);

                size_t l;
                for (l = 0; l < 4; l++)
                {
                    const int Phase = (int)((size_t)Lines[l] * LineBits % 8);

                    HalfByteStarts += Phase == 4 ? 1 : 0;
                    if (!CodeAndCompare(&Layout, Bits[b], Lines[l], Phase, DtFailures))
                        return;
                }
            }
        }
    }
    DT_ASSERT(HalfByteStarts > 0);
}

// 10-bit lines at every phase, with sections whose bits end anywhere in a byte, and
// 16-bit sections that are no multiple of four symbols.
DT_TEST(CodesAnyPhase)
{
    static const int Sizes[][2] = {{3, 5}, {1, 2}, {7, 9}, {4, 6}, {8, 8}};
    DtSdiFrameLayout Layout;
    size_t s;
    int Phase;

    for (s = 0; s < sizeof(Sizes) / sizeof(Sizes[0]); s++)
    {
        memset(&Layout, 0, sizeof(Layout));
        Layout.VidStd = DTAPI_VIDSTD_625I50;
        Layout.Alignment = 1;
        Layout.NumLines = 6;
        Layout.LineNumSymsHanc = Sizes[s][0];
        Layout.SectionBytesHanc = (Sizes[s][0] * 10 + 7) / 8;
        Layout.LineNumSymsVideo = Sizes[s][1];
        Layout.SectionBytesVideo = (Sizes[s][1] * 10 + 7) / 8;
        Layout.Stride = Layout.SectionBytesHanc + Layout.SectionBytesVideo;

        for (Phase = 0; Phase < 8; Phase++)
        {
            if (!CodeAndCompare(&Layout, 10, 2, Phase, DtFailures))
                return;
        }
        if (!CodeAndCompare(&Layout, 16, 5, 0, DtFailures))
            return;
    }

    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_720P24, 128));
    for (Phase = 0; Phase < 8; Phase++)
    {
        if (!CodeAndCompare(&Layout, 10, 1, Phase, DtFailures))
            return;
    }
}

// Other symbol sizes and phases write nothing.
DT_TEST(CodeLineRefuses)
{
    static const uint8_t Untouched[16] = {
        0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
        0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    };
    DtSdiFrameLayout Layout;

    memset(&Layout, 0, sizeof(Layout));
    Layout.VidStd = DTAPI_VIDSTD_625I50;
    Layout.Alignment = 1;
    Layout.NumLines = 1;
    Layout.LineNumSymsHanc = 4;
    Layout.SectionBytesHanc = 5;
    Layout.LineNumSymsVideo = 4;
    Layout.SectionBytesVideo = 5;
    Layout.Stride = 10;
    uint8_t Raw[32];
    memset(Raw, 0x5A, sizeof(Raw));
    uint8_t Coded[16];
    memset(Coded, 0xEE, sizeof(Coded));

    DT_ASSERT(!DtSdiFrame_CodeLine(&Layout, 8, Raw, 1, Coded));
    DT_ASSERT(!DtSdiFrame_CodeLine(&Layout, 12, Raw, 0, Coded));
    DT_ASSERT(!DtSdiFrame_CodeLine(&Layout, 10, Raw, -1, Coded));
    DT_ASSERT(!DtSdiFrame_CodeLine(&Layout, 10, Raw, 8, Coded));
    DT_ASSERT(!DtSdiFrame_CodeLine(&Layout, 16, Raw, 4, Coded));
    DT_ASSERT_MEM(Coded, Untouched, sizeof(Coded));

    DT_ASSERT(DtSdiFrame_CodeLine(&Layout, 10, Raw, 7, Coded));
    DT_ASSERT(DtSdiFrame_CodeLine(&Layout, 16, Raw, 0, Coded));
    DT_ASSERT_MEM(Coded + 10, Untouched, 6);
}

// An 8-bit symbol is coded as the 10-bit symbol with the same upper eight bits.
DT_TEST(CodeLine8Bits)
{
    DtSdiFrameLayout Layout;

    memset(&Layout, 0, sizeof(Layout));
    Layout.VidStd = DTAPI_VIDSTD_625I50;
    Layout.Alignment = 1;
    Layout.NumLines = 1;
    Layout.LineNumSymsHanc = 3;
    Layout.SectionBytesHanc = 4;
    Layout.LineNumSymsVideo = 5;
    Layout.SectionBytesVideo = 7;
    Layout.Stride = 11;
    static const uint8_t Raw8[8] = {0xFF, 0x00, 0x80, 0x9D, 0x01, 0xFE, 0x10, 0x7F};
    uint8_t Raw10[10];
    memset(Raw10, 0, sizeof(Raw10));
    for (int i = 0; i < 8; i++)
    {
        uint32_t Symbol = (uint32_t)Raw8[i] << 2;
        for (int Bit = 0; Bit < 10; Bit++)
            Raw10[(i * 10 + Bit) / 8] |=
                (uint8_t)((Symbol >> Bit & 1) << ((i * 10 + Bit) % 8));
    }
    uint8_t Coded8[11];
    uint8_t Coded10[11];
    memset(Coded8, 0xEE, sizeof(Coded8));
    memset(Coded10, 0x11, sizeof(Coded10));

    DT_ASSERT(DtSdiFrame_CodeLine(&Layout, 8, Raw8, 0, Coded8));
    DT_ASSERT(DtSdiFrame_CodeLine(&Layout, 10, Raw10, 0, Coded10));
    DT_ASSERT_MEM(Coded8, Coded10, sizeof(Coded8));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Black frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Symbol Index of a packed 10-bit section, read bit by bit.
static uint32_t GetSymbol(const uint8_t* Section, size_t Index)
{
    uint32_t Value = 0;

    for (size_t b = 0; b < 10; b++)
    {
        size_t Bit = Index * 10 + b;

        Value |= (uint32_t)(Section[Bit / 8] >> (Bit % 8) & 1) << b;
    }
    return Value;
}

// Continues SMPTE 292's CRC-18 Crc over Count 10-bit words, each least significant bit
// first, as the 18 stages of a shift register with feedback into x^0, x^4 and x^5.
static uint32_t RefCrc18(uint32_t Crc, const uint32_t* Words, size_t Count)
{
    uint8_t Stage[18];
    int i;

    for (i = 0; i < 18; i++)
        Stage[i] = (uint8_t)(Crc >> i & 1);
    for (size_t w = 0; w < Count; w++)
    {
        for (int b = 0; b < 10; b++)
        {
            const uint8_t In = (uint8_t)((Words[w] >> b & 1) ^ Stage[0]);

            for (i = 0; i < 17; i++)
                Stage[i] = Stage[i + 1];
            Stage[17] = In;
            Stage[13] = (uint8_t)(Stage[13] ^ In);
            Stage[12] = (uint8_t)(Stage[12] ^ In);
        }
    }
    for (Crc = 0, i = 0; i < 18; i++)
        Crc |= (uint32_t)Stage[i] << i;
    return Crc;
}

// Nine bits of Value with bit 9 the inverse of bit 8.
static uint32_t Protected(uint32_t Value)
{
    Value &= 0x1FF;
    return (Value & 0x100) != 0 ? Value : Value | 0x200;
}

// The fourth word of a timing reference for line Line, from 1, of a frame of Props, from
// SMPTE 125's table of the eight legal words by F, V and H.
static uint32_t ExpectedXyz(const DtFrameProps* Props, int Line, bool Eav)
{
    static const uint32_t Legal[8] = {0x200, 0x274, 0x2AC, 0x2D8,
                                      0x31C, 0x368, 0x3B0, 0x3C4};
    const int F = Props->NumFields == 2 && Line >= Props->Fields[1].StartLine ? 1 : 0;
    const DtFieldProps* Field = &Props->Fields[F];
    const int V = Line < Field->VidStartLine || Line > Field->VidEndLine ? 1 : 0;

    return Legal[F * 4 + V * 2 + (Eav ? 1 : 0)];
}

// Checks coded line Line, from 1, of a black frame: its timing references with, in HD,
// the line numbers and CRCs, where ActiveCrc holds each channel's CRC over a black active
// part; with Full also every other symbol and the padding. Returns what differs first, or
// NULL.
static const char* CheckBlackLine(const DtSdiFrameLayout* Layout,
                                  const DtFrameProps* Props, const uint32_t ActiveCrc[2],
                                  const uint8_t* Coded, int Line, bool Full)
{
    const uint8_t* Sections[2] = {Coded, Coded + Layout->SectionBytesHanc};
    const size_t Syms[2] = {(size_t)Layout->LineNumSymsHanc,
                            (size_t)Layout->LineNumSymsVideo};
    const size_t Bytes[2] = {(size_t)Layout->SectionBytesHanc,
                             (size_t)Layout->SectionBytesVideo};
    const bool Hd = Props->LineNumSymEav == 16;
    const size_t Width = Hd ? 2 : 1;  // Symbols per word of a timing reference
    const size_t Start = Hd ? 16 : 4; // Symbols of the EAV, line numbers and CRCs
    const size_t Sav = Syms[0] - 4 * Width;
    size_t s;
    size_t i;
    size_t c;

    for (c = 0; c < Width; c++)
    {
        uint32_t Words[8] = {0x3FF, 0x000, 0x000, ExpectedXyz(Props, Line, true),
                             0,     0,     0,     0};

        if (Hd)
        {
            Words[4] = Protected((uint32_t)Line << 2);
            Words[5] = Protected((uint32_t)Line >> 7 << 2);
            uint32_t Crc = RefCrc18(ActiveCrc[c], Words, 6);
            Words[6] = Protected(Crc);
            Words[7] = Protected(Crc >> 9);
        }
        for (i = 0; i < Start / Width; i++)
        {
            if (GetSymbol(Coded, i * Width + c) != Words[i])
                return i < 4 ? "EAV" : i < 6 ? "line number" : "CRC";
        }
        Words[3] = ExpectedXyz(Props, Line, false);
        for (i = 0; i < 4; i++)
        {
            if (GetSymbol(Coded, Sav + i * Width + c) != Words[i])
                return "SAV";
        }
    }

    for (s = 0; Full && s < 2; s++)
    {
        for (i = 0; i < Syms[s]; i++)
        {
            if (s == 0 && (i < Start || i >= Sav))
                continue;
            if (GetSymbol(Sections[s], i) != (i % 2 == 0 ? 0x200u : 0x040u))
                return s == 0 ? "blanking" : "active part";
        }
        for (i = Syms[s] * 10; i < Bytes[s] * 8; i++)
        {
            if ((Sections[s][i / 8] >> (i % 8) & 1) != 0)
                return "padding";
        }
    }
    return NULL;
}

// Every line's timing references, and all of the first, the last and every 97th line,
// in every standard with the card's alignment and with 24 bits.
DT_TEST(BlackFramesEveryStandard)
{
    static const int Alignments[] = {128, 24};

    for (int i = 0; i < STANDARD_COUNT; i++)
    {
        for (size_t a = 0; a < sizeof(Alignments) / sizeof(Alignments[0]); a++)
        {
            DtSdiFrameLayout Layout;
            uint32_t ActiveCrc[2] = {0, 0};
            const uint32_t Black[2] = {0x200, 0x040};

            DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, g_Standards[i], Alignments[a]));
            DtFrameProps Props;
            DT_ASSERT(DtFrameProps_Init(&Props, g_Standards[i]));
            size_t Stride = (size_t)Layout.Stride;
            uint8_t* Lines = (uint8_t*)malloc((size_t)Layout.NumLines * Stride);
            DT_ASSERT(Lines != NULL);
            memset(Lines, 0xEE, (size_t)Layout.NumLines * Stride);
            DtSdiFrame_BlackLines(&Layout, Lines);

            for (size_t v = 0; v < (size_t)Layout.LineNumSymsVideo / 2; v++)
            {
                ActiveCrc[0] = RefCrc18(ActiveCrc[0], &Black[0], 1);
                ActiveCrc[1] = RefCrc18(ActiveCrc[1], &Black[1], 1);
            }
            for (int Line = 1; Line <= Layout.NumLines; Line++)
            {
                const bool Full = Line == 1 || Line == Layout.NumLines || Line % 97 == 0;
                const char* Failure =
                    CheckBlackLine(&Layout, &Props, ActiveCrc,
                                   Lines + (size_t)(Line - 1) * Stride, Line, Full);

                if (Failure != NULL)
                {
                    printf("    FAIL: standard %d, alignment %d, line %d: %s\n",
                           g_Standards[i], Alignments[a], Line, Failure);
                    (*DtFailures)++;
                    free(Lines);
                    return;
                }
            }
            DT_ASSERT_OK(DtSdiFrame_CheckLines(
                &Layout, Lines, Lines + (size_t)(Layout.NumLines - 1) * Stride));

            // CRCs worked out from the polynomial for the first line of 1080i50.
            if (g_Standards[i] == DTAPI_VIDSTD_1080I50)
            {
                DT_ASSERT_EQ(GetSymbol(Lines, 12), 0x2F7);
                DT_ASSERT_EQ(GetSymbol(Lines, 14), 0x1E8);
                DT_ASSERT_EQ(GetSymbol(Lines, 13), 0x2BB);
                DT_ASSERT_EQ(GetSymbol(Lines, 15), 0x23C);
            }
            free(Lines);
        }
    }
}

// A black frame converted into a raw frame and coded back gives the same lines, in 10
// and 16 bits.
DT_TEST(BlackFrameRoundTrip)
{
    static const int Bits[] = {10, 16};

    for (int i = 0; i < STANDARD_COUNT; i++)
    {
        DtSdiFrameLayout Layout;

        DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, g_Standards[i], 128));
        size_t Stride = (size_t)Layout.Stride;
        uint8_t* Lines = (uint8_t*)malloc((size_t)Layout.NumLines * Stride);
        uint8_t* Coded = (uint8_t*)malloc(Stride);
        DT_ASSERT(Lines != NULL && Coded != NULL);
        DtSdiFrame_BlackLines(&Layout, Lines);

        size_t b;
        for (b = 0; b < sizeof(Bits) / sizeof(Bits[0]); b++)
        {
            const size_t LineBits = DtSdiFrame_RawLineNumBits(&Layout, Bits[b]);
            uint8_t* Raw = (uint8_t*)calloc(DtSdiFrame_RawSize(&Layout, Bits[b]), 1);
            int Differ = -1;

            DT_ASSERT(Raw != NULL);
            int Line;
            for (Line = 0; Line < Layout.NumLines; Line++)
                DtSdiFrame_ConvertLine(&Layout, Bits[b], Lines + (size_t)Line * Stride,
                                       Line, Raw);
            for (Line = 0; Line < Layout.NumLines && Differ < 0; Line++)
            {
                const size_t Bit = (size_t)Line * LineBits;

                DT_ASSERT(DtSdiFrame_CodeLine(&Layout, Bits[b], Raw + Bit / 8,
                                              (int)(Bit % 8), Coded));
                if (memcmp(Coded, Lines + (size_t)Line * Stride, Stride) != 0)
                    Differ = Line;
            }
            free(Raw);
            if (Differ >= 0)
            {
                printf("    FAIL: standard %d, %d bits: line %d differs\n",
                       g_Standards[i], Bits[b], Differ);
                (*DtFailures)++;
                break;
            }
        }
        free(Coded);
        free(Lines);
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= 4K +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A 4K frame is built here from four links whose lines the tests make up: each link line
// has an EAV with its line number and each stream's CRC-18 over the active part of the
// line before, a SAV, and HANC and active symbols of its own. The coded lines are packed
// bit by bit, by the two-sample interleave the card has (0014, step A), and the raw
// line is checked word by word against that order, worked out again here and
// independently of the module's own permutation.

// The 4K standards tested, 12G and 6G, with the three HANC sizes.
static const int g_Standards4k[] = {DTAPI_VIDSTD_2160P50, DTAPI_VIDSTD_2160P60,
                                    DTAPI_VIDSTD_2160P30, DTAPI_VIDSTD_2160P24};

// Link lines at the edges of the picture: the first and last blanking and picture lines.
static const int g_Lines4k[] = {1, 2, 41, 42, 43, 1120, 1121, 1122, 1125};

#define LINES_4K ((int)(sizeof(g_Lines4k) / sizeof(g_Lines4k[0])))

// The links whose C words and then Y words take the places of a group of eight words.
static const int g_Order4k[4] = {3, 1, 2, 0};

// SMPTE 292's CRC-18 over one 10-bit word, least significant bit first, bit by bit.
static uint32_t TestCrc18(uint32_t Crc, uint32_t Word)
{
    for (int Bit = 0; Bit < 10; Bit++)
    {
        uint32_t Feedback = (Crc ^ (Word >> Bit)) & 1;

        Crc >>= 1;
        if (Feedback != 0)
            Crc ^= 0x23000;
    }
    return Crc;
}

// Nine bits with bit 9 the inverse of bit 8.
static uint32_t Parity9(uint32_t Nine)
{
    Nine &= 0x1FF;
    return Nine | ((Nine >> 8) ^ 1) << 9;
}

// The word of a link line's HANC section (Part 0) or active part (Part 1) at Index.
static uint32_t LinkSymbol(int Link, int Line, int Part, size_t Index)
{
    return Symbol(Line * 4 + Link, Part + 2, Index);
}

// The words of link Link's line Line, from 1: its HANC section, then its active part,
// C and Y interleaved, C first.
static void LinkLine(const DtSdiFrameLayout* Layout, int Link, int Line, uint16_t* Words)
{
    const size_t Hanc = (size_t)Layout->SectionNumSymsHanc;
    const size_t Active = (size_t)Layout->SectionNumSymsVideo / 2;
    const uint32_t V = Line < Layout->PictureStart || Line > Layout->PictureEnd ? 1 : 0;
    const uint32_t Eav =
        0x200 | V << 7 | 1u << 6 | (V ^ 1) << 5 | 1u << 4 | V << 3 | (V ^ 1) << 2;
    const uint32_t Sav = 0x200 | V << 7 | V << 5 | V << 3 | V << 2;
    const int Previous = Line == 1 ? Layout->NumLines : Line - 1;
    size_t i;

    for (i = 0; i < Hanc; i++)
        Words[i] = (uint16_t)LinkSymbol(Link, Line, 0, i);
    for (i = 0; i < Active; i++)
        Words[Hanc + i] = (uint16_t)LinkSymbol(Link, Line, 1, i);

    for (size_t Channel = 0; Channel < 2; Channel++)
    {
        uint32_t Start[6] = {0x3FF,
                             0,
                             0,
                             Eav,
                             Parity9((uint32_t)(Line & 0x7F) << 2),
                             Parity9((uint32_t)(Line >> 7 & 0xF) << 2)};
        uint32_t Crc = 0;

        for (i = Channel; i < Active; i += 2)
            Crc = TestCrc18(Crc, LinkSymbol(Link, Previous, 1, i));
        for (i = 0; i < 6; i++)
        {
            Crc = TestCrc18(Crc, Start[i]);
            Words[2 * i + Channel] = (uint16_t)Start[i];
        }
        Words[12 + Channel] = (uint16_t)Parity9(Crc);
        Words[14 + Channel] = (uint16_t)Parity9(Crc >> 9);
        Words[Hanc - 8 + Channel] = 0x3FF;
        Words[Hanc - 6 + Channel] = 0;
        Words[Hanc - 4 + Channel] = 0;
        Words[Hanc - 2 + Channel] = (uint16_t)Sav;
    }
}

// Packs Count words into a section of Bytes bytes, bit by bit, the padding zero.
static void PackBitByBit(const uint16_t* Words, size_t Count, uint8_t* Section,
                         size_t Bytes)
{
    memset(Section, 0, Bytes);
    for (size_t i = 0; i < Count; i++)
    {
        for (size_t b = 0; b < 10; b++)
        {
            if (Words[i] >> b & 1)
                SetBit(Section, i * 10 + b);
        }
    }
}

// The test's own 4K frame lines: the four link lines of line Line, and the two coded
// lines they make, each Layout->Stride bytes.
typedef struct Line4k
{
    uint16_t* Links[4];
    uint8_t* Coded[2];
    uint16_t* Video;
} Line4k;

static bool Line4kAlloc(const DtSdiFrameLayout* Layout, Line4k* L)
{
    size_t Words =
        (size_t)Layout->SectionNumSymsHanc + (size_t)Layout->SectionNumSymsVideo / 2;
    bool Ok = true;

    for (int i = 0; i < 4; i++)
        Ok = (L->Links[i] = (uint16_t*)malloc(Words * 2)) != NULL && Ok;
    for (int i = 0; i < 2; i++)
        Ok = (L->Coded[i] = (uint8_t*)malloc((size_t)Layout->Stride)) != NULL && Ok;
    Ok =
        (L->Video = (uint16_t*)malloc((size_t)Layout->SectionNumSymsVideo * 2)) != NULL &&
        Ok;
    return Ok;
}

static void Line4kFree(Line4k* L)
{
    for (int i = 0; i < 4; i++)
        free(L->Links[i]);
    for (int i = 0; i < 2; i++)
        free(L->Coded[i]);
    free(L->Video);
}

// Makes the link lines of line Line and codes them: a picture line's links 1 and 2 take
// its pixel pairs in turn, a blanking line's their own halves.
static void Line4kMake(const DtSdiFrameLayout* Layout, int Line, Line4k* L)
{
    const size_t Hanc = (size_t)Layout->SectionNumSymsHanc;
    const size_t Active = (size_t)Layout->SectionNumSymsVideo / 2;
    const bool Blanking = Line < Layout->PictureStart || Line > Layout->PictureEnd;

    for (int i = 0; i < 4; i++)
        LinkLine(Layout, i, Line, L->Links[i]);
    for (int c = 0; c < 2; c++)
    {
        const uint16_t* First = L->Links[2 * c];
        const uint16_t* Second = L->Links[2 * c + 1];
        uint8_t* Coded = L->Coded[c];

        for (size_t a = 0; a < Active; a++)
        {
            size_t To1 = Blanking ? a : a / 4 * 8 + a % 4;
            size_t To2 = Blanking ? Active + a : a / 4 * 8 + 4 + a % 4;

            L->Video[To1] = First[Hanc + a];
            L->Video[To2] = Second[Hanc + a];
        }
        PackBitByBit(First, Hanc, Coded, (size_t)Layout->SectionBytesHanc);
        PackBitByBit(Second, Hanc, Coded + Layout->SectionBytesHanc,
                     (size_t)Layout->SectionBytesHanc);
        PackBitByBit(L->Video, (size_t)Layout->SectionNumSymsVideo,
                     Coded + 2 * Layout->SectionBytesHanc,
                     (size_t)Layout->SectionBytesVideo);
    }
}

// Symbol Index of a raw line whose symbols take BitsPerSymbol, read bit by bit.
static uint32_t RawSymbol(const uint8_t* Line, int BitsPerSymbol, size_t Index)
{
    uint32_t Value = 0;

    for (size_t b = 0; b < (size_t)BitsPerSymbol; b++)
    {
        size_t Bit = Index * (size_t)BitsPerSymbol + b;
        Value |= (uint32_t)(Line[Bit / 8] >> (Bit % 8) & 1) << b;
    }
    return BitsPerSymbol == 8 ? Value << 2 : Value & 0x3FF;
}

// Checks that the raw line at Raw holds L's links in the two-sample interleave order.
// Returns false, having reported the first difference, when it does not.
static bool Raw4kMatches(const DtSdiFrameLayout* Layout, int BitsPerSymbol,
                         const uint8_t* Raw, const Line4k* L, int* DtFailures)
{
    size_t Words =
        (size_t)Layout->SectionNumSymsHanc + (size_t)Layout->SectionNumSymsVideo / 2;
    uint32_t Mask = BitsPerSymbol == 8 ? 0x3FC : 0x3FF;

    for (size_t w = 0; w < Words / 2; w++)
    {
        for (size_t p = 0; p < 8; p++)
        {
            const uint16_t* Link = L->Links[g_Order4k[p % 4]];
            uint32_t Want = Link[2 * w + p / 4] & Mask;
            uint32_t Have = RawSymbol(Raw, BitsPerSymbol, 8 * w + p);

            if (Have != Want)
            {
                printf(
                    "    FAIL: standard %d, %d bits: word %zu of the raw line is %03X, "
                    "not %03X\n",
                    Layout->VidStd, BitsPerSymbol, 8 * w + p, Have, Want);
                (*DtFailures)++;
                return false;
            }
        }
    }
    return true;
}

// The layouts of 12G 2160p50 and 6G 2160p30, as the card's ring showed them.
DT_TEST(Layout4k)
{
    DtSdiFrameLayout L;

    DT_ASSERT(DtSdiFrame_LayoutInit(&L, DTAPI_VIDSTD_2160P50, 128));
    DT_ASSERT(L.Is4k);
    DT_ASSERT_EQ(L.NumLines, 1125);
    DT_ASSERT_EQ(L.NumCodedLines, 2250);
    DT_ASSERT_EQ(L.NumHancSections, 2);
    DT_ASSERT_EQ(L.SectionNumSymsHanc, 1440);
    DT_ASSERT_EQ(L.SectionNumSymsVideo, 7680);
    DT_ASSERT_EQ(L.LineNumSymsHanc, 5760);
    DT_ASSERT_EQ(L.LineNumSymsVideo, 15360);
    DT_ASSERT_EQ(L.SectionBytesHanc, 1808);
    DT_ASSERT_EQ(L.SectionBytesVideo, 9600);
    DT_ASSERT_EQ(L.Stride, 13216);
    DT_ASSERT_EQ(L.TxLineHeaderNumBytes, 16);
    DT_ASSERT_EQ(L.TxStride, 13232);
    DT_ASSERT_EQ(L.PictureStart, 42);
    DT_ASSERT_EQ(L.PictureEnd, 1121);
    DT_ASSERT_EQ(L.Format, DT_SDIFRAME_FORMAT_UNCOMPRESSED_4K);
    DT_ASSERT_EQ(L.SdiRate, DT_SDIRATE_12G);
    DT_ASSERT_EQ((long)DtSdiFrame_CodedSize(&L), 29736016L);
    DT_ASSERT_EQ((long)DtSdiFrame_TxCodedSize(&L), 32L + 2250L * 13232L);
    DT_ASSERT_EQ((long)DtSdiFrame_RawSize(&L, 10), 29700000L);
    DT_ASSERT_EQ((long)DtSdiFrame_RawSize(&L, 16), 47520000L);
    DT_ASSERT_EQ((long)DtSdiFrame_RawSize(&L, 8), 23760000L);
    DT_ASSERT_EQ((long)DtSdiFrame_RawLineNumBits(&L, 10), 211200L);

    DT_ASSERT(DtSdiFrame_LayoutInit(&L, DTAPI_VIDSTD_2160P30, 128));
    DT_ASSERT_EQ(L.SectionNumSymsHanc, 560);
    DT_ASSERT_EQ(L.SectionBytesHanc, 704);
    DT_ASSERT_EQ(L.SdiRate, DT_SDIRATE_6G);
    DT_ASSERT_EQ((long)DtSdiFrame_CodedSize(&L), 24768016L);
    DT_ASSERT_EQ((long)DtSdiFrame_RawSize(&L, 10), 24750000L);

    DT_ASSERT(DtSdiFrame_LayoutInit(&L, DTAPI_VIDSTD_2160P24, 128));
    DT_ASSERT_EQ(L.LineNumSymsHanc, 6640);

    // A standard that is not 4K keeps one section of each kind and no line header.
    DT_ASSERT(DtSdiFrame_LayoutInit(&L, DTAPI_VIDSTD_1080P50, 128));
    DT_ASSERT(!L.Is4k);
    DT_ASSERT_EQ(L.NumCodedLines, L.NumLines);
    DT_ASSERT_EQ(L.NumHancSections, 1);
    DT_ASSERT_EQ(L.TxStride, L.Stride);
    DT_ASSERT_EQ((long)DtSdiFrame_NumScratchSymbols(&L), 0L);
}

// Each raw 4K line holds the four links' words in the two-sample interleave order, in
// every symbol size, on blanking and picture lines alike.
DT_TEST(Converts4k)
{
    static const int Bits[] = {10, 16, 8};

    for (size_t s = 0; s < sizeof(g_Standards4k) / sizeof(g_Standards4k[0]); s++)
    {
        DtSdiFrameLayout Layout;
        Line4k L;

        DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, g_Standards4k[s], 128));
        size_t LineBytes = DtSdiFrame_RawLineNumBits(&Layout, 16) / 8;
        uint16_t* Scratch = (uint16_t*)malloc(DtSdiFrame_NumScratchSymbols(&Layout) * 2);
        uint8_t* Raw = (uint8_t*)malloc(LineBytes);
        DT_ASSERT(Line4kAlloc(&Layout, &L) && Scratch != NULL && Raw != NULL);

        for (int i = 0; i < LINES_4K; i++)
        {
            Line4kMake(&Layout, g_Lines4k[i], &L);
            for (size_t b = 0; b < sizeof(Bits) / sizeof(Bits[0]); b++)
            {
                memset(Raw, 0xA5, LineBytes);
                DtSdiFrame_ConvertLine4k(&Layout, Bits[b], L.Coded[0], L.Coded[1],
                                         g_Lines4k[i] - 1, Raw, Scratch);
                if (!Raw4kMatches(&Layout, Bits[b], Raw, &L, DtFailures))
                    break;
            }
        }
        Line4kFree(&L);
        free(Scratch);
        free(Raw);
    }
}

// Coding a raw 4K line gives back the coded lines it was converted from, byte for byte,
// in 10 and 16 bits; in 8 bits, the raw line again once converted.
DT_TEST(Codes4k)
{
    static const int Bits[] = {10, 16, 8};

    for (size_t s = 0; s < sizeof(g_Standards4k) / sizeof(g_Standards4k[0]); s++)
    {
        DtSdiFrameLayout Layout;
        Line4k L;

        DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, g_Standards4k[s], 128));
        size_t Stride = (size_t)Layout.Stride;
        size_t LineBytes = DtSdiFrame_RawLineNumBits(&Layout, 16) / 8;
        uint16_t* Scratch = (uint16_t*)malloc(DtSdiFrame_NumScratchSymbols(&Layout) * 2);
        uint8_t* Raw = (uint8_t*)malloc(LineBytes);
        uint8_t* Again = (uint8_t*)malloc(LineBytes);
        uint8_t* A = (uint8_t*)malloc(Stride);
        uint8_t* B = (uint8_t*)malloc(Stride);
        DT_ASSERT(Line4kAlloc(&Layout, &L) && Scratch != NULL && Raw != NULL &&
                  Again != NULL && A != NULL && B != NULL);

        for (int i = 0; i < LINES_4K; i++)
        {
            int Index = g_Lines4k[i] - 1;
            Line4kMake(&Layout, g_Lines4k[i], &L);
            for (size_t b = 0; b < sizeof(Bits) / sizeof(Bits[0]); b++)
            {
                DtSdiFrame_ConvertLine4k(&Layout, Bits[b], L.Coded[0], L.Coded[1], Index,
                                         Raw, Scratch);
                memset(A, 0xA5, Stride);
                memset(B, 0xA5, Stride);
                DT_ASSERT(
                    DtSdiFrame_CodeLine4k(&Layout, Bits[b], Raw, Index, A, B, Scratch));
                if (Bits[b] != 8)
                {
                    DT_ASSERT(memcmp(A, L.Coded[0], Stride) == 0);
                    DT_ASSERT(memcmp(B, L.Coded[1], Stride) == 0);
                    continue;
                }
                DtSdiFrame_ConvertLine4k(&Layout, 8, A, B, Index, Again, Scratch);
                DT_ASSERT(memcmp(Again, Raw, DtSdiFrame_RawLineNumBits(&Layout, 8) / 8) ==
                          0);
            }
        }
        DT_ASSERT(!DtSdiFrame_CodeLine4k(&Layout, 12, Raw, 0, A, B, Scratch));
        Line4kFree(&L);
        free(Scratch);
        free(Raw);
        free(Again);
        free(A);
        free(B);
    }

    DtSdiFrameLayout Hd;
    uint8_t Line[16];
    uint16_t Scratch[4];
    DT_ASSERT(DtSdiFrame_LayoutInit(&Hd, DTAPI_VIDSTD_1080I50, 128));
    DT_ASSERT(!DtSdiFrame_CodeLine4k(&Hd, 10, Line, 0, Line, Line, Scratch));
}

// A 4K frame starts at coded line 1, line 1 of links 1 and 2, and ends at coded line
// 2250, line 1125 of links 3 and 4.
DT_TEST(ChecksLines4k)
{
    DtSdiFrameLayout Layout;
    Line4k First;
    Line4k Last;

    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_2160P50, 128));
    DT_ASSERT(Line4kAlloc(&Layout, &First) && Line4kAlloc(&Layout, &Last));
    Line4kMake(&Layout, 1, &First);
    Line4kMake(&Layout, 1125, &Last);
    DT_ASSERT_EQ(DtSdiFrame_CheckLines(&Layout, First.Coded[0], Last.Coded[1]), DTAPI_OK);
    DT_ASSERT_EQ(DtSdiFrame_CheckLines(&Layout, Last.Coded[1], First.Coded[0]),
                 DTAPI_E_OUT_OF_SYNC);
    Line4kFree(&First);
    Line4kFree(&Last);
}

// Sent, a 4K frame names 2250 coded lines, one HANC section and the video section, and
// each coded line has its header: 1 on a blanking line, 0 on a picture line.
DT_TEST(TxLines4k)
{
    DtSdiFrameLayout Layout;
    DtSdiFrameTxHeader Header;
    uint8_t Bytes[16];

    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_2160P50, 128));
    DtSdiFrame_TxHeaderInit(&Layout, 7, &Header);
    DT_ASSERT_EQ(Header.Format, DT_SDIFRAME_FORMAT_UNCOMPRESSED_4K);
    DT_ASSERT_EQ(Header.SdiRate, DT_SDIRATE_12G);
    DT_ASSERT_EQ(Header.NumCodedLines, 2250);
    DT_ASSERT_EQ(Header.NumSymsHanc, 1440);
    DT_ASSERT_EQ(Header.NumWordsHanc, 113);
    DT_ASSERT_EQ(Header.NumSymsVideo, 7680);
    DT_ASSERT_EQ(Header.NumWordsVideo, 600);

    static const struct
    {
        int Coded;
        uint8_t Header;
    } Cases[] = {{0, 1},  {1, 1},    {80, 1},   {81, 1},   {82, 0},
                 {83, 0}, {2240, 0}, {2241, 0}, {2242, 1}, {2249, 1}};
    for (size_t i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        memset(Bytes, 0xA5, sizeof(Bytes));
        DtSdiFrame_EncodeTxLineHeader(&Layout, Cases[i].Coded, Bytes);
        DT_ASSERT_EQ(Bytes[0], Cases[i].Header);
        for (size_t b = 1; b < sizeof(Bytes); b++)
            DT_ASSERT_EQ(Bytes[b], 0);
    }

    // No line header for a standard that is not 4K.
    DtSdiFrameLayout Hd;
    DT_ASSERT(DtSdiFrame_LayoutInit(&Hd, DTAPI_VIDSTD_1080I50, 128));
    memset(Bytes, 0xA5, sizeof(Bytes));
    DtSdiFrame_EncodeTxLineHeader(&Hd, 0, Bytes);
    DT_ASSERT_EQ(Bytes[0], 0xA5);
}

// A black 4K frame: each coded line's header, the same HANC section twice, black video;
// raw, every stream starts with its EAV and line number, and each stream's CRC is SMPTE
// 292's over the black active part of the line before and its own EAV and line number.
DT_TEST(BlackFrame4k)
{
    DtSdiFrameLayout Layout;

    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_2160P50, 128));
    size_t TxStride = (size_t)Layout.TxStride;
    size_t LineBytes = DtSdiFrame_RawLineNumBits(&Layout, 10) / 8;
    uint8_t* Lines = (uint8_t*)malloc((size_t)Layout.NumCodedLines * TxStride);
    uint8_t* Raw = (uint8_t*)malloc(LineBytes);
    uint16_t* Scratch = (uint16_t*)malloc(DtSdiFrame_NumScratchSymbols(&Layout) * 2);
    DT_ASSERT(Lines != NULL && Raw != NULL && Scratch != NULL);
    DT_ASSERT(DtSdiFrame_BlackLines(&Layout, Lines));

    uint32_t ActiveCrc[2] = {0, 0};
    for (size_t i = 0; i < (size_t)Layout.SectionNumSymsVideo / 2; i++)
        ActiveCrc[i % 2] = TestCrc18(ActiveCrc[i % 2], i % 2 == 0 ? 0x200 : 0x040);

    for (int i = 0; i < LINES_4K; i++)
    {
        int Line = g_Lines4k[i];
        const uint8_t* A = Lines + (size_t)(2 * Line - 2) * TxStride;
        const uint8_t* B = A + TxStride;
        size_t Header = (size_t)Layout.TxLineHeaderNumBytes;
        size_t HancNumBytes = (size_t)Layout.SectionBytesHanc;
        bool Blanking = Line < 42 || Line > 1121;

        DT_ASSERT_EQ(A[0], Blanking ? 1 : 0);
        DT_ASSERT_EQ(B[0], Blanking ? 1 : 0);
        DT_ASSERT(memcmp(A + Header, A + Header + HancNumBytes, HancNumBytes) == 0);
        DT_ASSERT(memcmp(A + Header, B + Header, HancNumBytes) == 0);

        DtSdiFrame_ConvertLine4k(&Layout, 10, A + Header, B + Header, Line - 1, Raw,
                                 Scratch);
        for (size_t s = 0; s < 8; s++)
        {
            uint32_t Channel = s < 4 ? 0 : 1;
            uint32_t Words[8];
            uint32_t Crc = ActiveCrc[Channel];
            for (size_t w = 0; w < 8; w++)
                Words[w] = RawSymbol(Raw, 10, 8 * w + s);
            DT_ASSERT_EQ(Words[0], 0x3FF);
            DT_ASSERT_EQ(Words[1], 0);
            DT_ASSERT_EQ(Words[2], 0);
            DT_ASSERT_EQ(Words[4], Parity9((uint32_t)(Line & 0x7F) << 2));
            for (size_t w = 0; w < 6; w++)
                Crc = TestCrc18(Crc, Words[w]);
            DT_ASSERT_EQ(Words[6], Parity9(Crc));
            DT_ASSERT_EQ(Words[7], Parity9(Crc >> 9));
            DT_ASSERT_EQ(RawSymbol(Raw, 10, (size_t)Layout.LineNumSymsHanc + s),
                         Channel == 0 ? 0x200u : 0x040u);
        }
    }
    free(Lines);
    free(Raw);
    free(Scratch);
}

// The padding of the six sections of two coded lines, which a conversion may write into
// and DtSdiFrame_CodeLine4k clears afterwards.
static void ClearPadding4k(uint8_t* Coded, const DtSdiFrameLayout* Layout)
{
    const size_t Hanc = (size_t)Layout->SectionBytesHanc;
    const size_t HancSyms = (size_t)Layout->SectionNumSymsHanc * 10 / 8;
    const size_t VideoSyms = (size_t)Layout->SectionNumSymsVideo * 10 / 8;

    for (int Line = 0; Line < 2; Line++)
    {
        uint8_t* At = Coded + (size_t)Line * (size_t)Layout->Stride;

        memset(At + HancSyms, 0, Hanc - HancSyms);
        memset(At + Hanc + HancSyms, 0, Hanc - HancSyms);
        memset(At + 2 * Hanc + VideoSyms, 0,
               (size_t)Layout->SectionBytesVideo - VideoSyms);
    }
}

// Every conversion the processor runs gives what the portable one gives, for every 4K
// standard, symbol size and kind of line, in both directions. The symbols are random,
// since the conversion moves them and reads none.
DT_TEST(Conv4kSetsAgree)
{
    static const int Bits[] = {10, 16};
    const DtSdi4kConv* Sets[] = {DtSdi4kConv_Ssse3()};
    uint32_t State = 2160;

    for (size_t k = 0; k < sizeof(Sets) / sizeof(Sets[0]); k++)
    {
        if (Sets[k] == NULL || Sets[k] == DtSdi4kConv_C())
            continue;
        for (size_t s = 0; s < sizeof(g_Standards4k) / sizeof(g_Standards4k[0]); s++)
        {
            DtSdiFrameLayout Layout;

            DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, g_Standards4k[s], 128));
            const size_t Coded = (size_t)Layout.Stride;
            const size_t RawBytes = DtSdiFrame_RawLineNumBits(&Layout, 16) / 8;
            uint8_t* CodedA = (uint8_t*)malloc(2 * Coded);
            uint8_t* CodedB = (uint8_t*)malloc(2 * Coded);
            uint8_t* RawC = (uint8_t*)malloc(RawBytes);
            uint8_t* RawSet = (uint8_t*)malloc(RawBytes);
            uint8_t* BackC = (uint8_t*)malloc(2 * Coded);
            uint8_t* BackSet = (uint8_t*)malloc(2 * Coded);
            uint16_t* Scratch = (uint16_t*)malloc(DtSdiFrame_NumScratchSymbols(&Layout) *
                                                  sizeof(uint16_t));

            DT_ASSERT(CodedA != NULL && CodedB != NULL && RawC != NULL && RawSet != NULL);
            DT_ASSERT(BackC != NULL && BackSet != NULL && Scratch != NULL);
            for (size_t i = 0; i < 2 * Coded; i++)
            {
                State = State * 1664525u + 1013904223u;
                CodedA[i] = (uint8_t)(State >> 24);
                State = State * 1664525u + 1013904223u;
                CodedB[i] = (uint8_t)(State >> 24);
            }

            for (int i = 0; i < LINES_4K; i++)
            {
                const int Line = g_Lines4k[i] - 1;

                for (size_t b = 0; b < sizeof(Bits) / sizeof(Bits[0]); b++)
                {
                    // The same padding in both, which neither conversion touches.
                    memset(RawC, 0x5A, RawBytes);
                    memset(RawSet, 0x5A, RawBytes);
                    DtSdi4kConv_C()->ConvertLine(&Layout, Bits[b], CodedA, CodedB, Line,
                                                 RawC, Scratch);
                    Sets[k]->ConvertLine(&Layout, Bits[b], CodedA, CodedB, Line, RawSet,
                                         Scratch);
                    DT_ASSERT_MEM(RawSet, RawC,
                                  DtSdiFrame_RawLineNumBits(&Layout, Bits[b]) / 8);

                    memset(BackC, 0x11, 2 * Coded);
                    memset(BackSet, 0x11, 2 * Coded);
                    DtSdi4kConv_C()->CodeLine(&Layout, Bits[b], RawC, Line, BackC,
                                              BackC + Coded, Scratch);
                    Sets[k]->CodeLine(&Layout, Bits[b], RawC, Line, BackSet,
                                      BackSet + Coded, Scratch);
                    ClearPadding4k(BackC, &Layout);
                    ClearPadding4k(BackSet, &Layout);
                    DT_ASSERT_MEM(BackSet, BackC, 2 * Coded);
                }
            }
            free(CodedA);
            free(CodedB);
            free(RawC);
            free(RawSet);
            free(BackC);
            free(BackSet);
            free(Scratch);
        }
    }
}

DT_TEST_MAIN("SdiFrame", DT_RUN(Layout1080I50), DT_RUN(LayoutOtherAlignments),
             DT_RUN(LayoutRefuses), DT_RUN(LayoutEveryStandard), DT_RUN(HeaderNumBytes),
             DT_RUN(HeaderFieldWidths), DT_RUN(HeaderCheck), DT_RUN(RawSizes),
             DT_RUN(ConvertsEveryStandard), DT_RUN(ConvertsOddSections),
             DT_RUN(ConvertsNothingForOtherSizes), DT_RUN(ChecksFirstAndLastLine),
             DT_RUN(LayoutTransmit), DT_RUN(TxHeaderNumBytes),
             DT_RUN(TxHeaderFieldWidths), DT_RUN(RawLineBits), DT_RUN(CodesEveryStandard),
             DT_RUN(CodesAnyPhase), DT_RUN(CodeLineRefuses), DT_RUN(CodeLine8Bits),
             DT_RUN(BlackFramesEveryStandard), DT_RUN(BlackFrameRoundTrip),
             DT_RUN(Layout4k), DT_RUN(Converts4k), DT_RUN(Codes4k), DT_RUN(ChecksLines4k),
             DT_RUN(TxLines4k), DT_RUN(BlackFrame4k), DT_RUN(Conv4kSetsAgree))
