// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiGeometry.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Where the image of an SDI frame lies in its raw frame
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>

// CDTAPI includes
#include "Video/DtSdiFrame.h" // The raw frame's layout.
#include "cdtapi.h"           // DtapiResult.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Geometry +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The image of a video standard and where its lines lie in a raw frame. Line indexes
// count the raw frame's lines from 0, so the frame's line 1 has index 0.
//
// Of a standard up to 3G, each line of the image is one raw line's active part. The
// fields of an interlaced or segmented frame are woven, field 1 on top: image line 2n is
// line n of field 1, image line 2n + 1 line n of field 2.
//
// Of 2160p, each raw line on the picture's lines holds two lines of the image, spread
// over the four links (see DtSdiFrame.h): image line 2n comes from links 1 and 2,
// image line 2n + 1 from links 3 and 4.
typedef struct DtSdiGeometry
{
    int VidStd;              // The DTAPI_VIDSTD_ code
    bool Is4k;               // 2160p over one link
    DtSdiFrameLayout Layout; // The layout of the raw frame
    int Width;               // The image's width in pixels
    int Height;              // The image's height in lines
    int NumFields;           // 1, or 2 for an interlaced or segmented frame
    int FieldFirstIndex[2];  // The index of each field's first active line
    int FieldNumLines[2];    // The active lines of each field
    int LinkWidth;           // 2160p: the pixels of one link's line
    int PictureFirstIndex;   // 2160p: the index of the first raw line of the picture

    // The streams of a raw line, each a sequence of words: one in SD; C and Y in HD and
    // 3G, the C word first; and C and Y of each of four links in 2160p. Word k of
    // stream s is symbol StreamFirst[s] + k * NumStreams of the line.
    int NumStreams;         // 1, 2 or 8
    int StreamFirst[8];     // Where each stream's first word is
    bool StreamIsChroma[8]; // The stream is a C stream, in HD and up
    int StreamLink[8];      // The link the stream belongs to, from 1
    int StreamEavWords;     // Words of EAV, line number and CRC at a stream's start
    int StreamHancWords;    // Words of horizontal blanking, EAV and SAV included
    int StreamSavWords;     // Words of SAV at the end of the horizontal blanking
    int StreamActiveWords;  // Words of the active part
    int SwitchingIndex;     // The index of field 1's switching line
    DtFrameProps Props;     // The frame's fields and lines: of one link in 2160p
} DtSdiGeometry;

// Fills *Geo for video standard VidStd. Returns DTAPI_OK, or DTAPI_E_INVALID_VIDSTD for a
// code that is not a standard, and for a standard of 3G level B, which a channel does
// not carry.
DtapiResult DtSdiGeometry_Init(DtSdiGeometry* Geo, int VidStd);

// Returns whether the active part of raw line LineIndex is vertical blanking rather than
// image.
bool DtSdiGeometry_IsVanc(const DtSdiGeometry* Geo, int LineIndex);

// Returns the index of the raw line that holds line ImageLine of the image, for a
// standard that is not 2160p.
int DtSdiGeometry_RawLine(const DtSdiGeometry* Geo, int ImageLine);
