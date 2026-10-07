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
// The frame's content does not matter to the image's speed, so it is noise. Plan 0032
// adds the parser's audio and ancillary data, the builder, the vector versions and the
// worker pool as they come.
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
// Parses View into Image for Seconds, and returns the milliseconds a frame took, or a
// negative number when the parser refused.
//
static double Measure(DtSdiParser* Parser, const DtSdiView* View, DtSdiImage* Image,
                      int Seconds)
{
    const uint64_t Start = OsTime_MonotonicMs();
    uint64_t Elapsed = 0;
    int Frames = 0;

    while (Elapsed < (uint64_t)Seconds * 1000u || Frames == 0)
    {
        if (DtSdiParser_Parse(Parser, View, Image, NULL, NULL) != DTAPI_OK)
            return -1.0;
        Frames++;
        Elapsed = OsTime_MonotonicMs() - Start;
    }
    return (double)Elapsed / Frames;
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
                const double Ms = Measure(Parser, View, &Image, Seconds);
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

    DtSdiView_Free(View);
    DtSdiParser_Free(Parser);
    return Status;
}
