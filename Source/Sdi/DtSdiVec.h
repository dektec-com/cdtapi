// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiVec.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The conversions of SDI symbols that the parser and the builder spend their
// time in, in portable C and with SSSE3 and AVX2
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "cdtapi_sdi.h" // DtSdiParser and DtSdiBuilder.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Conversions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A line's symbols are 16-bit words, one symbol each, in the order of an SDI line's
// active part: Cb, Y, Cr, Y and so on. The conversions take a run of them to or from a
// raw frame's packed 10-bit symbols, and to or from each pixel format's bytes, the
// multi-byte values little endian. The symbols they take are of ten bits, 0 to 1023, as
// a raw frame holds them. Those from a pixel format limit every sample to 4..1019,
// which timing references keep apart. Every version gives the same result as
// the portable one, which is the reference; the vector versions work a block at a time
// and leave the rest of a run to it.
//
// No conversion reads or writes past the run it is given: Count symbols, or for 10-bit
// symbols Count * 10 / 8 bytes.
//

typedef struct DtSdiVec
{
    // Packed 10-bit symbols, the first at bit 0 of Bytes, least significant bit first;
    // Count a multiple of 4.
    void (*Unpack10)(const uint8_t* Bytes, size_t Count, uint16_t* Symbols);
    void (*Pack10)(const uint16_t* Symbols, size_t Count, uint8_t* Bytes);

    // Limits Count symbols of ten bits, 0 to 1023, to 4..1019, in place.
    void (*Limit)(uint16_t* Symbols, size_t Count);

    // The pixel formats; Count a multiple of 4, two pixels.
    void (*ToPlanar10)(const uint16_t* Symbols, size_t Count, uint8_t* Y, uint8_t* Cb,
                       uint8_t* Cr);
    void (*FromPlanar10)(const uint8_t* Y, const uint8_t* Cb, const uint8_t* Cr,
                         size_t Count, uint16_t* Symbols);
    void (*ToPlanar8)(const uint16_t* Symbols, size_t Count, uint8_t* Y, uint8_t* Cb,
                      uint8_t* Cr);
    void (*FromPlanar8)(const uint8_t* Y, const uint8_t* Cb, const uint8_t* Cr,
                        size_t Count, uint16_t* Symbols);
    void (*ToUyvy8)(const uint16_t* Symbols, size_t Count, uint8_t* Bytes);
    void (*FromUyvy8)(const uint8_t* Bytes, size_t Count, uint16_t* Symbols);
    void (*ToY210)(const uint16_t* Symbols, size_t Count, uint8_t* Bytes);
    void (*FromY210)(const uint8_t* Bytes, size_t Count, uint16_t* Symbols);

    // v210: three symbols a 32-bit word, in bits 0 to 9, 10 to 19 and 20 to 29; Count a
    // multiple of 4, the last word holding one or two symbols when Count is not a
    // multiple of 3, its other bits 0.
    void (*ToV210)(const uint16_t* Symbols, size_t Count, uint8_t* Bytes);
    void (*FromV210)(const uint8_t* Bytes, size_t Count, uint16_t* Symbols);
} DtSdiVec;

// Returns the portable conversions.
const DtSdiVec* DtSdiVec_C(void);

// Returns the conversions with SSSE3, or NULL when the processor or the build has none.
const DtSdiVec* DtSdiVec_Ssse3(void);

// Returns the conversions with AVX2, or NULL when the processor or the build has none.
const DtSdiVec* DtSdiVec_Avx2(void);

// Returns the fastest conversions the processor has.
const DtSdiVec* DtSdiVec_Best(void);

// The conversions with SSSE3 and AVX2, without asking the processor; for the functions
// above. They exist only in a build for x86 processors.
const DtSdiVec* DtSdiVec_Ssse3Unchecked(void);
const DtSdiVec* DtSdiVec_Avx2Unchecked(void);

// Makes Parser or Builder use Vec rather than the fastest conversions, for the tests and
// the benchmark that compare them. Vec must outlive their use.
void DtSdiParser_UseVec(DtSdiParser* Parser, const DtSdiVec* Vec);
void DtSdiBuilder_UseVec(DtSdiBuilder* Builder, const DtSdiVec* Vec);
