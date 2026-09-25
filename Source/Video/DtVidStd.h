// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtVidStd.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Video standard knowledge shared inside the library
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// CDTAPI includes
#include "DtFrameProps.h" // The frame of one link.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Link standards +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// How a 4K picture is carried. The public header names none of these, so a program
// passes the numbers themselves, which fixes their values. They stay here rather than
// being published.
//

#define DT_VIDLNK_NONE -1        // Not a multi-link standard
#define DT_VIDLNK_4K_SMPTE425 0  // Four 3G links, SMPTE 425-5, two-sample interleave
#define DT_VIDLNK_4K_SMPTE425B 1 // Four 3G links, SMPTE 425-5 annex B, square division
#define DT_VIDLNK_4K_SMPTE2081 2 // One 6G link
#define DT_VIDLNK_4K_SMPTE2082 3 // One 12G link

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Standards +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// What CDTAPI knows of each video standard, from the one list in
// Tables/DtVidStdList.inc: the frame rate, how the frame is built, and how it is carried.
//

// How a frame is scanned.
#define DT_SCAN_P 0   // Progressive
#define DT_SCAN_I 1   // Interlaced
#define DT_SCAN_PSF 2 // Progressive, as segmented frames

typedef struct DtVidStdEntry
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
} DtVidStdEntry;

// The information of a video standard; NULL for a code that is not a standard.
const DtVidStdEntry* DtVidStd_Find(int VidStd);

// The frame rate of a video standard as a reduced fraction; 0/1 for anything else.
void DtVidStd_FrameRate(int VidStd, int* Num, int* Den);

// The standards in the order deduction tries them: DtVidStd_At(Index) for an Index from
// 0 to DtVidStd_Count() - 1, NULL outside that range.
int DtVidStd_Count(void);
const DtVidStdEntry* DtVidStd_At(int Index);

// True for the 2160p standards, the ones an I/O standard of 6G or 12G carries.
bool DtVidStd_Is4k(int VidStd);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Standard properties +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A video standard, how it is carried, and the frame of one link. For a 2160p standard
// that frame is the 1080p frame of the same rate.
//

typedef struct DtVidStdProps
{
    int VidStd;         // DTAPI_VIDSTD_UNKNOWN when the properties are not valid
    int LinkStd;        // DT_VIDLNK_ code
    DtFrameProps Frame; // The frame of one link
} DtVidStdProps;

// Fills Props for a video standard carried as LinkStd. Returns false, with Props->VidStd
// DTAPI_VIDSTD_UNKNOWN, for an unknown video or link standard and for a 2160p standard
// without a link standard.
bool DtVidStdProps_Init(DtVidStdProps* Props, int VidStd, int LinkStd);

// The standard a VPID describes.
void DtVidStdProps_FromSmpte352(DtVidStdProps* Props, uint32_t Vpid);

// Finds the standard of a signal from what an SDI receiver reports; the arguments are
// those of DtFrameProps_Deduce.
void DtVidStdProps_Deduce(DtVidStdProps* Props, int NumLinesF1, int NumLinesF2,
                          int LineNumSymHancInclTiming, int LineNumSymActive,
                          double FrameRate, bool Is3gLevelB, uint32_t Vpid, int SdiRate);

// The number of cables a link standard uses: four for SMPTE 425 quad links, one for the
// others, 0 for a value that is not a link standard.
int DtVidStd_NumPhysicalLinks(int LinkStd);
