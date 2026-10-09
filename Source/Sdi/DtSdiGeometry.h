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

    // 3G level B only. The frame is one picture in the layout of level A, as the card
    // holds it. On the line, two pictures make one frame of an interlaced interface:
    // field 1 and field 2 (SMPTE ST 372).
    bool IsLevelB;       // The standard is 3G level B
    int InterfaceVidStd; // The 1080i standard of the interface, whose audio it carries
} DtSdiGeometry;

// The number of the interface's line that holds the last line of field 1 of a 3G level-B
// frame. Field 2 starts on the next one.
#define DT_SDIGEOMETRY_LEVELB_FIELD1_END 562

// Fills in *Geo for video standard VidStd.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_VIDSTD  VidStd is not a standard, or is 2160p on 3G level B links
DtapiResult DtSdiGeometry_Init(DtSdiGeometry* Geo, int VidStd);

// Returns whether the active part of raw line LineIndex is vertical blanking rather than
// image.
bool DtSdiGeometry_IsVanc(const DtSdiGeometry* Geo, int LineIndex);

// Returns the number of the interface's line, from 1, that carries line LineIndex of a
// 3G level-B picture of field Field, on the link DtSdiGeometry_LevelBLink() gives. Line 1
// of a field-1 picture lies on line 1125 of the interface frame before; this returns 0
// for it.
int DtSdiGeometry_LevelBInterfaceLine(int Field, int LineIndex);

// Returns the link that carries line LineIndex of a 3G level-B picture of field Field (1
// or 2): 1 for link A, 2 for link B. In field 1 the even lines, counted from 1, are link
// A's; in field 2 the odd lines.
int DtSdiGeometry_LevelBLink(int Field, int LineIndex);

// Returns the index of the raw line that holds line ImageLine of the image, for a
// standard that is not 2160p.
int DtSdiGeometry_RawLine(const DtSdiGeometry* Geo, int ImageLine);
