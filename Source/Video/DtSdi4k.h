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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tiles +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A raw 4K line is built from, and split into, its four links four symbols at a time: two
// pixels of one link, which pack into exactly five bytes and which the raw line carries
// as two groups of eight words. Every section of a 4K line holds a multiple of four
// symbols, so a line is a whole number of such tiles and every tile lies on a byte
// boundary on both sides.
//

// Where the four links' five bytes of tile Tile lie in their coded lines, links 1 and 2
// in the first and links 3 and 4 in the second: the HANC sections first, tile by tile,
// and then the active part, whose blocks of four symbols a picture line gives to the two
// links of a coded line in turn and a blanking line in halves. The one rule both ways,
// and for every instruction set.
static inline void DtSdi4k_TileBlocks(const DtSdiFrameLayout* Layout, bool Blanking,
                                      size_t Tile, size_t Offset[4])
{
    const size_t HancTiles = (size_t)Layout->SectionNumSymsHanc / 4;
    const size_t HancBytes = (size_t)Layout->SectionBytesHanc;

    if (Tile < HancTiles)
    {
        for (size_t L = 0; L < 4; L++)
            Offset[L] = (L & 1) * HancBytes + 5 * Tile;
        return;
    }

    const size_t Video = 2 * HancBytes;
    const size_t Half = (size_t)Layout->SectionNumSymsVideo / 8 * 5;
    const size_t t = Tile - HancTiles;

    for (size_t L = 0; L < 4; L++)
        Offset[L] = Video + (Blanking ? (L & 1) * Half + 5 * t : 5 * (2 * t + (L & 1)));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Conversions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// DtSdiFrame_ConvertLine4k and DtSdiFrame_CodeLine4k are these, with the fastest set the
// processor runs. A test compares the sets with one another, and the benchmark measures
// them; nothing else calls them directly.
//

// Fills the raw line with the two coded lines 2n-1 and 2n at CodedA and CodedB, as
// DtSdiFrame_ConvertLine4k does.
typedef void (*DtSdi4kGather)(const DtSdiFrameLayout* Layout, int SymbolBits,
                              const uint8_t* CodedA, const uint8_t* CodedB, int LineIndex,
                              uint8_t* RawLine, uint16_t* Scratch);

// Fills the two coded lines with the raw line, as DtSdiFrame_CodeLine4k does.
typedef void (*DtSdi4kScatter)(const DtSdiFrameLayout* Layout, int SymbolBits,
                               const uint8_t* RawLine, int LineIndex, uint8_t* CodedA,
                               uint8_t* CodedB, uint16_t* Scratch);

typedef struct DtSdi4kConv
{
    DtSdi4kGather ConvertLine;
    DtSdi4kScatter CodeLine;
} DtSdi4kConv;

// The conversion in portable C.
const DtSdi4kConv* DtSdi4kConv_C(void);

// The conversion with SSSE3, or NULL when the library was built without it or the
// processor lacks SSSE3.
const DtSdi4kConv* DtSdi4kConv_Ssse3(void);

// The fastest conversion the processor runs.
const DtSdi4kConv* DtSdi4kConv_Best(void);

// The SSSE3 conversion, whatever the processor supports; only for the build of this
// library on x86, as DtSdi4kConv_Ssse3 chooses it.
const DtSdi4kConv* DtSdi4kConv_Ssse3Table(void);
