// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtFrameProps.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The geometry of an SDI frame per video standard - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <string.h>

// CDTAPI includes
#include "DtFrameProps.h" // Interface being implemented.
#include "DtVidStd.h"     // The standards, and the VPID route of deduction.
#include "cdtapi.h"       // DTAPI_VIDSTD_ codes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame rates +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame geometry +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetFieldLines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void SetFieldLines(DtFieldProps* Field, int Start, int End, int VidStart,
                          int VidEnd, int Switching)
{
    Field->StartLine = Start;
    Field->EndLine = End;
    Field->ActiveStartLine = VidStart;
    Field->ActiveEndLine = VidEnd;
    Field->SwitchingLine = Switching;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFrameProps_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The geometry of the default variant of each standard. The fields and the widths follow
// from the number of lines and the scan, and the line numbers are tabulated per line
// count. A 2160p standard has the geometry of one of its links.
//
bool DtFrameProps_Init(DtFrameProps* Props, int VidStd)
{
    const DtVidStdEntry* Info = DtVidStd_Find(VidStd);

    memset(Props, 0, sizeof(*Props));
    Props->VidStd = DTAPI_VIDSTD_UNKNOWN;
    DtVidStd_FrameRate(VidStd, &Props->FpsNum, &Props->FpsDen);
    if (Info == NULL)
        return false;

    Props->LineNumSymHanc = Info->LineNumSymHanc;

    switch (Info->NumLines)
    {
    case 525:
        Props->NumFields = 2;
        SetFieldLines(&Props->Fields[0], 1, 262, 17, 260, 7);
        SetFieldLines(&Props->Fields[1], 263, 525, 280, 522, 270);
        Props->LineNumSymEav = 4;
        Props->LineNumSymSav = 4;
        Props->LineNumSymActive = 720 * 2;
        break;

    case 625:
        Props->NumFields = 2;
        SetFieldLines(&Props->Fields[0], 1, 312, 23, 310, 6);
        SetFieldLines(&Props->Fields[1], 313, 625, 336, 623, 319);
        Props->LineNumSymEav = 4;
        Props->LineNumSymSav = 4;
        Props->LineNumSymActive = 720 * 2;
        break;

    case 750:
        Props->NumFields = 1;
        SetFieldLines(&Props->Fields[0], 1, 750, 26, 745, 7);
        Props->LineNumSymEav = 8 * 2;
        Props->LineNumSymSav = 4 * 2;
        Props->LineNumSymActive = 1280 * 2;
        break;

    default: // 1125 lines
        if (Info->Scan == DT_SCAN_P)
        {
            Props->NumFields = 1;
            SetFieldLines(&Props->Fields[0], 1, 1125, 42, 1121, 7);
        }
        else
        {
            Props->NumFields = 2;
            SetFieldLines(&Props->Fields[0], 1, 563, 21, 560, 7);
            SetFieldLines(&Props->Fields[1], 564, 1125, 584, 1123, 569);
        }
        Props->LineNumSymEav = 8 * 2;
        Props->LineNumSymSav = 4 * 2;
        Props->LineNumSymActive = 1920 * 2;
        break;
    }

    Props->VidStd = VidStd;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFrameProps_NumLines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtFrameProps_NumLines(const DtFrameProps* Props)
{
    int Lines = 0;

    for (int i = 0; i < Props->NumFields; i++)
        Lines += Props->Fields[i].EndLine - Props->Fields[i].StartLine + 1;
    return Lines;
}

// .-.-.-.-.-.-.-.-.-.-.-.- DtFrameProps_LineNumSymHancInclTiming -.-.-.-.-.-.-.-.-.-.-.-.
//
int DtFrameProps_LineNumSymHancInclTiming(const DtFrameProps* Props)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFrameProps_IsSd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Frames of up to 625 lines are SD, all others HD.
//
bool DtFrameProps_IsSd(const DtFrameProps* Props)
{
    return IsValid(Props) && DtFrameProps_NumLines(Props) <= 625;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFrameProps_IsHd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtFrameProps_IsHd(const DtFrameProps* Props)
{
    return IsValid(Props) && !DtFrameProps_IsSd(Props);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFrameProps_Is3g -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A 1125-line frame at 50 frames per second or more, other than 2160p.
//
bool DtFrameProps_Is3g(const DtFrameProps* Props)
{
    if (!IsValid(Props) || DtVidStd_Is4k(Props->VidStd))
        return false;
    return DtFrameProps_NumLines(Props) == 1125 &&
           (double)Props->FpsNum / Props->FpsDen >= 50.0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFrameProps_Is3gLevelB -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtFrameProps_Is3gLevelB(const DtFrameProps* Props)
{
    const DtVidStdEntry* Info = DtVidStd_Find(Props->VidStd);

    return DtFrameProps_Is3g(Props) && Info != NULL && Info->IsLevelB;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFrameProps_IsInterlaced -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Two fields, which includes PsF.
//
bool DtFrameProps_IsInterlaced(const DtFrameProps* Props)
{
    return IsValid(Props) && Props->NumFields > 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFrameProps_IsPsF -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtFrameProps_IsPsF(const DtFrameProps* Props)
{
    const DtVidStdEntry* Info = DtVidStd_Find(Props->VidStd);

    return DtFrameProps_IsInterlaced(Props) && Info != NULL && Info->Scan == DT_SCAN_PSF;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Deduction +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFrameProps_MatchesGeometry -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtFrameProps_MatchesGeometry(const DtFrameProps* Props, int NumLinesF1,
                                  int NumLinesF2, int LineNumSymHancInclTiming,
                                  int LineNumSymActive)
{
    return DtFrameProps_LineNumSymHancInclTiming(Props) == LineNumSymHancInclTiming &&
           Props->LineNumSymActive == LineNumSymActive &&
           Props->Fields[0].EndLine - Props->Fields[0].StartLine + 1 == NumLinesF1 &&
           DtFrameProps_NumLines(Props) == NumLinesF1 + NumLinesF2;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MatchesReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The tests one candidate standard must pass, in the order they are made.
//
static bool MatchesReceiver(const DtFrameProps* Props, int NumLinesF1, int NumLinesF2,
                            int LineNumSymHancInclTiming, int LineNumSymActive,
                            double FrameRate, bool Is3gLevelB, uint32_t Vpid, int SdiRate)
{
    double StdFrameRate = (double)Props->FpsNum / Props->FpsDen;
    double Deviation =
        FrameRate > StdFrameRate ? FrameRate - StdFrameRate : StdFrameRate - FrameRate;

    if (!DtFrameProps_MatchesGeometry(Props, NumLinesF1, NumLinesF2,
                                      LineNumSymHancInclTiming, LineNumSymActive))
        return false;
    if (Props->NumFields > 1 && NumLinesF2 == 0)
        return false;

    if (DtFrameProps_Is3g(Props) && DtFrameProps_Is3gLevelB(Props) != Is3gLevelB)
        return false;

    // The frame rate may deviate by 500 ppm.
    if (Deviation > StdFrameRate * 500 / 1e6)
        return false;

    // Without a VPID, HD PsF is taken to be interlaced; only 23.98 and 24, which have no
    // interlaced counterpart, remain PsF.
    if (DtFrameProps_IsHd(Props) && DtFrameProps_IsPsF(Props) && Vpid == 0 &&
        Props->VidStd != DTAPI_VIDSTD_1080PSF23_98 &&
        Props->VidStd != DTAPI_VIDSTD_1080PSF24)
    {
        return false;
    }

    // With a VPID, its scan bits must fit a frame of two fields, and its payload
    // identifier the 3G level; a progressive frame accepts any scan bits. PsF has two
    // fields, so the interlaced test applies to it as well, and no VPID passes both: the
    // search never gives PsF when there is a VPID.
    if (!DtFrameProps_IsSd(Props) && Vpid != 0)
    {
        if (DtFrameProps_IsPsF(Props) && (Vpid & 0x0000C000) != 0x00008000)
            return false;
        if (DtFrameProps_IsInterlaced(Props) && (Vpid & 0x0000C000) != 0x00000000)
            return false;
        if (DtFrameProps_Is3g(Props) && DtFrameProps_Is3gLevelB(Props) &&
            (Vpid & 0xFF) != 0x8A)
            return false;
        if (DtFrameProps_Is3g(Props) && !DtFrameProps_Is3gLevelB(Props) &&
            (Vpid & 0xFF) == 0x8A)
            return false;
    }

    // At 6G and 12G only a 2160p standard will do.
    if ((SdiRate == DT_SDIRATE_6G || SdiRate == DT_SDIRATE_12G) &&
        !DtVidStd_Is4k(Props->VidStd))
    {
        return false;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFrameProps_Deduce -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The standards are tried in the order of Tables/DtVidStdList.inc. When none matches, a
// VPID that decodes to a standard whose geometry matches still gives one. For a 2160p
// VPID that is the frame of one link, and Props->VidStd is the link's 1080p standard.
//
void DtFrameProps_Deduce(DtFrameProps* Props, int NumLinesF1, int NumLinesF2,
                         int LineNumSymHancInclTiming, int LineNumSymActive,
                         double FrameRate, bool Is3gLevelB, uint32_t Vpid, int SdiRate)
{
    for (int i = 0; i < DtVidStd_Count(); i++)
    {
        DtFrameProps_Init(Props, DtVidStd_At(i)->VidStd);
        if (MatchesReceiver(Props, NumLinesF1, NumLinesF2, LineNumSymHancInclTiming,
                            LineNumSymActive, FrameRate, Is3gLevelB, Vpid, SdiRate))
        {
            return;
        }
    }

    if (Vpid != 0)
    {
        DtVidStdProps FromVpid;

        DtVidStdProps_FromSmpte352(&FromVpid, Vpid);
        if (FromVpid.VidStd != DTAPI_VIDSTD_UNKNOWN &&
            DtFrameProps_MatchesGeometry(&FromVpid.Frame, NumLinesF1, NumLinesF2,
                                         LineNumSymHancInclTiming, LineNumSymActive))
        {
            *Props = FromVpid.Frame;
            return;
        }
    }

    memset(Props, 0, sizeof(*Props));
    Props->VidStd = DTAPI_VIDSTD_UNKNOWN;
}
