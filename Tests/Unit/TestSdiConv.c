// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestSdiConv.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The vector conversions of SDI symbols against the portable ones
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Checks that every conversion of the SSSE3 and the AVX2 versions gives the same bytes as
// the portable version. The versions run only where the processor has them. The input is
// random. The runs have every length a line can have, and many short lengths. A guard of
// bytes after each output shows that no conversion writes past its run. The file also
// checks that the parser and the builder give the same results with each version. The
// portable conversions themselves are checked by the parser's and the builder's tests.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// CDTAPI includes
#include "DtTest.h"        // Test framework.
#include "Sdi/DtSdiConv.h" // The conversions under test.
#include "cdtapi_sdi.h"    // The parser and the builder.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Runs +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

#define MAX_COUNT 7680 // The number of symbols in a 2160p image line
#define GUARD 64       // The number of bytes after an output that must stay unchanged
#define GUARD_BYTE 0x5A

// Holds the run lengths that are tried. These are every multiple of 4 up to 256, and the
// line lengths of SD, 720p, 1080 and 2160p and of a 2160p link.
static size_t g_Counts[64 + 6];
static int g_NumCounts = 0;

static void MakeCounts(void)
{
    g_NumCounts = 0;
    for (size_t c = 4; c <= 256; c += 4)
        g_Counts[g_NumCounts++] = c;
    const size_t Lines[] = {1440, 2560, 3840, 7680, 1920, 2564};
    for (size_t i = 0; i < sizeof(Lines) / sizeof(Lines[0]); i++)
        g_Counts[g_NumCounts++] = Lines[i];
}

static uint32_t g_Seed = 12345;

static uint8_t RandomByte(void)
{
    g_Seed = g_Seed * 1103515245u + 12345u;
    return (uint8_t)(g_Seed >> 16);
}

static void Fill(uint8_t* Bytes, size_t Size)
{
    for (size_t i = 0; i < Size; i++)
        Bytes[i] = RandomByte();
}

// Fills Symbols with random 10-bit values, which is what the conversions take. The bytes
// of the pixel formats can have any value, so Fill() serves for those.
static void FillSymbols(uint16_t* Symbols, size_t Count)
{
    for (size_t i = 0; i < Count; i++)
        Symbols[i] = (uint16_t)((RandomByte() | RandomByte() << 8) & 0x3FF);
}

// Holds the buffers of one comparison. There is one input and there are two outputs.
// Each output has up to three planes, and a guard follows each plane.
typedef struct Bufs
{
    uint16_t Symbols[MAX_COUNT];
    uint8_t In[3][4 * MAX_COUNT];
    uint8_t Out[2][3][4 * MAX_COUNT + GUARD];
    uint16_t OutSymbols[2][MAX_COUNT + GUARD];
} Bufs;

static void ClearOut(Bufs* B)
{
    memset(B->Out, GUARD_BYTE, sizeof(B->Out));
    memset(B->OutSymbols, GUARD_BYTE, sizeof(B->OutSymbols));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Comparisons +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Compares the bytes that the two versions wrote to each plane, Sizes[p] bytes per plane.
// Also checks that the guards after the planes are unchanged. Returns NULL, or a message
// that says what differs.
static const char* SameBytes(const Bufs* B, const size_t Sizes[3], char* Message,
                             size_t MessageSize)
{
    for (int p = 0; p < 3; p++)
    {
        if (memcmp(B->Out[0][p], B->Out[1][p], Sizes[p]) != 0)
        {
            snprintf(Message, MessageSize, "plane %d differs", p);
            return Message;
        }
        for (size_t g = 0; g < GUARD; g++)
            if (B->Out[1][p][Sizes[p] + g] != GUARD_BYTE)
            {
                snprintf(Message, MessageSize, "plane %d: written past its end", p);
                return Message;
            }
    }
    return NULL;
}

// Compares the first Count symbols that the two versions wrote, and checks that the guard
// after them is unchanged. Returns NULL, or a message that says what differs.
static const char* SameSymbols(const Bufs* B, size_t Count, char* Message,
                               size_t MessageSize)
{
    if (memcmp(B->OutSymbols[0], B->OutSymbols[1], Count * sizeof(uint16_t)) != 0)
    {
        snprintf(Message, MessageSize, "the symbols differ");
        return Message;
    }
    const uint8_t* Guard = (const uint8_t*)(B->OutSymbols[1] + Count);
    for (size_t g = 0; g < GUARD; g++)
        if (Guard[g] != GUARD_BYTE)
        {
            snprintf(Message, MessageSize, "symbols written past their end");
            return Message;
        }
    return NULL;
}

// Runs every conversion of Conv and of the portable version on the same input of Count
// symbols. Returns NULL, or which conversion differs and how.
static const char* Compare(const DtSdiConv* Conv, Bufs* B, size_t Count, char* Message,
                           size_t MessageSize)
{
    const DtSdiConv* C = DtSdiConv_C();
    const DtSdiConv* V[2] = {C, Conv};
    const char* Failure = NULL;
    char Detail[96];

    FillSymbols(B->Symbols, Count);
    for (int p = 0; p < 3; p++)
        Fill(B->In[p], sizeof(B->In[p]));

#define TO(Name, Sizes, ...)                                                             \
    do                                                                                   \
    {                                                                                    \
        ClearOut(B);                                                                     \
        for (int v = 0; v < 2; v++)                                                      \
            V[v]->Name(__VA_ARGS__);                                                     \
        Failure = SameBytes(B, Sizes, Detail, sizeof(Detail));                           \
        if (Failure != NULL)                                                             \
        {                                                                                \
            snprintf(Message, MessageSize, "%s of %zu: %s", #Name, Count, Failure);      \
            return Message;                                                              \
        }                                                                                \
    } while (0)
#define FROM(Name, ...)                                                                  \
    do                                                                                   \
    {                                                                                    \
        ClearOut(B);                                                                     \
        for (int v = 0; v < 2; v++)                                                      \
            V[v]->Name(__VA_ARGS__);                                                     \
        Failure = SameSymbols(B, Count, Detail, sizeof(Detail));                         \
        if (Failure != NULL)                                                             \
        {                                                                                \
            snprintf(Message, MessageSize, "%s of %zu: %s", #Name, Count, Failure);      \
            return Message;                                                              \
        }                                                                                \
    } while (0)

    const size_t Packed[3] = {Count * 10 / 8, 0, 0};
    const size_t Planar10[3] = {Count, Count / 2, Count / 2};
    const size_t Planar8[3] = {Count / 2, Count / 4, Count / 4};
    const size_t Uyvy8[3] = {Count, 0, 0};
    const size_t Y210[3] = {2 * Count, 0, 0};
    const size_t V210[3] = {(Count + 2) / 3 * 4, 0, 0};

    TO(Pack10, Packed, B->Symbols, Count, B->Out[v][0]);
    FROM(Unpack10, B->In[0], Count, B->OutSymbols[v]);
    TO(ToPlanar10, Planar10, B->Symbols, Count, B->Out[v][0], B->Out[v][1], B->Out[v][2]);
    FROM(FromPlanar10, B->In[0], B->In[1], B->In[2], Count, B->OutSymbols[v]);
    TO(ToPlanar8, Planar8, B->Symbols, Count, B->Out[v][0], B->Out[v][1], B->Out[v][2]);
    FROM(FromPlanar8, B->In[0], B->In[1], B->In[2], Count, B->OutSymbols[v]);
    TO(ToUyvy8, Uyvy8, B->Symbols, Count, B->Out[v][0]);
    FROM(FromUyvy8, B->In[0], Count, B->OutSymbols[v]);
    TO(ToY210, Y210, B->Symbols, Count, B->Out[v][0]);
    FROM(FromY210, B->In[0], Count, B->OutSymbols[v]);
    TO(ToV210, V210, B->Symbols, Count, B->Out[v][0]);
    FROM(FromV210, B->In[0], Count, B->OutSymbols[v]);

    // Limit works in place on 10-bit symbols. Each version gets its own copy of the same
    // symbols.
    ClearOut(B);
    for (int v = 0; v < 2; v++)
    {
        for (size_t i = 0; i < Count; i++)
            B->OutSymbols[v][i] = (uint16_t)(B->Symbols[i] & 0x3FF);
        V[v]->Limit(B->OutSymbols[v], Count);
    }
    Failure = SameSymbols(B, Count, Detail, sizeof(Detail));
    if (Failure != NULL)
    {
        snprintf(Message, MessageSize, "Limit of %zu: %s", Count, Failure);
        return Message;
    }
    // Checks Split4k and Join4k, which convert between a 2160p line and its links. The
    // test uses one pair of pixels for every sixteen raw words in the run. The output
    // holds the upper line followed by the lower line.
    const size_t Pixels = Count / 16 * 2;
    if (Pixels > 0)
    {
        ClearOut(B);
        for (int v = 0; v < 2; v++)
            V[v]->Split4k(B->Symbols, Pixels, B->OutSymbols[v],
                          B->OutSymbols[v] + 4 * Pixels);
        Failure = SameSymbols(B, 8 * Pixels, Detail, sizeof(Detail));
        if (Failure == NULL)
        {
            ClearOut(B);
            for (int v = 0; v < 2; v++)
                V[v]->Join4k(B->Symbols, B->Symbols + 4 * Pixels, Pixels,
                             B->OutSymbols[v]);
            Failure = SameSymbols(B, 8 * Pixels, Detail, sizeof(Detail));
        }
        if (Failure != NULL)
        {
            snprintf(Message, MessageSize, "Split4k or Join4k of %zu pixels: %s", Pixels,
                     Failure);
            return Message;
        }
    }
#undef TO
#undef FROM
    return NULL;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Compares every conversion of Conv with the portable version, three times for each run
// length. Returns NULL, or a message that says what differs.
static const char* CheckVersion(const DtSdiConv* Conv, char* Message, size_t MessageSize)
{
    MakeCounts();
    Bufs* B = (Bufs*)malloc(sizeof(Bufs));
    if (B == NULL)
        return "out of memory";
    const char* Failure = NULL;
    for (int r = 0; r < 3 && Failure == NULL; r++)
        for (int i = 0; i < g_NumCounts && Failure == NULL; i++)
            Failure = Compare(Conv, B, g_Counts[i], Message, MessageSize);
    free(B);
    return Failure;
}

// Checks that every SSSE3 conversion gives the same result as the portable one. Skips the
// test where the processor has no SSSE3.
DT_TEST(Ssse3EqualsPortable)
{
    const DtSdiConv* Conv = DtSdiConv_Ssse3();
    if (Conv == NULL)
    {
        printf("    skipped: no SSSE3\n");
        return;
    }
    char Message[160];
    const char* Failure = CheckVersion(Conv, Message, sizeof(Message));
    if (Failure != NULL)
        DT_FAIL("SSSE3: %s", Failure);
}

// Checks that every AVX2 conversion gives the same result as the portable one. Skips the
// test where the processor has no AVX2.
DT_TEST(Avx2EqualsPortable)
{
    const DtSdiConv* Conv = DtSdiConv_Avx2();
    if (Conv == NULL)
    {
        printf("    skipped: no AVX2\n");
        return;
    }
    char Message[160];
    const char* Failure = CheckVersion(Conv, Message, sizeof(Message));
    if (Failure != NULL)
        DT_FAIL("AVX2: %s", Failure);
}

// Checks that the fastest version is one of the three versions.
DT_TEST(BestIsAVersion)
{
    const DtSdiConv* Best = DtSdiConv_Best();
    DT_ASSERT(Best != NULL);
    DT_ASSERT(Best == DtSdiConv_C() || Best == DtSdiConv_Ssse3() ||
              Best == DtSdiConv_Avx2());
}

// Holds an image in one pixel format. Each plane has the smallest stride the format
// allows and is filled with random bytes.
typedef struct Image
{
    DtSdiImage Image;
    size_t Bytes[3];
} Image;

// Allocates the planes of an image of VidStd in Format and fills them with random bytes.
// Returns false if the size is unknown or memory runs out.
static bool Image_Alloc(Image* I, int VidStd, DtSdiPixelFormat Format)
{
    memset(I, 0, sizeof(*I));
    int Width = 0;
    int Height = 0;
    int Strides[3];
    if (DtSdiImage_GetSize(VidStd, Format, &Width, &Height, Strides) != DTAPI_OK)
        return false;
    I->Image.Format = Format;
    I->Image.Fields = DT_SDI_FIELDS_WOVEN;
    for (int p = 0; p < 3 && Strides[p] != 0; p++)
    {
        I->Bytes[p] = (size_t)Strides[p] * (size_t)Height;
        I->Image.Planes[p] = (uint8_t*)malloc(I->Bytes[p]);
        I->Image.Strides[p] = Strides[p];
        if (I->Image.Planes[p] == NULL)
            return false;
        Fill(I->Image.Planes[p], I->Bytes[p]);
    }
    return true;
}

static void Image_Free(Image* I)
{
    for (int p = 0; p < 3; p++)
        free(I->Image.Planes[p]);
    memset(I, 0, sizeof(*I));
}

static bool Image_Same(const Image* A, const Image* B)
{
    for (int p = 0; p < 3; p++)
        if (A->Bytes[p] != 0 &&
            memcmp(A->Image.Planes[p], B->Image.Planes[p], A->Bytes[p]))
            return false;
    return true;
}

// Checks that the parser and the builder give the same result with every version as with
// the portable one, in every pixel format. Each version builds a frame from the same
// random image and parses that frame back. The test compares both the frames and the
// parsed images. It runs in 525i, 720p24, 1080i and 2160p, with 10 and 16 bits a symbol.
// The lines of 720p24 start part-way through a byte.
DT_TEST(ParserAndBuilderAgree)
{
    static const int Stds[] = {DTAPI_VIDSTD_525I59_94, DTAPI_VIDSTD_720P24,
                               DTAPI_VIDSTD_1080I50, DTAPI_VIDSTD_2160P50};
    static const DtSdiPixelFormat Formats[] = {
        DT_SDI_PIXFMT_UYVY_10B, DT_SDI_PIXFMT_UYVY_8B,     DT_SDI_PIXFMT_V210,
        DT_SDI_PIXFMT_Y210,     DT_SDI_PIXFMT_YUV422P_10B, DT_SDI_PIXFMT_YUV422P_8B};
    const DtSdiConv* Versions[3] = {DtSdiConv_C(), DtSdiConv_Ssse3(), DtSdiConv_Avx2()};
    for (size_t s = 0; s < sizeof(Stds) / sizeof(Stds[0]); s++)
    {
        for (int Bits = 10; Bits <= 16; Bits += 6)
        {
            size_t Size = 0;
            DT_ASSERT_OK(DtSdiView_RawFrameSize(Stds[s], Bits, &Size));
            uint8_t* Frames[3] = {(uint8_t*)malloc(Size), (uint8_t*)malloc(Size),
                                  (uint8_t*)malloc(Size)};
            DtSdiView* View = DtSdiView_Alloc();
            DT_ASSERT(Frames[0] != NULL && Frames[1] != NULL && Frames[2] != NULL &&
                      View != NULL);
            for (size_t f = 0; f < sizeof(Formats) / sizeof(Formats[0]); f++)
            {
                Image In;
                Image Out[3];
                DT_ASSERT(Image_Alloc(&In, Stds[s], Formats[f]));
                for (int v = 0; v < 3; v++)
                    DT_ASSERT(Image_Alloc(&Out[v], Stds[s], Formats[f]));
                for (int v = 0; v < 3; v++)
                {
                    if (Versions[v] == NULL)
                        continue;
                    DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
                    DtSdiParser* Parser = DtSdiParser_Alloc();
                    DT_ASSERT(Builder != NULL && Parser != NULL);
                    DtSdiBuilder_UseConv(Builder, Versions[v]);
                    DtSdiParser_UseConv(Parser, Versions[v]);
                    DT_ASSERT_OK(
                        DtSdiView_SetRawFrame(View, Frames[v], Size, Stds[s], Bits));
                    DT_ASSERT_OK(
                        DtSdiBuilder_Build(Builder, View, &In.Image, NULL, NULL));
                    DT_ASSERT_OK(
                        DtSdiParser_Parse(Parser, View, &Out[v].Image, NULL, NULL));
                    DtSdiParser_Free(Parser);
                    DtSdiBuilder_Free(Builder);
                    if (v > 0 && memcmp(Frames[v], Frames[0], Size) != 0)
                        DT_FAIL("std %d, %d bits, format %d: version %d builds another "
                                "frame",
                                Stds[s], Bits, (int)Formats[f], v);
                    if (v > 0 && !Image_Same(&Out[v], &Out[0]))
                        DT_FAIL("std %d, %d bits, format %d: version %d parses another "
                                "image",
                                Stds[s], Bits, (int)Formats[f], v);
                }
                for (int v = 0; v < 3; v++)
                    Image_Free(&Out[v]);
                Image_Free(&In);
            }
            DtSdiView_Free(View);
            for (int v = 0; v < 3; v++)
                free(Frames[v]);
        }
    }
}

DT_TEST_MAIN("SdiConv", DT_RUN(Ssse3EqualsPortable), DT_RUN(Avx2EqualsPortable),
             DT_RUN(BestIsAVersion), DT_RUN(ParserAndBuilderAgree))
