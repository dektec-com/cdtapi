// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtFrameProps.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The geometry of an SDI frame per video standard, and deducing the standard
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame geometry +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// DtFrameProps describes the geometry of a frame: the lines of each field, where the
// active video and the switching line are, and how many symbols each part of a line
// has. For a 2160p standard it describes one link: the 1080p frame of the same rate.
//
// A symbol count counts both components, so an active line of 1920 samples is 3840
// symbols.
//

// The lines of one field.
typedef struct DtFieldProps
{
    int StartLine;       // First line of the field
    int EndLine;         // Last line of the field
    int ActiveStartLine; // First line with active video
    int ActiveEndLine;   // Last line with active video
    int SwitchingLine;   // The field's switching line
} DtFieldProps;

// The geometry of a frame.
typedef struct DtFrameProps
{
    int VidStd;             // DTAPI_VIDSTD_UNKNOWN when the properties are not valid
    int FpsNum;             // Frame rate numerator, e.g. 25 or 30000
    int FpsDen;             // Frame rate denominator, e.g. 1 or 1001 (reduced)
    int NumFields;          // 1 for progressive, 2 for interlaced and PsF
    DtFieldProps Fields[2]; // The fields; Fields[1] is unused when NumFields is 1
    int LineNumSymEav;      // Symbols in EAV; in HD this includes line number and CRC
    int LineNumSymHanc;     // Symbols in HANC, not counting EAV and SAV
    int LineNumSymSav;      // Symbols in SAV
    int LineNumSymActive;   // Symbols in the active part of a line, video or VANC
} DtFrameProps;

// The SDI rates the library tells apart. The values are those of the driver's
// DT_DRV_SDIRATE_.
#define DT_SDIRATE_UNKNOWN -1
#define DT_SDIRATE_SD 0
#define DT_SDIRATE_HD 1
#define DT_SDIRATE_3G 2
#define DT_SDIRATE_6G 3
#define DT_SDIRATE_12G 4

// Fills *Props for video standard VidStd. Returns false, with Props->VidStd set to
// DTAPI_VIDSTD_UNKNOWN, for DTAPI_VIDSTD_UNKNOWN or a code that is not a standard.
bool DtFrameProps_Init(DtFrameProps* Props, int VidStd);

// Returns the number of lines in the frame, both fields together.
int DtFrameProps_NumLines(const DtFrameProps* Props);

// Returns the number of symbols in the horizontal blanking of a line, EAV and SAV
// included. That is how the SDI receiver counts them.
int DtFrameProps_LineNumSymHancInclTiming(const DtFrameProps* Props);

// Returns whether the frame has the geometry an SDI receiver reports: NumLinesF1 lines
// in the first field, NumLinesF1 + NumLinesF2 in the frame, and the given numbers of
// symbols per line in the horizontal blanking (EAV and SAV included) and the active
// part.
bool DtFrameProps_MatchesGeometry(const DtFrameProps* Props, int NumLinesF1,
                                  int NumLinesF2, int LineNumSymHancInclTiming,
                                  int LineNumSymActive);

// Return what kind of frame it is. All return false for properties that are not valid.
bool DtFrameProps_IsSd(const DtFrameProps* Props);
bool DtFrameProps_IsHd(const DtFrameProps* Props);
bool DtFrameProps_Is3g(const DtFrameProps* Props);
bool DtFrameProps_Is3gLevelB(const DtFrameProps* Props);
bool DtFrameProps_IsInterlaced(const DtFrameProps* Props);
bool DtFrameProps_IsPsF(const DtFrameProps* Props);

// Finds the video standard of a frame from what an SDI receiver reports, and fills
// *Props with it. The receiver reports the lines of each field, the symbols per line in
// the horizontal blanking (EAV and SAV included) and in the active part, the frame rate,
// whether 3G is level B, the VPID and the SDI rate (a DT_SDIRATE_ value).
// Props->VidStd is DTAPI_VIDSTD_UNKNOWN when no standard matches.
void DtFrameProps_Deduce(DtFrameProps* Props, int NumLinesF1, int NumLinesF2,
                         int LineNumSymHancInclTiming, int LineNumSymActive,
                         double FrameRate, bool Is3gLevelB, uint32_t Vpid, int SdiRate);
