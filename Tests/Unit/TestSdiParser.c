// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestSdiParser.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The parser's image, views of raw frames, and the sizes of images
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The frames here are built from the standards' own numbers, not from the library's
// tables: the active lines of each field (SMPTE ST 125, BT.656, ST 274 and ST 296, in
// the line numbers a raw frame counts), the line lengths of SdiFormats.inc, and for
// 2160p the division of the image over four links of SMPTE ST 2082-10. Each symbol of
// the active video gets a value that tells its line and place apart, and the blanking a
// value no image symbol has; so a line read from the wrong place shows. Each pixel
// format is checked against a packing of its own, written from the format's definition.
//
// With CDTAPI_TEST_SDI_DIR set, one more case reads frames that FFmpeg's sdi muxer made:
// <Name>.raw holds 10-bit raw frames of standard <Name>, as in SdiFormats.inc, and
// <Name>.yuv the yuv422p10le images they were made from. The parser must give each image
// back exactly.

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

// The active lines of each field, as a raw frame numbers its lines from 1.
typedef struct ActiveLines
{
    int NumFields;
    int First[2];
    int Last[2];
} ActiveLines;

// Fills *Lines for Format; returns false for 2160p, whose lines the links hold.
static bool GetActiveLines(const SdiFormat* Format, ActiveLines* Lines)
{
    memset(Lines, 0, sizeof(*Lines));
    if (Format->Lines == 525)
    {
        // SMPTE ST 125: lines 20 to 263 and 283 to 525, three lower as a frame counts.
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

// The raw line (from 1) that holds line y of the woven image.
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

// The standards a channel carries: every one but those of 3G level B.
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

// The value of symbol s of image line y: different for neighbouring lines and symbols,
// and never 0x3FF, which the blanking holds.
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

// Symbols of one whole raw line: of one link, times four for 2160p.
static size_t LineSymbols(const SdiFormat* Format)
{
    return (size_t)(2 * Format->Samples) * (Is4k(Format) ? 4 : 1);
}

// The size of a raw frame, padded to 64 bits, as SDI File Format says.
static size_t FrameBytes(const SdiFormat* Format, int Bits)
{
    const size_t FrameBits = (size_t)Format->Lines * LineSymbols(Format) * (size_t)Bits;
    return (FrameBits + 63) / 64 * 8;
}

// Builds a raw frame of Format: blanking everywhere, and ImageSymbol where the image is.
// Returns NULL when there is no memory.
static uint8_t* BuildFrame(const SdiFormat* Format, int Bits, size_t* Size)
{
    *Size = FrameBytes(Format, Bits);
    uint8_t* Frame = (uint8_t*)calloc(*Size, 1);
    if (Frame == NULL)
        return NULL;

    // Every symbol BLANKING_SYMBOL, all ten bits set: in 10 bits that is every byte
    // 0xFF, and the padding at the end does not matter.
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

    // ST 2082-10: the pixel pairs of image line 2k go to links 1 and 2 in turn, those of
    // line 2k + 1 to links 3 and 4; link line 42 + k carries them. Each link is a C and
    // a Y stream, its horizontal blanking first; the raw line takes word n of the C
    // streams of links 4, 2, 3 and 1, then word n of their Y streams.
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

// The bytes of plane Plane of line y of an image Width wide in Format, written from the
// format's definition. Returns the number of bytes.
static int ExpectedLine(DtSdiPixelFormat Format, int Width, int y, int Plane,
                        uint8_t* Out)
{
    const int N = 2 * Width;
    int Bytes = 0;
    switch (Format)
    {
    case DT_SDI_PIXFMT_UYVY_10B:
        // Ten bits a symbol, the least significant first.
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
        // Three samples a little-endian word, in the order Cb Y Cr Y; six pixels in 16
        // bytes; the line padded to 128 bytes.
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
        // Y0 Cb Y1 Cr, each a little-endian word with the ten bits at the top.
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

// An image in memory, its planes with strides a little above the least, so that a
// write past the line's end shows in the bytes between.
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

// Compares every line of every plane of T with ExpectedLine, and the slack after it with
// SLACK_BYTE. Returns NULL, or what differs.
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

// Builds a frame of Format with Bits bits a symbol, parses it into PixFmt, and checks
// the image. Returns NULL, or what failed.
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

// The size of every standard's raw frame and image, and the standards a view refuses.
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

// The least strides of each format, for 1920 and 1280 pixels: v210 rounds its lines up
// to 128 bytes.
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

// The image of every standard up to 3G, from a 10-bit frame, as planar 10-bit.
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

// Every pixel format, from 10- and 16-bit frames: SD, 720p, whose 10-bit lines do not
// all start on a byte, and 1080i.
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

// The image of 2160p from its four links, in 10 and 16 bits.
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

// Every line of the image read where it lies, through the symbol pointer: also the
// lines of 720p24 in 10 bits that start half-way through a byte.
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

// What a view and the parser refuse, and the frame's format a view tells.
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

// Frames that FFmpeg's sdi muxer made, where CDTAPI_TEST_SDI_DIR names a directory of
// them; see the top of this file.
DT_TEST(FramesOfTheSdiMuxer)
{
    const char* Dir = getenv("CDTAPI_TEST_SDI_DIR");
    if (Dir == NULL || Dir[0] == '\0')
    {
        printf("    skipped: CDTAPI_TEST_SDI_DIR is not set\n");
        return;
    }

    int Checked = 0;
    for (int i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        const SdiFormat* F = &g_SdiFormats[i];
        char RawName[512];
        char YuvName[512];
        snprintf(RawName, sizeof(RawName), "%s/%s.raw", Dir, F->Name);
        snprintf(YuvName, sizeof(YuvName), "%s/%s.yuv", Dir, F->Name);
        FILE* Raw = fopen(RawName, "rb");
        FILE* Yuv = fopen(YuvName, "rb");
        if (Raw == NULL || Yuv == NULL)
        {
            if (Raw != NULL)
                fclose(Raw);
            if (Yuv != NULL)
                fclose(Yuv);
            continue;
        }

        size_t FrameSize = 0;
        TestImage T;
        memset(&T, 0, sizeof(T));
        DtSdiView_RawFrameSize(F->VidStd, 10, &FrameSize);
        uint8_t* Frame = (uint8_t*)malloc(FrameSize);
        DtSdiView* View = DtSdiView_Alloc();
        DtSdiParser* Parser = DtSdiParser_Alloc();
        const bool Ready = Frame != NULL && View != NULL && Parser != NULL &&
                           TestImage_Alloc(&T, F->VidStd, DT_SDI_PIXFMT_YUV422P_10B);
        const size_t PlaneBytes[3] = {2 * (size_t)T.Width * (size_t)T.Height,
                                      (size_t)T.Width * (size_t)T.Height,
                                      (size_t)T.Width * (size_t)T.Height};
        uint8_t* Expected = Ready ? (uint8_t*)malloc(PlaneBytes[0]) : NULL;
        const char* Failure = Ready && Expected != NULL ? NULL : "out of memory";
        int NumFrames = 0;

        while (Failure == NULL && fread(Frame, 1, FrameSize, Raw) == FrameSize)
        {
            if (DtSdiView_SetRawFrame(View, Frame, FrameSize, F->VidStd, 10) !=
                    DTAPI_OK ||
                DtSdiParser_Parse(Parser, View, &T.Image, NULL, NULL) != DTAPI_OK)
            {
                Failure = "the frame was refused";
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
            NumFrames++;
        }

        free(Expected);
        TestImage_Free(&T);
        DtSdiParser_Free(Parser);
        DtSdiView_Free(View);
        free(Frame);
        fclose(Raw);
        fclose(Yuv);
        if (Failure != NULL)
            DT_FAIL("%s: %s", F->Name, Failure);
        printf("    %s: %d frames\n", F->Name, NumFrames);
        Checked++;
    }
    printf("    %d standards checked\n", Checked);
}

DT_TEST_MAIN("SdiParser", DT_RUN(SizesEveryStandard), DT_RUN(LeastStrides),
             DT_RUN(ImageEveryStandard), DT_RUN(EveryPixelFormat), DT_RUN(Image4k),
             DT_RUN(ActiveLinesWhereTheyLie), DT_RUN(Refusals),
             DT_RUN(FramesOfTheSdiMuxer))
