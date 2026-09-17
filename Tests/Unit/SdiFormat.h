// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# SdiFormat.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - The SDI line timing of every video standard, and what a receiver reports
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

// CDtapiLite includes
#include "CDtapiLite.h"         // DTAPI_VIDSTD_ codes.
#include "Video/DtFrameProps.h" // DT_SDIRATE_ codes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Formats +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The fields are those of SdiFormats.inc, which says where each number comes from. The
// numbers are the standards', not DTAPI's, so that the library is checked against them
// rather than against a copy of its own tables.
//

#define SDI_SCAN_P 0 // Progressive
#define SDI_SCAN_I 1 // Interlaced
#define SDI_SCAN_S 2 // PsF

typedef struct SdiFormat
{
    const char* Name;
    int VidStd;
    int Lines;
    int LinesF1;
    int ActiveLines;
    int Samples;
    int Active;
    int FpsNum;
    int FpsDen;
    int Scan;
    int Payload;
    int SdiRate;
    int NoVpid;
} SdiFormat;

// clang-format off
#define F(Name, Lines, LinesF1, ActiveLines, Samples, Active, FpsNum, FpsDen, Scan,      \
          Payload, Rate, NoVpid)                                                         \
    {#Name, DTAPI_VIDSTD_##Name, Lines, LinesF1, ActiveLines, Samples, Active, FpsNum,   \
     FpsDen, SDI_SCAN_##Scan, Payload, DT_SDIRATE_##Rate, DTAPI_VIDSTD_##NoVpid},
// clang-format on

static const SdiFormat g_SdiFormats[] = {
#include "SdiFormats.inc"
};

#undef F

#define SDI_FORMAT_COUNT ((int)(sizeof(g_SdiFormats) / sizeof(g_SdiFormats[0])))

// DT_ASSERT_EQ for a check inside a loop over the formats: the message names the format.
// Requires DtTest.h.
#define SDI_ASSERT_EQ(Format, Actual, Expected)                                          \
    do                                                                                   \
    {                                                                                    \
        int64_t SdiA = (int64_t)(Actual);                                                \
        int64_t SdiE = (int64_t)(Expected);                                              \
        if (SdiA != SdiE)                                                                \
            DT_FAIL("%s: %s: expected %" PRId64 ", got %" PRId64, (Format)->Name,        \
                    #Actual, SdiE, SdiA);                                                \
    } while (0)

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Receiver view +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Lines in the second field; 0 for a progressive frame.
static inline int SdiFormat_LinesF2(const SdiFormat* Format)
{
    return Format->Lines - Format->LinesF1;
}

// Symbols of horizontal blanking per line, EAV and SAV included, luma and chroma.
static inline int SdiFormat_HancSymbols(const SdiFormat* Format)
{
    return 2 * (Format->Samples - Format->Active);
}

// Symbols of picture per line, luma and chroma.
static inline int SdiFormat_VancSymbols(const SdiFormat* Format)
{
    return 2 * Format->Active;
}

static inline double SdiFormat_Fps(const SdiFormat* Format)
{
    return (double)Format->FpsNum / Format->FpsDen;
}

// Whether the format is carried as 3G level B, on one link or on four.
static inline bool SdiFormat_IsLevelB(const SdiFormat* Format)
{
    return Format->Payload == 0x8A || Format->Payload == 0x98;
}

// The SMPTE ST 352 picture rate code, byte 2 bits 3..0, of the format's frame rate.
static inline uint32_t SdiFormat_RateCode(const SdiFormat* Format)
{
    switch (Format->FpsNum)
    {
    case 24000:
        return 0x2;
    case 24:
        return 0x3;
    case 25:
        return 0x5;
    case 30000:
        return 0x6;
    case 30:
        return 0x7;
    case 50:
        return 0x9;
    case 60000:
        return 0xA;
    default: // 60
        return 0xB;
    }
}

// The VPID a transmitter puts on the format: the payload identifier; the picture rate
// with bit 7 set for a progressive transport and bit 6 for a progressive picture; and for
// HD a 16:9 aspect ratio, byte 3 bit 7.
static inline uint32_t SdiFormat_Vpid(const SdiFormat* Format)
{
    uint32_t Byte2 = SdiFormat_RateCode(Format);

    if (Format->Scan == SDI_SCAN_P)
        Byte2 |= 0x80;
    if (Format->Scan != SDI_SCAN_I)
        Byte2 |= 0x40;

    return (uint32_t)Format->Payload | (Byte2 << 8) |
           (Format->Lines > 625 ? 0x800000u : 0u);
}
