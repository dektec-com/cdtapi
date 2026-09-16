// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtVidStd.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Video standard knowledge shared inside the library
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DT_VID_STD_H
#define CDTAPILITE_DT_VID_STD_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// CDtapiLite includes
#include "DtFrameProps.h" // The frame of one link.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Link standards +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// How a 4K picture is carried. CDTAPI.h does not define these, so its users pass the
// numbers directly; the values are DTAPI's (DTAPI.h.tpl:4380-4384) and are kept private
// here rather than added to the public header.
//

#define DT_VIDLNK_NONE -1        // Not a multi-link standard
#define DT_VIDLNK_4K_SMPTE425 0  // Four 3G links, SMPTE 425 level A
#define DT_VIDLNK_4K_SMPTE425B 1 // Four 3G links, SMPTE 425 annex B
#define DT_VIDLNK_4K_SMPTE2081 2 // One 6G link
#define DT_VIDLNK_4K_SMPTE2082 3 // One 12G link

// True for the eleven 2160p standards, as HdSdiUtil::Is4k in DTAPI.
bool DtVidStdIs4k(int VidStd);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Standard properties +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// What DTAPI's MxVidStdPropsSdi holds: a video standard, how it is carried, and the frame
// of one link. For a 2160p standard that frame is the 1080p frame of the same rate.
//

typedef struct DtVidStdProps
{
    int VidStd;         // DTAPI_VIDSTD_UNKNOWN when the properties are not valid
    int LinkStd;        // DT_VIDLNK_ code
    DtFrameProps Frame; // The frame of one link
} DtVidStdProps;

// Fills Props for a video standard carried as LinkStd, as MxVidStdPropsSdi::Init. Returns
// false, with Props->VidStd DTAPI_VIDSTD_UNKNOWN, for an unknown video or link standard
// and for a 2160p standard without a link standard.
bool DtVidStdPropsInit(DtVidStdProps* Props, int VidStd, int LinkStd);

// The standard a VPID describes, as MxVidStdPropsSdi::FromSmpte352.
void DtVidStdPropsFromSmpte352(DtVidStdProps* Props, uint32_t Vpid);

// Finds the standard of a signal from what an SDI receiver reports, as
// MxVidStdPropsSdi::Deduce; the arguments are those of DtFramePropsDeduce.
void DtVidStdPropsDeduce(DtVidStdProps* Props, int NumLinesF1, int NumLinesF2,
                         int LineNumSymHanc, int LineNumSymVanc, double Fps,
                         bool Is3gLevelB, uint32_t Vpid, int SdiRate);

// The number of cables a link standard uses: four for SMPTE 425 quad links, one for the
// others, 0 for a value that is no link standard.
int DtVidStdNumPhysicalLinks(int LinkStd);

#endif // CDTAPILITE_DT_VID_STD_H
