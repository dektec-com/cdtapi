// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiConv.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The conversions of SDI symbols that the parser and the builder use most, in
// portable C and with SSSE3 and AVX2
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
// These conversions turn a run of a line's symbols into another form and back. In the
// run, each symbol is a 16-bit word, in the order of an SDI line's active part: Cb, Y,
// Cr, Y and so on. The other form is either a raw frame's packed 10-bit symbols or the
// bytes of a pixel format. A pixel format stores its multi-byte values little endian.
//
// The symbols a conversion takes are 10-bit values, 0 to 1023, as a raw frame holds
// them. The symbols a conversion makes from a pixel format are limited to 4..1019,
// because SDI keeps 0 to 3 and 1020 to 1023 for timing references.
//
// Each conversion exists in portable C, with SSSE3 and with AVX2. The portable version is
// the reference, and every other version gives the same result. A vector version
// converts a block at a time and leaves the rest of the run to a slower version.
//
// No conversion reads or writes past the run it is given. A run is Count symbols, which
// take Count * 10 / 8 bytes as packed 10-bit symbols.
//

typedef struct DtSdiConv
{
    // Unpacks Count packed 10-bit symbols from Bytes into Symbols. The first symbol
    // starts at bit 0 of Bytes, and each symbol is stored least significant bit first.
    // Count is a multiple of 4. The symbols are not limited.
    void (*Unpack10)(const uint8_t* Bytes, size_t Count, uint16_t* Symbols);

    // Packs Count symbols into Bytes as 10-bit symbols, in the layout Unpack10 reads.
    // Count is a multiple of 4.
    void (*Pack10)(const uint16_t* Symbols, size_t Count, uint8_t* Bytes);

    // Limits Count 10-bit symbols, 0 to 1023, to 4..1019, in place.
    void (*Limit)(uint16_t* Symbols, size_t Count);

    // Splits Count symbols into three planes of 16-bit words: Y, Cb and Cr. Each word
    // holds its 10-bit value in the low bits. Count is a multiple of 4, two pixels.
    void (*ToPlanar10)(const uint16_t* Symbols, size_t Count, uint8_t* Y, uint8_t* Cb,
                       uint8_t* Cr);

    // Interleaves three planes of 16-bit words, Y, Cb and Cr, into Count symbols. It
    // takes the low 10 bits of each word. Count is a multiple of 4, two pixels.
    void (*FromPlanar10)(const uint8_t* Y, const uint8_t* Cb, const uint8_t* Cr,
                         size_t Count, uint16_t* Symbols);

    // Splits Count symbols into three planes of bytes: Y, Cb and Cr. Each byte holds the
    // top 8 bits of a symbol. Count is a multiple of 4, two pixels.
    void (*ToPlanar8)(const uint16_t* Symbols, size_t Count, uint8_t* Y, uint8_t* Cb,
                      uint8_t* Cr);

    // Interleaves three planes of bytes, Y, Cb and Cr, into Count symbols. Each byte
    // becomes the top 8 bits of a symbol, with two zero bits below. Count is a multiple
    // of 4, two pixels.
    void (*FromPlanar8)(const uint8_t* Y, const uint8_t* Cb, const uint8_t* Cr,
                        size_t Count, uint16_t* Symbols);

    // Writes Count symbols as UYVY bytes, one byte a symbol, in the symbols' order. Each
    // byte holds the top 8 bits of a symbol. Count is a multiple of 4, two pixels.
    void (*ToUyvy8)(const uint16_t* Symbols, size_t Count, uint8_t* Bytes);

    // Reads Count symbols from UYVY bytes. Each byte becomes the top 8 bits of a symbol,
    // with two zero bits below. Count is a multiple of 4, two pixels.
    void (*FromUyvy8)(const uint8_t* Bytes, size_t Count, uint16_t* Symbols);

    // Writes Count symbols as Y210. Each pair of pixels takes four 16-bit words, in the
    // order Y0, Cb, Y1, Cr, with the 10 bits at the top of each word. Count is a multiple
    // of 4, two pixels.
    void (*ToY210)(const uint16_t* Symbols, size_t Count, uint8_t* Bytes);

    // Reads Count symbols from Y210, in the layout ToY210 writes. It takes the top 10
    // bits of each word. Count is a multiple of 4, two pixels.
    void (*FromY210)(const uint8_t* Bytes, size_t Count, uint16_t* Symbols);

    // Writes Count symbols as v210. Each 32-bit word holds three symbols, in bits 0 to 9,
    // 10 to 19 and 20 to 29. Count is a multiple of 4. When Count is not a multiple of 3,
    // the last word holds one or two symbols and its other bits are 0.
    void (*ToV210)(const uint16_t* Symbols, size_t Count, uint8_t* Bytes);

    // Reads Count symbols from v210, in the layout ToV210 writes. Count is a multiple
    // of 4.
    void (*FromV210)(const uint8_t* Bytes, size_t Count, uint16_t* Symbols);

    // Splits the active part of a raw 2160p line into its two image lines, Upper and
    // Lower. Pixels is the number of pixels each link carries, and is even.
    //
    // The raw line takes eight words for each word n of the links. First come word n of
    // the C streams of links 4, 2, 3 and 1, then word n of their Y streams. Links 1 and 2
    // carry the pixel pairs of the upper image line in turn. Links 3 and 4 do the same
    // for the lower image line.
    void (*Split4k)(const uint16_t* Raw, size_t Pixels, uint16_t* Upper, uint16_t* Lower);

    // Joins two image lines, Upper and Lower, into the active part of a raw 2160p line,
    // in the layout Split4k reads. Pixels is the number of pixels each link carries, and
    // is even.
    void (*Join4k)(const uint16_t* Upper, const uint16_t* Lower, size_t Pixels,
                   uint16_t* Raw);

    // Joins a line of link B and a line of link A of 3G level B, Count words each, C and
    // Y in turn, into a line of the interface of 2 * Count words: word 0 of link B, word
    // 0 of link A, word 1 of link B, and so on (SMPTE ST 424 puts data stream two, link
    // B, first). Count is a multiple of 4.
    void (*JoinLevelB)(const uint16_t* LinkB, const uint16_t* LinkA, size_t Count,
                       uint16_t* Line);

    // Splits a line of the interface of 3G level B, 2 * Count words, into its line of
    // link B and its line of link A, in the layout JoinLevelB writes. Count is a
    // multiple of 4.
    void (*SplitLevelB)(const uint16_t* Line, size_t Count, uint16_t* LinkB,
                        uint16_t* LinkA);
} DtSdiConv;

// Returns the portable conversions.
const DtSdiConv* DtSdiConv_C(void);

// Returns the conversions with SSSE3, or NULL when the processor or the build has none.
const DtSdiConv* DtSdiConv_Ssse3(void);

// Returns the conversions with AVX2, or NULL when the processor or the build has none.
const DtSdiConv* DtSdiConv_Avx2(void);

// Returns the fastest conversions the processor has.
const DtSdiConv* DtSdiConv_Best(void);

// Returns the conversions with SSSE3 without asking the processor whether it has SSSE3.
// DtSdiConv_Ssse3 calls it after it checks, and the AVX2 conversions hand the rest of a
// run to it. It exists only in a build for x86 processors.
const DtSdiConv* DtSdiConv_Ssse3Unchecked(void);

// Returns the conversions with AVX2 without asking the processor whether it has AVX2.
// DtSdiConv_Avx2 calls it after it checks. It exists only in a build for x86 processors.
const DtSdiConv* DtSdiConv_Avx2Unchecked(void);

// Makes Parser use Conv instead of the fastest conversions. The tests and the benchmark
// use it to compare the versions. Conv must outlive its use.
void DtSdiParser_UseConv(DtSdiParser* Parser, const DtSdiConv* Conv);

// Makes Builder use Conv instead of the fastest conversions. The tests and the benchmark
// use it to compare the versions. Conv must outlive its use.
void DtSdiBuilder_UseConv(DtSdiBuilder* Builder, const DtSdiConv* Conv);
