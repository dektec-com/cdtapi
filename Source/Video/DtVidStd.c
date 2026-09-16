// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtVidStd.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Video standard classification and the video to I/O standard mapping
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "DtVidStd.h"   // Interface being implemented.
#include "CDtapiLite.h" // Public constants and the function being implemented.
#include "DtSmpte352.h" // VPID fields.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Classification +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtVidStdIs4k -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtVidStdIs4k(int VidStd)
{
    switch (VidStd)
    {
    case DTAPI_VIDSTD_2160P23_98:
    case DTAPI_VIDSTD_2160P24:
    case DTAPI_VIDSTD_2160P25:
    case DTAPI_VIDSTD_2160P29_97:
    case DTAPI_VIDSTD_2160P30:
    case DTAPI_VIDSTD_2160P50:
    case DTAPI_VIDSTD_2160P50B:
    case DTAPI_VIDSTD_2160P59_94:
    case DTAPI_VIDSTD_2160P59_94B:
    case DTAPI_VIDSTD_2160P60:
    case DTAPI_VIDSTD_2160P60B:
        return true;
    default:
        return false;
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Standard properties +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtVidStdPropsInit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtVidStdPropsInit(DtVidStdProps* Props, int VidStd, int LinkStd)
{
    int VidStdLink = VidStd;

    Props->VidStd = DTAPI_VIDSTD_UNKNOWN;
    Props->LinkStd = DT_VIDLNK_NONE;
    DtFramePropsInit(&Props->Frame, DTAPI_VIDSTD_UNKNOWN);

    if (LinkStd < DT_VIDLNK_NONE || LinkStd > DT_VIDLNK_4K_SMPTE2082)
        return false;
    if (DtVidStdIs4k(VidStd) && LinkStd == DT_VIDLNK_NONE)
        return false;

    // A 2160p link carries the 1080p frame of the same rate.
    switch (VidStd)
    {
    case DTAPI_VIDSTD_2160P23_98:
        VidStdLink = DTAPI_VIDSTD_1080P23_98;
        break;
    case DTAPI_VIDSTD_2160P24:
        VidStdLink = DTAPI_VIDSTD_1080P24;
        break;
    case DTAPI_VIDSTD_2160P25:
        VidStdLink = DTAPI_VIDSTD_1080P25;
        break;
    case DTAPI_VIDSTD_2160P29_97:
        VidStdLink = DTAPI_VIDSTD_1080P29_97;
        break;
    case DTAPI_VIDSTD_2160P30:
        VidStdLink = DTAPI_VIDSTD_1080P30;
        break;
    case DTAPI_VIDSTD_2160P50:
        VidStdLink = DTAPI_VIDSTD_1080P50;
        break;
    case DTAPI_VIDSTD_2160P50B:
        VidStdLink = DTAPI_VIDSTD_1080P50B;
        break;
    case DTAPI_VIDSTD_2160P59_94:
        VidStdLink = DTAPI_VIDSTD_1080P59_94;
        break;
    case DTAPI_VIDSTD_2160P59_94B:
        VidStdLink = DTAPI_VIDSTD_1080P59_94B;
        break;
    case DTAPI_VIDSTD_2160P60:
        VidStdLink = DTAPI_VIDSTD_1080P60;
        break;
    case DTAPI_VIDSTD_2160P60B:
        VidStdLink = DTAPI_VIDSTD_1080P60B;
        break;
    default:
        break;
    }

    if (!DtFramePropsInit(&Props->Frame, VidStdLink))
        return false;

    Props->VidStd = VidStd;
    Props->LinkStd = LinkStd;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ByLowRate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Picks the standard for a rate of 23.98 to 30 frames per second from a list in that
// order; DTAPI_VIDSTD_UNKNOWN for another rate.
//
static int ByLowRate(int Num, int Den, const int Stds[5])
{
    if (Num == 24000 && Den == 1001)
        return Stds[0];
    if (Num == 24)
        return Stds[1];
    if (Num == 25)
        return Stds[2];
    if (Num == 30000 && Den == 1001)
        return Stds[3];
    if (Num == 30)
        return Stds[4];
    return DTAPI_VIDSTD_UNKNOWN;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ByHighRate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Picks the standard for 50, 59.94 or 60 frames per second from a list in that order.
//
static int ByHighRate(int Num, int Den, const int Stds[3])
{
    if (Num == 50)
        return Stds[0];
    if (Num == 60000 && Den == 1001)
        return Stds[1];
    if (Num == 60)
        return Stds[2];
    return DTAPI_VIDSTD_UNKNOWN;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Hd1080Std -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A 1080-line HD standard: progressive in a progressive transport, PsF in an interlaced
// one, interlaced if both are. 23.98 and 24 have no interlaced form.
//
static int Hd1080Std(int Num, int Den, bool IStructure, bool ITransport)
{
    static const int Progressive[5] = {DTAPI_VIDSTD_1080P23_98, DTAPI_VIDSTD_1080P24,
                                       DTAPI_VIDSTD_1080P25, DTAPI_VIDSTD_1080P29_97,
                                       DTAPI_VIDSTD_1080P30};
    static const int PsF[5] = {DTAPI_VIDSTD_1080PSF23_98, DTAPI_VIDSTD_1080PSF24,
                               DTAPI_VIDSTD_1080PSF25, DTAPI_VIDSTD_1080PSF29_97,
                               DTAPI_VIDSTD_1080PSF30};
    static const int Interlaced[5] = {DTAPI_VIDSTD_UNKNOWN, DTAPI_VIDSTD_UNKNOWN,
                                      DTAPI_VIDSTD_1080I50, DTAPI_VIDSTD_1080I59_94,
                                      DTAPI_VIDSTD_1080I60};

    if (!IStructure && !ITransport)
        return ByLowRate(Num, Den, Progressive);
    if (!IStructure)
        return ByLowRate(Num, Den, PsF);
    if (ITransport)
        return ByLowRate(Num, Den, Interlaced);
    return DTAPI_VIDSTD_UNKNOWN;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtVidStdPropsFromSmpte352 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Each payload identifier admits a set of rates and scans; a VPID outside that set gives
// no standard. SD takes the rate alone; 1080-line HD separates progressive, PsF and
// interlaced; all other payloads must be progressive. The 2160p payloads set their link
// standard whether or not the rest decodes.
//
void DtVidStdPropsFromSmpte352(DtVidStdProps* Props, uint32_t Vpid)
{
    static const int P720Low[5] = {DTAPI_VIDSTD_720P23_98, DTAPI_VIDSTD_720P24,
                                   DTAPI_VIDSTD_720P25, DTAPI_VIDSTD_720P29_97,
                                   DTAPI_VIDSTD_720P30};
    static const int P720High[3] = {DTAPI_VIDSTD_720P50, DTAPI_VIDSTD_720P59_94,
                                    DTAPI_VIDSTD_720P60};
    static const int P1080A[3] = {DTAPI_VIDSTD_1080P50, DTAPI_VIDSTD_1080P59_94,
                                  DTAPI_VIDSTD_1080P60};
    static const int P1080B[3] = {DTAPI_VIDSTD_1080P50B, DTAPI_VIDSTD_1080P59_94B,
                                  DTAPI_VIDSTD_1080P60B};
    static const int P2160Low[5] = {DTAPI_VIDSTD_2160P23_98, DTAPI_VIDSTD_2160P24,
                                    DTAPI_VIDSTD_2160P25, DTAPI_VIDSTD_2160P29_97,
                                    DTAPI_VIDSTD_2160P30};
    static const int P2160A[3] = {DTAPI_VIDSTD_2160P50, DTAPI_VIDSTD_2160P59_94,
                                  DTAPI_VIDSTD_2160P60};
    static const int P2160B[3] = {DTAPI_VIDSTD_2160P50B, DTAPI_VIDSTD_2160P59_94B,
                                  DTAPI_VIDSTD_2160P60B};
    int Num;
    int Den;
    bool Progressive = !DtSmpte352IsInterlacedStructure(Vpid);
    int VidStd = DTAPI_VIDSTD_UNKNOWN;
    int LinkStd = DT_VIDLNK_NONE;

    DtSmpte352PictureRate(Vpid, &Num, &Den);

    switch (DtSmpte352PayloadId(Vpid))
    {
    case DT_S352_ID_S259:
        if (Num == 25)
            VidStd = DTAPI_VIDSTD_625I50;
        else if (Num == 30000 && Den == 1001)
            VidStd = DTAPI_VIDSTD_525I59_94;
        break;

    case DT_S352_ID_S292_720:
        if (Progressive)
        {
            VidStd = ByLowRate(Num, Den, P720Low);
            if (VidStd == DTAPI_VIDSTD_UNKNOWN)
                VidStd = ByHighRate(Num, Den, P720High);
        }
        break;

    case DT_S352_ID_S292_1080:
        VidStd = Hd1080Std(Num, Den, !Progressive, DtSmpte352IsInterlacedTransport(Vpid));
        break;

    case DT_S352_ID_S425_1080_A:
        if (Progressive)
            VidStd = ByHighRate(Num, Den, P1080A);
        break;

    case DT_S352_ID_S425_1080_B:
        if (Progressive)
            VidStd = ByHighRate(Num, Den, P1080B);
        break;

    case DT_S352_ID_S425_5_2160_A:
        if (Progressive)
        {
            VidStd = ByLowRate(Num, Den, P2160Low);
            if (VidStd == DTAPI_VIDSTD_UNKNOWN)
                VidStd = ByHighRate(Num, Den, P2160A);
        }
        LinkStd = DT_VIDLNK_4K_SMPTE425;
        break;

    case DT_S352_ID_S425_5_2160_B:
        if (Progressive)
            VidStd = ByHighRate(Num, Den, P2160B);
        LinkStd = DT_VIDLNK_4K_SMPTE425;
        break;

    case DT_S352_ID_S2081_2160:
        if (Progressive)
            VidStd = ByLowRate(Num, Den, P2160Low);
        LinkStd = DT_VIDLNK_4K_SMPTE2081;
        break;

    case DT_S352_ID_S2082_2160:
        if (Progressive)
            VidStd = ByHighRate(Num, Den, P2160A);
        LinkStd = DT_VIDLNK_4K_SMPTE2082;
        break;

    default:
        break;
    }

    DtVidStdPropsInit(Props, VidStd, LinkStd);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtVidStdPropsDeduce -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A VPID whose standard has the reported geometry decides. Otherwise the frame is deduced
// from the counters, and a 2160p frame gets the link standard of the SDI rate; at any
// other rate it gets none, which leaves the properties invalid.
//
void DtVidStdPropsDeduce(DtVidStdProps* Props, int NumLinesF1, int NumLinesF2,
                         int LineNumSymHanc, int LineNumSymVanc, double Fps,
                         bool Is3gLevelB, uint32_t Vpid, int SdiRate)
{
    DtFrameProps Frame;
    int LinkStd = DT_VIDLNK_NONE;

    if (Vpid != 0)
    {
        DtVidStdPropsFromSmpte352(Props, Vpid);
        if (Props->VidStd != DTAPI_VIDSTD_UNKNOWN &&
            DtFramePropsMatchesGeometry(&Props->Frame, NumLinesF1, NumLinesF2,
                                        LineNumSymHanc, LineNumSymVanc))
        {
            return;
        }
    }

    DtFramePropsDeduce(&Frame, NumLinesF1, NumLinesF2, LineNumSymHanc, LineNumSymVanc,
                       Fps, Is3gLevelB, Vpid, SdiRate);
    if (DtVidStdIs4k(Frame.VidStd))
    {
        if (SdiRate == DT_SDIRATE_6G)
            LinkStd = DT_VIDLNK_4K_SMPTE2081;
        else if (SdiRate == DT_SDIRATE_12G)
            LinkStd = DT_VIDLNK_4K_SMPTE2082;
    }
    DtVidStdPropsInit(Props, Frame.VidStd, LinkStd);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtVidStdNumPhysicalLinks -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtVidStdNumPhysicalLinks(int LinkStd)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IoStdOfOneLink -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The I/O standard of one link of a 2160p standard, whose link standard has been checked.
//
static unsigned int IoStdOfOneLink(int VideoStandard, int LinkStandard, int* Value,
                                   int* SubValue)
{
    DtVidStdProps Props;

    if (!DtVidStdPropsInit(&Props, VideoStandard, LinkStandard))
        return DTAPI_E_INTERNAL;
    return DtapiVidStd2IoStd(Props.Frame.VidStd, DT_VIDLNK_NONE, Value, SubValue);
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
// A 2160p standard that is not on the one link of its rate class is configured as the
// standard of one of its links, the 1080p standard of the same rate. DTAPI decides this
// by the rate class alone, so a 50 Hz and up standard on 6G is taken as that too:
// 2160p50 with SMPTE 2081 gives 3G-SDI 1080p50.
//
unsigned int DtapiVidStd2IoStd(int VideoStandard, int LinkStandard, int* Value,
                               int* SubValue)
{
    bool Is4k;

    if (Value == NULL || SubValue == NULL)
        return DTAPI_E_INVALID_ARG;

    *Value = -1;
    *SubValue = -1;

    // A 4K standard needs one of the four ways of carrying it; anything else takes none.
    Is4k = DtVidStdIs4k(VideoStandard);
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

    switch (VideoStandard)
    {
    case DTAPI_VIDSTD_525I59_94:
    case DTAPI_VIDSTD_625I50:
        *Value = DTAPI_IOCONFIG_SDI;
        break;

    case DTAPI_VIDSTD_720P23_98:
    case DTAPI_VIDSTD_720P24:
    case DTAPI_VIDSTD_720P25:
    case DTAPI_VIDSTD_720P29_97:
    case DTAPI_VIDSTD_720P30:
    case DTAPI_VIDSTD_720P59_94:
    case DTAPI_VIDSTD_720P50:
    case DTAPI_VIDSTD_720P60:
    case DTAPI_VIDSTD_1080P23_98:
    case DTAPI_VIDSTD_1080P24:
    case DTAPI_VIDSTD_1080P25:
    case DTAPI_VIDSTD_1080P29_97:
    case DTAPI_VIDSTD_1080P30:
    case DTAPI_VIDSTD_1080PSF23_98:
    case DTAPI_VIDSTD_1080PSF24:
    case DTAPI_VIDSTD_1080PSF25:
    case DTAPI_VIDSTD_1080PSF29_97:
    case DTAPI_VIDSTD_1080PSF30:
    case DTAPI_VIDSTD_1080I50:
    case DTAPI_VIDSTD_1080I59_94:
    case DTAPI_VIDSTD_1080I60:
        *Value = DTAPI_IOCONFIG_HDSDI;
        break;

    case DTAPI_VIDSTD_1080P50:
    case DTAPI_VIDSTD_1080P50B:
    case DTAPI_VIDSTD_1080P59_94:
    case DTAPI_VIDSTD_1080P59_94B:
    case DTAPI_VIDSTD_1080P60:
    case DTAPI_VIDSTD_1080P60B:
        *Value = DTAPI_IOCONFIG_3GSDI;
        break;

    case DTAPI_VIDSTD_2160P23_98:
    case DTAPI_VIDSTD_2160P24:
    case DTAPI_VIDSTD_2160P25:
    case DTAPI_VIDSTD_2160P29_97:
    case DTAPI_VIDSTD_2160P30:
        if (LinkStandard != DT_VIDLNK_4K_SMPTE2081)
            return IoStdOfOneLink(VideoStandard, LinkStandard, Value, SubValue);
        *Value = DTAPI_IOCONFIG_6GSDI;
        break;

    case DTAPI_VIDSTD_2160P50:
    case DTAPI_VIDSTD_2160P50B:
    case DTAPI_VIDSTD_2160P59_94:
    case DTAPI_VIDSTD_2160P59_94B:
    case DTAPI_VIDSTD_2160P60:
    case DTAPI_VIDSTD_2160P60B:
        if (LinkStandard != DT_VIDLNK_4K_SMPTE2082)
            return IoStdOfOneLink(VideoStandard, LinkStandard, Value, SubValue);
        *Value = DTAPI_IOCONFIG_12GSDI;
        break;

    default:
        return DTAPI_E_INVALID_VIDSTD;
    }

    *SubValue = VideoStandard;
    return DTAPI_OK;
}
