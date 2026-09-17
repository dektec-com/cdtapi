// #*#*#*#*#*#*#*#*#*#*#*#*#* BenchAvPixConv.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Measures the throughput of each pixel conversion, portable and SSSE3
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Converts the rows of a 3840x2160 frame over and over for about a second per conversion
// and prints megabytes of input per second. Not a test: it asserts nothing about time.
//
// Usage: BenchAvPixConv [seconds per conversion]

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <stdlib.h>

// CDtapiLite includes
#include "AvFifo/DtAvPixConv.h" // Conversions measured.
#include "OAL/OsThread.h"       // The monotonic clock.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define WIDTH 3840
#define HEIGHT 2160
#define PGROUPS_PER_ROW (WIDTH / 2)

typedef enum Which
{
    PG10_TO_UYVY10,
    PG10_TO_UYVY8,
    UYVY10_TO_PG10,
    UYVY8_TO_YUV422P,
    NUM_WHICH
} Which;

static const char* const Names[NUM_WHICH] = {
    "pgroup 10 to UYVY 10", "pgroup 10 to UYVY 8", "UYVY 10 to pgroup 10",
    "UYVY 8 to YUV 4:2:2p"};

// Converts every row of a frame once.
static void ConvertFrame(const DtAvPixConv* Conv, Which Kind, const uint8_t* Src,
                         uint8_t* Dst)
{
    for (size_t Row = 0; Row < HEIGHT; Row++)
    {
        size_t In = Row * PGROUPS_PER_ROW;
        switch (Kind)
        {
        case PG10_TO_UYVY10:
            Conv->Pg10ToUyvy10(Src + In * 5, Dst + In * 5, PGROUPS_PER_ROW);
            break;
        case PG10_TO_UYVY8:
            Conv->Pg10ToUyvy8(Src + In * 5, Dst + In * 4, PGROUPS_PER_ROW);
            break;
        case UYVY10_TO_PG10:
            Conv->Uyvy10ToPg10(Src + In * 5, Dst + In * 5, PGROUPS_PER_ROW);
            break;
        default:
        {
            size_t Plane = (size_t)HEIGHT * PGROUPS_PER_ROW;
            Conv->Uyvy8ToYuv422p(Src + In * 4, PGROUPS_PER_ROW, Dst + In * 2,
                                 Dst + Plane * 2 + In, Dst + Plane * 3 + In);
            break;
        }
        }
    }
}

// Megabytes of input per second of one conversion.
static double Measure(const DtAvPixConv* Conv, Which Kind, const uint8_t* Src,
                      uint8_t* Dst, int Seconds)
{
    size_t InputBytes =
        (size_t)HEIGHT * PGROUPS_PER_ROW * (Kind == UYVY8_TO_YUV422P ? 4u : 5u);
    uint64_t Start = OsTime_MonotonicMs();
    uint64_t Elapsed = 0;
    int Frames = 0;
    while (Elapsed < (uint64_t)Seconds * 1000u)
    {
        ConvertFrame(Conv, Kind, Src, Dst);
        Frames++;
        Elapsed = OsTime_MonotonicMs() - Start;
    }
    return (double)Frames * (double)InputBytes / 1e6 / ((double)Elapsed / 1000.0);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

int main(int Argc, char** Argv)
{
    int Seconds = Argc > 1 ? atoi(Argv[1]) : 1;
    size_t Size = (size_t)HEIGHT * PGROUPS_PER_ROW * 5;
    uint8_t* Src = (uint8_t*)malloc(Size);
    uint8_t* Dst = (uint8_t*)malloc(Size);
    if (Src == NULL || Dst == NULL || Seconds <= 0)
    {
        fprintf(stderr, "Usage: BenchAvPixConv [seconds per conversion]\n");
        return 1;
    }
    uint32_t State = 2110;
    for (size_t i = 0; i < Size; i++)
    {
        State = State * 1664525u + 1013904223u;
        Src[i] = (uint8_t)(State >> 24);
    }

    const DtAvPixConv* Ssse3 = DtAvPixConv_Ssse3();
    printf("%-24s %14s %14s\n", "MB/s of input, 2160p", "portable C",
           Ssse3 != NULL ? "SSSE3" : "no SSSE3");
    for (int Kind = 0; Kind < NUM_WHICH; Kind++)
    {
        double C = Measure(DtAvPixConv_C(), (Which)Kind, Src, Dst, Seconds);
        printf("%-24s %14.0f", Names[Kind], C);
        if (Ssse3 != NULL)
            printf(" %14.0f", Measure(Ssse3, (Which)Kind, Src, Dst, Seconds));
        printf("\n");
    }
    free(Src);
    free(Dst);
    return 0;
}
