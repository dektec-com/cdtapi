// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestSdiParser.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The parser's image, views of raw frames, and the sizes of images
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Checks that the parser takes the right image out of raw frames of every standard. The
// test builds its frames from the standards' own numbers, not from the library's tables.
// These numbers are:
// - the active lines of each field, from SMPTE ST 125, BT.656, ST 274 and ST 296, in the
//   line numbers of a raw frame;
// - the line lengths of SdiFormats.inc;
// - for 2160p, how SMPTE ST 2082-10 divides the image over four links.
//
// Each symbol of the active video gets a value that differs for each line and place. The
// blanking gets a value that no image symbol has. So a line read from the wrong place
// shows. Each pixel format is checked against a packing that the test writes itself,
// from the format's definition.
//
// When CDTAPI_TEST_SDI_DIR is set, one more case reads frames made by FFmpeg's sdi muxer.
// <Name>.raw holds 10-bit raw frames of standard <Name>, as named in SdiFormats.inc.
// <Name>.yuv holds the yuv422p10le images they were made from. The parser must give back
// each image exactly.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// CDTAPI includes
#include "DtTest.h"     // Test framework.
#include "SdiFormat.h"  // Every standard's line timing.
#include "cdtapi_sdi.h" // Public API under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Standards +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Holds the first and last active line of each field. Lines count from 1, as in a raw
// frame.
typedef struct ActiveLines
{
    int NumFields;
    int First[2];
    int Last[2];
} ActiveLines;

// Fills *Lines with the active lines of Format. Returns false for 2160p, because there
// the links hold the image lines.
static bool GetActiveLines(const SdiFormat* Format, ActiveLines* Lines)
{
    memset(Lines, 0, sizeof(*Lines));
    if (Format->Lines == 525)
    {
        // SMPTE ST 125 has active lines 20 to 263 and 283 to 525. A raw frame counts
        // them three lower.
        *Lines = (ActiveLines){2, {17, 280}, {260, 522}};
        return true;
    }
    if (Format->Lines == 625)
    {
        *Lines = (ActiveLines){2, {23, 336}, {310, 623}}; // BT.656
        return true;
    }
    if (Format->Lines == 750)
    {
        *Lines = (ActiveLines){1, {26, 0}, {745, 0}}; // ST 296
        return true;
    }
    if (strncmp(Format->Name, "2160", 4) == 0)
        return false;
    if (Format->Scan == SDI_SCAN_P)
        *Lines = (ActiveLines){1, {42, 0}, {1121, 0}}; // ST 274, progressive
    else
        *Lines = (ActiveLines){2, {21, 584}, {560, 1123}}; // ST 274, two fields
    return true;
}

// Returns the raw line, counted from 1, that holds line y of the woven image.
static int SdiLineOf(const ActiveLines* Lines, int y)
{
    if (Lines->NumFields == 1)
        return Lines->First[0] + y;
    return Lines->First[y & 1] + y / 2;
}

static bool Is4k(const SdiFormat* Format)
{
    return strncmp(Format->Name, "2160", 4) == 0;
}

// Returns whether a channel carries Format. A channel carries every standard except
// those of 3G level B.
static bool IsCarried(const SdiFormat* Format)
{
    return !SdiFormat_IsLevelB(Format);
}

static const SdiFormat* FindFormat(const char* Name)
{
    for (int i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        if (strcmp(g_SdiFormats[i].Name, Name) == 0)
            return &g_SdiFormats[i];
    }
    return NULL;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Returns the value of symbol s of image line y. Neighbouring lines and symbols get
// different values. The value is never 0x3FF, which is what the blanking holds.
static uint16_t ImageSymbol(int y, int s)
{
    return (uint16_t)((y * 31 + s * 7 + (s & 1) * 300) % 1023);
}

#define BLANKING_SYMBOL 0x3FF

// Writes Value as symbol Index of a raw frame with Bits bits a symbol.
static void PutSymbol(uint8_t* Frame, int Bits, size_t Index, uint16_t Value)
{
    if (Bits == 16)
    {
        Frame[2 * Index] = (uint8_t)Value;
        Frame[2 * Index + 1] = (uint8_t)(Value >> 8);
        return;
    }
    // A 10-bit symbol starts at an even bit, so it lies within two bytes.
    const size_t FirstBit = 10 * Index;
    uint8_t* Bytes = Frame + FirstBit / 8;
    const unsigned Shift = (unsigned)(FirstBit % 8);
    uint32_t Word = (uint32_t)Bytes[0] | (uint32_t)Bytes[1] << 8;
    Word = (Word & ~(0x3FFu << Shift)) | (uint32_t)Value << Shift;
    Bytes[0] = (uint8_t)Word;
    Bytes[1] = (uint8_t)(Word >> 8);
}

// Returns the number of symbols in one whole raw line. For 2160p, that is four times the
// symbols of one link.
static size_t LineSymbols(const SdiFormat* Format)
{
    return (size_t)(2 * Format->Samples) * (Is4k(Format) ? 4 : 1);
}

// Returns the size in bytes of a raw frame. The frame is padded to 64 bits, as the SDI
// File Format specifies.
static size_t FrameBytes(const SdiFormat* Format, int Bits)
{
    const size_t FrameBits = (size_t)Format->Lines * LineSymbols(Format) * (size_t)Bits;
    return (FrameBits + 63) / 64 * 8;
}

// Builds a raw frame of Format. The image area holds ImageSymbol() values and the rest
// holds BLANKING_SYMBOL. Returns NULL when there is no memory.
static uint8_t* BuildFrame(const SdiFormat* Format, int Bits, size_t* Size)
{
    *Size = FrameBytes(Format, Bits);
    uint8_t* Frame = (uint8_t*)calloc(*Size, 1);
    if (Frame == NULL)
        return NULL;

    // Sets every symbol to BLANKING_SYMBOL, which has all ten bits set. With 10 bits a
    // symbol, that makes every byte 0xFF. The padding at the end does not matter.
    const size_t PerLine = LineSymbols(Format);
    if (Bits == 10)
        memset(Frame, 0xFF, *Size);
    else
        for (size_t i = 0; i < (size_t)Format->Lines * PerLine; i++)
            PutSymbol(Frame, Bits, i, BLANKING_SYMBOL);

    if (!Is4k(Format))
    {
        ActiveLines Lines;
        GetActiveLines(Format, &Lines);
        const size_t ActiveStart = (size_t)(2 * (Format->Samples - Format->Active));
        for (int y = 0; y < Format->ActiveLines; y++)
        {
            const size_t Line = (size_t)(SdiLineOf(&Lines, y) - 1);
            for (int s = 0; s < 2 * Format->Active; s++)
                PutSymbol(Frame, Bits, Line * PerLine + ActiveStart + (size_t)s,
                          ImageSymbol(y, s));
        }
        return Frame;
    }

    // Divides the image over four links as SMPTE ST 2082-10 does:
    // - the pixel pairs of image line 2k go to links 1 and 2 in turn;
    // - the pixel pairs of image line 2k + 1 go to links 3 and 4 in turn;
    // - link line 42 + k carries them.
    // Each link has a C and a Y stream, each starting with its horizontal blanking. The
    // raw line holds word n of the C streams of links 4, 2, 3 and 1, followed by word n
    // of their Y streams.
    static const int Place[4] = {3, 1, 2, 0};
    const int HancWords = Format->Samples - Format->Active;
    for (int k = 0; k < 1080; k++)
    {
        const size_t Line = (size_t)(42 + k - 1);
        for (int Link = 0; Link < 4; Link++)
        {
            const int y = 2 * k + Link / 2;
            for (int x = 0; x < 1920; x++)
            {
                const int Pair = 2 * (x / 2) + Link % 2;
                const int X = 2 * Pair + x % 2;
                const size_t Word = (size_t)(HancWords + x);
                const size_t Base = Line * PerLine + 8 * Word + (size_t)Place[Link];
                PutSymbol(Frame, Bits, Base, ImageSymbol(y, 2 * X));
                PutSymbol(Frame, Bits, Base + 4, ImageSymbol(y, 2 * X + 1));
            }
        }
    }
    return Frame;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Pixel formats +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

static const DtSdiPixelFormat g_Formats[] = {
    DT_SDI_PIXFMT_UYVY_10B, DT_SDI_PIXFMT_UYVY_8B,     DT_SDI_PIXFMT_V210,
    DT_SDI_PIXFMT_Y210,     DT_SDI_PIXFMT_YUV422P_10B, DT_SDI_PIXFMT_YUV422P_8B};
#define NUM_FORMATS ((int)(sizeof(g_Formats) / sizeof(g_Formats[0])))

static bool IsPlanar(DtSdiPixelFormat Format)
{
    return Format == DT_SDI_PIXFMT_YUV422P_10B || Format == DT_SDI_PIXFMT_YUV422P_8B;
}

// Writes to Out the bytes that line y of plane Plane must hold, for an image in Format
// that is Width pixels wide. The packing follows the format's definition. Returns the
// number of bytes.
static int ExpectedLine(DtSdiPixelFormat Format, int Width, int y, int Plane,
                        uint8_t* Out)
{
    const int N = 2 * Width;
    int Bytes = 0;
    switch (Format)
    {
    case DT_SDI_PIXFMT_UYVY_10B:
        // Packs ten bits a symbol, the least significant bit first.
        Bytes = N * 10 / 8;
        memset(Out, 0, (size_t)Bytes);
        for (int s = 0; s < N; s++)
            for (int b = 0; b < 10; b++)
                if ((ImageSymbol(y, s) >> b) & 1)
                    Out[(10 * s + b) / 8] |= (uint8_t)(1u << ((10 * s + b) % 8));
        return Bytes;
    case DT_SDI_PIXFMT_UYVY_8B:
        for (int s = 0; s < N; s++)
            Out[s] = (uint8_t)(ImageSymbol(y, s) >> 2);
        return N;
    case DT_SDI_PIXFMT_V210:
    {
        // Packs three samples into each little-endian word, in the order Cb Y Cr Y. Six
        // pixels take 16 bytes. The line is padded to a multiple of 128 bytes.
        Bytes = ((Width + 5) / 6 * 16 + 127) / 128 * 128;
        memset(Out, 0, (size_t)Bytes);
        for (int s = 0; s < N; s++)
        {
            const uint32_t Bits = (uint32_t)ImageSymbol(y, s) << (10 * (s % 3));
            for (int b = 0; b < 4; b++)
                Out[4 * (s / 3) + b] |= (uint8_t)(Bits >> (8 * b));
        }
        return Bytes;
    }
    case DT_SDI_PIXFMT_Y210:
        // Writes Y0 Cb Y1 Cr. Each is a little-endian 16-bit word with the ten bits at
        // the top.
        for (int p = 0; p < Width / 2; p++)
        {
            const uint16_t Words[4] = {ImageSymbol(y, 4 * p + 1), ImageSymbol(y, 4 * p),
                                       ImageSymbol(y, 4 * p + 3),
                                       ImageSymbol(y, 4 * p + 2)};
            for (int w = 0; w < 4; w++)
            {
                Out[8 * p + 2 * w] = (uint8_t)(Words[w] << 6);
                Out[8 * p + 2 * w + 1] = (uint8_t)((Words[w] << 6) >> 8);
            }
        }
        return 4 * Width;
    case DT_SDI_PIXFMT_YUV422P_10B:
    case DT_SDI_PIXFMT_YUV422P_8B:
    {
        const bool Ten = Format == DT_SDI_PIXFMT_YUV422P_10B;
        const int Count = Plane == 0 ? Width : Width / 2;
        for (int i = 0; i < Count; i++)
        {
            const int s = Plane == 0 ? 2 * i + 1 : 4 * i + (Plane == 1 ? 0 : 2);
            const uint16_t V = ImageSymbol(y, s);
            if (Ten)
            {
                Out[2 * i] = (uint8_t)V;
                Out[2 * i + 1] = (uint8_t)(V >> 8);
            }
            else
                Out[i] = (uint8_t)(V >> 2);
        }
        return Ten ? 2 * Count : Count;
    }
    default:
        return 0;
    }
}

// Each plane of a TestImage has a stride of STRIDE_SLACK bytes more than the smallest
// stride. These extra bytes hold SLACK_BYTE, so a write past the end of a line shows.
#define STRIDE_SLACK 64
#define SLACK_BYTE 0xA5

typedef struct TestImage
{
    DtSdiImage Image;
    uint8_t* Memory[3];
    int Width;
    int Height;
} TestImage;

static void TestImage_Free(TestImage* T)
{
    for (int p = 0; p < 3; p++)
        free(T->Memory[p]);
    memset(T, 0, sizeof(*T));
}

static bool TestImage_Alloc(TestImage* T, int VidStd, DtSdiPixelFormat Format)
{
    memset(T, 0, sizeof(*T));
    int MinStrides[3];
    if (DtSdiImage_GetSize(VidStd, Format, &T->Width, &T->Height, MinStrides) != DTAPI_OK)
        return false;
    T->Image.Format = Format;
    T->Image.Fields = DT_SDI_FIELDS_WOVEN;
    for (int p = 0; p < 3 && MinStrides[p] != 0; p++)
    {
        const int Stride = MinStrides[p] + STRIDE_SLACK;
        T->Memory[p] = (uint8_t*)malloc((size_t)Stride * (size_t)T->Height);
        if (T->Memory[p] == NULL)
        {
            TestImage_Free(T);
            return false;
        }
        memset(T->Memory[p], SLACK_BYTE, (size_t)Stride * (size_t)T->Height);
        T->Image.Planes[p] = T->Memory[p];
        T->Image.Strides[p] = Stride;
    }
    return true;
}

// Compares each line of each plane of T with what ExpectedLine() writes. Also checks
// that the slack after each line still holds SLACK_BYTE. Returns NULL, or a message that
// says what differs.
static const char* CheckImage(const TestImage* T, char* Message, size_t Size)
{
    uint8_t Expected[16384];
    const int Planes = IsPlanar(T->Image.Format) ? 3 : 1;
    for (int p = 0; p < Planes; p++)
    {
        for (int y = 0; y < T->Height; y++)
        {
            const uint8_t* Line =
                T->Image.Planes[p] + (size_t)y * (size_t)T->Image.Strides[p];
            const int Bytes = ExpectedLine(T->Image.Format, T->Width, y, p, Expected);
            if (memcmp(Line, Expected, (size_t)Bytes) != 0)
            {
                snprintf(Message, Size, "format %d, plane %d, line %d differs",
                         (int)T->Image.Format, p, y);
                return Message;
            }
            for (int b = Bytes; b < T->Image.Strides[p]; b++)
            {
                if (Line[b] != SLACK_BYTE)
                {
                    snprintf(Message, Size, "format %d, plane %d, line %d: byte %d",
                             (int)T->Image.Format, p, y, b);
                    return Message;
                }
            }
        }
    }
    return NULL;
}

// Builds a frame of Format with Bits bits a symbol, parses it into an image in PixFmt and
// checks that image. Returns NULL, or a message that says what failed.
static const char* ParseAndCheck(const SdiFormat* Format, int Bits,
                                 DtSdiPixelFormat PixFmt, char* Message, size_t Size)
{
    size_t FrameSize = 0;
    uint8_t* Frame = BuildFrame(Format, Bits, &FrameSize);
    DtSdiView* View = DtSdiView_Alloc();
    DtSdiParser* Parser = DtSdiParser_Alloc();
    TestImage T;
    const char* Failure = NULL;

    if (Frame == NULL || View == NULL || Parser == NULL ||
        !TestImage_Alloc(&T, Format->VidStd, PixFmt))
    {
        Failure = "out of memory";
        memset(&T, 0, sizeof(T));
    }
    else if (DtSdiView_SetRawFrame(View, Frame, FrameSize, Format->VidStd, Bits) !=
             DTAPI_OK)
        Failure = "SetRawFrame refused the frame";
    else if (DtSdiParser_Parse(Parser, View, &T.Image, NULL, NULL) != DTAPI_OK)
        Failure = "Parse failed";
    else
        Failure = CheckImage(&T, Message, Size);

    TestImage_Free(&T);
    DtSdiParser_Free(Parser);
    DtSdiView_Free(View);
    free(Frame);
    return Failure;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Checks the size of every standard's raw frame and image, and that a view refuses the
// standards a channel does not carry.
DT_TEST(SizesEveryStandard)
{
    for (int i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        const SdiFormat* F = &g_SdiFormats[i];
        size_t Size = 0;
        int Width = 0;
        int Height = 0;
        int Strides[3];
        if (!IsCarried(F))
        {
            SDI_ASSERT_EQ(F, DtSdiView_RawFrameSize(F->VidStd, 10, &Size),
                          DTAPI_E_INVALID_VIDSTD);
            continue;
        }
        SDI_ASSERT_EQ(F, DtSdiView_RawFrameSize(F->VidStd, 10, &Size), DTAPI_OK);
        SDI_ASSERT_EQ(F, Size, FrameBytes(F, 10));
        SDI_ASSERT_EQ(F, DtSdiView_RawFrameSize(F->VidStd, 16, &Size), DTAPI_OK);
        SDI_ASSERT_EQ(F, Size, FrameBytes(F, 16));
        SDI_ASSERT_EQ(F,
                      DtSdiImage_GetSize(F->VidStd, DT_SDI_PIXFMT_YUV422P_10B, &Width,
                                         &Height, Strides),
                      DTAPI_OK);
        SDI_ASSERT_EQ(F, Width, Is4k(F) ? 3840 : F->Active);
        SDI_ASSERT_EQ(F, Height, Is4k(F) ? 2160 : F->ActiveLines);
        SDI_ASSERT_EQ(F, Strides[0], 2 * Width);
        SDI_ASSERT_EQ(F, Strides[1], Width);
        SDI_ASSERT_EQ(F, Strides[2], Width);
    }
}

// Checks the smallest strides of each format for 1080i50, and of v210 for 720p50 and
// 525i59.94. v210 rounds its lines up to a multiple of 128 bytes. Also checks that a
// size query for DT_SDI_PIXFMT_NONE fails with DTAPI_E_INVALID_FORMAT.
DT_TEST(LeastStrides)
{
    static const struct
    {
        int VidStd;
        DtSdiPixelFormat Format;
        int Strides[3];
    } Cases[] = {
        {DTAPI_VIDSTD_1080I50, DT_SDI_PIXFMT_UYVY_10B, {4800, 0, 0}},
        {DTAPI_VIDSTD_1080I50, DT_SDI_PIXFMT_UYVY_8B, {3840, 0, 0}},
        {DTAPI_VIDSTD_1080I50, DT_SDI_PIXFMT_V210, {5120, 0, 0}},
        {DTAPI_VIDSTD_1080I50, DT_SDI_PIXFMT_Y210, {7680, 0, 0}},
        {DTAPI_VIDSTD_1080I50, DT_SDI_PIXFMT_YUV422P_10B, {3840, 1920, 1920}},
        {DTAPI_VIDSTD_1080I50, DT_SDI_PIXFMT_YUV422P_8B, {1920, 960, 960}},
        {DTAPI_VIDSTD_720P50, DT_SDI_PIXFMT_V210, {3456, 0, 0}},
        {DTAPI_VIDSTD_525I59_94, DT_SDI_PIXFMT_V210, {1920, 0, 0}},
    };
    for (size_t i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        int Strides[3];
        DT_ASSERT_OK(
            DtSdiImage_GetSize(Cases[i].VidStd, Cases[i].Format, NULL, NULL, Strides));
        for (int p = 0; p < 3; p++)
            DT_ASSERT_EQ(Strides[p], Cases[i].Strides[p]);
    }
    DT_ASSERT_EQ(
        DtSdiImage_GetSize(DTAPI_VIDSTD_1080I50, DT_SDI_PIXFMT_NONE, NULL, NULL, NULL),
        DTAPI_E_INVALID_FORMAT);
}

// Checks the parsed image of every standard up to 3G, from a 10-bit frame into planar
// 10-bit.
DT_TEST(ImageEveryStandard)
{
    char Message[128];
    for (int i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        const SdiFormat* F = &g_SdiFormats[i];
        if (!IsCarried(F) || Is4k(F))
            continue;
        const char* Failure =
            ParseAndCheck(F, 10, DT_SDI_PIXFMT_YUV422P_10B, Message, sizeof(Message));
        if (Failure != NULL)
            DT_FAIL("%s: %s", F->Name, Failure);
    }
}

// Checks the parsed image in every pixel format, from 10-bit and 16-bit frames of
// 525i59.94, 720p24 and 1080i50. Not every 10-bit line of 720p starts on a byte.
DT_TEST(EveryPixelFormat)
{
    static const char* const Names[] = {"525I59_94", "720P24", "1080I50"};
    char Message[128];
    for (size_t n = 0; n < sizeof(Names) / sizeof(Names[0]); n++)
    {
        const SdiFormat* F = FindFormat(Names[n]);
        DT_ASSERT(F != NULL);
        for (int Bits = 10; Bits <= 16; Bits += 6)
        {
            for (int f = 0; f < NUM_FORMATS; f++)
            {
                const char* Failure =
                    ParseAndCheck(F, Bits, g_Formats[f], Message, sizeof(Message));
                if (Failure != NULL)
                    DT_FAIL("%s, %d bits: %s", F->Name, Bits, Failure);
            }
        }
    }
}

// Checks that the parser puts the 2160p image together from its four links. The test
// parses 2160p50 and 2160p23.98 frames of 10 and 16 bits into planar 10-bit, and a
// 10-bit 2160p50 frame into v210.
DT_TEST(Image4k)
{
    static const char* const Names[] = {"2160P50", "2160P23_98"};
    char Message[128];
    for (size_t n = 0; n < sizeof(Names) / sizeof(Names[0]); n++)
    {
        const SdiFormat* F = FindFormat(Names[n]);
        DT_ASSERT(F != NULL);
        for (int Bits = 10; Bits <= 16; Bits += 6)
        {
            const char* Failure = ParseAndCheck(F, Bits, DT_SDI_PIXFMT_YUV422P_10B,
                                                Message, sizeof(Message));
            if (Failure != NULL)
                DT_FAIL("%s, %d bits: %s", F->Name, Bits, Failure);
        }
    }
    const char* Failure = ParseAndCheck(FindFormat("2160P50"), 10, DT_SDI_PIXFMT_V210,
                                        Message, sizeof(Message));
    if (Failure != NULL)
        DT_FAIL("2160P50 as v210: %s", Failure);
}

// Checks that DtSdiView_GetActiveLine() points at each active line where it lies in the
// frame. Every symbol read through the pointer must match. In 10 bits, half the lines of
// 720p24 start part-way through a byte, and the test counts them. Asking for the line
// after the last fails with DTAPI_E_INVALID_LINE.
DT_TEST(ActiveLinesWhereTheyLie)
{
    static const char* const Names[] = {"525I59_94", "720P24", "720P50", "1080I50",
                                        "1080P50"};
    for (size_t n = 0; n < sizeof(Names) / sizeof(Names[0]); n++)
    {
        const SdiFormat* F = FindFormat(Names[n]);
        DT_ASSERT(F != NULL);
        for (int Bits = 10; Bits <= 16; Bits += 6)
        {
            size_t Size = 0;
            uint8_t* Frame = BuildFrame(F, Bits, &Size);
            DtSdiView* View = DtSdiView_Alloc();
            DT_ASSERT(Frame != NULL && View != NULL);
            DT_ASSERT_OK(DtSdiView_SetRawFrame(View, Frame, Size, F->VidStd, Bits));

            int OddStarts = 0;
            const char* Failure = NULL;
            for (int y = 0; y < F->ActiveLines && Failure == NULL; y++)
            {
                DtSdiSymbolPtr Line;
                if (DtSdiView_GetActiveLine(View, y, &Line) != DTAPI_OK)
                    Failure = "GetActiveLine failed";
                OddStarts += Line.Bit != 0;
                for (int s = 0; s < 2 * F->Active && Failure == NULL; s++)
                {
                    if (DtSdiSymbolPtr_Get(&Line, (size_t)s) != ImageSymbol(y, s))
                        Failure = "a symbol differs";
                }
            }
            DtSdiSymbolPtr Past;
            const DtapiResult PastLine =
                DtSdiView_GetActiveLine(View, F->ActiveLines, &Past);
            DtSdiView_Free(View);
            free(Frame);
            if (Failure != NULL)
                DT_FAIL("%s, %d bits: %s", F->Name, Bits, Failure);
            DT_ASSERT_EQ(PastLine, DTAPI_E_INVALID_LINE);
            const bool HalfBytes = Bits == 10 && F->Samples == 4125;
            DT_ASSERT_EQ(OddStarts, HalfBytes ? F->ActiveLines / 2 : 0);
        }
    }
}

// Checks what a view and the parser refuse, and that a view reports the standard and
// symbol size of its frame.
DT_TEST(Refusals)
{
    const SdiFormat* F = FindFormat("1080I50");
    DT_ASSERT(F != NULL);
    size_t Size = 0;
    uint8_t* Frame = BuildFrame(F, 10, &Size);
    DtSdiView* View = DtSdiView_Alloc();
    DtSdiParser* Parser = DtSdiParser_Alloc();
    DT_ASSERT(Frame != NULL && View != NULL && Parser != NULL);

    DtSdiSymbolPtr Line;
    DT_ASSERT_EQ(DtSdiView_GetActiveLine(View, 0, &Line), DTAPI_E_STATE);
    DT_ASSERT_EQ(DtSdiView_GetFormat(View, NULL, NULL), DTAPI_E_STATE);
    DT_ASSERT_EQ(DtSdiParser_Parse(Parser, View, NULL, NULL, NULL), DTAPI_E_STATE);
    DT_ASSERT_EQ(DtSdiView_SetRawFrame(View, Frame, Size - 8, F->VidStd, 10),
                 DTAPI_E_INVALID_SIZE);
    DT_ASSERT_EQ(DtSdiView_SetRawFrame(View, Frame, Size, F->VidStd, 8),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtSdiView_SetRawFrame(View, Frame, Size, DTAPI_VIDSTD_1080P50B, 10),
                 DTAPI_E_INVALID_VIDSTD);
    DT_ASSERT_EQ(DtSdiView_SetRawFrame(View, NULL, Size, F->VidStd, 10),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_OK(DtSdiView_SetRawFrame(View, Frame, Size, F->VidStd, 10));
    int VidStd = 0;
    int Bits = 0;
    DT_ASSERT_OK(DtSdiView_GetFormat(View, &VidStd, &Bits));
    DT_ASSERT_EQ(VidStd, F->VidStd);
    DT_ASSERT_EQ(Bits, 10);

    TestImage T;
    DT_ASSERT(TestImage_Alloc(&T, F->VidStd, DT_SDI_PIXFMT_UYVY_10B));
    T.Image.Fields = DT_SDI_FIELDS_NONE;
    const DtapiResult NoFields = DtSdiParser_Parse(Parser, View, &T.Image, NULL, NULL);
    T.Image.Fields = DT_SDI_FIELDS_SEPARATE;
    const DtapiResult Separate = DtSdiParser_Parse(Parser, View, &T.Image, NULL, NULL);
    T.Image.Fields = DT_SDI_FIELDS_WOVEN;
    T.Image.Strides[0] = 4799;
    const DtapiResult Narrow = DtSdiParser_Parse(Parser, View, &T.Image, NULL, NULL);
    T.Image.Strides[0] = 4800;
    T.Image.Format = DT_SDI_PIXFMT_NONE;
    const DtapiResult NoFormat = DtSdiParser_Parse(Parser, View, &T.Image, NULL, NULL);
    T.Image.Format = DT_SDI_PIXFMT_YUV422P_8B;
    const DtapiResult NoPlanes = DtSdiParser_Parse(Parser, View, &T.Image, NULL, NULL);
    TestImage_Free(&T);

    DtSdiParser_Free(Parser);
    DtSdiView_Free(View);
    free(Frame);
    DT_ASSERT_EQ(NoFields, DTAPI_E_INVALID_FORMAT);
    DT_ASSERT_EQ(Separate, DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(Narrow, DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(NoFormat, DTAPI_E_INVALID_FORMAT);
    DT_ASSERT_EQ(NoPlanes, DTAPI_E_INVALID_ARG);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Ancillary packets +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// SMPTE ST 291 puts each packet in one stream of a line. In SD, that is the single
// stream. In HD, it is the C or the Y stream. In 2160p, it is one of the C and Y streams
// of the four links. A stream starts with EAV, which in HD is followed by the line number
// and the CRC. Together these are four words in SD and eight in HD. The horizontal
// blanking comes next. On a blanking line, the active part follows.
//

// Describes where one stream lies in a raw line. First is the symbol of its first word,
// and Step is the distance between its words.
typedef struct TestStream
{
    int First;
    int Step;
} TestStream;

// Returns the C or the Y stream of link Link of Format. Links count from 1.
static TestStream StreamOf(const SdiFormat* Format, int Link, bool Chroma)
{
    static const int Place[4] = {3, 1, 2, 0};
    if (Is4k(Format))
        return (TestStream){Place[Link - 1] + (Chroma ? 0 : 4), 8};
    if (Format->Lines <= 625)
        return (TestStream){0, 1};
    return (TestStream){Chroma ? 0 : 1, 2};
}

// Returns the number of words in a stream's horizontal blanking, including the timing
// references.
static int StreamHancWords(const SdiFormat* Format)
{
    return (Format->Samples - Format->Active) * (Format->Lines <= 625 ? 2 : 1);
}

// Returns the first word of a stream's horizontal blanking that follows EAV, the line
// number and the CRC.
static int StreamHancStart(const SdiFormat* Format)
{
    return Format->Lines <= 625 ? 4 : 8;
}

// Writes Value as word Word of stream S in line Line. Lines count from 1.
static void PutStreamWord(uint8_t* Frame, int Bits, const SdiFormat* Format, int Line,
                          TestStream S, int Word, uint16_t Value)
{
    const size_t Symbol = (size_t)(Line - 1) * LineSymbols(Format) + (size_t)S.First +
                          (size_t)Word * (size_t)S.Step;
    PutSymbol(Frame, Bits, Symbol, Value);
}

// Returns Value with its even parity in bit 8 and the inverse of that bit in bit 9.
static uint16_t WithParity(uint8_t Value)
{
    int Ones = 0;
    for (int b = 0; b < 8; b++)
        Ones += (Value >> b) & 1;
    const uint16_t Bit8 = (uint16_t)(Ones & 1);
    return (uint16_t)(Value | Bit8 << 8 | (Bit8 ^ 1) << 9);
}

// Writes a packet into stream S of line Line, starting at word Word. The packet holds:
// - the ancillary data flag;
// - Did, Sdid and the count, each with parity;
// - Count words of Data;
// - the checksum, which is wrong when BadSum is true.
// Returns the word after the packet.
static int PutPacket(uint8_t* Frame, int Bits, const SdiFormat* Format, int Line,
                     TestStream S, int Word, uint8_t Did, uint8_t Sdid,
                     const uint16_t* Data, int Count, bool BadSum)
{
    uint16_t Words[263];
    int n = 0;
    Words[n++] = 0x000;
    Words[n++] = 0x3FF;
    Words[n++] = 0x3FF;
    Words[n++] = WithParity(Did);
    Words[n++] = WithParity(Sdid);
    Words[n++] = WithParity((uint8_t)Count);
    for (int i = 0; i < Count; i++)
        Words[n++] = Data[i];
    unsigned Sum = 0;
    for (int i = 3; i < n; i++)
        Sum += Words[i] & 0x1FF;
    Sum = (Sum + (BadSum ? 1u : 0u)) & 0x1FF;
    Words[n++] = (uint16_t)(Sum | (((Sum >> 8) & 1) ^ 1) << 9);
    for (int i = 0; i < n; i++)
        PutStreamWord(Frame, Bits, Format, Line, S, Word + i, Words[i]);
    return Word + n;
}

// Describes one packet of a test frame. The tests list these packets in the order in
// which the parser must list them.
typedef struct TestPacket
{
    int Line;
    bool InHanc;
    int Link;
    bool Chroma;
    uint8_t Did;
    uint8_t Sdid;
    int Count;
    bool BadSum;
} TestPacket;

// Writes Packets into a frame of Format. Each packet goes at the start of its section,
// or after the packet before it when that is in the same section. Also writes the
// payload ID 85 C5 00 01 on line PayloadLine.
static void PutPackets(uint8_t* Frame, int Bits, const SdiFormat* Format,
                       const TestPacket* Packets, int NumPackets, int PayloadLine)
{
    uint16_t Data[255];
    for (int i = 0; i < 255; i++)
        Data[i] = (uint16_t)((i * 37 + 5) & 0x3FF);

    int Next[2] = {0, 0}; // The word for the next packet in the section just written
    for (int p = 0; p < NumPackets; p++)
    {
        const TestPacket* P = &Packets[p];
        const TestStream S = StreamOf(Format, P->Link, P->Chroma);
        const bool SameSection = p > 0 && Packets[p - 1].Line == P->Line &&
                                 Packets[p - 1].InHanc == P->InHanc &&
                                 Packets[p - 1].Link == P->Link &&
                                 Packets[p - 1].Chroma == P->Chroma;
        int Word = P->InHanc ? StreamHancStart(Format) : StreamHancWords(Format);
        if (SameSection)
            Word = Next[P->InHanc];
        Next[P->InHanc] = PutPacket(Frame, Bits, Format, P->Line, S, Word, P->Did,
                                    P->Sdid, Data, P->Count, P->BadSum);
    }

    static const uint16_t Payload[4] = {0x85, 0xC5, 0x00, 0x01};
    PutPacket(Frame, Bits, Format, PayloadLine, StreamOf(Format, 1, false),
              StreamHancStart(Format), 0x41, 0x01, Payload, 4, false);
}

// Checks that Anc lists Expected[0] to Expected[NumExpected - 1], in that order. When
// Anc has a word buffer, also checks each packet's words. Returns NULL, or a message that
// says what differs.
static const char* CheckPackets(const DtSdiAncData* Anc,
                                const TestPacket* const* Expected, int NumExpected,
                                char* Message, size_t Size)
{
    if (Anc->NumPackets != NumExpected)
    {
        snprintf(Message, Size, "%d packets listed, %d expected", Anc->NumPackets,
                 NumExpected);
        return Message;
    }
    for (int p = 0; p < NumExpected; p++)
    {
        const DtSdiAncPacket* Got = &Anc->Packets[p];
        const TestPacket* Want = Expected[p];
        const bool Same = Got->Line == Want->Line && Got->InHanc == Want->InHanc &&
                          Got->VirtualInterface == Want->Link &&
                          Got->OnChroma == Want->Chroma && Got->Did == Want->Did &&
                          Got->SdidOrDbn == Want->Sdid && Got->NumWords == Want->Count &&
                          Got->ChecksumOk == !Want->BadSum;
        if (!Same)
        {
            snprintf(Message, Size, "packet %d (line %d, DID %02X) differs", p,
                     Want->Line, Want->Did);
            return Message;
        }
        for (int w = 0; Anc->Words != NULL && w < Got->NumWords; w++)
        {
            if (Got->Words[w] != (uint16_t)((w * 37 + 5) & 0x3FF))
            {
                snprintf(Message, Size, "packet %d: word %d differs", p, w);
                return Message;
            }
        }
        if (Anc->Words == NULL && Got->Words != NULL)
            return "a packet has words without a word buffer";
    }
    return NULL;
}

// The room that the tests give the parser for packets and their words.
#define TEST_MAX_PACKETS 16
#define TEST_MAX_WORDS 4096

typedef struct TestAnc
{
    DtSdiAncData Data;
    DtSdiAncPacket Packets[TEST_MAX_PACKETS];
    uint16_t Words[TEST_MAX_WORDS];
} TestAnc;

// Points T's DtSdiAncData at its own arrays, with room for MaxPackets packets and
// MaxWords words. Leaves out the word buffer when WithWords is false.
static void TestAnc_Init(TestAnc* T, int MaxPackets, int MaxWords, bool WithWords)
{
    memset(T, 0, sizeof(*T));
    T->Data.Packets = T->Packets;
    T->Data.MaxPackets = MaxPackets;
    T->Data.Words = WithWords ? T->Words : NULL;
    T->Data.MaxWords = MaxWords;
}

// Lists the packets of the 1080i50 frame, in the order the parser lists them. That order
// is line by line, and within a line the horizontal blanking before the active part. The
// frame also holds an audio data packet, an audio control packet and the payload ID. The
// parser lists these only when a filter names them.
static const TestPacket g_Hd[] = {
    {9, false, 1, false, 0x61, 0x01, 10, false},   // Captions, VANC, Y
    {15, false, 1, true, 0x45, 0x05, 3, true},     // A bad checksum, VANC, C
    {100, true, 1, true, 0x60, 0x60, 16, false},   // ATC, HANC, C
    {100, true, 1, true, 0xE7, 0x01, 24, false},   // Audio data, group 1
    {100, true, 1, false, 0xE3, 0x00, 11, false},  // Audio control, group 1
    {1124, false, 1, false, 0x41, 0x05, 8, false}, // AFD, VANC in field 2's blanking
};

// Builds the 1080i50 frame with g_Hd's packets. Returns NULL when there is no memory.
static uint8_t* BuildHdPackets(int Bits, size_t* Size)
{
    const SdiFormat* F = FindFormat("1080I50");
    uint8_t* Frame = BuildFrame(F, Bits, Size);
    if (Frame != NULL)
        PutPackets(Frame, Bits, F, g_Hd, (int)(sizeof(g_Hd) / sizeof(g_Hd[0])), 10);
    return Frame;
}

// Sets Filters on Parser, parses View into Anc and checks that Parse returns Result.
// Then checks the listed packets against Expected. Returns NULL, or a message that says
// what failed.
static const char* ParseAndCheckAnc(DtSdiParser* Parser, DtSdiView* View,
                                    const DtSdiAncFilter* Filters, int NumFilters,
                                    TestAnc* Anc, DtapiResult Result,
                                    const TestPacket* const* Expected, int NumExpected,
                                    char* Message, size_t Size)
{
    if (DtSdiParser_SetAncFilter(Parser, Filters, NumFilters) != DTAPI_OK)
        return "SetAncFilter refused the filters";
    if (DtSdiParser_Parse(Parser, View, NULL, NULL, &Anc->Data) != Result)
        return "Parse returned another result";
    return CheckPackets(&Anc->Data, Expected, NumExpected, Message, Size);
}

// Checks that the parser lists the packets of a 1080i50 frame in the frame's order, each
// with its place, IDs, words and checksum result. Audio and the payload ID are left out
// unless a filter names them. The test also checks:
// - a filter for any DID from line 11, one for captions and one for HANC on lines 90
//   to 200;
// - that without a word buffer the packets are listed without their words;
// - that room for two packets lists two, sets NumLost to 2 and returns
//   DTAPI_E_BUF_TOO_SMALL;
// - that room for 13 words lists the first two packets and returns
//   DTAPI_E_BUF_TOO_SMALL;
// - that the view finds the payload ID 85 C5 00 01.
DT_TEST(AncPacketsHd)
{
    char Message[160];
    size_t Size = 0;
    uint8_t* Frame = BuildHdPackets(10, &Size);
    DtSdiView* View = DtSdiView_Alloc();
    DtSdiParser* Parser = DtSdiParser_Alloc();
    TestAnc* Anc = (TestAnc*)malloc(sizeof(TestAnc));
    DT_ASSERT(Frame != NULL && View != NULL && Parser != NULL && Anc != NULL);
    DT_ASSERT_OK(DtSdiView_SetRawFrame(View, Frame, Size, DTAPI_VIDSTD_1080I50, 10));

    const TestPacket* Default[] = {&g_Hd[0], &g_Hd[1], &g_Hd[2], &g_Hd[5]};
    const TestPacket* From11[] = {&g_Hd[1], &g_Hd[2], &g_Hd[3], &g_Hd[4], &g_Hd[5]};
    const TestPacket* Captions[] = {&g_Hd[0]};
    const TestPacket* HancFrom90[] = {&g_Hd[2], &g_Hd[3], &g_Hd[4]};
    const TestPacket* FirstTwo[] = {&g_Hd[0], &g_Hd[1]};

    DtSdiAncFilter Any;
    memset(&Any, 0, sizeof(Any));
    Any.AnyDid = true;
    Any.Space = DT_SDI_ANC_SPACE_BOTH;
    Any.FirstLine = 11; // Starts after the payload ID on line 10, which has other words
    DtSdiAncFilter Cap = {false, 0x61, false, 0x01, DT_SDI_ANC_SPACE_VANC, 0, 0};
    DtSdiAncFilter Hanc = {true, 0, false, 0, DT_SDI_ANC_SPACE_HANC, 90, 200};

    const char* Failure = NULL;
    uint32_t PayloadId = 0;
    TestAnc_Init(Anc, TEST_MAX_PACKETS, TEST_MAX_WORDS, true);
    Failure = ParseAndCheckAnc(Parser, View, NULL, 0, Anc, DTAPI_OK, Default, 4, Message,
                               sizeof(Message));
    if (Failure == NULL)
        Failure = ParseAndCheckAnc(Parser, View, &Any, 1, Anc, DTAPI_OK, From11, 5,
                                   Message, sizeof(Message));
    if (Failure == NULL)
        Failure = ParseAndCheckAnc(Parser, View, &Cap, 1, Anc, DTAPI_OK, Captions, 1,
                                   Message, sizeof(Message));
    if (Failure == NULL)
        Failure = ParseAndCheckAnc(Parser, View, &Hanc, 1, Anc, DTAPI_OK, HancFrom90, 3,
                                   Message, sizeof(Message));
    if (Failure == NULL)
    {
        // Without a word buffer, the parser lists the packets and does not copy their
        // words.
        TestAnc_Init(Anc, TEST_MAX_PACKETS, 0, false);
        Failure = ParseAndCheckAnc(Parser, View, NULL, 0, Anc, DTAPI_OK, Default, 4,
                                   Message, sizeof(Message));
    }
    if (Failure == NULL)
    {
        // With room for two packets, the other two are lost and the call reports it.
        TestAnc_Init(Anc, 2, TEST_MAX_WORDS, true);
        Failure = ParseAndCheckAnc(Parser, View, NULL, 0, Anc, DTAPI_E_BUF_TOO_SMALL,
                                   FirstTwo, 2, Message, sizeof(Message));
        if (Failure == NULL && Anc->Data.NumLost != 2)
            Failure = "NumLost is not 2";
    }
    if (Failure == NULL)
    {
        // Gives room for the words of the first packet only. The second packet still
        // fits, because it has only three words.
        TestAnc_Init(Anc, TEST_MAX_PACKETS, 13, true);
        Failure = ParseAndCheckAnc(Parser, View, NULL, 0, Anc, DTAPI_E_BUF_TOO_SMALL,
                                   FirstTwo, 2, Message, sizeof(Message));
    }
    const DtapiResult Payload = DtSdiView_GetPayloadId(View, &PayloadId);

    free(Anc);
    DtSdiParser_Free(Parser);
    DtSdiView_Free(View);
    free(Frame);
    if (Failure != NULL)
        DT_FAIL("%s", Failure);
    DT_ASSERT_OK(Payload);
    DT_ASSERT_EQ(PayloadId, 0x85C50001u);
}

// Checks that the parser writes the whole image when the packets do not all fit. With
// room for one packet, Parse returns DTAPI_E_BUF_TOO_SMALL and NumLost is 3.
DT_TEST(ImageWhenPacketsAreLost)
{
    char Message[128];
    size_t Size = 0;
    uint8_t* Frame = BuildHdPackets(10, &Size);
    DtSdiView* View = DtSdiView_Alloc();
    DtSdiParser* Parser = DtSdiParser_Alloc();
    TestAnc* Anc = (TestAnc*)malloc(sizeof(TestAnc));
    TestImage T;
    DT_ASSERT(Frame != NULL && View != NULL && Parser != NULL && Anc != NULL);
    DT_ASSERT(TestImage_Alloc(&T, DTAPI_VIDSTD_1080I50, DT_SDI_PIXFMT_YUV422P_10B));
    DT_ASSERT_OK(DtSdiView_SetRawFrame(View, Frame, Size, DTAPI_VIDSTD_1080I50, 10));
    TestAnc_Init(Anc, 1, TEST_MAX_WORDS, true);

    const DtapiResult Result =
        DtSdiParser_Parse(Parser, View, &T.Image, NULL, &Anc->Data);
    const char* Failure = CheckImage(&T, Message, sizeof(Message));
    const int Lost = Anc->Data.NumLost;

    TestImage_Free(&T);
    free(Anc);
    DtSdiParser_Free(Parser);
    DtSdiView_Free(View);
    free(Frame);
    DT_ASSERT_EQ(Result, DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_EQ(Lost, 3);
    if (Failure != NULL)
        DT_FAIL("%s", Failure);
}

// Checks the packets of 625i50, in its one stream, and of 2160p50, in the streams of
// each link. Also checks the payload ID of each.
DT_TEST(AncPacketsSdAnd4k)
{
    static const TestPacket Sd[] = {
        {10, false, 1, false, 0x61, 0x01, 6, false}, // VANC
        {40, true, 1, false, 0x60, 0x60, 16, false}, // HANC
    };
    static const TestPacket Uhd[] = {
        {10, false, 2, false, 0x61, 0x01, 6, false}, // VANC, link 2, Y
        {40, true, 1, true, 0x60, 0x60, 16, false},  // HANC, link 1, C
        {40, true, 4, false, 0x51, 0x02, 5, false},  // HANC, link 4, Y
    };
    static const struct
    {
        const char* Name;
        int VidStd;
        const TestPacket* Packets;
        int NumPackets;
        int PayloadLine;
    } Cases[] = {
        {"625I50", DTAPI_VIDSTD_625I50, Sd, 2, 9},
        {"2160P50", DTAPI_VIDSTD_2160P50, Uhd, 3, 10},
    };

    char Message[160];
    for (size_t c = 0; c < sizeof(Cases) / sizeof(Cases[0]); c++)
    {
        const SdiFormat* F = FindFormat(Cases[c].Name);
        DT_ASSERT(F != NULL);
        size_t Size = 0;
        uint8_t* Frame = BuildFrame(F, 10, &Size);
        DtSdiView* View = DtSdiView_Alloc();
        DtSdiParser* Parser = DtSdiParser_Alloc();
        TestAnc* Anc = (TestAnc*)malloc(sizeof(TestAnc));
        DT_ASSERT(Frame != NULL && View != NULL && Parser != NULL && Anc != NULL);
        PutPackets(Frame, 10, F, Cases[c].Packets, Cases[c].NumPackets,
                   Cases[c].PayloadLine);
        DT_ASSERT_OK(DtSdiView_SetRawFrame(View, Frame, Size, Cases[c].VidStd, 10));

        const TestPacket* Expected[3];
        for (int p = 0; p < Cases[c].NumPackets; p++)
            Expected[p] = &Cases[c].Packets[p];
        // SD has no C stream, so its packets are never on chroma.
        TestAnc_Init(Anc, TEST_MAX_PACKETS, TEST_MAX_WORDS, true);
        const char* Failure =
            ParseAndCheckAnc(Parser, View, NULL, 0, Anc, DTAPI_OK, Expected,
                             Cases[c].NumPackets, Message, sizeof(Message));
        uint32_t PayloadId = 0;
        const DtapiResult Payload = DtSdiView_GetPayloadId(View, &PayloadId);

        free(Anc);
        DtSdiParser_Free(Parser);
        DtSdiView_Free(View);
        free(Frame);
        if (Failure != NULL)
            DT_FAIL("%s: %s", Cases[c].Name, Failure);
        DT_ASSERT_OK(Payload);
        DT_ASSERT_EQ(PayloadId, 0x85C50001u);
    }
}

// Checks that DtSdiView_GetPayloadId() returns DTAPI_E_NOT_FOUND and sets the ID to 0
// for a frame without a payload ID.
DT_TEST(NoPayloadId)
{
    size_t Size = 0;
    uint8_t* Frame = BuildFrame(FindFormat("720P50"), 10, &Size);
    DtSdiView* View = DtSdiView_Alloc();
    DT_ASSERT(Frame != NULL && View != NULL);
    DT_ASSERT_OK(DtSdiView_SetRawFrame(View, Frame, Size, DTAPI_VIDSTD_720P50, 10));
    uint32_t PayloadId = 1;
    const DtapiResult Result = DtSdiView_GetPayloadId(View, &PayloadId);
    DtSdiView_Free(View);
    free(Frame);
    DT_ASSERT_EQ(Result, DTAPI_E_NOT_FOUND);
    DT_ASSERT_EQ(PayloadId, 0);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Audio +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The audio packets here follow SMPTE ST 299-1 and ST 272 word for word. An HD data
// packet holds two clock words, four words for each channel of the group and six words
// of BCH code. An SD data packet holds subframes of three words.
//

// Returns the 24 bits of audio of sample n of channel c. Channels count from 0.
static uint32_t AudioValue(int c, int n)
{
    return ((uint32_t)c * 0x123457u + (uint32_t)n * 0x9E3779u) & 0xFFFFFF;
}

// Returns the V bit of sample n of channel c. Only one sample of channel 3 has it set.
static bool AudioInvalid(int c, int n)
{
    return c == 2 && n == 5;
}

// Returns the AES3 subframe of sample n of channel c, in the layout of DT_SDI_AUDIO_AES3.
// The subframe holds:
// - Z, set at the start of every block of 192 samples on channels 1 and 3 of a group;
// - the audio;
// - V;
// - a U bit and a C bit that change from sample to sample;
// - P.
static uint32_t AudioSubframe(int c, int n)
{
    const uint32_t Z = (n % 192 == 0 && c % 2 == 0) ? 1u : 0u;
    const uint32_t V = AudioInvalid(c, n) ? 1u : 0u;
    const uint32_t U = (uint32_t)(n & 1);
    const uint32_t C = (uint32_t)((n >> 1) & 1);
    const uint32_t P = (uint32_t)((c + n) & 1);
    return Z << 3 | AudioValue(c, n) << 4 | V << 28 | U << 29 | C << 30 | P << 31;
}

// Returns the BCH code of ST 299-1 over the lower bytes of Count words.
static uint64_t TestBch(const uint16_t* Words, int Count)
{
    uint64_t Bch = 0;
    for (int i = 0; i < Count; i++)
        Bch = Bch >> 8 ^ (((Words[i] ^ Bch) & 0xFF) * 0x10101010001ULL);
    return Bch;
}

// Writes the HD audio data packet of sample n of group Group into stream S of line Line,
// starting at word Word. Groups count from 0. The BCH code is wrong when BadBch is true.
// Returns the word after the packet.
static int PutHdAudio(uint8_t* Frame, const SdiFormat* Format, int Line, TestStream S,
                      int Word, int Group, int n, bool BadBch)
{
    uint16_t Data[24];
    Data[0] = WithParity((uint8_t)n);
    Data[1] = WithParity(0);
    for (int c = 0; c < 4; c++)
    {
        const uint32_t Aes3 = AudioSubframe(4 * Group + c, n);
        for (int b = 0; b < 4; b++)
            Data[2 + 4 * c + b] = WithParity((uint8_t)(Aes3 >> (8 * b)));
    }
    // The code covers the flag, the IDs, the count and the first 18 user data words.
    uint16_t Covered[24] = {0x000,
                            0x3FF,
                            0x3FF,
                            WithParity((uint8_t)(0xE7 - Group)),
                            WithParity((uint8_t)(n & 0xFF)),
                            WithParity(24)};
    for (int i = 0; i < 18; i++)
        Covered[6 + i] = Data[i];
    const uint64_t Bch = TestBch(Covered, 24) ^ (BadBch ? 1u : 0u);
    for (int i = 0; i < 6; i++)
        Data[18 + i] = WithParity((uint8_t)(Bch >> (8 * i)));
    return PutPacket(Frame, 10, Format, Line, S, Word, (uint8_t)(0xE7 - Group),
                     (uint8_t)(n & 0xFF), Data, 24, false);
}

// Builds an HD frame of Format with NumSamples samples of audio groups 1 and 2. The
// frame holds:
// - the data packets, two a line in the C stream, from line 1 on and skipping lines 8
//   and 9;
// - a control packet for group 1 with frame number FrameNumber, in the Y stream of
//   line 9;
// - a wrong BCH code in one packet of group 2.
static uint8_t* BuildHdAudio(const SdiFormat* Format, int NumSamples, int FrameNumber,
                             size_t* Size)
{
    uint8_t* Frame = BuildFrame(Format, 10, Size);
    if (Frame == NULL)
        return NULL;
    const TestStream C = StreamOf(Format, 1, true);
    for (int n = 0; n < NumSamples; n++)
    {
        const int Line = 1 + n / 2 + (n / 2 >= 7 ? 2 : 0); // Skips lines 8 and 9
        int Word = StreamHancStart(Format) + (n % 2) * 2 * 31;
        Word = PutHdAudio(Frame, Format, Line, C, Word, 0, n, false);
        PutHdAudio(Frame, Format, Line, C, Word, 1, n, n == 100);
    }
    uint16_t Control[11] = {WithParity((uint8_t)FrameNumber)};
    for (int i = 1; i < 11; i++)
        Control[i] = WithParity(0);
    PutPacket(Frame, 10, Format, 9, StreamOf(Format, 1, false), StreamHancStart(Format),
              0xE3, 0x00, Control, 11, false);
    return Frame;
}

// Builds a 625-line frame with NumSamples samples of channels 1 and 2. The samples are
// ST 272 subframes, in packets of eight samples a channel. There is one packet a line,
// from line 1 on.
static uint8_t* BuildSdAudio(const SdiFormat* Format, int NumSamples, size_t* Size)
{
    uint8_t* Frame = BuildFrame(Format, 10, Size);
    if (Frame == NULL)
        return NULL;
    const TestStream S = StreamOf(Format, 1, false);
    for (int First = 0; First < NumSamples; First += 8)
    {
        uint16_t Data[48];
        int Count = 0;
        for (int n = First; n < First + 8 && n < NumSamples; n++)
        {
            for (int c = 0; c < 2; c++)
            {
                // Takes the upper 20 of the 24 bits of audio. Z, V, U, C and P are as
                // in the subframe.
                const uint32_t Aes3 = AudioSubframe(c, n);
                const uint32_t A = (Aes3 >> 8) & 0xFFFFF;
                const uint32_t X = ((Aes3 >> 3) & 1) | (uint32_t)c << 1 | (A & 0x3F) << 3;
                const uint32_t Y = (A >> 6) & 0x1FF;
                const uint32_t Z = ((A >> 15) & 0x1F) | (Aes3 >> 28) << 5;
                Data[Count++] = (uint16_t)(X | (((X >> 8) & 1) ^ 1) << 9);
                Data[Count++] = (uint16_t)(Y | (((Y >> 8) & 1) ^ 1) << 9);
                Data[Count++] = (uint16_t)(Z | (((Z >> 8) & 1) ^ 1) << 9);
            }
        }
        PutPacket(Frame, 10, Format, 1 + First / 8, S, StreamHancStart(Format), 0xFF,
                  (uint8_t)(First / 8), Data, Count, false);
    }
    return Frame;
}

// Holds room for a frame's samples of each channel, one array a channel, and a
// DtSdiAudio set up to use it.
typedef struct TestAudio
{
    DtSdiAudio Audio;
    uint32_t Samples[DT_SDI_AUDIO_MAX_CHANNELS][2048];
} TestAudio;

// Sets up the first NumPairs channel pairs of T in Format, each channel with room for
// 2048 samples.
static void TestAudio_Init(TestAudio* T, DtSdiAudioFormat Format, int NumPairs)
{
    memset(T, 0, sizeof(*T));
    for (int p = 0; p < NumPairs; p++)
        T->Audio.Formats[p] = Format;
    for (int c = 0; c < 2 * NumPairs; c++)
    {
        T->Audio.Channels[c].Samples = T->Samples[c];
        T->Audio.Channels[c].Stride = 1;
        T->Audio.Channels[c].MaxSamples = 2048;
    }
}

// Checks Count samples of each of the first NumChannels channels. Without Pcm, the
// samples are subframes. With Pcm, they hold the audio at the top of 32 bits. The audio
// has 24 bits, or 20 when Twenty is true. Also checks the Invalid flag of each channel,
// and that no other channel is present. Returns NULL, or a message that says what
// differs.
static const char* CheckAudio(const TestAudio* T, int NumChannels, int Count, bool Pcm,
                              bool Twenty, char* Message, size_t Size)
{
    for (int c = 0; c < NumChannels; c++)
    {
        const DtSdiAudioChannel* C = &T->Audio.Channels[c];
        if (C->NumSamples != Count || !C->Present)
        {
            snprintf(Message, Size, "channel %d: %d samples, %d expected", c + 1,
                     C->NumSamples, Count);
            return Message;
        }
        if (C->Invalid != (c == 2))
        {
            snprintf(Message, Size, "channel %d: Invalid is wrong", c + 1);
            return Message;
        }
        for (int n = 0; n < Count; n++)
        {
            uint32_t Want = AudioSubframe(c, n);
            if (Twenty)
                Want &= ~0xF0u;
            if (Pcm)
                Want = (Want << 4) & 0xFFFFFF00u;
            if (T->Samples[c][n] != Want)
            {
                snprintf(Message, Size, "channel %d, sample %d: %08X, %08X expected",
                         c + 1, n, (unsigned)T->Samples[c][n], (unsigned)Want);
                return Message;
            }
        }
    }
    for (int c = NumChannels; c < DT_SDI_AUDIO_MAX_CHANNELS; c++)
    {
        if (T->Audio.Channels[c].Present)
            return "a channel without audio is present";
    }
    return NULL;
}

// Checks HD audio at 25 and at 29.97 frames a second. The test checks:
// - the samples of two groups, as AES3 subframes and as PCM;
// - the frame number of the control packet;
// - with the checks on, that the packet with a wrong BCH code is counted in group 2.
DT_TEST(AudioHd)
{
    static const struct
    {
        const char* Name;
        int NumSamples;
        int FrameNumber;
    } Cases[] = {{"1080I50", 1920, 1},     // No cadence, so the packet's 1 reads as 0
                 {"1080I59_94", 1601, 2}}; // Frame 2 of the cadence
    char Message[160];
    TestAudio* T = (TestAudio*)malloc(sizeof(TestAudio));
    DtSdiParser* Parser = DtSdiParser_Alloc();
    DtSdiView* View = DtSdiView_Alloc();
    DT_ASSERT(T != NULL && Parser != NULL && View != NULL);

    const char* Failure = NULL;
    int Errors[4] = {0};
    int FrameNumbers[2] = {-1, -1};
    for (size_t k = 0; k < sizeof(Cases) / sizeof(Cases[0]) && Failure == NULL; k++)
    {
        const SdiFormat* F = FindFormat(Cases[k].Name);
        size_t Size = 0;
        uint8_t* Frame =
            BuildHdAudio(F, Cases[k].NumSamples, Cases[k].FrameNumber, &Size);
        if (Frame == NULL ||
            DtSdiView_SetRawFrame(View, Frame, Size, F->VidStd, 10) != DTAPI_OK)
        {
            Failure = "no frame";
        }

        // Parses the audio as subframes, then as PCM with the checks on. The parser
        // fills in only the audio.
        TestAudio_Init(T, DT_SDI_AUDIO_AES3, 4);
        if (Failure == NULL &&
            DtSdiParser_Parse(Parser, View, NULL, &T->Audio, NULL) != DTAPI_OK)
            Failure = "Parse failed";
        if (Failure == NULL)
            Failure = CheckAudio(T, 8, Cases[k].NumSamples, false, false, Message,
                                 sizeof(Message));
        FrameNumbers[k] = T->Audio.FrameNumber;
        TestAudio_Init(T, DT_SDI_AUDIO_PCM, 4);
        DtSdiParser_SetAudioChecks(Parser, true);
        if (Failure == NULL &&
            DtSdiParser_Parse(Parser, View, NULL, &T->Audio, NULL) != DTAPI_OK)
            Failure = "Parse failed";
        if (Failure == NULL)
            Failure = CheckAudio(T, 8, Cases[k].NumSamples, true, false, Message,
                                 sizeof(Message));
        for (int g = 0; g < 4; g++)
            Errors[g] += T->Audio.NumPacketErrors[g];
        DtSdiParser_SetAudioChecks(Parser, false);
        free(Frame);
    }

    DtSdiView_Free(View);
    DtSdiParser_Free(Parser);
    free(T);
    if (Failure != NULL)
        DT_FAIL("%s", Failure);
    DT_ASSERT_EQ(FrameNumbers[0], 0);
    DT_ASSERT_EQ(FrameNumbers[1], 2);
    DT_ASSERT_EQ(Errors[0], 0);
    DT_ASSERT_EQ(Errors[1], 2); // One packet with a wrong code, in each of two frames
    DT_ASSERT_EQ(Errors[2] + Errors[3], 0);
}

// Checks SD audio of ST 272 in 625i50. The 20 bits of audio must come out right, both
// as PCM and as subframes with Z, V, U, C and P.
DT_TEST(AudioSd)
{
    char Message[160];
    const SdiFormat* F = FindFormat("625I50");
    size_t Size = 0;
    uint8_t* Frame = BuildSdAudio(F, 1920, &Size);
    TestAudio* T = (TestAudio*)malloc(sizeof(TestAudio));
    DtSdiParser* Parser = DtSdiParser_Alloc();
    DtSdiView* View = DtSdiView_Alloc();
    DT_ASSERT(Frame != NULL && T != NULL && Parser != NULL && View != NULL);
    DT_ASSERT_OK(DtSdiView_SetRawFrame(View, Frame, Size, F->VidStd, 10));

    TestAudio_Init(T, DT_SDI_AUDIO_AES3, 1);
    DtapiResult Result = DtSdiParser_Parse(Parser, View, NULL, &T->Audio, NULL);
    const char* Failure = Result != DTAPI_OK ? "Parse failed"
                                             : CheckAudio(T, 2, 1920, false, true,
                                                          Message, sizeof(Message));
    if (Failure == NULL)
    {
        TestAudio_Init(T, DT_SDI_AUDIO_PCM, 1);
        Result = DtSdiParser_Parse(Parser, View, NULL, &T->Audio, NULL);
        Failure = Result != DTAPI_OK
                      ? "Parse failed"
                      : CheckAudio(T, 2, 1920, true, true, Message, sizeof(Message));
    }

    DtSdiView_Free(View);
    DtSdiParser_Free(Parser);
    free(T);
    free(Frame);
    if (Failure != NULL)
        DT_FAIL("%s", Failure);
}

// Checks which audio buffers the parser refuses before it writes anything, in 1080i59.94.
// The test checks that:
// - a channel with room for 1601 samples gives DTAPI_E_BUF_TOO_SMALL, since a frame
//   holds up to 1602;
// - an unknown audio format gives DTAPI_E_INVALID_FORMAT;
// - a negative stride gives DTAPI_E_INVALID_ARG;
// - a buffer that is too small is accepted for a pair that is not wanted.
DT_TEST(AudioRefusals)
{
    const SdiFormat* F = FindFormat("1080I59_94");
    size_t Size = 0;
    uint8_t* Frame = BuildFrame(F, 10, &Size);
    TestAudio* T = (TestAudio*)malloc(sizeof(TestAudio));
    DtSdiParser* Parser = DtSdiParser_Alloc();
    DtSdiView* View = DtSdiView_Alloc();
    DT_ASSERT(Frame != NULL && T != NULL && Parser != NULL && View != NULL);
    DT_ASSERT_OK(DtSdiView_SetRawFrame(View, Frame, Size, F->VidStd, 10));

    int Most = 0;
    const DtapiResult MaxResult = DtSdiAudio_MaxSamples(F->VidStd, &Most);
    TestAudio_Init(T, DT_SDI_AUDIO_PCM, 1);
    T->Audio.Channels[1].MaxSamples = 1601;
    const DtapiResult Small = DtSdiParser_Parse(Parser, View, NULL, &T->Audio, NULL);
    TestAudio_Init(T, DT_SDI_AUDIO_PCM, 1);
    T->Audio.Formats[0] = (DtSdiAudioFormat)7;
    const DtapiResult Unknown = DtSdiParser_Parse(Parser, View, NULL, &T->Audio, NULL);
    TestAudio_Init(T, DT_SDI_AUDIO_PCM, 1);
    T->Audio.Channels[0].Stride = -1;
    const DtapiResult Backwards = DtSdiParser_Parse(Parser, View, NULL, &T->Audio, NULL);
    TestAudio_Init(T, DT_SDI_AUDIO_NONE, 1);
    T->Audio.Channels[0].MaxSamples = 1; // The pair is not wanted, so this is not checked
    const DtapiResult NotWanted = DtSdiParser_Parse(Parser, View, NULL, &T->Audio, NULL);

    DtSdiView_Free(View);
    DtSdiParser_Free(Parser);
    free(T);
    free(Frame);
    DT_ASSERT_OK(MaxResult);
    DT_ASSERT_EQ(Most, 1602);
    DT_ASSERT_EQ(Small, DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_EQ(Unknown, DTAPI_E_INVALID_FORMAT);
    DT_ASSERT_EQ(Backwards, DTAPI_E_INVALID_ARG);
    DT_ASSERT_OK(NotWanted);
}

// Checks the largest number of audio samples a frame holds at each frame rate. Also
// checks that 1080p50 level B gives DTAPI_E_INVALID_VIDSTD.
DT_TEST(MaxSamplesPerRate)
{
    static const struct
    {
        int VidStd;
        int Most;
    } Cases[] = {{DTAPI_VIDSTD_1080P23_98, 2002}, {DTAPI_VIDSTD_1080P24, 2000},
                 {DTAPI_VIDSTD_1080I50, 1920},    {DTAPI_VIDSTD_525I59_94, 1602},
                 {DTAPI_VIDSTD_720P50, 960},      {DTAPI_VIDSTD_720P59_94, 801},
                 {DTAPI_VIDSTD_2160P60, 800}};
    for (size_t i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        int Most = 0;
        DT_ASSERT_OK(DtSdiAudio_MaxSamples(Cases[i].VidStd, &Most));
        DT_ASSERT_EQ(Most, Cases[i].Most);
    }
    int Most = 0;
    DT_ASSERT_EQ(DtSdiAudio_MaxSamples(DTAPI_VIDSTD_1080P50B, &Most),
                 DTAPI_E_INVALID_VIDSTD);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= FFmpeg's sdi muxer +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The number of audio channels the muxer writes. These are the channels of group 1.
#define MUXER_CHANNELS 4

// Checks the audio that the parser took from a frame against the next samples of Pcm.
// Pcm is the muxer's input, four interleaved channels of s32le. The frame carries the
// upper 24 bits of each sample, or 20 in SD, and Mask keeps those bits. Also checks that
// the four channels have the same number of samples and that no packet failed its
// checks. Returns NULL, or a message that says what differs.
static const char* CheckMuxerAudio(const TestAudio* A, FILE* Pcm, uint32_t Mask,
                                   char* Message, size_t Size)
{
    const int Count = A->Audio.Channels[0].NumSamples;
    for (int c = 0; c < MUXER_CHANNELS; c++)
    {
        if (!A->Audio.Channels[c].Present || A->Audio.Channels[c].NumSamples != Count)
            return "the channels differ in their samples";
    }
    for (int g = 0; g < DT_SDI_AUDIO_MAX_CHANNELS / 4; g++)
    {
        if (A->Audio.NumPacketErrors[g] != 0)
            return "a packet failed its checks";
    }
    for (int n = 0; n < Count; n++)
    {
        uint8_t Bytes[4 * MUXER_CHANNELS];
        if (fread(Bytes, 1, sizeof(Bytes), Pcm) != sizeof(Bytes))
            return "the .pcm file ends early";
        for (int c = 0; c < MUXER_CHANNELS; c++)
        {
            const uint32_t Source =
                (uint32_t)Bytes[4 * c] | (uint32_t)Bytes[4 * c + 1] << 8 |
                (uint32_t)Bytes[4 * c + 2] << 16 | (uint32_t)Bytes[4 * c + 3] << 24;
            if (A->Samples[c][n] != (Source & Mask))
            {
                snprintf(Message, Size, "channel %d, sample %d: %08X, %08X expected",
                         c + 1, n, (unsigned)A->Samples[c][n], (unsigned)(Source & Mask));
                return Message;
            }
        }
    }
    return NULL;
}

// Checks that the parser gives back the images of frames made by FFmpeg's sdi muxer. The
// frames are in the directory that CDTAPI_TEST_SDI_DIR names, as the top of this file
// describes. Each frame must also have a payload ID that names the standard's payload.
// Where <Name>.pcm is present too, the frames carry its audio in four channels. The
// parser must then take out that audio exactly, and every packet's BCH code and checksum
// must be right.
DT_TEST(FramesOfTheSdiMuxer)
{
    const char* Dir = getenv("CDTAPI_TEST_SDI_DIR");
    if (Dir == NULL || Dir[0] == '\0')
    {
        printf("    skipped: CDTAPI_TEST_SDI_DIR is not set\n");
        return;
    }

    char Message[160];
    int Checked = 0;
    for (int i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        const SdiFormat* F = &g_SdiFormats[i];
        char Name[512];
        snprintf(Name, sizeof(Name), "%s/%s.raw", Dir, F->Name);
        FILE* Raw = fopen(Name, "rb");
        snprintf(Name, sizeof(Name), "%s/%s.yuv", Dir, F->Name);
        FILE* Yuv = fopen(Name, "rb");
        snprintf(Name, sizeof(Name), "%s/%s.pcm", Dir, F->Name);
        FILE* Pcm = fopen(Name, "rb");
        // Skips the audio of 2160p. There the muxer puts its audio in the C streams of
        // links 4 and 2. SMPTE ST 299-1 puts it in link 1, and the parser takes it from
        // there.
        const bool AudioElsewhere = Pcm != NULL && Is4k(F);
        if (AudioElsewhere)
        {
            fclose(Pcm);
            Pcm = NULL;
        }
        if (Raw == NULL || Yuv == NULL)
        {
            if (Raw != NULL)
                fclose(Raw);
            if (Yuv != NULL)
                fclose(Yuv);
            if (Pcm != NULL)
                fclose(Pcm);
            continue;
        }

        size_t FrameSize = 0;
        TestImage T;
        memset(&T, 0, sizeof(T));
        DtSdiView_RawFrameSize(F->VidStd, 10, &FrameSize);
        uint8_t* Frame = (uint8_t*)malloc(FrameSize);
        DtSdiView* View = DtSdiView_Alloc();
        DtSdiParser* Parser = DtSdiParser_Alloc();
        TestAudio* A = Pcm != NULL ? (TestAudio*)malloc(sizeof(TestAudio)) : NULL;
        const bool Ready = Frame != NULL && View != NULL && Parser != NULL &&
                           (Pcm == NULL || A != NULL) &&
                           TestImage_Alloc(&T, F->VidStd, DT_SDI_PIXFMT_YUV422P_10B);
        const size_t PlaneBytes[3] = {2 * (size_t)T.Width * (size_t)T.Height,
                                      (size_t)T.Width * (size_t)T.Height,
                                      (size_t)T.Width * (size_t)T.Height};
        uint8_t* Expected = Ready ? (uint8_t*)malloc(PlaneBytes[0]) : NULL;
        const char* Failure = Ready && Expected != NULL ? NULL : "out of memory";
        int NumFrames = 0;
        int NumSamples = 0;
        char Numbers[64] = "";
        if (A != NULL)
        {
            TestAudio_Init(A, DT_SDI_AUDIO_PCM, MUXER_CHANNELS / 2);
            DtSdiParser_SetAudioChecks(Parser, true);
        }

        while (Failure == NULL && fread(Frame, 1, FrameSize, Raw) == FrameSize)
        {
            if (DtSdiView_SetRawFrame(View, Frame, FrameSize, F->VidStd, 10) !=
                    DTAPI_OK ||
                DtSdiParser_Parse(Parser, View, &T.Image, A != NULL ? &A->Audio : NULL,
                                  NULL) != DTAPI_OK)
            {
                Failure = "the frame was refused";
                break;
            }
            // The first byte of the muxer's payload ID names the standard's payload.
            uint32_t PayloadId = 0;
            if (DtSdiView_GetPayloadId(View, &PayloadId) != DTAPI_OK ||
                (int)(PayloadId >> 24) != F->Payload)
            {
                Failure = "the payload ID is missing or wrong";
                break;
            }
            for (int p = 0; p < 3 && Failure == NULL; p++)
            {
                if (fread(Expected, 1, PlaneBytes[p], Yuv) != PlaneBytes[p])
                {
                    Failure = "the .yuv file ends early";
                    break;
                }
                const size_t LineBytes = PlaneBytes[p] / (size_t)T.Height;
                for (int y = 0; y < T.Height; y++)
                {
                    if (memcmp(T.Image.Planes[p] + (size_t)y * (size_t)T.Image.Strides[p],
                               Expected + (size_t)y * LineBytes, LineBytes) != 0)
                    {
                        Failure = "the image differs";
                        printf("    %s frame %d plane %d line %d differs\n", F->Name,
                               NumFrames, p, y);
                        break;
                    }
                }
            }
            if (Failure == NULL && A != NULL)
            {
                Failure =
                    CheckMuxerAudio(A, Pcm, F->Lines <= 625 ? 0xFFFFF000u : 0xFFFFFF00u,
                                    Message, sizeof(Message));
                NumSamples += A->Audio.Channels[0].NumSamples;
                const size_t Used = strlen(Numbers);
                if (Used + 4 < sizeof(Numbers))
                    snprintf(Numbers + Used, sizeof(Numbers) - Used, " %d",
                             A->Audio.FrameNumber);
            }
            NumFrames++;
        }

        free(Expected);
        free(A);
        TestImage_Free(&T);
        DtSdiParser_Free(Parser);
        DtSdiView_Free(View);
        free(Frame);
        fclose(Raw);
        fclose(Yuv);
        if (Pcm != NULL)
            fclose(Pcm);
        if (Failure != NULL)
            DT_FAIL("%s: frame %d: %s", F->Name, NumFrames, Failure);
        if (Pcm != NULL)
            printf("    %s: %d frames, %d samples a channel, frame numbers%s\n", F->Name,
                   NumFrames, NumSamples, Numbers);
        else
            printf("    %s: %d frames%s\n", F->Name, NumFrames,
                   AudioElsewhere ? ", its audio on the wrong link" : "");
        Checked++;
    }
    printf("    %d standards checked\n", Checked);
}

// Checks that the parser gives the right image over a worker pool of four threads. The
// test parses 2160p50 and 1080i50 into v210. It uses as many pieces as the standard
// calls for, and three pieces. 2160p splits into pieces by itself, and 1080i50 splits
// only when asked.
DT_TEST(ImageOverWorkerPool)
{
    static const char* Names[] = {"2160P50", "1080I50"};
    DtWorkerPool* Pool = DtWorkerPool_Alloc();
    DT_ASSERT(Pool != NULL);
    DT_ASSERT_OK(DtWorkerPool_StartThreads(Pool, 4));
    char Message[160];
    for (size_t i = 0; i < sizeof(Names) / sizeof(Names[0]); i++)
    {
        const SdiFormat* F = FindFormat(Names[i]);
        DT_ASSERT(F != NULL);
        for (int Threads = 0; Threads <= 3; Threads += 3)
        {
            size_t FrameSize = 0;
            uint8_t* Frame = BuildFrame(F, 10, &FrameSize);
            DtSdiView* View = DtSdiView_Alloc();
            DtSdiParser* Parser = DtSdiParser_Alloc();
            TestImage T;
            const char* Failure = NULL;
            if (Frame == NULL || View == NULL || Parser == NULL ||
                !TestImage_Alloc(&T, F->VidStd, DT_SDI_PIXFMT_V210))
            {
                Failure = "out of memory";
                memset(&T, 0, sizeof(T));
            }
            else if (DtSdiParser_SetWorkerPool(Parser, Pool, Threads) != DTAPI_OK ||
                     DtSdiView_SetRawFrame(View, Frame, FrameSize, F->VidStd, 10) !=
                         DTAPI_OK ||
                     DtSdiParser_Parse(Parser, View, &T.Image, NULL, NULL) != DTAPI_OK)
                Failure = "the parse failed";
            else
                Failure = CheckImage(&T, Message, sizeof(Message));
            TestImage_Free(&T);
            DtSdiParser_Free(Parser);
            DtSdiView_Free(View);
            free(Frame);
            if (Failure != NULL)
            {
                DtWorkerPool_Free(Pool);
                DT_FAIL("%s, %d threads: %s", F->Name, Threads, Failure);
            }
        }
    }
    DtWorkerPool_Free(Pool);
}

DT_TEST_MAIN("SdiParser", DT_RUN(SizesEveryStandard), DT_RUN(LeastStrides),
             DT_RUN(ImageEveryStandard), DT_RUN(EveryPixelFormat), DT_RUN(Image4k),
             DT_RUN(ActiveLinesWhereTheyLie), DT_RUN(Refusals),
             DT_RUN(FramesOfTheSdiMuxer), DT_RUN(AncPacketsHd),
             DT_RUN(ImageWhenPacketsAreLost), DT_RUN(AncPacketsSdAnd4k),
             DT_RUN(NoPayloadId), DT_RUN(AudioHd), DT_RUN(AudioSd), DT_RUN(AudioRefusals),
             DT_RUN(MaxSamplesPerRate), DT_RUN(ImageOverWorkerPool))
