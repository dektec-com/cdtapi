// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtVidStd.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Video standard classification and the video to I/O standard mapping
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "DtVidStd.h"   // Link standards.
#include "CDtapiLite.h" // Public constants and the function being implemented.

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
// 4K over four links is not handled yet. DTAPI reduces such a standard to the standard of
// one link through its frame-property table, which CDtapiLite adds in milestone M4; until
// then those combinations return DTAPI_E_NOT_IMPLEMENTED.
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
            return DTAPI_E_NOT_IMPLEMENTED;
        *Value = DTAPI_IOCONFIG_6GSDI;
        break;

    case DTAPI_VIDSTD_2160P50:
    case DTAPI_VIDSTD_2160P50B:
    case DTAPI_VIDSTD_2160P59_94:
    case DTAPI_VIDSTD_2160P59_94B:
    case DTAPI_VIDSTD_2160P60:
    case DTAPI_VIDSTD_2160P60B:
        if (LinkStandard != DT_VIDLNK_4K_SMPTE2082)
            return DTAPI_E_NOT_IMPLEMENTED;
        *Value = DTAPI_IOCONFIG_12GSDI;
        break;

    default:
        return DTAPI_E_INVALID_VIDSTD;
    }

    *SubValue = VideoStandard;
    return DTAPI_OK;
}
