// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiView.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Views of SDI frames: what the library uses besides the API
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "DtSdiGeometry.h" // Where the image lies.
#include "cdtapi_sdi.h"    // The view.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Describes one SDI frame: its format and where its data is. A view points either at a
// raw frame (DtSdiView_SetRawFrame) or at a frame in an input channel's receive buffer,
// the ring (DtSdiView_SetRingFrame).
//
// In the ring, each raw line is stored as one coded line, or two for 2160p. A coded line
// holds the line's horizontal blanking and its active part, each in a section that
// starts on a byte. The frame can wrap around the end of the ring; the one line that
// does is copied into WrapLine when the frame is lent, so that every line can be read
// in one piece.
//
// Up to 3G, a section stores the symbols in the same order as a raw line, so a line can
// be read directly from the ring. A 2160p line has its own layout across two coded lines
// and is decoded first.
struct DtSdiView
{
    bool HasFrame;      // The view describes a frame
    DtSdiGeometry Geo;  // The frame's standard and where its image lies
    int LevelBField;    // 3G level B: the field of the interface frame the picture is,
                        // 1 or 2; 0 while not known, and for any other standard
    int BitsPerSymbol;  // 10 or 16
    uint8_t* Frame;     // The raw frame; NULL for a frame of an input channel
    size_t FrameSize;   // Bytes in Frame
    size_t LineNumBits; // Bits of one raw line
    void* Holder;       // The channel that lent the frame; NULL for a raw frame

    // For a frame in a ring only.
    uint8_t* RingBase;        // The ring
    size_t RingSize;          // Bytes in the ring
    size_t LinesStart;        // Where the frame's first coded line starts in the ring
    size_t CodedBytesPerLine; // Bytes of the coded lines of one raw line
    int WrapLineIndex;        // The raw line that wraps around the ring's end; -1 if none
    uint8_t* WrapLine;        // A copy of that line, in one piece
    size_t WrapLineRoom;      // Bytes allocated for WrapLine

    // For room for a frame in a transmit ring only.
    bool IsTx;    // The view describes room in an output channel's transmit ring
    bool IsBuilt; // The builder has written the whole frame since it was lent
};

// Buffers for decoding one 2160p line of a frame in a ring before it is read. Each thread
// that reads lines needs its own.
typedef struct DtSdiLineScratch
{
    uint8_t* Raw;      // The raw line
    uint16_t* Symbols; // The band buffer of DtSdiFrame_DecodeLine4k
} DtSdiLineScratch;

// Allocates the buffers Scratch needs to read lines of View's frame. Only a 2160p frame
// in a ring needs any. Returns false if there is not enough memory.
bool DtSdiLineScratch_Alloc(DtSdiLineScratch* Scratch, const DtSdiView* View);

// Frees Scratch's buffers. Scratch may have none, or be all zero.
void DtSdiLineScratch_Free(DtSdiLineScratch* Scratch);

// Returns a pointer to symbol Symbol (from 0) of raw line LineIndex (from 0), in the
// frame View describes. The view must describe a raw frame.
DtSdiSymbolPtr DtSdiView_RawSymbols(const DtSdiView* View, int LineIndex, size_t Symbol);

// Returns a pointer to symbol Symbol of raw line LineIndex, in a raw frame or in a ring.
// The symbols read from it must stay within either the line's horizontal blanking or its
// active part.
//
// A 2160p line in a ring is decoded into Scratch first; Scratch must have been prepared
// with DtSdiLineScratch_Alloc. The pointer is then valid until the next call with the
// same Scratch.
DtSdiSymbolPtr DtSdiView_LineSymbols(const DtSdiView* View, int LineIndex, size_t Symbol,
                                     DtSdiLineScratch* Scratch);

// Returns a pointer to the horizontal blanking of link Link (1 to 4) of raw line
// LineIndex, for a 2160p frame in a ring. Each link's blanking is a separate section,
// with C and Y words alternating, C first, as in an HD line, from EAV to SAV.
DtSdiSymbolPtr DtSdiView_LinkHanc(const DtSdiView* View, int LineIndex, int Link);

// Points View at a frame in a ring and copies the line that wraps around the ring's end,
// if any.
//   Layout      the layout of the coded lines, with the card's alignment
//   RingBase    the ring, RingSize bytes
//   LinesStart  where the frame's first coded line starts, from the ring's start
//   Holder      the input channel that holds the frame
// The symbols are 10 bits.
//
// Returns DTAPI_OK, DTAPI_E_INVALID_VIDSTD, or DTAPI_E_OUT_OF_MEM if there is no memory
// for the copy. After a failure View describes no frame.
DtapiResult DtSdiView_SetRingFrame(DtSdiView* View, const DtSdiFrameLayout* Layout,
                                   uint8_t* RingBase, size_t RingSize, size_t LinesStart,
                                   void* Holder);

// Returns which field of the interface frame, 1 or 2, the 3G level-B picture that View
// describes is, as the line of its payload ID of link A gives it; 0 when it has none, or
// when the view describes no picture of level B.
int DtSdiView_FindLevelBField(const DtSdiView* View);

// Sets which field of the interface frame, 1 or 2, the 3G level-B picture that View
// describes is. A channel calls it when it lends a picture. For any other standard the
// view keeps 0.
void DtSdiView_SetLevelBField(DtSdiView* View, int Field);

// Points View at one 3G level-B picture in the layout of level A, in the program's
// memory, as the card holds it; Field (1 or 2) is its field of the interface frame. It
// takes the arguments of DtSdiView_SetRawFrame(), which refuses level B, and returns its
// results; DTAPI_E_INVALID_VIDSTD also when VidStd is not 3G level B, and
// DTAPI_E_INVALID_ARG also for another Field.
DtapiResult DtSdiView_SetRawPicture(DtSdiView* View, void* Frame, size_t Size, int VidStd,
                                    int BitsPerSymbol, int Field);

// Points View at room for one frame of Layout in a transmit ring, for the builder to
// write the frame into.
//   Layout      the layout of the coded lines, with the card's alignment
//   RingBase    the transmit ring, RingSize bytes
//   LinesStart  where the frame's first coded line goes, from the ring's start
//   Holder      the output channel that lent the room
// The coded lines of the one raw line that would wrap around the ring's end go to a
// buffer of the view instead. DtSdiView_StoreWrapLine() copies them into the ring.
//
// Returns DTAPI_OK, DTAPI_E_INVALID_VIDSTD, or DTAPI_E_OUT_OF_MEM if there is no memory
// for that buffer. After a failure View describes no frame.
DtapiResult DtSdiView_SetTxFrame(DtSdiView* View, const DtSdiFrameLayout* Layout,
                                 uint8_t* RingBase, size_t RingSize, size_t LinesStart,
                                 void* Holder);

// Returns where the coded lines of raw line LineIndex (from 0) of a transmit view go:
// into the ring, or into the view's buffer for the line that wraps around the ring's
// end. A 2160p line has two coded lines, each Layout->TxStride bytes and each starting
// with its line header; other lines have one, without a header.
uint8_t* DtSdiView_TxCodedLines(const DtSdiView* View, int LineIndex);

// Copies the coded lines of the line that wraps around the ring's end from the view's
// buffer into the ring, in two pieces. Does nothing when no line wraps.
void DtSdiView_StoreWrapLine(const DtSdiView* View);

// Makes View describe no frame and clears its holder.
void DtSdiView_Forget(DtSdiView* View);
