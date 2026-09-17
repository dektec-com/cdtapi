// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtFrameProps.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - The geometry of an SDI frame per video standard, and deducing the standard
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame geometry +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// What DTAPI's MxFramePropsSdi holds of a frame, without its audio properties: the lines
// of each field, where the active video and the switching line are, and how many symbols
// each part of a line has. For a 2160p standard these are the properties of one link,
// the 1080p frame of the same rate, as in DTAPI.
//
// Symbol counts count both components, so a 1920-sample active line is 3840 symbols.
//

typedef struct DtFieldProps
{
    int StartLine;     // First line of the field
    int EndLine;       // Last line of the field
    int VidStartLine;  // First line with active video
    int VidEndLine;    // Last line with active video
    int SwitchingLine; // The field's switching line
} DtFieldProps;

typedef struct DtFrameProps
{
    int VidStd; // DTAPI_VIDSTD_UNKNOWN when the properties are not valid
    int FpsNum; // Frame rate as a reduced fraction: 25/1, 30000/1001
    int FpsDen;
    int NumFields; // 1 for progressive, 2 for interlaced and PsF
    DtFieldProps Fields[2];
    int LineNumSymEav;  // Symbols in EAV
    int LineNumSymHanc; // Symbols in HANC, not counting EAV and SAV
    int LineNumSymSav;  // Symbols in SAV
    int LineNumSymVanc; // Symbols in the active part of a line, video or VANC
} DtFrameProps;

// The SDI rates DTAPI distinguishes, with DTAPI's values.
#define DT_SDIRATE_UNKNOWN -1
#define DT_SDIRATE_SD 0
#define DT_SDIRATE_HD 1
#define DT_SDIRATE_3G 2
#define DT_SDIRATE_6G 3
#define DT_SDIRATE_12G 4

// Fills Props for a video standard, as MxFramePropsSdi::Init. Returns false, with
// Props->VidStd DTAPI_VIDSTD_UNKNOWN, for DTAPI_VIDSTD_UNKNOWN or a code that is no
// standard.
bool DtFrameProps_Init(DtFrameProps* Props, int VidStd);

// The frame rate of a video standard as a reduced fraction; 0/1 for anything else.
void DtVidStd_Fps(int VidStd, int* Num, int* Den);

// Lines in the frame, over both fields.
int DtFrameProps_NumLines(const DtFrameProps* Props);

// Symbols in the horizontal blanking of a line, EAV and SAV included, which is how the
// SDI receiver counts them.
int DtFrameProps_LineSymbolsHanc(const DtFrameProps* Props);

// Whether a frame has the geometry an SDI receiver reports: the lines of the first field
// and of the frame, the symbols per line in HANC (EAV and SAV included) and in the active
// part.
bool DtFrameProps_MatchesGeometry(const DtFrameProps* Props, int NumLinesF1,
                                  int NumLinesF2, int LineNumSymHanc, int LineNumSymVanc);

// The classifications MxFramePropsSdi makes. All are false for invalid properties.
bool DtFrameProps_IsSd(const DtFrameProps* Props);
bool DtFrameProps_IsHd(const DtFrameProps* Props);
bool DtFrameProps_Is3g(const DtFrameProps* Props);
bool DtFrameProps_Is3gLevelB(const DtFrameProps* Props);
bool DtFrameProps_IsInterlaced(const DtFrameProps* Props);
bool DtFrameProps_IsPsF(const DtFrameProps* Props);

// Finds the video standard of a frame from what an SDI receiver reports, as
// MxFramePropsSdi::Deduce: the lines of each field, the symbols per line in HANC (EAV and
// SAV included) and in the active part, the frame rate, whether 3G is level B, the VPID
// and the SDI rate. Props->VidStd is DTAPI_VIDSTD_UNKNOWN when no standard matches.
void DtFrameProps_Deduce(DtFrameProps* Props, int NumLinesF1, int NumLinesF2,
                         int LineNumSymHanc, int LineNumSymVanc, double Fps,
                         bool Is3gLevelB, uint32_t Vpid, int SdiRate);
