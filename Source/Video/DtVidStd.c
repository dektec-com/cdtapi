// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtVidStd.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Video standard classification and the video to I/O standard mapping
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>

// CDtapiLite includes
#include "CDtapiLite.h" // Public constants and the function being implemented.
#include "DtSmpte352.h" // VPID fields.
#include "DtVidStd.h"   // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Standards +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

static const DtVidStdInfo g_VidStds[] = {
#define X(Name, FpsNum, FpsDen, Lines, Scan, Hanc, LevelB, IoStd, OneLink)               \
    {DTAPI_VIDSTD_##Name,   FpsNum, FpsDen, Lines,                                       \
     DT_SCAN_##Scan,        Hanc,   LevelB, DTAPI_IOCONFIG_##IoStd,                      \
     DTAPI_VIDSTD_##OneLink},
#include "Tables/DtVidStdList.inc"
#undef X
};

#define VIDSTD_COUNT ((int)(sizeof(g_VidStds) / sizeof(g_VidStds[0])))

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtVidStd_Find -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A linear search over the forty-one standards.
//
const DtVidStdInfo* DtVidStd_Find(int VidStd)
{
    for (int i = 0; i < VIDSTD_COUNT; i++)
    {
        if (g_VidStds[i].VidStd == VidStd)
            return &g_VidStds[i];
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtVidStd_Count -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtVidStd_Count(void)
{
    return VIDSTD_COUNT;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtVidStd_At -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtVidStdInfo* DtVidStd_At(int Index)
{
    return Index >= 0 && Index < VIDSTD_COUNT ? &g_VidStds[Index] : NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsInfo4k -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The 2160p standards are the ones carried by 6G or 12G.
//
static bool IsInfo4k(const DtVidStdInfo* Info)
{
    return Info != NULL &&
           (Info->IoStd == DTAPI_IOCONFIG_6GSDI || Info->IoStd == DTAPI_IOCONFIG_12GSDI);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Classification +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtVidStd_Is4k -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtVidStd_Is4k(int VidStd)
{
    return IsInfo4k(DtVidStd_Find(VidStd));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Standard properties +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtVidStdProps_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A 2160p link carries the 1080p frame of the same rate.
//
bool DtVidStdProps_Init(DtVidStdProps* Props, int VidStd, int LinkStd)
{
    const DtVidStdInfo* Info = DtVidStd_Find(VidStd);

    Props->VidStd = DTAPI_VIDSTD_UNKNOWN;
    Props->LinkStd = DT_VIDLNK_NONE;
    DtFrameProps_Init(&Props->Frame, DTAPI_VIDSTD_UNKNOWN);

    if (LinkStd < DT_VIDLNK_NONE || LinkStd > DT_VIDLNK_4K_SMPTE2082)
        return false;
    if (IsInfo4k(Info) && LinkStd == DT_VIDLNK_NONE)
        return false;

    if (!DtFrameProps_Init(&Props->Frame, IsInfo4k(Info) ? Info->OneLinkVidStd : VidStd))
        return false;

    Props->VidStd = VidStd;
    Props->LinkStd = LinkStd;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindStd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The standard carried by IoStd, with NumLines lines (any number for 0), scanned as Scan,
// at Num/Den frames per second, of 3G level B or not; DTAPI_VIDSTD_UNKNOWN for none.
//
static int FindStd(int IoStd, int NumLines, int Scan, int Num, int Den, bool LevelB)
{
    for (int i = 0; i < VIDSTD_COUNT; i++)
    {
        const DtVidStdInfo* Info = &g_VidStds[i];

        if (Info->IoStd == IoStd && (NumLines == 0 || Info->NumLines == NumLines) &&
            Info->Scan == Scan && Info->FpsNum == Num && Info->FpsDen == Den &&
            Info->IsLevelB == LevelB)
        {
            return Info->VidStd;
        }
    }
    return DTAPI_VIDSTD_UNKNOWN;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtVidStdProps_FromSmpte352 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Each payload identifier admits the standards of one I/O standard, and some of those
// only; a VPID outside that set gives no standard. SD takes the rate alone; 1080-line HD
// separates progressive, PsF and interlaced; all other payloads must be progressive. The
// 2160p payloads set their link standard whether or not the rest decodes.
//
void DtVidStdProps_FromSmpte352(DtVidStdProps* Props, uint32_t Vpid)
{
    int Num;
    int Den;
    bool Progressive = !DtSmpte352_IsInterlacedStructure(Vpid);
    bool ITransport = DtSmpte352_IsInterlacedTransport(Vpid);
    int VidStd = DTAPI_VIDSTD_UNKNOWN;
    int LinkStd = DT_VIDLNK_NONE;

    DtSmpte352_PictureRate(Vpid, &Num, &Den);

    switch (DtSmpte352_PayloadId(Vpid))
    {
    case DT_S352_ID_S259:
        VidStd = FindStd(DTAPI_IOCONFIG_SDI, 0, DT_SCAN_I, Num, Den, false);
        break;

    case DT_S352_ID_S292_720:
        if (Progressive)
            VidStd = FindStd(DTAPI_IOCONFIG_HDSDI, 750, DT_SCAN_P, Num, Den, false);
        break;

    // Progressive in a progressive transport, PsF in an interlaced one, interlaced if
    // both are.
    case DT_S352_ID_S292_1080:
        if (Progressive || ITransport)
        {
            int Scan = !Progressive ? DT_SCAN_I : (ITransport ? DT_SCAN_PSF : DT_SCAN_P);

            VidStd = FindStd(DTAPI_IOCONFIG_HDSDI, 1125, Scan, Num, Den, false);
        }
        break;

    case DT_S352_ID_S425_1080_A:
        if (Progressive)
            VidStd = FindStd(DTAPI_IOCONFIG_3GSDI, 1125, DT_SCAN_P, Num, Den, false);
        break;

    case DT_S352_ID_S425_1080_B:
        if (Progressive)
            VidStd = FindStd(DTAPI_IOCONFIG_3GSDI, 1125, DT_SCAN_P, Num, Den, true);
        break;

    // Four 3G links carry the rates of 6G as well as those of 12G.
    case DT_S352_ID_S425_5_2160_A:
        if (Progressive)
        {
            VidStd = FindStd(DTAPI_IOCONFIG_6GSDI, 1125, DT_SCAN_P, Num, Den, false);
            if (VidStd == DTAPI_VIDSTD_UNKNOWN)
                VidStd = FindStd(DTAPI_IOCONFIG_12GSDI, 1125, DT_SCAN_P, Num, Den, false);
        }
        LinkStd = DT_VIDLNK_4K_SMPTE425;
        break;

    case DT_S352_ID_S425_5_2160_B:
        if (Progressive)
            VidStd = FindStd(DTAPI_IOCONFIG_12GSDI, 1125, DT_SCAN_P, Num, Den, true);
        LinkStd = DT_VIDLNK_4K_SMPTE425;
        break;

    case DT_S352_ID_S2081_2160:
        if (Progressive)
            VidStd = FindStd(DTAPI_IOCONFIG_6GSDI, 1125, DT_SCAN_P, Num, Den, false);
        LinkStd = DT_VIDLNK_4K_SMPTE2081;
        break;

    case DT_S352_ID_S2082_2160:
        if (Progressive)
            VidStd = FindStd(DTAPI_IOCONFIG_12GSDI, 1125, DT_SCAN_P, Num, Den, false);
        LinkStd = DT_VIDLNK_4K_SMPTE2082;
        break;

    default:
        break;
    }

    DtVidStdProps_Init(Props, VidStd, LinkStd);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtVidStdProps_Deduce -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A VPID whose standard has the reported geometry decides. Otherwise the frame is deduced
// from the counters, and a 2160p frame gets the link standard of the SDI rate; at any
// other rate it gets none, which leaves the properties invalid.
//
void DtVidStdProps_Deduce(DtVidStdProps* Props, int NumLinesF1, int NumLinesF2,
                          int LineNumSymHanc, int LineNumSymVanc, double Fps,
                          bool Is3gLevelB, uint32_t Vpid, int SdiRate)
{
    int LinkStd = DT_VIDLNK_NONE;

    if (Vpid != 0)
    {
        DtVidStdProps_FromSmpte352(Props, Vpid);
        if (Props->VidStd != DTAPI_VIDSTD_UNKNOWN &&
            DtFrameProps_MatchesGeometry(&Props->Frame, NumLinesF1, NumLinesF2,
                                         LineNumSymHanc, LineNumSymVanc))
        {
            return;
        }
    }

    DtFrameProps Frame;
    DtFrameProps_Deduce(&Frame, NumLinesF1, NumLinesF2, LineNumSymHanc, LineNumSymVanc,
                        Fps, Is3gLevelB, Vpid, SdiRate);
    if (DtVidStd_Is4k(Frame.VidStd))
    {
        if (SdiRate == DT_SDIRATE_6G)
            LinkStd = DT_VIDLNK_4K_SMPTE2081;
        else if (SdiRate == DT_SDIRATE_12G)
            LinkStd = DT_VIDLNK_4K_SMPTE2082;
    }
    DtVidStdProps_Init(Props, Frame.VidStd, LinkStd);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtVidStd_NumPhysicalLinks -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtVidStd_NumPhysicalLinks(int LinkStd)
{
    switch (LinkStd)
    {
    case DT_VIDLNK_NONE:
    case DT_VIDLNK_4K_SMPTE2081:
    case DT_VIDLNK_4K_SMPTE2082:
        return 1;
    case DT_VIDLNK_4K_SMPTE425:
    case DT_VIDLNK_4K_SMPTE425B:
        return 4;
    default:
        return 0;
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Public API +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtapiVidStd2IoStd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Follows DtapiVidStd2IoStd in DTAPI's Dtapi.cpp, including the order of its checks,
// since that order decides which error a caller sees when more than one thing is wrong:
// first the output pointers, then the link standard, then the video standard.
//
// The sub-value is always the video standard itself, because every video standard shares
// its number with the I/O configuration sub-value of the same name.
//
// A 2160p standard that is not on the one link of its rate class, 6G up to 30 frames and
// 12G from 50, is configured as the standard of one of its links, the 1080p standard of
// the same rate. DTAPI decides this by the rate class alone, so a 50 Hz and up standard
// on 6G is taken as that too: 2160p50 with SMPTE 2081 gives 3G-SDI 1080p50.
//
DtapiResult DtapiVidStd2IoStd(int VideoStandard, int LinkStandard, int* Value,
                              int* SubValue)
{
    if (Value == NULL || SubValue == NULL)
        return DTAPI_E_INVALID_ARG;

    *Value = -1;
    *SubValue = -1;

    // A 4K standard needs one of the four ways of carrying it; anything else takes none.
    const DtVidStdInfo* Info = DtVidStd_Find(VideoStandard);
    bool Is4k = IsInfo4k(Info);
    if (Is4k)
    {
        if (LinkStandard != DT_VIDLNK_4K_SMPTE425 &&
            LinkStandard != DT_VIDLNK_4K_SMPTE425B &&
            LinkStandard != DT_VIDLNK_4K_SMPTE2081 &&
            LinkStandard != DT_VIDLNK_4K_SMPTE2082)
        {
            return DTAPI_E_INVALID_LINKSTD;
        }
    }
    else if (LinkStandard != DT_VIDLNK_NONE)
    {
        return DTAPI_E_INVALID_LINKSTD;
    }

    if (Info == NULL)
        return DTAPI_E_INVALID_VIDSTD;

    if (Is4k &&
        LinkStandard != (Info->IoStd == DTAPI_IOCONFIG_6GSDI ? DT_VIDLNK_4K_SMPTE2081
                                                             : DT_VIDLNK_4K_SMPTE2082))
    {
        return DtapiVidStd2IoStd(Info->OneLinkVidStd, DT_VIDLNK_NONE, Value, SubValue);
    }

    *Value = Info->IoStd;
    *SubValue = VideoStandard;
    return DTAPI_OK;
}
