// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtFrameProps.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - The geometry of an SDI frame per video standard - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <string.h>

// CDtapiLite includes
#include "CDtapiLite.h"   // DTAPI_VIDSTD_ codes.
#include "DtFrameProps.h" // Interface being implemented.
#include "DtVidStd.h"     // 4K classification and the VPID route of deduction.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame rates +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtVidStdFps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// MxVidStdPropsSdi::Fps. An interlaced standard is named after its field rate and has
// half that frame rate: 1080i50 is 25 frames per second.
//
void DtVidStdFps(int VidStd, int* Num, int* Den)
{
    *Num = 0;
    *Den = 1;

    switch (VidStd)
    {
    case DTAPI_VIDSTD_720P23_98:
    case DTAPI_VIDSTD_1080P23_98:
    case DTAPI_VIDSTD_1080PSF23_98:
    case DTAPI_VIDSTD_2160P23_98:
        *Num = 24000;
        *Den = 1001;
        break;
    case DTAPI_VIDSTD_720P24:
    case DTAPI_VIDSTD_1080P24:
    case DTAPI_VIDSTD_1080PSF24:
    case DTAPI_VIDSTD_2160P24:
        *Num = 24;
        break;
    case DTAPI_VIDSTD_625I50:
    case DTAPI_VIDSTD_1080I50:
    case DTAPI_VIDSTD_720P25:
    case DTAPI_VIDSTD_1080P25:
    case DTAPI_VIDSTD_1080PSF25:
    case DTAPI_VIDSTD_2160P25:
        *Num = 25;
        break;
    case DTAPI_VIDSTD_525I59_94:
    case DTAPI_VIDSTD_1080I59_94:
    case DTAPI_VIDSTD_720P29_97:
    case DTAPI_VIDSTD_1080P29_97:
    case DTAPI_VIDSTD_1080PSF29_97:
    case DTAPI_VIDSTD_2160P29_97:
        *Num = 30000;
        *Den = 1001;
        break;
    case DTAPI_VIDSTD_720P30:
    case DTAPI_VIDSTD_1080I60:
    case DTAPI_VIDSTD_1080P30:
    case DTAPI_VIDSTD_1080PSF30:
    case DTAPI_VIDSTD_2160P30:
        *Num = 30;
        break;
    case DTAPI_VIDSTD_720P50:
    case DTAPI_VIDSTD_1080P50:
    case DTAPI_VIDSTD_1080P50B:
    case DTAPI_VIDSTD_2160P50:
    case DTAPI_VIDSTD_2160P50B:
        *Num = 50;
        break;
    case DTAPI_VIDSTD_720P59_94:
    case DTAPI_VIDSTD_1080P59_94:
    case DTAPI_VIDSTD_1080P59_94B:
    case DTAPI_VIDSTD_2160P59_94:
    case DTAPI_VIDSTD_2160P59_94B:
        *Num = 60000;
        *Den = 1001;
        break;
    case DTAPI_VIDSTD_720P60:
    case DTAPI_VIDSTD_1080P60:
    case DTAPI_VIDSTD_1080P60B:
    case DTAPI_VIDSTD_2160P60:
    case DTAPI_VIDSTD_2160P60B:
        *Num = 60;
        break;
    default:
        break;
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame geometry +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetField -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void SetField(DtFieldProps* Field, int Start, int End, int VidStart, int VidEnd,
                     int Switching)
{
    Field->StartLine = Start;
    Field->EndLine = End;
    Field->VidStartLine = VidStart;
    Field->VidEndLine = VidEnd;
    Field->SwitchingLine = Switching;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- -HdHancByRate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The HANC width of a 1125-line frame at 30, 25 or 24 frames per second, or, doubled, 60
// or 50; the two are the same line timing.
//
static int HdHancByRate(int FpsNum)
{
    switch (FpsNum)
    {
    case 30:
    case 30000:
    case 60:
    case 60000:
        return 268 * 2;
    case 25:
    case 50:
        return 708 * 2;
    default: // 24, 24000
        return 818 * 2;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFramePropsInit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// MxFramePropsSdi::Init, for the default variant of each standard. The line numbers and
// widths are DTAPI's (MxVideoProps.cpp:1252-1460).
//
bool DtFramePropsInit(DtFrameProps* Props, int VidStd)
{
    memset(Props, 0, sizeof(*Props));
    Props->VidStd = DTAPI_VIDSTD_UNKNOWN;

    DtVidStdFps(VidStd, &Props->FpsNum, &Props->FpsDen);
    if (Props->FpsNum == 0)
        return false;

    switch (VidStd)
    {
    case DTAPI_VIDSTD_525I59_94:
        Props->NumFields = 2;
        SetField(&Props->Fields[0], 1, 262, 17, 260, 7);
        SetField(&Props->Fields[1], 263, 525, 280, 522, 270);
        Props->LineNumSymEav = 4;
        Props->LineNumSymHanc = 268;
        Props->LineNumSymSav = 4;
        Props->LineNumSymVanc = 720 * 2;
        break;

    case DTAPI_VIDSTD_625I50:
        Props->NumFields = 2;
        SetField(&Props->Fields[0], 1, 312, 23, 310, 6);
        SetField(&Props->Fields[1], 313, 625, 336, 623, 319);
        Props->LineNumSymEav = 4;
        Props->LineNumSymHanc = 280;
        Props->LineNumSymSav = 4;
        Props->LineNumSymVanc = 720 * 2;
        break;

    case DTAPI_VIDSTD_1080P30:
    case DTAPI_VIDSTD_1080P29_97:
    case DTAPI_VIDSTD_1080P25:
    case DTAPI_VIDSTD_1080P24:
    case DTAPI_VIDSTD_1080P23_98:
    case DTAPI_VIDSTD_2160P30:
    case DTAPI_VIDSTD_2160P29_97:
    case DTAPI_VIDSTD_2160P25:
    case DTAPI_VIDSTD_2160P24:
    case DTAPI_VIDSTD_2160P23_98:
    case DTAPI_VIDSTD_1080P60:
    case DTAPI_VIDSTD_1080P60B:
    case DTAPI_VIDSTD_1080P59_94:
    case DTAPI_VIDSTD_1080P59_94B:
    case DTAPI_VIDSTD_1080P50:
    case DTAPI_VIDSTD_1080P50B:
    case DTAPI_VIDSTD_2160P60:
    case DTAPI_VIDSTD_2160P60B:
    case DTAPI_VIDSTD_2160P59_94:
    case DTAPI_VIDSTD_2160P59_94B:
    case DTAPI_VIDSTD_2160P50:
    case DTAPI_VIDSTD_2160P50B:
        Props->NumFields = 1;
        SetField(&Props->Fields[0], 1, 1125, 42, 1121, 7);
        Props->LineNumSymEav = 8 * 2;
        Props->LineNumSymHanc = HdHancByRate(Props->FpsNum);
        Props->LineNumSymSav = 4 * 2;
        Props->LineNumSymVanc = 1920 * 2;
        break;

    case DTAPI_VIDSTD_1080I60:
    case DTAPI_VIDSTD_1080I59_94:
    case DTAPI_VIDSTD_1080I50:
    case DTAPI_VIDSTD_1080PSF30:
    case DTAPI_VIDSTD_1080PSF29_97:
    case DTAPI_VIDSTD_1080PSF25:
    case DTAPI_VIDSTD_1080PSF24:
    case DTAPI_VIDSTD_1080PSF23_98:
        Props->NumFields = 2;
        SetField(&Props->Fields[0], 1, 563, 21, 560, 7);
        SetField(&Props->Fields[1], 564, 1125, 584, 1123, 569);
        Props->LineNumSymEav = 8 * 2;
        Props->LineNumSymHanc = HdHancByRate(Props->FpsNum);
        Props->LineNumSymSav = 4 * 2;
        Props->LineNumSymVanc = 1920 * 2;
        break;

    default: // The 720p standards, the only ones left with a frame rate.
        Props->NumFields = 1;
        SetField(&Props->Fields[0], 1, 750, 26, 745, 7);
        Props->LineNumSymEav = 8 * 2;
        switch (Props->FpsNum)
        {
        case 60:
        case 60000:
            Props->LineNumSymHanc = 358 * 2;
            break;
        case 50:
            Props->LineNumSymHanc = 688 * 2;
            break;
        case 30:
        case 30000:
            Props->LineNumSymHanc = 2008 * 2;
            break;
        case 25:
            Props->LineNumSymHanc = 2668 * 2;
            break;
        default: // 24, 24000
            Props->LineNumSymHanc = 2833 * 2;
            break;
        }
        Props->LineNumSymSav = 4 * 2;
        Props->LineNumSymVanc = 1280 * 2;
        break;
    }

    Props->VidStd = VidStd;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFramePropsNumLines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtFramePropsNumLines(const DtFrameProps* Props)
{
    int Lines = 0;
    int i;

    for (i = 0; i < Props->NumFields; i++)
        Lines += Props->Fields[i].EndLine - Props->Fields[i].StartLine + 1;
    return Lines;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFramePropsLineSymbolsHanc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtFramePropsLineSymbolsHanc(const DtFrameProps* Props)
{
    return Props->LineNumSymEav + Props->LineNumSymHanc + Props->LineNumSymSav;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Classification +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsValid -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool IsValid(const DtFrameProps* Props)
{
    return Props->VidStd != DTAPI_VIDSTD_UNKNOWN;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFramePropsIsSd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Frames of up to 625 lines are SD, all others HD.
//
bool DtFramePropsIsSd(const DtFrameProps* Props)
{
    return IsValid(Props) && DtFramePropsNumLines(Props) <= 625;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFramePropsIsHd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtFramePropsIsHd(const DtFrameProps* Props)
{
    return IsValid(Props) && !DtFramePropsIsSd(Props);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFramePropsIs3g -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A 1125-line frame at 50 frames per second or more, other than 2160p.
//
bool DtFramePropsIs3g(const DtFrameProps* Props)
{
    if (!IsValid(Props) || DtVidStdIs4k(Props->VidStd))
        return false;
    return DtFramePropsNumLines(Props) == 1125 &&
           (double)Props->FpsNum / Props->FpsDen >= 50.0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFramePropsIs3gLevelB -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtFramePropsIs3gLevelB(const DtFrameProps* Props)
{
    if (!DtFramePropsIs3g(Props))
        return false;
    return Props->VidStd == DTAPI_VIDSTD_1080P50B ||
           Props->VidStd == DTAPI_VIDSTD_1080P59_94B ||
           Props->VidStd == DTAPI_VIDSTD_1080P60B;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFramePropsIsInterlaced -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Two fields, which includes PsF.
//
bool DtFramePropsIsInterlaced(const DtFrameProps* Props)
{
    return IsValid(Props) && Props->NumFields > 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFramePropsIsPsF -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtFramePropsIsPsF(const DtFrameProps* Props)
{
    if (!DtFramePropsIsInterlaced(Props))
        return false;
    switch (Props->VidStd)
    {
    case DTAPI_VIDSTD_1080PSF23_98:
    case DTAPI_VIDSTD_1080PSF24:
    case DTAPI_VIDSTD_1080PSF25:
    case DTAPI_VIDSTD_1080PSF29_97:
    case DTAPI_VIDSTD_1080PSF30:
        return true;
    default:
        return false;
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Deduction +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The standards deduction tries, in DTAPI's order (MxFramePropsSdi::Deduce). The order
// decides results: the first match wins, so a 1080p standard is found before the 2160p
// standard with the same geometry, and a level-A 2160p before its level-B form.
static const int g_DeduceOrder[] = {
    DTAPI_VIDSTD_525I59_94,    DTAPI_VIDSTD_625I50,     DTAPI_VIDSTD_720P23_98,
    DTAPI_VIDSTD_720P24,       DTAPI_VIDSTD_720P25,     DTAPI_VIDSTD_720P29_97,
    DTAPI_VIDSTD_720P30,       DTAPI_VIDSTD_720P50,     DTAPI_VIDSTD_720P59_94,
    DTAPI_VIDSTD_720P60,       DTAPI_VIDSTD_1080P23_98, DTAPI_VIDSTD_1080P24,
    DTAPI_VIDSTD_1080P25,      DTAPI_VIDSTD_1080P29_97, DTAPI_VIDSTD_1080P30,
    DTAPI_VIDSTD_1080PSF23_98, DTAPI_VIDSTD_1080PSF24,  DTAPI_VIDSTD_1080PSF25,
    DTAPI_VIDSTD_1080PSF29_97, DTAPI_VIDSTD_1080PSF30,  DTAPI_VIDSTD_1080I50,
    DTAPI_VIDSTD_1080I59_94,   DTAPI_VIDSTD_1080I60,    DTAPI_VIDSTD_1080P50,
    DTAPI_VIDSTD_1080P50B,     DTAPI_VIDSTD_1080P59_94, DTAPI_VIDSTD_1080P59_94B,
    DTAPI_VIDSTD_1080P60,      DTAPI_VIDSTD_1080P60B,   DTAPI_VIDSTD_2160P50,
    DTAPI_VIDSTD_2160P50B,     DTAPI_VIDSTD_2160P59_94, DTAPI_VIDSTD_2160P59_94B,
    DTAPI_VIDSTD_2160P60,      DTAPI_VIDSTD_2160P60B,   DTAPI_VIDSTD_2160P23_98,
    DTAPI_VIDSTD_2160P24,      DTAPI_VIDSTD_2160P25,    DTAPI_VIDSTD_2160P29_97,
    DTAPI_VIDSTD_2160P30,
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFramePropsMatchesGeometry -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtFramePropsMatchesGeometry(const DtFrameProps* Props, int NumLinesF1,
                                 int NumLinesF2, int LineNumSymHanc, int LineNumSymVanc)
{
    return DtFramePropsLineSymbolsHanc(Props) == LineNumSymHanc &&
           Props->LineNumSymVanc == LineNumSymVanc &&
           Props->Fields[0].EndLine - Props->Fields[0].StartLine + 1 == NumLinesF1 &&
           DtFramePropsNumLines(Props) == NumLinesF1 + NumLinesF2;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Matches -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The tests of MxFramePropsSdi::Deduce for one candidate, in DTAPI's order.
//
static bool Matches(const DtFrameProps* Props, int NumLinesF1, int NumLinesF2,
                    int LineNumSymHanc, int LineNumSymVanc, double Fps, bool Is3gLevelB,
                    uint32_t Vpid, int SdiRate)
{
    double Rate = (double)Props->FpsNum / Props->FpsDen;
    double Deviation = Fps > Rate ? Fps - Rate : Rate - Fps;

    if (!DtFramePropsMatchesGeometry(Props, NumLinesF1, NumLinesF2, LineNumSymHanc,
                                     LineNumSymVanc))
        return false;
    if (Props->NumFields > 1 && NumLinesF2 == 0)
        return false;

    if (DtFramePropsIs3g(Props) && DtFramePropsIs3gLevelB(Props) != Is3gLevelB)
        return false;

    // The frame rate may deviate by 500 ppm.
    if (Deviation > Rate * 500 / 1e6)
        return false;

    // Without a VPID, HD PsF is taken to be interlaced; only 23.98 and 24, which have no
    // interlaced counterpart, remain PsF.
    if (DtFramePropsIsHd(Props) && DtFramePropsIsPsF(Props) && Vpid == 0 &&
        Props->VidStd != DTAPI_VIDSTD_1080PSF23_98 &&
        Props->VidStd != DTAPI_VIDSTD_1080PSF24)
    {
        return false;
    }

    // With a VPID, its transport bits separate PsF from interlaced, and its payload
    // identifier 3G level A from level B.
    if (!DtFramePropsIsSd(Props) && Vpid != 0)
    {
        if (DtFramePropsIsPsF(Props) && (Vpid & 0x0000C000) != 0x00008000)
            return false;
        if (!DtFramePropsIsPsF(Props) && DtFramePropsIsInterlaced(Props) &&
            (Vpid & 0x0000C000) != 0x00000000)
        {
            return false;
        }
        if (DtFramePropsIs3g(Props) && DtFramePropsIs3gLevelB(Props) &&
            (Vpid & 0xFF) != 0x8A)
            return false;
        if (DtFramePropsIs3g(Props) && !DtFramePropsIs3gLevelB(Props) &&
            (Vpid & 0xFF) == 0x8A)
            return false;
    }

    // At 6G and 12G only a 2160p standard will do.
    if ((SdiRate == DT_SDIRATE_6G || SdiRate == DT_SDIRATE_12G) &&
        !DtVidStdIs4k(Props->VidStd))
    {
        return false;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFramePropsDeduce -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// When no standard in the list matches, a VPID that decodes to a standard whose geometry
// matches still gives one; then the properties are those of one link.
//
void DtFramePropsDeduce(DtFrameProps* Props, int NumLinesF1, int NumLinesF2,
                        int LineNumSymHanc, int LineNumSymVanc, double Fps,
                        bool Is3gLevelB, uint32_t Vpid, int SdiRate)
{
    size_t i;

    for (i = 0; i < sizeof(g_DeduceOrder) / sizeof(g_DeduceOrder[0]); i++)
    {
        DtFramePropsInit(Props, g_DeduceOrder[i]);
        if (Matches(Props, NumLinesF1, NumLinesF2, LineNumSymHanc, LineNumSymVanc, Fps,
                    Is3gLevelB, Vpid, SdiRate))
        {
            return;
        }
    }

    if (Vpid != 0)
    {
        DtVidStdProps FromVpid;

        DtVidStdPropsFromSmpte352(&FromVpid, Vpid);
        if (FromVpid.VidStd != DTAPI_VIDSTD_UNKNOWN &&
            DtFramePropsMatchesGeometry(&FromVpid.Frame, NumLinesF1, NumLinesF2,
                                        LineNumSymHanc, LineNumSymVanc))
        {
            *Props = FromVpid.Frame;
            return;
        }
    }

    memset(Props, 0, sizeof(*Props));
    Props->VidStd = DTAPI_VIDSTD_UNKNOWN;
}
