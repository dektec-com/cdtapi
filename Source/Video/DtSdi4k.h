// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdi4k.h #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The conversion of a 4K line between the ring and a raw frame
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "DtSdiFrame.h" // The layout the conversion follows.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tiles +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The 4K conversions build a raw 4K line from its four links, or split it into them, one
// "tile" at a time. A tile is four symbols of each link: two pixels, which pack into
// exactly five bytes, and which the raw line carries as two groups of eight words. Every
// section of a 4K line holds a multiple of four symbols. So a line is a whole number of
// tiles, and every tile starts and ends on a byte boundary.
//

// The order of the links (from 0) in a group of eight words of a raw 4K line: the C
// words of links 4, 2, 3 and 1, then their Y words in the same order.
static const size_t g_LinkOrder[4] = {3, 1, 2, 0};

// Returns in Offset[L] where the five bytes of tile Tile of link L (from 0) lie in its
// coded line. Links 1 and 2 are in the first coded line, links 3 and 4 in the second.
// The HANC sections come first, tile by tile, then the active part. On a picture line,
// the active part gives its blocks of four symbols to the two links in turn; on a
// blanking line (Blanking true), each link has one half. Both directions and every
// instruction set use this one function.
static inline void DtSdi4k_TileOffsets(const DtSdiFrameLayout* Layout, bool Blanking,
                                       size_t Tile, size_t Offset[4])
{
    const size_t HancTiles = (size_t)Layout->SectionNumSymsHanc / 4;
    const size_t HancNumBytes = (size_t)Layout->SectionBytesHanc;

    if (Tile < HancTiles)
    {
        for (size_t L = 0; L < 4; L++)
            Offset[L] = (L & 1) * HancNumBytes + 5 * Tile;
        return;
    }

    const size_t VideoStart = 2 * HancNumBytes;
    const size_t HalfBytes = (size_t)Layout->SectionNumSymsActive / 8 * 5;
    const size_t VideoTile = Tile - HancTiles;

    for (size_t L = 0; L < 4; L++)
        Offset[L] = VideoStart + (Blanking ? (L & 1) * HalfBytes + 5 * VideoTile
                                           : 5 * (2 * VideoTile + (L & 1)));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Conversions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The 4K line conversions exist in portable C and, on x86, with SSSE3.
// DtSdiFrame_DecodeLine4k and DtSdiFrame_EncodeLine4k use the fastest one the processor
// runs. The functions below are for the tests that compare the versions, and for the
// benchmark that measures them; and the SSSE3 version calls the portable one for 8-bit
// symbols.
//

// Decodes coded lines 2n-1 and 2n at CodedA and CodedB into the raw line, as
// DtSdiFrame_DecodeLine4k does.
typedef void (*DtSdi4kGather)(const DtSdiFrameLayout* Layout, int BitsPerSymbol,
                              const uint8_t* CodedA, const uint8_t* CodedB, int LineIndex,
                              uint8_t* RawLine, uint16_t* BandSymbols);

// Encodes the raw line into the two coded lines, as DtSdiFrame_EncodeLine4k does, but
// leaves the sections' padding untouched.
typedef void (*DtSdi4kScatter)(const DtSdiFrameLayout* Layout, int BitsPerSymbol,
                               const uint8_t* RawLine, int LineIndex, uint8_t* CodedA,
                               uint8_t* CodedB, uint16_t* BandSymbols);

// One version of the 4K line conversions.
typedef struct DtSdi4kConv
{
    DtSdi4kGather DecodeLine;  // Coded lines to raw line
    DtSdi4kScatter EncodeLine; // Raw line to coded lines
} DtSdi4kConv;

// Returns the conversion in portable C.
const DtSdi4kConv* DtSdi4kConv_C(void);

// Returns the conversion with SSSE3, or NULL when the library was built without it or
// the processor lacks SSSE3.
const DtSdi4kConv* DtSdi4kConv_Ssse3(void);

// Returns the fastest conversion the processor runs.
const DtSdi4kConv* DtSdi4kConv_Best(void);

// Returns the SSSE3 conversion, without checking the processor. It exists only in a
// build with SSSE3 on x86; DtSdi4kConv_Ssse3 returns it once CPUID reports SSSE3.
const DtSdi4kConv* DtSdi4kConv_Ssse3Unchecked(void);
