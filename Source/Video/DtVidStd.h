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
// A link standard says how a 4K picture is carried. The public header does not name
// these values, so a program passes the numbers themselves; the values can therefore
// never change. They are kept here, not published.
//

#define DT_VIDLNK_NONE -1        // Not a multi-link standard
#define DT_VIDLNK_4K_SMPTE425 0  // Four 3G links, SMPTE 425-5, two-sample interleave
#define DT_VIDLNK_4K_SMPTE425B 1 // Four 3G links, SMPTE 425-5 annex B, square division
#define DT_VIDLNK_4K_SMPTE2081 2 // One 6G link
#define DT_VIDLNK_4K_SMPTE2082 3 // One 12G link

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Standards +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// What the library knows of each video standard: its frame rate, how its frame is built,
// and how it is carried. All of it comes from one list, Tables/DtVidStdList.inc.
//

// How a frame is scanned.
#define DT_SCAN_P 0   // Progressive
#define DT_SCAN_I 1   // Interlaced
#define DT_SCAN_PSF 2 // Progressive, as segmented frames

// One video standard.
typedef struct DtVidStdEntry
{
    int VidStd;         // The DTAPI_VIDSTD_ code
    int FpsNum;         // Frame rate numerator
    int FpsDen;         // Frame rate denominator; the fraction is reduced
    int NumLines;       // Lines of the frame
    int Scan;           // A DT_SCAN_ value
    int LineNumSymHanc; // Symbols per line in HANC, not counting EAV and SAV
    bool IsLevelB;      // True for 3G level B, and for 2160p made of level-B links
    int IoStd;          // The DTAPI_IOCONFIG_ I/O standard that carries it
    int OneLinkVidStd;  // For 2160p: the 1080p standard of one link; else unknown
} DtVidStdEntry;

// Returns the entry of video standard VidStd, or NULL for a code that is not a standard.
const DtVidStdEntry* DtVidStd_Find(int VidStd);

// Returns the frame rate of VidStd in *Num / *Den, as a reduced fraction. Returns 0/1
// for a code that is not a standard.
void DtVidStd_FrameRate(int VidStd, int* Num, int* Den);

// Walk the standards in the order that deduction tries them. DtVidStd_At returns the
// standard at Index, from 0 to DtVidStd_Count() - 1, or NULL outside that range.
int DtVidStd_Count(void);
const DtVidStdEntry* DtVidStd_At(int Index);

// Returns whether VidStd is a 2160p standard: one that 6G or 12G SDI carries.
bool DtVidStd_Is4k(int VidStd);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Standard properties +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// DtVidStdProps holds a video standard, how it is carried, and the frame of one link.
// For a 2160p standard that frame is the 1080p frame of the same rate.
//

typedef struct DtVidStdProps
{
    int VidStd;         // DTAPI_VIDSTD_UNKNOWN when the properties are not valid
    int LinkStd;        // A DT_VIDLNK_ value
    DtFrameProps Frame; // The frame of one link
} DtVidStdProps;

// Fills *Props for video standard VidStd, carried as link standard LinkStd. Returns
// false, with Props->VidStd set to DTAPI_VIDSTD_UNKNOWN, for an unknown video or link
// standard, and for a 2160p standard with DT_VIDLNK_NONE.
bool DtVidStdProps_Init(DtVidStdProps* Props, int VidStd, int LinkStd);

// Fills *Props with the standard that a VPID describes. Props->VidStd is
// DTAPI_VIDSTD_UNKNOWN when the VPID describes no standard the library knows.
void DtVidStdProps_FromSmpte352(DtVidStdProps* Props, uint32_t Vpid);

// Finds the standard of a signal from what an SDI receiver reports, and fills *Props
// with it. The arguments are those of DtFrameProps_Deduce. A VPID wins when its standard
// has the reported geometry; otherwise the geometry decides. Props->VidStd is
// DTAPI_VIDSTD_UNKNOWN when no standard matches, and for a 2160p signal that is not on
// 6G or 12G.
void DtVidStdProps_Deduce(DtVidStdProps* Props, int NumLinesF1, int NumLinesF2,
                          int LineNumSymHancInclTiming, int LineNumSymActive,
                          double FrameRate, bool Is3gLevelB, uint32_t Vpid, int SdiRate);

// Returns the number of cables a link standard uses: four for the SMPTE 425 quad links,
// one for the others, and 0 for a value that is not a link standard.
int DtVidStd_NumPhysicalLinks(int LinkStd);
