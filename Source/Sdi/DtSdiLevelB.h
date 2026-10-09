// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiLevelB.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - 3G level B: between two pictures and the frame on the line
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "DtSdiConv.h"     // Packing and unpacking.
#include "DtSdiCrc.h"      // The line CRCs.
#include "DtSdiGeometry.h" // The picture's lines and streams.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Level B +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A 3G level-B signal carries two pictures in one frame of an interlaced interface of
// half the picture rate (SMPTE ST 372, ST 425-1): field 1, the earlier picture, on
// interface lines 1 to 562, and field 2 on lines 563 to 1125. Line 563 is the last line
// of the interface's first field in its timing (F is set from line 564 on, as in 1080i),
// but carries the second picture's first lines (ST 372, Figure 2). Each interface line
// carries two lines of a picture, one on link A and one on link B; see
// DtSdiGeometry_LevelBInterfaceLine(). The card holds each picture as a frame of level
// A. An .sdi file, and a raw frame of DtInpChannel_ReadFrame() and
// DtOutpChannel_WriteFrame(), holds the frame of the interface as the line carries it:
//   - its lines are those of the interface, each the words of link B and link A
//     interleaved, B's first (SMPTE ST 424), and each link's words C first;
//   - each link has the timing references of the interlaced interface, the interface's
//     line numbers, and its own CRCs.
//
// The converter puts pictures into such a frame and takes them out. Line 1 of a field-1
// picture belongs on interface line 1125 of link B of the frame before; a frame gets
// blanking there, and a picture taken out of a frame gets blanking on its line 1.
//
// A picture taken out gets the timing references and line numbers of level A, and CRC
// words of 200 (hex), for the transmitter to fill in. Lines put into a frame get their
// CRCs.
//

// The most words in one link's line: 1080p50 has 2 x 2640.
#define DT_SDILEVELB_MAX_LINK_WORDS 5280

// The state of a converter.
typedef struct DtSdiLevelB
{
    DtSdiGeometry Geo;       // The picture's standard, of 3G level B
    DtFrameProps Interface;  // The interlaced interface
    const DtSdiConv* Conv;   // Packing and unpacking
    DtSdiCrcFunc Crc;        // The CRC of a line's active part
    uint32_t CrcTable[1024]; // The CRC-18 of each 10-bit word, from a CRC of 0
    int CrcLine; // The interface line whose active part LastCrc covers; 0 if none
    uint32_t LastCrc[2][2]; // Per link (A, B) and stream (C, Y): that CRC
    uint16_t Link[2][DT_SDILEVELB_MAX_LINK_WORDS];  // One line of link A and of link B
    uint16_t Line[2 * DT_SDILEVELB_MAX_LINK_WORDS]; // One interface line
} DtSdiLevelB;

// Writes a black picture into Picture, a raw frame of level A of the standard with
// BitsPerSymbol bits per symbol: blanking with the timing of level A, and CRC words for
// the transmitter.
void DtSdiLevelB_BlackPicture(DtSdiLevelB* Converter, uint8_t* Picture,
                              int BitsPerSymbol);

// Returns the size in bytes of a frame of the interface of Geo's 3G level-B standard,
// with BitsPerSymbol (10 or 16) bits per symbol, padding included, as
// DtSdiFrame_RawSize() gives that of a raw frame.
size_t DtSdiLevelB_FrameSize(const DtSdiGeometry* Geo, int BitsPerSymbol);

// Sets up Converter for 3G level-B standard VidStd.
//
// Returns DTAPI_OK, or DTAPI_E_INVALID_VIDSTD when VidStd is not 3G level B.
DtapiResult DtSdiLevelB_Init(DtSdiLevelB* Converter, int VidStd);

// Puts picture Picture, a raw frame of level A of the standard with PictureBits bits per
// symbol, into the lines of field Field (1 or 2) of the interface frame Frame, with
// FrameBits bits per symbol. The other field's lines stay as they are. The CRC of each
// line covers the line before: for the first line of field 2 that is line 562 of Frame,
// and for line 1 the last line put into a frame, when that was line 1125.
void DtSdiLevelB_PutField(DtSdiLevelB* Converter, int Field, const uint8_t* Picture,
                          int PictureBits, uint8_t* Frame, int FrameBits);

// Takes the picture of field Field (1 or 2) out of the interface frame Frame, with
// FrameBits bits per symbol, into Picture, a raw frame of level A with PictureBits bits
// per symbol.
void DtSdiLevelB_TakeField(DtSdiLevelB* Converter, int Field, const uint8_t* Frame,
                           int FrameBits, uint8_t* Picture, int PictureBits);
