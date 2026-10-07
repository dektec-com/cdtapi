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

// A view of one SDI frame: its format and where its memory is. DtSdiView_SetRawFrame
// points it at a raw frame; an input channel, with DtSdiView_SetRingFrame, at a frame in
// its receive buffer, the ring.
//
// In the ring a frame is its coded lines, one a raw line, two for 2160p, one after the
// other, each its horizontal blanking and its active part in sections of their own
// that start on a byte. A frame can run across the end of the ring: the one line that
// does is copied into WrapLine when the frame is lent, so that every line can be read in
// one piece. Up to 3G a section holds its symbols as a raw line does, so a line is read
// where it lies, section by section; a 2160p line, whose links two coded lines share in
// their own way, is decoded first.
struct DtSdiView
{
    bool HasFrame;      // The view describes a frame
    DtSdiGeometry Geo;  // The frame's standard and where its image lies
    int BitsPerSymbol;  // 10 or 16
    uint8_t* Frame;     // The raw frame; NULL for a frame of an input channel
    size_t FrameSize;   // Bytes in Frame
    size_t LineNumBits; // Bits of one raw line
    void* Holder;       // The input channel that holds the frame; NULL for a raw frame

    // A frame in a ring: the ring, where the frame's first coded line starts in it, and
    // the bytes of a raw line's coded lines; the line that runs across the end of the
    // ring, -1 for none, its copy, and the bytes the copy has room for.
    const uint8_t* RingBase;
    size_t RingSize;
    size_t LinesStart;
    size_t CodedBytesPerLine;
    int WrapLineIndex;
    uint8_t* WrapLine;
    size_t WrapLineRoom;
};

// The buffers that a 2160p line of a frame in a ring is decoded into, to be read: of one
// thread that reads lines.
typedef struct DtSdiLineScratch
{
    uint8_t* Raw;      // The raw line
    uint16_t* Symbols; // The band buffer of DtSdiFrame_DecodeLine4k
} DtSdiLineScratch;

// Gives Scratch the buffers a line of View's frame needs: none but for 2160p in a ring.
// Returns false when there is no memory.
bool DtSdiLineScratch_Alloc(DtSdiLineScratch* Scratch, const DtSdiView* View);

// Frees Scratch's buffers. A scratch that has none, or is all zero, is fine.
void DtSdiLineScratch_Free(DtSdiLineScratch* Scratch);

// Returns where symbol Symbol (from 0) of raw line LineIndex (from 0) of the frame View
// describes lies. The view must describe a raw frame.
DtSdiSymbolPtr DtSdiView_RawSymbols(const DtSdiView* View, int LineIndex, size_t Symbol);

// Returns where symbol Symbol of raw line LineIndex lies, for a run of symbols that stays
// within the line's horizontal blanking, or within its active part: of a raw frame or a
// frame in a ring. A 2160p line in a ring is decoded into Scratch first, which
// DtSdiLineScratch_Alloc prepared; the pointer then stays good until Scratch takes the
// next line.
DtSdiSymbolPtr DtSdiView_LineSymbols(const DtSdiView* View, int LineIndex, size_t Symbol,
                                     DtSdiLineScratch* Scratch);

// Returns where the horizontal blanking of link Link (1 to 4) of raw line LineIndex of
// a 2160p frame in a ring starts: a section of its own in the coded lines, the link's
// C and Y words in turn, C first, as an HD line has them, EAV and SAV included.
DtSdiSymbolPtr DtSdiView_LinkHanc(const DtSdiView* View, int LineIndex, int Link);

// Points View at a frame of Layout, the layout of the ring's coded lines with the
// card's alignment, in a ring of RingSize bytes at RingBase, its first coded line
// LinesStart bytes from the ring's start, 10 bits a symbol, held by Holder; copies the
// line that runs across the end of the ring.
//
// Returns DTAPI_OK, DTAPI_E_INVALID_VIDSTD, or DTAPI_E_OUT_OF_MEM when there is no room
// for that copy; on a failure View describes no frame.
DtapiResult DtSdiView_SetRingFrame(DtSdiView* View, const DtSdiFrameLayout* Layout,
                                   const uint8_t* RingBase, size_t RingSize,
                                   size_t LinesStart, void* Holder);

// Makes View describe no frame, and forgets its holder.
void DtSdiView_Forget(DtSdiView* View);
