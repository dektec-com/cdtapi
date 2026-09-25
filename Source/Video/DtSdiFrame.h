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
#include "cdtapi.h" // DtapiResult.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Coded frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The firmware exchanges SDI frames with the DMA buffers in two formats: "SDI RX simple"
// from a receiver and "SDI TX simple" to a transmitter. They differ in the header and,
// for 4K, in a line header before each coded line sent. Per frame:
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
// rate, divided by two-sample interleave: the pixel pairs of an even picture line go to
// links 1 and 2 in turn, those of an odd one to 3 and 4. The receiver undoes it, and a
// 4K frame has two coded lines per link line (plan 0014):
//
//   coded line 2n-1   HANC section of link 1, HANC section of link 2, video section
//   coded line 2n     HANC section of link 3, HANC section of link 4, video section
//
// A HANC section is one link's EAV, HANC and SAV, its C and Y words interleaved, C first.
// On a picture line the video section is the picture line itself, 3840 pixels, C Y C Y;
// on a blanking line it is the two links' own active parts side by side. A transmitter
// takes the same lines, each after a line header of 4 bytes padded to the alignment,
// whose first byte is 1 for a blanking line and 0 for a picture line.
//

// The first word of every header.
#define DT_SDIFRAME_SYNC_WORD 0xFFEFFBFEu

// The headers' sizes before padding.
#define DT_SDIFRAME_HEADER_BYTES 16
#define DT_SDIFRAME_TX_HEADER_BYTES 20

// The format events per frame a channel asks the card for, in both directions; a
// channel's waits are a quarter frame long because of it.
#define DT_SDIFRAME_FMT_EVENTS_PER_FRAME 4

// The formats a header names.
#define DT_SDIFRAME_FORMAT_UNCOMPRESSED 0
#define DT_SDIFRAME_FORMAT_UNCOMPRESSED_4K 1

typedef struct DtSdiFrameLayout
{
    int VidStd;               // DTAPI_VIDSTD_ code
    bool Is4k;                // 2160p over one 6G or 12G link
    int Alignment;            // Bytes every part is padded to
    int HeaderNumBytes;       // The receive header with its padding
    int TxHeaderNumBytes;     // The transmit header with its padding
    int NumLines;             // Lines per frame: of the raw frame, and of each link of 4K
    int NumCodedLines;        // Coded lines per frame: NumLines, or twice that for 4K
    int LineNumSymsHanc;      // Symbols of EAV, HANC and SAV in a raw line
    int LineNumSymsVideo;     // Symbols in the active part of a raw line
    int NumHancSections;      // HANC sections per coded line: 1, or 2 for 4K
    int SectionNumSymsHanc;   // Symbols in one HANC section
    int SectionNumSymsVideo;  // Symbols in the video section
    int SectionBytesHanc;     // Bytes of one HANC section with its padding
    int SectionBytesVideo;    // Bytes of the video section with its padding
    int Stride;               // Bytes per coded line received
    int TxLineHeaderNumBytes; // Bytes before each coded line sent: 0, or 4 padded for 4K
    int TxStride;             // Bytes per coded line sent
    int PictureStart;         // For 4K the first link line of the picture, from 1
    int PictureEnd;           // For 4K the last link line of the picture, from 1
    int Format;               // The format a header of this standard names
    int SdiRate;              // DT_SDIRATE_ value of the standard
} DtSdiFrameLayout;

// Fills Layout for a video standard and a stream alignment in bits. Returns false for an
// unknown standard, a 4K standard made of level-B links, and an alignment that is not a
// positive number of whole bytes.
bool DtSdiFrame_LayoutInit(DtSdiFrameLayout* Layout, int VidStd, int AlignmentInBits);

// The bytes one coded frame takes: its header and its lines, for reception and for
// transmission.
size_t DtSdiFrame_CodedSize(const DtSdiFrameLayout* Layout);
size_t DtSdiFrame_TxCodedSize(const DtSdiFrameLayout* Layout);

// The pieces a channel divides a frame's lines into when the program leaves the number to
// the library. It follows the standard of the layout, the one the channel is set to, and
// not the fastest the port can carry: 4 for 2160p50 and 2160p60, which a 12G link
// carries; 2 for 2160p24 to 2160p30, which a 6G link carries; and 1 for everything up to
// 3G, where dividing costs more than it saves, SD on a 12G port included. The coding a
// second grows with the standard's rate, so each piece gets about the work of one 3G
// link. A port that sends 4K over four 3G links codes it as 12G.
int DtSdiFrame_NumWorkPieces(const DtSdiFrameLayout* Layout);

// The coded lines one raw line is made of, and the bytes all of them together take as
// they are received and as they are sent. For 4K a raw line is two coded lines, so the
// two sizes are not those of a single coded line, which are Stride and TxStride.
static inline int DtSdiFrame_NumCodedLinesPerLine(const DtSdiFrameLayout* Layout)
{
    return Layout->NumCodedLines / Layout->NumLines;
}
static inline size_t DtSdiFrame_CodedBytesPerLine(const DtSdiFrameLayout* Layout)
{
    return (size_t)DtSdiFrame_NumCodedLinesPerLine(Layout) * (size_t)Layout->Stride;
}
static inline size_t DtSdiFrame_TxBytesPerLine(const DtSdiFrameLayout* Layout)
{
    return (size_t)DtSdiFrame_NumCodedLinesPerLine(Layout) * (size_t)Layout->TxStride;
}

// A receive header, decoded.
typedef struct DtSdiFrameHeader
{
    uint32_t SyncWord;
    int ProtocolVersion;
    int Format;
    int FrameId;
    uint32_t PtpSeconds;
    uint32_t PtpNanoseconds;
} DtSdiFrameHeader;

// Decodes the DT_SDIFRAME_HEADER_BYTES bytes at Bytes.
void DtSdiFrame_DecodeHeader(const uint8_t* Bytes, DtSdiFrameHeader* Header);

// Encodes Header into DT_SDIFRAME_HEADER_BYTES bytes at Bytes. The reserved bits are 0.
void DtSdiFrame_EncodeHeader(const DtSdiFrameHeader* Header, uint8_t* Bytes);

// Checks a header: DTAPI_E_OUT_OF_SYNC for a wrong sync word; DTAPI_E_INVALID when
// ExpectedId is not -1 and the frame ID differs; DTAPI_E_INVALID_FORMAT for a format the
// layout does not expect; otherwise DTAPI_OK.
DtapiResult DtSdiFrame_CheckHeader(const DtSdiFrameLayout* Layout,
                                   const DtSdiFrameHeader* Header, int ExpectedId);

// A transmit header, decoded: each field holds the value of its bit field, unscaled.
// Encoding cuts a wider value to the field's width in the header.
typedef struct DtSdiFrameTxHeader
{
    uint32_t SyncWord;
    int ProtocolVersion;
    int Format;
    bool SdiRateValid;
    int SdiRate; // DT_SDIRATE_ value
    int FrameId;
    int NumCodedLines;
    int NumWordsHanc; // The HANC section's size in alignment words
    int NumSymsHanc;
    int NumWordsVideo; // The video section's size in alignment words
    int NumSymsVideo;
} DtSdiFrameTxHeader;

// Fills Header for a frame of Layout with frame ID FrameId: protocol version 0 and a
// valid SDI rate, and the coded lines and the sizes of one HANC section and of the video
// section.
void DtSdiFrame_TxHeaderInit(const DtSdiFrameLayout* Layout, int FrameId,
                             DtSdiFrameTxHeader* Header);

// True when raw line LineIndex, from 0, of a 4K frame is a blanking line: a link line
// before or after the picture. False for a picture line, and for a standard that is
// not 4K.
bool DtSdiFrame_IsBlankingLine(const DtSdiFrameLayout* Layout, int LineIndex);

// Writes the Layout->TxLineHeaderNumBytes bytes of the line header before coded line
// CodedIndex, from 0, of a frame sent: 1 for a blanking line, 0 for a picture line, then
// zeros. Writes nothing for a standard that is not 4K.
void DtSdiFrame_EncodeTxLineHeader(const DtSdiFrameLayout* Layout, int CodedIndex,
                                   uint8_t* Bytes);

// Decodes the DT_SDIFRAME_TX_HEADER_BYTES bytes at Bytes.
void DtSdiFrame_DecodeTxHeader(const uint8_t* Bytes, DtSdiFrameTxHeader* Header);

// Encodes Header into DT_SDIFRAME_TX_HEADER_BYTES bytes at Bytes, reserved bits 0.
void DtSdiFrame_EncodeTxHeader(const DtSdiFrameTxHeader* Header, uint8_t* Bytes);

// The bytes at the start of a coded line that hold its EAV and, in HD, its line number:
// twelve symbols.
#define DT_SDIFRAME_LINE_START_BYTES 15

// Checks that a frame's lines start and end where they should: in HD the first line has
// line number 1 and the last the frame's number of lines, in both channels and after a
// valid EAV; in SD the first line's EAV has the XYZ of line 1 and the last line's that of
// the last line, in their upper eight bits. FirstLine and LastLine point to
// DT_SDIFRAME_LINE_START_BYTES bytes at the start of the first and the last coded line.
// Returns DTAPI_OK or DTAPI_E_OUT_OF_SYNC.
DtapiResult DtSdiFrame_CheckLines(const DtSdiFrameLayout* Layout,
                                  const uint8_t* FirstLine, const uint8_t* LastLine);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Raw frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The raw SDI frame, as a receive channel delivers it and a transmit channel takes it in
// DTAPI_RXMODE_SDI_FULL and DTAPI_TXMODE_SDI_FULL: every line of the frame, EAV first, as
// one stream of symbols, padded with zero bits to a 64-bit boundary. A symbol takes
//
//   8 bits    its upper eight bits
//   10 bits   packed, least significant bit first, as in the coded frame
//   16 bits   its value unshifted, little endian
//
// A line of 10-bit symbols takes whole bytes in every standard except 720p23.98 and
// 720p24. Their lines end half-way through a byte, so every other line starts at bit 4 of
// a byte it shares with the line before it.
//

// The bytes of a raw frame whose symbols take BitsPerSymbol, 8, 10 or 16, padding
// included; 0 for any other symbol size.
size_t DtSdiFrame_RawSize(const DtSdiFrameLayout* Layout, int BitsPerSymbol);

// The bits one line of a raw frame takes whose symbols take BitsPerSymbol, 8, 10 or 16; 0
// for any other symbol size. Line LineIndex, from 0, starts at bit LineIndex times that.
size_t DtSdiFrame_RawLineNumBits(const DtSdiFrameLayout* Layout, int BitsPerSymbol);

// The fewest lines whose raw bits make a whole number of bytes, from 1 to 8. A band of
// lines that starts on a multiple of them shares no byte of the raw frame with the band
// before it, so bands cut that way may be converted at once on more than one thread. It
// is 1 wherever every line starts on a byte of its own, which is every 8- and 16-bit
// frame and every 4K one; only a 10-bit frame that is not 4K can need more.
int DtSdiFrame_BandLineStep(const DtSdiFrameLayout* Layout, int BitsPerSymbol);

// For a standard that is not 4K: decodes the coded line at CodedLine, the line with
// index LineIndex from 0, into its place in the raw frame at Raw, whose symbols take
// BitsPerSymbol, 8, 10 or 16. With 10 bits a line can share a byte with the line before
// or after it, so the raw frame must be cleared beforehand; the lines can then be
// converted in any order. Padding bits of the coded line are not copied.
void DtSdiFrame_DecodeLine(const DtSdiFrameLayout* Layout, int BitsPerSymbol,
                           const uint8_t* CodedLine, int LineIndex, uint8_t* Raw);

// For a standard that is not 4K: encodes one raw line into the coded line at CodedLine,
// Layout->Stride bytes: the HANC section and the video section, each padded with zero
// bits. The symbols take BitsPerSymbol, 8, 10 or 16; a 16-bit symbol gives its lower ten
// bits, and an 8-bit one its eight bits shifted up by two. The line's first bit is bit
// Phase, from 0 for the least significant to 7, of the byte at RawLine, and no byte after
// the one holding its last bit is read. Returns false, writing nothing, for another
// symbol size, a Phase outside 0 to 7, and a Phase other than 0 with 8 or 16 bits.
bool DtSdiFrame_EncodeLine(const DtSdiFrameLayout* Layout, int BitsPerSymbol,
                           const uint8_t* RawLine, int Phase, uint8_t* CodedLine);

// A raw 4K line is the link's data stream as a 6G or 12G link carries it (SMPTE ST
// 2081-10 and 2082-10, and DekTec's SDI File Format Specification): the eight streams of
// the four links, C and Y of each, word by word. Per eight words, the C words of links 4,
// 2, 3 and 1, then their Y words. Each link's stream is its HANC section followed by its
// active part, the pixel pairs of the picture line that are its own. A raw 4K line takes
// whole bytes in every symbol size.
//
// The conversions work through a scratch buffer of DtSdiFrame_NumScratchSymbols(Layout)
// 16-bit symbols the caller provides, so that they allocate nothing per line. It holds
// one raw line's symbols in the order the raw line carries them, one to a 16-bit word:
// what the coded lines have been read into but not yet packed to the raw line's symbol
// size, or what the raw line has been unpacked into but not yet written to the coded
// lines. So it is what the portable conversion hands from its first pass to its second,
// and a line of a 2160p standard is some tens of kilobytes, which stays in the cache.
//
// The vector conversions do both passes over a tile at a time and keep the symbols in
// registers, so they touch the buffer only where they fall back to the portable code,
// which is 8-bit symbols; for 10 and 16 bits they leave it untouched. One buffer serves
// any number of lines in turn, but not two conversions at once: a caller converting the
// lines of a frame on more than one thread gives each thread its own.
//
// A layout that is not 4K needs none, and the count is 0.
size_t DtSdiFrame_NumScratchSymbols(const DtSdiFrameLayout* Layout);

// Decodes coded lines 2n-1 and 2n of a 4K frame, at CodedA and CodedB, into raw line
// LineIndex, n-1 from 0, at RawLine, whose symbols take BitsPerSymbol, 8, 10 or 16: the
// line's DtSdiFrame_RawLineNumBits / 8 bytes, which in a raw 4K frame start at LineIndex
// times that. Does nothing for another symbol size or a standard that is not 4K. Padding
// bits of the coded lines are not copied.
void DtSdiFrame_DecodeLine4k(const DtSdiFrameLayout* Layout, int BitsPerSymbol,
                             const uint8_t* CodedA, const uint8_t* CodedB, int LineIndex,
                             uint8_t* RawLine, uint16_t* Scratch);

// Encodes raw line LineIndex, n-1 from 0, of a 4K frame, whose symbols take
// BitsPerSymbol, 8, 10 or 16, at RawLine, into coded lines 2n-1 and 2n at CodedA and
// CodedB, Layout->Stride bytes each, their sections padded with zero bits; the line
// headers of transmission are not written. A 16-bit symbol gives its lower ten bits, and
// an 8-bit one its eight bits shifted up by two. Returns false, writing nothing, for
// another symbol size or a standard that is not 4K.
bool DtSdiFrame_EncodeLine4k(const DtSdiFrameLayout* Layout, int BitsPerSymbol,
                             const uint8_t* RawLine, int LineIndex, uint8_t* CodedA,
                             uint8_t* CodedB, uint16_t* Scratch);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Black frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A black frame holds the lines of a standard with nothing in them: every symbol that is
// not a timing reference is 200 in the chrominance and 040 in the luminance, which is
// black in the active video and empty blanking elsewhere. The timing references are those
// of the line: EAV and SAV with the field, vertical blanking and protection bits of the
// video standard's field layout, and in HD and 3G, for each channel, the line number and
// SMPTE 292's CRC-18 over that channel's active part of the line before it, then the EAV
// and the line number. For the first line the line before it is the frame's last line,
// which in a black frame is as black as any other.
//

// Writes the coded lines of a black frame of Layout's standard at Lines, as a transmitter
// takes them: Layout->NumCodedLines times Layout->TxStride bytes, line headers included,
// padding bits 0. A 4K frame's four links are each the black frame of a 1080p link.
// Returns false when a 4K frame's working memory cannot be allocated.
bool DtSdiFrame_BlackLines(const DtSdiFrameLayout* Layout, uint8_t* Lines);
