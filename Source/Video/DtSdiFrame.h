// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiFrame.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The firmware's coded SDI frames, the raw SDI frame, and black frames
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "DtFrameProps.h" // The fields of a frame.
#include "cdtapi.h"       // DtapiResult.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Coded frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The firmware moves SDI frames through the DMA buffers in a coded format: "SDI RX
// simple" from a receiver, and "SDI TX simple" to a transmitter. The two differ in the
// header and, for 4K, in a line header before each coded line sent. A frame is laid out
// as follows:
//
//   header      padded to the stream alignment
//                 receive, 16 bytes
//                   32 bits  sync word 0xFFEFFBFE
//                   32 bits  protocol version (bits 0-3), format (bits 4-7), reserved
//                            (bits 8-15), frame ID (bits 16-31)
//                   64 bits  PTP time of arrival: seconds, then nanoseconds
//                 transmit, 20 bytes
//                   32 bits  sync word 0xFFEFFBFE
//                   32 bits  protocol version (bits 0-3), format (bits 4-7), SDI rate
//                            valid (bit 8), SDI rate (bits 9-11), reserved (bits 12-31)
//                   32 bits  frame ID (bits 0-15), coded lines (bits 16-31)
//                   32 bits  the HANC section's size in alignment words (bits 0-15) and
//                            in symbols (bits 16-31)
//                   32 bits  the same for the video section
//   lines       one coded line per SDI line, two for 4K (below), each
//                 the HANC section: EAV, HANC and SAV
//                 the video section: the active part, video or VANC
//               both as packed 10-bit symbols, least significant bit first, and each
//               padded to the stream alignment
//
// All values are little endian. DtSdiFrameLayout holds the sizes and counts of a frame
// of one video standard.
//
// 2160p over one 6G or 12G link carries four links, each a 1080p stream of the same
// rate. The picture is divided over them by two-sample interleave: the pixel pairs of an
// even picture line go to links 1 and 2 in turn, those of an odd line to links 3 and 4.
// The receiver joins them again, and a 4K frame has two coded lines per link line (plan
// 0014):
//
//   coded line 2n-1   HANC section of link 1, HANC section of link 2, video section
//   coded line 2n     HANC section of link 3, HANC section of link 4, video section
//
// A HANC section is one link's EAV, HANC and SAV, with its C and Y words interleaved, C
// first. On a picture line, the video section is the picture line itself: 3840 pixels,
// C Y C Y. On a blanking line, it is the two links' own active parts side by side. A
// transmitter takes the same lines, each after a line header of 4 bytes padded to the
// alignment. The header's first byte is 1 for a blanking line and 0 for a picture line.
//

// The first word of every header.
#define DT_SDIFRAME_SYNC_WORD 0xFFEFFBFEu

// The sizes of the receive and transmit headers, before padding.
#define DT_SDIFRAME_HEADER_BYTES 16
#define DT_SDIFRAME_TX_HEADER_BYTES 20

// The number of format events per frame that a channel asks the card for, in both
// directions. This is why a channel waits at most a quarter frame at a time.
#define DT_SDIFRAME_FMT_EVENTS_PER_FRAME 4

// The formats a header can name.
#define DT_SDIFRAME_FORMAT_UNCOMPRESSED 0
#define DT_SDIFRAME_FORMAT_UNCOMPRESSED_4K 1

// The sizes and counts of a coded and a raw frame of one video standard.
typedef struct DtSdiFrameLayout
{
    int VidStd;               // The DTAPI_VIDSTD_ code
    bool Is4k;                // 2160p over one 6G or 12G link
    int AlignmentInBytes;     // The stream alignment every part is padded to, in bytes
    int RxHeaderNumBytes;     // The receive header with its padding
    int TxHeaderNumBytes;     // The transmit header with its padding
    int NumLines;             // Lines per frame: of the raw frame, and of each link of 4K
    int NumCodedLines;        // Coded lines per frame: NumLines, or twice that for 4K
    int LineNumSymsHanc;      // Symbols of EAV, HANC and SAV in a raw line
    int LineNumSymsActive;    // Symbols in the active part of a raw line
    int NumHancSections;      // HANC sections per coded line: 1, or 2 for 4K
    int SectionNumSymsHanc;   // Symbols in one HANC section
    int SectionNumSymsActive; // Symbols in the video section
    int SectionBytesHanc;     // Bytes of one HANC section with its padding
    int SectionBytesActive;   // Bytes of the video section with its padding
    int RxStride;             // Bytes per coded line received
    int TxLineHeaderNumBytes; // Bytes before each coded line sent: 0, or 4 padded for 4K
    int TxStride;             // Bytes per coded line sent
    int PictureStartLine;     // For 4K the first link line of the picture, from 1
    int PictureEndLine;       // For 4K the last link line of the picture, from 1
    int Format;               // The DT_SDIFRAME_FORMAT_ a header of this standard names
    int SdiRate;              // The standard's SDI rate, a DT_SDIRATE_ value
} DtSdiFrameLayout;

// Fills *Layout for video standard VidStd and a stream alignment of AlignmentInBits.
// Returns false for an unknown standard, a 4K standard made of level-B links, and an
// alignment that is not a positive number of whole bytes.
bool DtSdiFrame_LayoutInit(DtSdiFrameLayout* Layout, int VidStd, int AlignmentInBits);

// Return the size in bytes of one coded frame, header and lines, as it is received and
// as it is sent.
size_t DtSdiFrame_RxCodedSize(const DtSdiFrameLayout* Layout);
size_t DtSdiFrame_TxCodedSize(const DtSdiFrameLayout* Layout);

// Returns into how many pieces a channel splits the lines of a frame, when the program
// leaves the number to the library. Each piece then gets about the work of one 3G link.
// The number follows the layout's video standard, whatever the port carries:
//   4  2160p50 and up, which 12G-SDI carries
//   2  2160p23.98 to 2160p30, which 6G-SDI carries
//   1  everything up to 3G, where splitting costs more than it saves; this includes SD
//      on a 12G port
int DtSdiFrame_NumJobPieces(const DtSdiFrameLayout* Layout);

// Return how many coded lines make one raw line, and their total size in bytes as they
// are received and as they are sent. For 4K, a raw line is two coded lines, so these
// sizes are twice RxStride and TxStride, the sizes of a single coded line.
static inline int DtSdiFrame_NumCodedLinesPerLine(const DtSdiFrameLayout* Layout)
{
    return Layout->NumCodedLines / Layout->NumLines;
}
static inline size_t DtSdiFrame_RxCodedBytesPerLine(const DtSdiFrameLayout* Layout)
{
    return (size_t)DtSdiFrame_NumCodedLinesPerLine(Layout) * (size_t)Layout->RxStride;
}
static inline size_t DtSdiFrame_TxCodedBytesPerLine(const DtSdiFrameLayout* Layout)
{
    return (size_t)DtSdiFrame_NumCodedLinesPerLine(Layout) * (size_t)Layout->TxStride;
}

// A receive header, decoded.
typedef struct DtSdiFrameRxHeader
{
    uint32_t SyncWord;       // DT_SDIFRAME_SYNC_WORD in a valid header
    int ProtocolVersion;     // The protocol version
    int Format;              // A DT_SDIFRAME_FORMAT_ value
    int FrameId;             // The frame's ID, which counts up per frame
    uint32_t PtpSeconds;     // The PTP time of arrival: seconds
    uint32_t PtpNanoseconds; // ... and nanoseconds
} DtSdiFrameRxHeader;

// Decodes the receive header in the DT_SDIFRAME_HEADER_BYTES bytes at Bytes.
void DtSdiFrame_DecodeRxHeader(const uint8_t* Bytes, DtSdiFrameRxHeader* Header);

// Encodes Header into the DT_SDIFRAME_HEADER_BYTES bytes at Bytes, with the reserved
// bits 0.
void DtSdiFrame_EncodeRxHeader(const DtSdiFrameRxHeader* Header, uint8_t* Bytes);

// Checks that a received header is the one expected for a frame of Layout. Pass -1 as
// ExpectedId to accept any frame ID.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_OUT_OF_SYNC     the sync word is wrong
//   DTAPI_E_INVALID         the frame ID is not ExpectedId
//   DTAPI_E_INVALID_FORMAT  the format is not the layout's
DtapiResult DtSdiFrame_CheckRxHeader(const DtSdiFrameLayout* Layout,
                                     const DtSdiFrameRxHeader* Header, int ExpectedId);

// A transmit header, decoded. Each field holds the value of its bit field, unscaled.
// Encoding cuts a wider value to the field's width in the header.
typedef struct DtSdiFrameTxHeader
{
    uint32_t SyncWord;   // DT_SDIFRAME_SYNC_WORD
    int ProtocolVersion; // The protocol version
    int Format;          // A DT_SDIFRAME_FORMAT_ value
    bool SdiRateValid;   // True when SdiRate holds the rate
    int SdiRate;         // A DT_SDIRATE_ value
    int FrameId;         // The frame's ID
    int NumCodedLines;   // The number of coded lines in the frame
    int NumWordsHanc;    // The size of one HANC section, in alignment words
    int NumSymsHanc;     // ... and in symbols
    int NumWordsVideo;   // The size of the video section, in alignment words
    int NumSymsVideo;    // ... and in symbols
} DtSdiFrameTxHeader;

// Fills *Header for a frame of Layout with ID FrameId: protocol version 0, the layout's
// format and SDI rate (marked valid), its number of coded lines, and the sizes of one
// HANC section and of the video section.
void DtSdiFrame_TxHeaderInit(const DtSdiFrameLayout* Layout, int FrameId,
                             DtSdiFrameTxHeader* Header);

// Returns whether raw line LineIndex (from 0) of a 4K frame is a blanking line: a link
// line before or after the picture. Returns false for a picture line, and for a standard
// that is not 4K.
bool DtSdiFrame_IsBlankingLine(const DtSdiFrameLayout* Layout, int LineIndex);

// Writes the line header that goes before coded line CodedIndex (from 0) of a frame
// sent: Layout->TxLineHeaderNumBytes bytes at Bytes. The first byte is 1 for a blanking
// line and 0 for a picture line; the rest are 0. Writes nothing for a standard that is
// not 4K.
void DtSdiFrame_EncodeTxLineHeader(const DtSdiFrameLayout* Layout, int CodedIndex,
                                   uint8_t* Bytes);

// Decodes the transmit header in the DT_SDIFRAME_TX_HEADER_BYTES bytes at Bytes.
void DtSdiFrame_DecodeTxHeader(const uint8_t* Bytes, DtSdiFrameTxHeader* Header);

// Encodes Header into the DT_SDIFRAME_TX_HEADER_BYTES bytes at Bytes, with the reserved
// bits 0.
void DtSdiFrame_EncodeTxHeader(const DtSdiFrameTxHeader* Header, uint8_t* Bytes);

// The number of bytes at the start of a coded line that hold its EAV and, in HD, its
// line number: twelve symbols.
#define DT_SDIFRAME_LINE_START_BYTES 15

// Checks that a frame starts at its first line and ends at its last. FirstLine and
// LastLine point to the first DT_SDIFRAME_LINE_START_BYTES bytes of the first and the
// last coded line.
// - In HD, both lines must start with a valid EAV, and carry line number 1 and the
//   frame's number of lines, in both channels.
// - In SD, the upper eight bits of the XYZ word of each line's EAV must be those of the
//   first and the last line.
//
// Returns DTAPI_OK, or DTAPI_E_OUT_OF_SYNC when the lines are not where they should be.
DtapiResult DtSdiFrame_CheckLineNumbers(const DtSdiFrameLayout* Layout,
                                        const uint8_t* FirstLine,
                                        const uint8_t* LastLine);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Raw frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A raw SDI frame is what a receive channel delivers in DTAPI_RXMODE_SDI_FULL, and what
// a transmit channel takes in DTAPI_TXMODE_SDI_FULL: every line of the frame, EAV first,
// as one stream of symbols, padded with zero bits to a 64-bit boundary. A symbol takes:
//
//   8 bits    its upper eight bits
//   10 bits   packed, least significant bit first, as in the coded frame
//   16 bits   its value unshifted, little endian
//
// A line of 10-bit symbols takes whole bytes in every standard except 720p23.98 and
// 720p24. Their lines end half-way through a byte, so every other line starts at bit 4
// of a byte that it shares with the line before it.
//

// Returns the size in bytes of a raw frame with BitsPerSymbol (8, 10 or 16) bits per
// symbol, padding included. Returns 0 for any other symbol size.
size_t DtSdiFrame_RawSize(const DtSdiFrameLayout* Layout, int BitsPerSymbol);

// Returns the size in bits of one line of a raw frame with BitsPerSymbol (8, 10 or 16)
// bits per symbol. Line LineIndex (from 0) starts at bit LineIndex times this. Returns 0
// for any other symbol size.
size_t DtSdiFrame_RawLineNumBits(const DtSdiFrameLayout* Layout, int BitsPerSymbol);

// Returns the smallest number of lines, from 1 to 8, whose raw bits make a whole number
// of bytes. A band of lines that starts on a multiple of this shares no byte of the raw
// frame with the band before it. Bands cut that way can therefore be converted on
// several threads at once. It is 1 when every line starts on a byte of its own, as in
// every 8- and 16-bit frame and every 4K frame; only a 10-bit frame that is not 4K can
// need more.
int DtSdiFrame_BandLineStep(const DtSdiFrameLayout* Layout, int BitsPerSymbol);

// Decodes coded line CodedLine, which is line LineIndex (from 0), into its place in the
// raw frame at Raw, with BitsPerSymbol (8, 10 or 16) bits per symbol. Only for a
// standard that is not 4K. The coded line's padding bits are not copied.
//
// With 10 bits, a line can share a byte with the line before or after it. So clear the
// raw frame first; the lines can then be decoded in any order.
void DtSdiFrame_DecodeLine(const DtSdiFrameLayout* Layout, int BitsPerSymbol,
                           const uint8_t* CodedLine, int LineIndex, uint8_t* Raw);

// Encodes one raw line into the coded line at CodedLine, Layout->RxStride bytes: the
// HANC section and the video section, each padded with zero bits. Only for a standard
// that is not 4K.
//
// The raw line has BitsPerSymbol (8, 10 or 16) bits per symbol. A 16-bit symbol gives
// its lower ten bits; an 8-bit symbol gives its eight bits shifted up by two. The line's
// first bit is bit LineStartBit (0 = least significant, up to 7) of the byte at RawLine.
// No byte after the one that holds the line's last bit is read.
//
// Returns false, and writes nothing, for another symbol size, for a LineStartBit outside
// 0 to 7, and for a LineStartBit other than 0 with 8 or 16 bits.
bool DtSdiFrame_EncodeLine(const DtSdiFrameLayout* Layout, int BitsPerSymbol,
                           const uint8_t* RawLine, int LineStartBit, uint8_t* CodedLine);

// Returns the size, in 16-bit symbols, of the band buffer that the 4K conversions below
// need. Returns 0 for a layout that is not 4K, which needs none.
//
// A raw 4K line is the data stream of a 6G or 12G link (SMPTE ST 2081-10 and 2082-10,
// and DekTec's SDI File Format Specification): the eight streams of the four links, C
// and Y of each, word by word. Each group of eight words holds the C words of links 4,
// 2, 3 and 1, then their Y words. Each link's stream is its HANC section followed by its
// active part: its own pixel pairs of the picture line. A raw 4K line takes whole bytes
// in every symbol size.
//
// The caller provides the band buffer, so that the conversions allocate nothing per
// line. It holds one raw line's symbols, one per 16-bit word, in raw-line order: the
// portable conversion reads the coded lines into it and then packs it into the raw line,
// or the other way round. A 2160p line takes some tens of kilobytes, which stays in the
// cache. The vector conversions keep the symbols in registers, and use the buffer only
// for 8-bit symbols, where they fall back to the portable code.
//
// One buffer serves any number of lines in turn, but not two conversions at once: give
// each thread that converts lines its own buffer.
size_t DtSdiFrame_NumBandSymbols(const DtSdiFrameLayout* Layout);

// Decodes coded lines 2n-1 and 2n of a 4K frame, at CodedA and CodedB, into raw line
// LineIndex (n-1, from 0) at RawLine, with BitsPerSymbol (8, 10 or 16) bits per symbol.
// The raw line is DtSdiFrame_RawLineNumBits / 8 bytes; in a raw 4K frame, line LineIndex
// starts at LineIndex times that. BandSymbols is the band buffer (see
// DtSdiFrame_NumBandSymbols). The coded lines' padding bits are not copied. Does nothing
// for another symbol size or a standard that is not 4K.
void DtSdiFrame_DecodeLine4k(const DtSdiFrameLayout* Layout, int BitsPerSymbol,
                             const uint8_t* CodedA, const uint8_t* CodedB, int LineIndex,
                             uint8_t* RawLine, uint16_t* BandSymbols);

// Encodes raw line LineIndex (n-1, from 0) of a 4K frame, at RawLine, into coded lines
// 2n-1 and 2n at CodedA and CodedB: Layout->RxStride bytes each, with their sections
// padded with zero bits. The transmit line headers are not written. BandSymbols is the
// band buffer (see DtSdiFrame_NumBandSymbols).
//
// The raw line has BitsPerSymbol (8, 10 or 16) bits per symbol. A 16-bit symbol gives
// its lower ten bits; an 8-bit symbol gives its eight bits shifted up by two.
//
// Returns false, and writes nothing, for another symbol size or a standard that is not
// 4K.
bool DtSdiFrame_EncodeLine4k(const DtSdiFrameLayout* Layout, int BitsPerSymbol,
                             const uint8_t* RawLine, int LineIndex, uint8_t* CodedA,
                             uint8_t* CodedB, uint16_t* BandSymbols);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Black frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A transmit channel sends a black frame when it has nothing else to send. In a black
// frame, every symbol that is not a timing reference is 200 (hex) in the chrominance and
// 040 in the luminance: black in the active video, and empty blanking elsewhere.
//
// The timing references are those of each line: EAV and SAV with the field, vertical
// blanking and protection bits of the standard's field layout. In HD and 3G, each
// channel's EAV is followed by the line number and SMPTE 292's CRC-18. The CRC covers
// that channel's active part of the line before it, then the EAV and the line number.
// For the first line, the line before it is the frame's last line, which is just as
// black.
//

// Writes the coded lines of a black frame of Layout's standard at Lines, as a
// transmitter takes them: Layout->NumCodedLines times Layout->TxStride bytes, line
// headers included, padding bits 0. For 4K, each of the four links is the black frame of
// a 1080p link.
//
// Returns false for 4K when the layout of one link cannot be made, or there is not
// enough memory.
bool DtSdiFrame_WriteBlackLines(const DtSdiFrameLayout* Layout, uint8_t* Lines);

// Returns Crc after one more 10-bit word, Word: SMPTE 292's CRC-18, x^18 + x^5 + x^4 + 1,
// least significant bit first. A line's CRC starts at 0.
uint32_t DtSdiFrame_Crc18(uint32_t Crc, uint32_t Word);

// Returns the fourth word of a timing reference of line Line (from 1) of a frame of
// Props: EAV when Eav is true, else SAV, with the field, vertical blanking and EAV bits
// and the protection bits over them.
uint32_t DtSdiFrame_Xyz(const DtFrameProps* Props, int Line, bool Eav);

// Returns the lower nine bits of Nine with bit 9 the inverse of bit 8, as line numbers
// and CRC words carry them.
uint32_t DtSdiFrame_WithParity(uint32_t Nine);
