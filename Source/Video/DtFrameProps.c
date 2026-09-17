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
#include "DtVidStd.h"     // The standards, and the VPID route of deduction.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame rates +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtVidStdFps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// MxVidStdPropsSdi::Fps.
//
void DtVidStdFps(int VidStd, int* Num, int* Den)
{
    const DtVidStdInfo* Info = DtVidStdFind(VidStd);

    *Num = Info != NULL ? Info->FpsNum : 0;
    *Den = Info != NULL ? Info->FpsDen : 1;
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFramePropsInit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// MxFramePropsSdi::Init, for the default variant of each standard. The fields and the
// widths follow from the number of lines and the scan; the line numbers are DTAPI's
// (MxVideoProps.cpp:1252-1460). A 2160p standard has the geometry of one of its links.
//
bool DtFramePropsInit(DtFrameProps* Props, int VidStd)
{
    const DtVidStdInfo* Info = DtVidStdFind(VidStd);

    memset(Props, 0, sizeof(*Props));
    Props->VidStd = DTAPI_VIDSTD_UNKNOWN;
    DtVidStdFps(VidStd, &Props->FpsNum, &Props->FpsDen);
    if (Info == NULL)
        return false;

    Props->LineNumSymHanc = Info->LineNumSymHanc;

    switch (Info->NumLines)
    {
    case 525:
        Props->NumFields = 2;
        SetField(&Props->Fields[0], 1, 262, 17, 260, 7);
        SetField(&Props->Fields[1], 263, 525, 280, 522, 270);
        Props->LineNumSymEav = 4;
        Props->LineNumSymSav = 4;
        Props->LineNumSymVanc = 720 * 2;
        break;

    case 625:
        Props->NumFields = 2;
        SetField(&Props->Fields[0], 1, 312, 23, 310, 6);
        SetField(&Props->Fields[1], 313, 625, 336, 623, 319);
        Props->LineNumSymEav = 4;
        Props->LineNumSymSav = 4;
        Props->LineNumSymVanc = 720 * 2;
        break;

    case 750:
        Props->NumFields = 1;
        SetField(&Props->Fields[0], 1, 750, 26, 745, 7);
        Props->LineNumSymEav = 8 * 2;
        Props->LineNumSymSav = 4 * 2;
        Props->LineNumSymVanc = 1280 * 2;
        break;

    default: // 1125 lines
        if (Info->Scan == DT_SCAN_P)
        {
            Props->NumFields = 1;
            SetField(&Props->Fields[0], 1, 1125, 42, 1121, 7);
        }
        else
        {
            Props->NumFields = 2;
            SetField(&Props->Fields[0], 1, 563, 21, 560, 7);
            SetField(&Props->Fields[1], 564, 1125, 584, 1123, 569);
        }
        Props->LineNumSymEav = 8 * 2;
        Props->LineNumSymSav = 4 * 2;
        Props->LineNumSymVanc = 1920 * 2;
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
    const DtVidStdInfo* Info = DtVidStdFind(Props->VidStd);

    return DtFramePropsIs3g(Props) && Info != NULL && Info->IsLevelB;
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
    const DtVidStdInfo* Info = DtVidStdFind(Props->VidStd);

    return DtFramePropsIsInterlaced(Props) && Info != NULL && Info->Scan == DT_SCAN_PSF;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Deduction +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

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

    // With a VPID, its scan bits must fit the frame, and its payload identifier the 3G
    // level. PsF has two fields, so the interlaced test applies to it as well, and no
    // VPID passes both: the search never gives PsF when there is a VPID.
    if (!DtFramePropsIsSd(Props) && Vpid != 0)
    {
        if (DtFramePropsIsPsF(Props) && (Vpid & 0x0000C000) != 0x00008000)
            return false;
        if (DtFramePropsIsInterlaced(Props) && (Vpid & 0x0000C000) != 0x00000000)
            return false;
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
// The standards are tried in the order of Tables/DtVidStdList.inc, which is DTAPI's.
// When none matches, a VPID that decodes to a standard whose geometry matches still gives
// one; then the properties are those of one link.
//
void DtFramePropsDeduce(DtFrameProps* Props, int NumLinesF1, int NumLinesF2,
                        int LineNumSymHanc, int LineNumSymVanc, double Fps,
                        bool Is3gLevelB, uint32_t Vpid, int SdiRate)
{
    int i;

    for (i = 0; i < DtVidStdCount(); i++)
    {
        DtFramePropsInit(Props, DtVidStdAt(i)->VidStd);
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
