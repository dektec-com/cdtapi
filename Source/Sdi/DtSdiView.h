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

// A view of one SDI frame: its format and where its memory is. An input channel fills it
// for a frame in its receive buffer; DtSdiView_SetRawFrame for a raw frame.
struct DtSdiView
{
    bool HasFrame;      // The view describes a frame
    DtSdiGeometry Geo;  // The frame's standard and where its image lies
    int BitsPerSymbol;  // 10 or 16
    uint8_t* Frame;     // The raw frame; NULL for a frame of an input channel
    size_t FrameSize;   // Bytes in Frame
    size_t LineNumBits; // Bits of one raw line
    void* Holder;       // The input channel that holds the frame; NULL for a raw frame
};

// Returns where symbol Symbol (from 0) of raw line LineIndex (from 0) of the frame View
// describes lies. The view must describe a raw frame.
DtSdiSymbolPtr DtSdiView_RawSymbols(const DtSdiView* View, int LineIndex, size_t Symbol);
