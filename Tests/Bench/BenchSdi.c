// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# BenchSdi.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Measures how fast the parser takes SDI frames apart
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Parses one raw frame over and over for a while per case, through the public API only,
// so that what is measured is what a program gets. For each standard, symbol size and
// pixel format it prints the milliseconds a frame takes, how much of the frame period
// that is, and the millions of cycles a frame where the system says what clock it runs
// at: the number that carries to another machine.
//
// The frame's content does not matter to the image's speed, so it is noise. A second
// table measures the walk over the blanking for audio and ancillary packets. Plan 0032
// adds the builder, the vector versions and the worker pool as they come.
//
// Not a test: it asserts nothing about time.
//
// Usage: BenchSdi [seconds per case]

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// CDTAPI includes
#include "BenchCommon.h"  // The clock, the compiler and its flags.
#include "OAL/OsThread.h" // The monotonic clock.
#include "cdtapi_sdi.h"   // The parser measured.

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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Measuring +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FillNoise -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
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
// Gives Image planes with the least strides for VidStd in Format. Returns false when
// there is no memory.
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
static void FreeImage(DtSdiImage* Image)
{
    for (int p = 0; p < 3; p++)
        free(Image->Planes[p]);
    memset(Image, 0, sizeof(*Image));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Measure -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Parses View into Image, Audio and Anc, any of which may be NULL, for Seconds, and
// returns the milliseconds a frame took, or a negative number when the parser refused.
// Packets that do not fit in Anc are no failure.
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
// Measures the walk over every line's blanking that takes out a frame's audio, all 16
// channels as PCM, and lists its ancillary packets, without the image: the cost of audio
// and ancillary data on top of the image's. A frame of noise holds few packets, so it is
// the walk that is measured, not the copying of packets.
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
    printf("\nThe parser's image, portable version, one thread\n");
    printf("%-10s %4s  %-14s %10s %9s %8s\n", "standard", "bits", "format", "ms/frame",
           "% period", "Mc");

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

            for (int f = 0; f < NUM_FORMATS; f++)
            {
                DtSdiImage Image;
                if (!AllocImage(&Image, Std->VidStd, g_Formats[f].Format))
                {
                    fprintf(stderr, "Out of memory\n");
                    FreeImage(&Image);
                    Status = 1;
                    break;
                }
                const double Ms = Measure(Parser, View, &Image, NULL, NULL, Seconds);
                FreeImage(&Image);
                if (Ms < 0.0)
                {
                    fprintf(stderr, "%s: the parser refused the frame\n", Std->Name);
                    Status = 1;
                    break;
                }
                printf("%-10s %4d  %-14s %10.2f %9.1f %8.1f\n", Std->Name, g_Bits[b],
                       g_Formats[f].Name, Ms, Ms * Std->FrameRate / 10.0, Ms * GHz);
                fflush(stdout);
            }
            free(Frame);
        }
    }

    if (Status == 0)
        Status = BenchBlanking(Parser, View, Seconds, GHz);
    DtSdiView_Free(View);
    DtSdiParser_Free(Parser);
    return Status;
}
