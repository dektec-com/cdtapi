// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# BenchSdi.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Measures how fast the parser takes SDI frames apart and the builder makes them
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Measures how long the parser and the builder take for one frame. Each case parses or
// builds one raw frame over and over for a set time. The benchmark uses only the public
// API, so it measures what a program gets. For each standard, symbol size and pixel
// format, it prints the milliseconds a frame takes and the share of the frame period
// that is. Where the system reports its clock, the blanking table also prints the
// millions of cycles a frame takes. That number carries over to another machine.
//
// 1080p50B is 3G level B through a raw frame of the interface, as an .sdi file holds
// it: a case parses or builds one picture of it, field 1, and so includes taking the
// picture out of the frame or putting it in. Through a frame a channel lends, a picture
// of level B costs what one of 1080p50 does.
//
// The content of the frames and images does not affect the speed, so it is noise. The
// benchmark prints five tables:
// - the parser's image, with a column for each version of the conversions (portable,
//   SSSE3 and AVX2);
// - the parser's walk over the blanking for audio and ancillary packets;
// - the builder, with a column for each version of the conversions;
// - the builder with its line CRCs and checksums;
// - the parser and the builder over a worker pool, with the fastest version.
//
// This is not a test. It asserts nothing about time.
//
// Usage: BenchSdi [seconds per case]

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// CDTAPI includes
#include "BenchCommon.h"   // The clock, the compiler and its flags.
#include "OAL/OsThread.h"  // The monotonic clock.
#include "Sdi/DtSdiConv.h" // The versions of the conversions.
#include "Sdi/DtSdiCrc.h"  // The versions of the line CRC.
#include "cdtapi_sdi.h"    // The parser measured.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct Standard
{
    const char* Name;
    int VidStd;
    double FrameRate;
} Standard;

static const Standard g_Standards[] = {
    {"525i59.94", DTAPI_VIDSTD_525I59_94, 30000.0 / 1001.0},
    {"720p50", DTAPI_VIDSTD_720P50, 50.0},
    {"1080i50", DTAPI_VIDSTD_1080I50, 25.0},
    {"1080p50", DTAPI_VIDSTD_1080P50, 50.0},
    {"1080p50B", DTAPI_VIDSTD_1080P50B, 50.0},
    {"2160p50", DTAPI_VIDSTD_2160P50, 50.0},
    {"2160p60", DTAPI_VIDSTD_2160P60, 60.0},
};
#define NUM_STANDARDS ((int)(sizeof(g_Standards) / sizeof(g_Standards[0])))

typedef struct PixelFormat
{
    const char* Name;
    DtSdiPixelFormat Format;
} PixelFormat;

static const PixelFormat g_Formats[] = {
    {"UYVY 10-bit", DT_SDI_PIXFMT_UYVY_10B},
    {"UYVY 8-bit", DT_SDI_PIXFMT_UYVY_8B},
    {"v210", DT_SDI_PIXFMT_V210},
    {"Y210", DT_SDI_PIXFMT_Y210},
    {"planar 10-bit", DT_SDI_PIXFMT_YUV422P_10B},
    {"planar 8-bit", DT_SDI_PIXFMT_YUV422P_8B},
};
#define NUM_FORMATS ((int)(sizeof(g_Formats) / sizeof(g_Formats[0])))

static const int g_Bits[] = {10, 16};
#define NUM_BITS ((int)(sizeof(g_Bits) / sizeof(g_Bits[0])))

// Names the versions of the conversions: portable, SSSE3 and AVX2. A version that the
// processor or the build lacks is NULL, and its column shows "-".
#define NUM_VERSIONS 3
static const char* g_VersionNames[NUM_VERSIONS] = {"portable", "SSSE3", "AVX2"};

// Fills Versions with the portable, the SSSE3 and the AVX2 version of the conversions.
static void GetVersions(const DtSdiConv* Versions[NUM_VERSIONS])
{
    Versions[0] = DtSdiConv_C();
    Versions[1] = DtSdiConv_Ssse3();
    Versions[2] = DtSdiConv_Avx2();
}

// Prints the header of a table that has a column for each version.
static void PrintVersionHead(const char* First)
{
    printf("%-10s %4s  %-14s", "standard", First, "format");
    for (int v = 0; v < NUM_VERSIONS; v++)
        printf("  %16s", g_VersionNames[v]);
    printf("\n");
}

// Prints one cell of a table: the milliseconds a frame takes and the share of the frame
// period that is. Prints "-" where Ms is negative.
static void PrintCell(double Ms, double FrameRate)
{
    if (Ms < 0.0)
        printf("  %16s", "-");
    else
        printf("  %8.2f %6.1f%%", Ms, Ms * FrameRate / 10.0);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Measuring +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FillNoise -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Fills Bytes with pseudo-random bytes. Every call gives the same bytes.
//
static void FillNoise(uint8_t* Bytes, size_t Size)
{
    uint32_t State = 2110;
    for (size_t i = 0; i < Size; i++)
    {
        State = State * 1664525u + 1013904223u;
        Bytes[i] = (uint8_t)(State >> 24);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AllocImage -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Allocates the planes of Image for VidStd in Format, each with the smallest stride the
// format allows. Returns false when the size is unknown or there is no memory.
//
static bool AllocImage(DtSdiImage* Image, int VidStd, DtSdiPixelFormat Format)
{
    int Height = 0;
    int Strides[3];
    memset(Image, 0, sizeof(*Image));
    if (DtSdiImage_GetSize(VidStd, Format, NULL, &Height, Strides) != DTAPI_OK)
        return false;
    Image->Format = Format;
    Image->Fields = DT_SDI_FIELDS_WOVEN;
    for (int p = 0; p < 3 && Strides[p] != 0; p++)
    {
        Image->Planes[p] = (uint8_t*)malloc((size_t)Strides[p] * (size_t)Height);
        Image->Strides[p] = Strides[p];
        if (Image->Planes[p] == NULL)
            return false;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FreeImage -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Frees the planes that AllocImage() allocated and clears Image.
//
static void FreeImage(DtSdiImage* Image)
{
    for (int p = 0; p < 3; p++)
        free(Image->Planes[p]);
    memset(Image, 0, sizeof(*Image));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Measure -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Parses View over and over for Seconds and returns the milliseconds a frame took. The
// parser fills Image, Audio and Anc, and any of them may be NULL. Returns a negative
// number when the parser refuses the frame. Packets that do not fit in Anc are not a
// failure.
//
static double Measure(DtSdiParser* Parser, const DtSdiView* View, DtSdiImage* Image,
                      DtSdiAudio* Audio, DtSdiAncData* Anc, int Seconds)
{
    const uint64_t Start = OsTime_MonotonicMs();
    uint64_t Elapsed = 0;
    int Frames = 0;

    while (Elapsed < (uint64_t)Seconds * 1000u || Frames == 0)
    {
        const DtapiResult Result = DtSdiParser_Parse(Parser, View, Image, Audio, Anc);
        if (Result != DTAPI_OK && Result != DTAPI_E_BUF_TOO_SMALL)
            return -1.0;
        Frames++;
        Elapsed = OsTime_MonotonicMs() - Start;
    }
    return (double)Elapsed / Frames;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BenchBlanking -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Measures what audio and ancillary data add to the time the parser takes for the image.
// The parser walks over the blanking of every line, without the image. It takes out the
// frame's audio, all 16 channels as PCM, and lists the ancillary packets. A frame of
// noise holds few packets, so this measures the walk and not the copying of packets.
//
static int BenchBlanking(DtSdiParser* Parser, DtSdiView* View, int Seconds, double GHz)
{
    enum
    {
        MAX_SAMPLES = 2048,
        MAX_PACKETS = 1024,
        MAX_WORDS = 65536
    };
    static int32_t Samples[DT_SDI_AUDIO_MAX_CHANNELS][MAX_SAMPLES];
    static DtSdiAncPacket Packets[MAX_PACKETS];
    static uint16_t Words[MAX_WORDS];

    DtSdiAudio Audio;
    memset(&Audio, 0, sizeof(Audio));
    for (int p = 0; p < DT_SDI_AUDIO_MAX_CHANNELS / 2; p++)
        Audio.Formats[p] = DT_SDI_AUDIO_PCM;
    for (int c = 0; c < DT_SDI_AUDIO_MAX_CHANNELS; c++)
    {
        Audio.Channels[c].Samples = Samples[c];
        Audio.Channels[c].Stride = 1;
        Audio.Channels[c].MaxSamples = MAX_SAMPLES;
    }
    DtSdiAncData Anc;
    memset(&Anc, 0, sizeof(Anc));
    Anc.Packets = Packets;
    Anc.MaxPackets = MAX_PACKETS;
    Anc.Words = Words;
    Anc.MaxWords = MAX_WORDS;

    printf(
        "\nThe parser's audio, 16 channels, and ancillary packets, without the image\n");
    printf("%-10s %4s  %-14s %10s %9s %8s\n", "standard", "bits", "", "ms/frame",
           "% period", "Mc");
    for (int s = 0; s < NUM_STANDARDS; s++)
    {
        const Standard* Std = &g_Standards[s];
        size_t Size = 0;
        DtSdiView_RawFrameSize(Std->VidStd, 10, &Size);
        uint8_t* Frame = (uint8_t*)malloc(Size);
        if (Frame == NULL)
        {
            fprintf(stderr, "Out of memory\n");
            return 1;
        }
        FillNoise(Frame, Size);
        DtSdiView_SetRawFrame(View, Frame, Size, Std->VidStd, 10);
        const double Ms = Measure(Parser, View, NULL, &Audio, &Anc, Seconds);
        free(Frame);
        if (Ms < 0.0)
        {
            fprintf(stderr, "%s: the parser refused the frame\n", Std->Name);
            return 1;
        }
        printf("%-10s %4d  %-14s %10.2f %9.1f %8.1f\n", Std->Name, 10, "", Ms,
               Ms * Std->FrameRate / 10.0, Ms * GHz);
        fflush(stdout);
    }
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BenchWorkers -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Measures how much a worker pool speeds up the parser's image and the builder. The pool
// has 8 of the library's own threads, and the work is split into 1, 2, 4 and 8 pieces.
// The formats are v210 and planar 10-bit, which a program most likely asks for.
//
static int BenchWorkers(DtSdiView* View, int Seconds, double GHz)
{
    static const int Pieces[] = {1, 2, 4, 8};
    static const DtSdiPixelFormat Formats[] = {DT_SDI_PIXFMT_V210,
                                               DT_SDI_PIXFMT_YUV422P_10B};
    static const char* FormatNames[] = {"v210", "planar 10-bit"};
    DtWorkerPool* Pool = DtWorkerPool_Alloc();
    if (Pool == NULL || DtWorkerPool_StartThreads(Pool, 8) != DTAPI_OK)
    {
        fprintf(stderr, "No worker pool\n");
        DtWorkerPool_Free(Pool);
        return 1;
    }
    printf("\nThe parser's image and the builder over a worker pool, ms/frame and "
           "%% period\n");
    printf("%-10s %-8s %-14s", "standard", "", "format");
    for (size_t p = 0; p < sizeof(Pieces) / sizeof(Pieces[0]); p++)
        printf("  %9d %s", Pieces[p], Pieces[p] == 1 ? "piece " : "pieces");
    printf("\n");

    int Status = 0;
    for (int s = 0; s < NUM_STANDARDS && Status == 0; s++)
    {
        const Standard* Std = &g_Standards[s];
        size_t Size = 0;
        DtSdiView_RawFrameSize(Std->VidStd, 10, &Size);
        uint8_t* Frame = (uint8_t*)malloc(Size);
        if (Frame == NULL)
        {
            Status = 1;
            break;
        }
        FillNoise(Frame, Size);
        DtSdiView_SetRawFrame(View, Frame, Size, Std->VidStd, 10);
        for (size_t f = 0; f < sizeof(Formats) / sizeof(Formats[0]) && Status == 0; f++)
        {
            DtSdiImage Image;
            if (!AllocImage(&Image, Std->VidStd, Formats[f]))
            {
                FreeImage(&Image);
                Status = 1;
                break;
            }
            printf("%-10s %-8s %-14s", Std->Name, "parser", FormatNames[f]);
            for (size_t p = 0; p < sizeof(Pieces) / sizeof(Pieces[0]); p++)
            {
                DtSdiParser* Parser = DtSdiParser_Alloc();
                if (Parser == NULL ||
                    DtSdiParser_SetWorkerPool(Parser, Pool, Pieces[p]) != DTAPI_OK)
                {
                    DtSdiParser_Free(Parser);
                    Status = 1;
                    break;
                }
                const double Ms = Measure(Parser, View, &Image, NULL, NULL, Seconds);
                DtSdiParser_Free(Parser);
                printf("  %7.2f %5.1f%%", Ms, Ms * Std->FrameRate / 10.0);
            }
            printf("\n%-10s %-8s %-14s", Std->Name, "builder", FormatNames[f]);
            for (size_t p = 0; p < sizeof(Pieces) / sizeof(Pieces[0]) && Status == 0; p++)
            {
                DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
                if (Builder == NULL ||
                    DtSdiBuilder_SetWorkerPool(Builder, Pool, Pieces[p]) != DTAPI_OK)
                {
                    DtSdiBuilder_Free(Builder);
                    Status = 1;
                    break;
                }
                const uint64_t Start = OsTime_MonotonicMs();
                uint64_t Elapsed = 0;
                int Frames = 0;
                while (Elapsed < (uint64_t)Seconds * 1000u || Frames == 0)
                {
                    DtSdiBuilder_Build(Builder, View, &Image, NULL, NULL);
                    Frames++;
                    Elapsed = OsTime_MonotonicMs() - Start;
                }
                DtSdiBuilder_Free(Builder);
                const double Ms = (double)Elapsed / Frames;
                printf("  %7.2f %5.1f%%", Ms, Ms * Std->FrameRate / 10.0);
            }
            printf("\n");
            fflush(stdout);
            FreeImage(&Image);
        }
        free(Frame);
    }
    (void)GHz;
    DtWorkerPool_Free(Pool);
    return Status;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BenchBuilder -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Measures how long the builder takes for a whole frame of 10-bit symbols, with each
// version of the conversions. The builder starts from an image of noise in each format.
// The frame includes the timing references, the line numbers and the payload ID. The
// builder leaves the line CRCs and checksums to the transmitter, which is the default.
//
static int BenchBuilder(DtSdiView* View, int Seconds, double GHz)
{
    (void)GHz;
    DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
    if (Builder == NULL)
    {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }
    printf("\nThe builder's image and raster, one thread, per version: ms/frame and "
           "%% period\n");
    PrintVersionHead("bits");
    const DtSdiConv* Versions[NUM_VERSIONS];
    GetVersions(Versions);

    int Status = 0;
    for (int s = 0; s < NUM_STANDARDS && Status == 0; s++)
    {
        const Standard* Std = &g_Standards[s];
        size_t Size = 0;
        DtSdiView_RawFrameSize(Std->VidStd, 10, &Size);
        uint8_t* Frame = (uint8_t*)malloc(Size);
        if (Frame == NULL)
        {
            fprintf(stderr, "Out of memory\n");
            Status = 1;
            break;
        }
        DtSdiView_SetRawFrame(View, Frame, Size, Std->VidStd, 10);

        for (int f = 0; f < NUM_FORMATS && Status == 0; f++)
        {
            DtSdiImage Image;
            if (!AllocImage(&Image, Std->VidStd, g_Formats[f].Format))
            {
                fprintf(stderr, "Out of memory\n");
                FreeImage(&Image);
                Status = 1;
                break;
            }
            int Height = 0;
            DtSdiImage_GetSize(Std->VidStd, g_Formats[f].Format, NULL, &Height, NULL);
            for (int p = 0; p < 3 && Image.Planes[p] != NULL; p++)
                FillNoise(Image.Planes[p], (size_t)Image.Strides[p] * (size_t)Height);
            printf("%-10s %4d  %-14s", Std->Name, 10, g_Formats[f].Name);
            for (int v = 0; v < NUM_VERSIONS; v++)
            {
                double Ms = -1.0;
                if (Versions[v] != NULL)
                {
                    DtSdiBuilder_UseConv(Builder, Versions[v]);
                    const uint64_t Start = OsTime_MonotonicMs();
                    uint64_t Elapsed = 0;
                    int Frames = 0;
                    while (Elapsed < (uint64_t)Seconds * 1000u || Frames == 0)
                    {
                        DtSdiBuilder_Build(Builder, View, &Image, NULL, NULL);
                        Frames++;
                        Elapsed = OsTime_MonotonicMs() - Start;
                    }
                    Ms = (double)Elapsed / Frames;
                }
                PrintCell(Ms, Std->FrameRate);
            }
            printf("\n");
            fflush(stdout);
            FreeImage(&Image);
        }
        free(Frame);
    }
    DtSdiBuilder_Free(Builder);
    return Status;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BenchChecksums -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Measures how much the line CRCs and packet checksums add to the builder's time. Builds
// from v210 in one thread with the fastest conversions, in each standard that has a line
// CRC, four ways: without CRCs, with the table, and with PCLMULQDQ packing with SSSE3
// or with AVX2.
//
static int BenchChecksums(DtSdiView* View, int Seconds)
{
    DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
    if (Builder == NULL)
    {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }
    static const char* Columns[4] = {"no CRC", "CRC by table", "PCLMULQDQ, SSSE3",
                                     "PCLMULQDQ, AVX2"};
    const DtSdiCrcFunc Crcs[4] = {NULL, DtSdiCrc_Streams, DtSdiCrc_Clmul(),
                                  DtSdiCrc_Avx2()};
    printf("\nThe builder from v210 with its line CRCs and checksums, one thread: "
           "ms/frame and %% period\n");
    printf("%-10s %4s  %-14s", "standard", "bits", "format");
    for (int c = 0; c < 4; c++)
        printf("  %16s", Columns[c]);
    printf("\n");

    int Status = 0;
    for (int s = 0; s < NUM_STANDARDS && Status == 0; s++)
    {
        const Standard* Std = &g_Standards[s];
        if (Std->VidStd == DTAPI_VIDSTD_525I59_94)
            continue; // SD has no line CRC
        size_t Size = 0;
        DtSdiView_RawFrameSize(Std->VidStd, 10, &Size);
        uint8_t* Frame = (uint8_t*)malloc(Size);
        DtSdiImage Image;
        if (Frame == NULL || !AllocImage(&Image, Std->VidStd, DT_SDI_PIXFMT_V210))
        {
            fprintf(stderr, "Out of memory\n");
            free(Frame);
            Status = 1;
            break;
        }
        int Height = 0;
        DtSdiImage_GetSize(Std->VidStd, DT_SDI_PIXFMT_V210, NULL, &Height, NULL);
        FillNoise(Image.Planes[0], (size_t)Image.Strides[0] * (size_t)Height);
        DtSdiView_SetRawFrame(View, Frame, Size, Std->VidStd, 10);
        printf("%-10s %4d  %-14s", Std->Name, 10, "v210");
        for (int c = 0; c < 4; c++)
        {
            double Ms = -1.0;
            if (c == 0 || Crcs[c] != NULL)
            {
                DtSdiBuilder_SetChecksums(Builder, c != 0);
                if (Crcs[c] != NULL)
                    DtSdiBuilder_UseCrc(Builder, Crcs[c]);
                const uint64_t Start = OsTime_MonotonicMs();
                uint64_t Elapsed = 0;
                int Frames = 0;
                while (Elapsed < (uint64_t)Seconds * 1000u || Frames == 0)
                {
                    DtSdiBuilder_Build(Builder, View, &Image, NULL, NULL);
                    Frames++;
                    Elapsed = OsTime_MonotonicMs() - Start;
                }
                Ms = (double)Elapsed / Frames;
            }
            PrintCell(Ms, Std->FrameRate);
        }
        printf("\n");
        fflush(stdout);
        FreeImage(&Image);
        free(Frame);
    }
    DtSdiBuilder_Free(Builder);
    return Status;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

int main(int Argc, char** Argv)
{
    const int Seconds = Argc > 1 ? atoi(Argv[1]) : 1;
    if (Seconds <= 0)
    {
        fprintf(stderr, "Usage: BenchSdi [seconds per case]\n");
        return 1;
    }

    DtSdiParser* Parser = DtSdiParser_Alloc();
    DtSdiView* View = DtSdiView_Alloc();
    if (Parser == NULL || View == NULL)
    {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }

    BenchPrintCompiler();
    const double GHz = BenchClockGHz();
    if (GHz > 0.0)
        printf("Clock %.2f GHz; Mc is millions of cycles a frame\n", GHz);
    printf("\nThe parser's image, one thread, per version: ms/frame and %% period\n");
    PrintVersionHead("bits");
    const DtSdiConv* Versions[NUM_VERSIONS];
    GetVersions(Versions);

    int Status = 0;
    for (int s = 0; s < NUM_STANDARDS && Status == 0; s++)
    {
        const Standard* Std = &g_Standards[s];
        for (int b = 0; b < NUM_BITS && Status == 0; b++)
        {
            size_t Size = 0;
            DtSdiView_RawFrameSize(Std->VidStd, g_Bits[b], &Size);
            uint8_t* Frame = (uint8_t*)malloc(Size);
            if (Frame == NULL)
            {
                fprintf(stderr, "Out of memory\n");
                Status = 1;
                break;
            }
            FillNoise(Frame, Size);
            DtSdiView_SetRawFrame(View, Frame, Size, Std->VidStd, g_Bits[b]);

            for (int f = 0; f < NUM_FORMATS && Status == 0; f++)
            {
                DtSdiImage Image;
                if (!AllocImage(&Image, Std->VidStd, g_Formats[f].Format))
                {
                    fprintf(stderr, "Out of memory\n");
                    FreeImage(&Image);
                    Status = 1;
                    break;
                }
                printf("%-10s %4d  %-14s", Std->Name, g_Bits[b], g_Formats[f].Name);
                for (int v = 0; v < NUM_VERSIONS; v++)
                {
                    double Ms = -1.0;
                    if (Versions[v] != NULL)
                    {
                        DtSdiParser_UseConv(Parser, Versions[v]);
                        Ms = Measure(Parser, View, &Image, NULL, NULL, Seconds);
                    }
                    PrintCell(Ms, Std->FrameRate);
                }
                printf("\n");
                fflush(stdout);
                FreeImage(&Image);
            }
            free(Frame);
        }
    }
    DtSdiParser_UseConv(Parser, DtSdiConv_Best());

    if (Status == 0)
        Status = BenchBlanking(Parser, View, Seconds, GHz);
    if (Status == 0)
        Status = BenchBuilder(View, Seconds, GHz);
    if (Status == 0)
        Status = BenchChecksums(View, Seconds);
    if (Status == 0)
        Status = BenchWorkers(View, Seconds, GHz);
    DtSdiView_Free(View);
    DtSdiParser_Free(Parser);
    return Status;
}
