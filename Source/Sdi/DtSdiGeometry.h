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

// Describes the image of a video standard and where its lines lie in a raw frame. Line
// indexes count the raw frame's lines from 0, so the frame's line 1 has index 0.
//
// For a standard up to 3G, each line of the image is the active part of one raw line.
// The fields of an interlaced or segmented frame are woven together, with field 1 on
// top. Image line 2n is line n of field 1, and image line 2n + 1 is line n of field 2.
//
// For 2160p, each raw line of the picture holds two lines of the image, spread over the
// four links (see DtSdiFrame.h). Image line 2n comes from links 1 and 2. Image line
// 2n + 1 comes from links 3 and 4.
typedef struct DtSdiGeometry
{
    int VidStd;              // The DTAPI_VIDSTD_ code
    bool Is4k;               // 2160p over one 6G or 12G link
    DtSdiFrameLayout Layout; // The layout of the raw frame
    int Width;               // The image's width in pixels
    int Height;              // The image's height in lines
    int NumFields;           // 1, or 2 for an interlaced or segmented frame
    int FieldFirstIndex[2];  // The index of each field's first active line
    int FieldNumLines[2];    // The number of active lines in each field
    int LinkWidth;           // 2160p only: the number of pixels in one link's line
    int PictureFirstIndex;   // 2160p only: the index of the picture's first raw line

    // A raw line consists of streams, each a sequence of words. SD has one stream. HD
    // and 3G have a C and a Y stream, with the C word first. 2160p has a C and a Y
    // stream for each of the four links. Word k of stream s is symbol
    // StreamFirst[s] + k * NumStreams of the line.
    int NumStreams;         // The number of streams: 1, 2 or 8
    int StreamFirst[8];     // The symbol index of each stream's first word
    bool StreamIsChroma[8]; // Whether the stream is a C stream, in HD and up
    int StreamLink[8];      // The link the stream belongs to, from 1
    int StreamEavWords;     // Words of EAV, line number and CRC at a stream's start
    int StreamHancWords;    // Words of horizontal blanking, EAV and SAV included
    int StreamSavWords;     // Words of SAV at the end of the horizontal blanking
    int StreamActiveWords;  // Words of the active part
    int SwitchingIndex;     // The index of field 1's switching line
    DtFrameProps Props;     // The frame's fields and lines, of one link in 2160p
} DtSdiGeometry;

// Fills in *Geo for video standard VidStd.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_VIDSTD  VidStd is not a standard, or is a 3G level B standard,
//                           which a channel does not carry
DtapiResult DtSdiGeometry_Init(DtSdiGeometry* Geo, int VidStd);

// Returns whether the active part of raw line LineIndex is vertical blanking rather than
// image.
bool DtSdiGeometry_IsVanc(const DtSdiGeometry* Geo, int LineIndex);

// Returns the index of the raw line that holds line ImageLine of the image, for a
// standard that is not 2160p.
int DtSdiGeometry_RawLine(const DtSdiGeometry* Geo, int ImageLine);
