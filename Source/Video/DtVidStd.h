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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Standards +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// What CDtapiLite knows of each video standard, from the one list in
// Tables/DtVidStdList.inc: the frame rate, how the frame is built, and how it is carried.
//

// How a frame is scanned.
#define DT_SCAN_P 0   // Progressive
#define DT_SCAN_I 1   // Interlaced
#define DT_SCAN_PSF 2 // Progressive, as segmented frames

typedef struct DtVidStdInfo
{
    int VidStd; // DTAPI_VIDSTD_ code
    int FpsNum; // Frames per second as a reduced fraction
    int FpsDen;
    int NumLines;       // Lines of the frame
    int Scan;           // DT_SCAN_ value
    int LineNumSymHanc; // Symbols per line in HANC, EAV and SAV not included
    bool IsLevelB;      // 3G level B, or 2160p made of level-B links
    int IoStd;          // The DTAPI_IOCONFIG_ I/O standard that carries it
    int OneLinkVidStd;  // For 2160p the 1080p standard of one link, else unknown
} DtVidStdInfo;

// The information of a video standard; NULL for a code that is no standard.
const DtVidStdInfo* DtVidStdFind(int VidStd);

// The standards in the order MxFramePropsSdi::Deduce tries them: DtVidStdAt(Index) for an
// Index from 0 to DtVidStdCount() - 1, NULL outside that range.
int DtVidStdCount(void);
const DtVidStdInfo* DtVidStdAt(int Index);

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
