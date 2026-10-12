// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# SimLevelB.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - How the emulated card carries 3G level B on the cable, in symbols
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// CDTAPI includes
#include "Video/DtFrameProps.h" // The picture and the interface.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Level B on the cable +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A 3G level-B signal carries two pictures in one frame of an interlaced interface of
// half the picture rate (SMPTE ST 372, ST 425-1). The card's ring holds each picture in
// the layout of level A; the cable, and so the emulator's SDI files, carry the frame of
// the interface. These functions convert between the two, as the card does in hardware.
//
// In field 1, interface line t carries picture lines 2t and 2t + 1; in field 2,
// interface line 563 + t carries picture lines 2t + 1 and 2t + 2 (ST 372, Figure 2). So
// line 1 of a picture of field 1 lies on line 1125 of the frame before, which the
// emulator leaves blanking, as it leaves link B of line 1125 when it makes a frame. Each
// interface line holds the words of link B and link A interleaved, B's first (SMPTE ST
// 424), and each link's words C first. Each link has the timing references and line
// numbers of the interface and its own CRCs.
//
// A picture is NumLines lines of LineSyms symbols, each line from its EAV on, and a frame
// of the interface NumLines lines of 2 * LineSyms. A blank line holds 200 (hex) in C and
// 040 in Y outside its timing references, line number and CRC.
//
// The emulator works this layout out itself, from plan 0039, rather than use the
// library's conversion. The tests then check one against the other.

typedef struct SimLevelB
{
    DtFrameProps Picture;   // One picture, in the layout of level A
    DtFrameProps Interface; // The interlaced interface that carries two
    int NumLines;           // The lines of a picture, and of the interface
    int HancSyms;           // The symbols of a line before its active part
    int LineSyms;           // The symbols of a picture's line, and of a link's line
    uint32_t LinkCrc[2][2]; // Merge: per link (A, B) and stream (C, Y), the CRC of the
                            // active part of the line before
    uint32_t PictureCrc[2]; // Split: per stream, the CRC of the active part of the
                            // picture line before
} SimLevelB;

// Fills Picture with a black picture of field Field, 1 or 2: blanking, with the timing
// references, line numbers and CRCs of level A, and the payload ID of each link on its
// line of the field (SMPTE ST 352: interface line 10 in field 1, 572 in field 2), so
// that a receiver finds the picture's field as it finds that of any other.
void SimLevelB_BlackPicture(const SimLevelB* LevelB, int Field, uint16_t* Picture);

// Returns the field, 1 or 2, of a picture of level B, as the line of its payload ID of
// link A gives it: in field 1 link A has the even lines, in field 2 the odd ones. Looks
// in the horizontal blanking of the Y stream of the lines before the active picture, for
// a payload ID whose byte 1 is 8A and whose byte 4 has bit 6 clear. Returns 0 when there
// is none.
int SimLevelB_Field(const SimLevelB* LevelB, const uint16_t* Picture);

// Sets LevelB up for the 3G level-B standard VidStd, with the CRCs of the lines before
// starting from 0. Returns false for a standard that is not 3G level B over one 3G
// interface.
bool SimLevelB_Init(SimLevelB* LevelB, int VidStd);

// Puts the pictures of field 1 and field 2 into Frame, a frame of the interface, with
// its CRCs. The CRCs of its line 1 follow on from line 1125 of the frame merged before.
void SimLevelB_Merge(SimLevelB* LevelB, const uint16_t* Field1, const uint16_t* Field2,
                     uint16_t* Frame);

// Takes the pictures of field 1 and field 2 out of Frame, a frame of the interface, each
// with the timing references, line numbers and CRCs of level A, as the card's ring holds
// a picture it received. Line 1 of field 1 is blanking. The CRCs of that line follow on
// from line 1125 of the picture of field 2 split before.
void SimLevelB_Split(SimLevelB* LevelB, const uint16_t* Frame, uint16_t* Field1,
                     uint16_t* Field2);
