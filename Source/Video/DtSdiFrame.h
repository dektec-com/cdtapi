// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiFrame.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The firmware's coded SDI frames, DTAPI's raw SDI frame, and black frames
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
// from a receiver (DTAPI: MxChannelMemlessRx::SdiRxSimpleProps) and "SDI TX simple" to a
// transmitter (MxChannelMemlessTx::SdiTxSimpleProps). They differ only in the header. Per
// frame:
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
//   lines       one coded line per SDI line, each
//                 the HANC section: EAV, HANC and SAV
//                 the video section: the active part, video or VANC
//               both as packed 10-bit symbols, least significant bit first, and each
//               padded to the stream alignment
//
// All values are little endian. The layout below holds what a frame of one video
// standard takes. 4K, whose coded lines have two sections of each kind, is not described.
//

// The first word of every header.
#define DT_SDIFRAME_SYNC_WORD 0xFFEFFBFEu

// The headers' sizes before padding.
#define DT_SDIFRAME_HEADER_BYTES 16
#define DT_SDIFRAME_TX_HEADER_BYTES 20

// The formats a header names.
#define DT_SDIFRAME_FORMAT_UNCOMPRESSED 0
#define DT_SDIFRAME_FORMAT_UNCOMPRESSED_4K 1

typedef struct DtSdiFrameLayout
{
    int VidStd;         // DTAPI_VIDSTD_ code
    int Alignment;      // Bytes every part is padded to
    int HeaderBytes;    // The receive header with its padding
    int TxHeaderBytes;  // The transmit header with its padding
    int NumLines;       // Coded lines per frame
    int LineSymsHanc;   // Symbols in the HANC section
    int LineBytesHanc;  // Bytes of the HANC section with its padding
    int LineSymsVideo;  // Symbols in the video section
    int LineBytesVideo; // Bytes of the video section with its padding
    int Stride;         // Bytes per coded line
    int Format;         // The format a header of this standard names
    int SdiRate;        // DT_SDIRATE_ value of the standard: SD, HD or 3G
} DtSdiFrameLayout;

// Fills Layout for a video standard and a stream alignment in bits, as
// SdiRxSimpleProps::Init and SdiTxSimpleProps::Init. Returns false for an unknown
// standard, a 4K standard, and an alignment that is not a positive number of whole bytes.
bool DtSdiFrame_LayoutInit(DtSdiFrameLayout* Layout, int VidStd, int AlignmentBits);

// The bytes one coded frame takes: its header and its lines, for reception and for
// transmission.
size_t DtSdiFrame_CodedSize(const DtSdiFrameLayout* Layout);
size_t DtSdiFrame_TxCodedSize(const DtSdiFrameLayout* Layout);

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

// Checks a header as MxChannelMemlessRx::CheckFrameHeader does: DTAPI_E_OUT_OF_SYNC for
// a wrong sync word; DTAPI_E_INVALID when ExpectedId is not -1 and the frame ID differs;
// DTAPI_E_INVALID_FORMAT for a format the layout does not expect; otherwise DTAPI_OK.
DtapiResult DtSdiFrame_CheckHeader(const DtSdiFrameLayout* Layout,
                                   const DtSdiFrameHeader* Header, int ExpectedId);

// A transmit header, decoded. Fields keep the widths the header gives them.
typedef struct DtSdiFrameTxHeader
{
    uint32_t SyncWord;
    int ProtocolVersion;
    int Format;
    bool SdiRateValid;
    int SdiRate; // DT_SDIRATE_ value
    int FrameId;
    int NumLines;
    int NumWordsHanc; // The HANC section's size in alignment words
    int NumSymsHanc;
    int NumWordsVideo; // The video section's size in alignment words
    int NumSymsVideo;
} DtSdiFrameTxHeader;

// Fills Header for a frame of Layout with frame ID FrameId, as
// MxChannelMemlessTx::SetVidStd does: protocol version 0 and a valid SDI rate.
void DtSdiFrame_TxHeaderInit(const DtSdiFrameLayout* Layout, int FrameId,
                             DtSdiFrameTxHeader* Header);

// Decodes the DT_SDIFRAME_TX_HEADER_BYTES bytes at Bytes.
void DtSdiFrame_DecodeTxHeader(const uint8_t* Bytes, DtSdiFrameTxHeader* Header);

// Encodes Header into DT_SDIFRAME_TX_HEADER_BYTES bytes at Bytes, reserved bits 0.
void DtSdiFrame_EncodeTxHeader(const DtSdiFrameTxHeader* Header, uint8_t* Bytes);

// The bytes at the start of a coded line that hold its EAV and, in HD, its line number:
// twelve symbols.
#define DT_SDIFRAME_LINE_START_BYTES 15

// Checks that a frame's lines start and end where they should, as HdSdiUtil::
// CheckFrameSync does for a raw frame: in HD the first line has line number 1 and the
// last the frame's number of lines, in both channels and after a valid EAV; in SD the
// first line's EAV has the XYZ of line 1 and the last line's that of the last line, in
// their upper eight bits. FirstLine and LastLine point to DT_SDIFRAME_LINE_START_BYTES
// bytes at the start of the first and the last coded line. Returns DTAPI_OK or
// DTAPI_E_OUT_OF_SYNC.
DtapiResult DtSdiFrame_CheckLines(const DtSdiFrameLayout* Layout,
                                  const uint8_t* FirstLine, const uint8_t* LastLine);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Raw frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// DTAPI's raw SDI frame, as ReadFrame delivers it and Write takes it in
// DTAPI_RXMODE_SDI_FULL and DTAPI_TXMODE_SDI_FULL: every line of the frame, EAV first, as
// one stream of symbols, padded with zero bits to a 64-bit boundary
// (SdiRxImpl_Bb2::RxIdle2Receive, PxCnvTaskRaw::Run). A symbol takes
//
//   8 bits    its upper eight bits
//   10 bits   packed, least significant bit first, as in the coded frame
//   16 bits   its value unshifted, little endian
//
// A line of 10-bit symbols takes whole bytes in every standard but 720p23.98 and 720p24.
// Their lines end half-way a byte, so every other line starts at bit 4 of a byte it
// shares with the line before it.
//

// The bytes of a raw frame whose symbols take SymbolBits, 8, 10 or 16, padding included;
// 0 for any other symbol size.
size_t DtSdiFrame_RawSize(const DtSdiFrameLayout* Layout, int SymbolBits);

// The bits one line of a raw frame takes whose symbols take SymbolBits, 8, 10 or 16; 0
// for any other symbol size. Line LineIndex, from 0, starts at LineIndex times that bit.
size_t DtSdiFrame_RawLineBits(const DtSdiFrameLayout* Layout, int SymbolBits);

// Converts the coded line at CodedLine, the line with index LineIndex from 0, into its
// place in the raw frame at Raw, whose symbols take SymbolBits, 8, 10 or 16. With 10 bits
// a line can share a byte with the line before or after it, so the raw frame must be
// cleared beforehand; the lines can then be converted in any order. Padding bits of the
// coded line are not copied.
void DtSdiFrame_ConvertLine(const DtSdiFrameLayout* Layout, int SymbolBits,
                            const uint8_t* CodedLine, int LineIndex, uint8_t* Raw);

// Codes one raw line into the coded line at CodedLine, Layout->Stride bytes, as
// PxCnvTaskRaw::Run does: the HANC section and the video section, each padded with zero
// bits. The symbols take SymbolBits, 8, 10 or 16; a 16-bit symbol gives its lower ten
// bits, and an 8-bit one its eight bits shifted up by two. The line's first bit is bit
// Phase, from 0 for the least significant to 7, of the byte at RawLine, and no byte after
// the one holding its last bit is read. Returns false, writing nothing, for another
// symbol size, for a Phase outside 0 to 7, and for a Phase other than 0 with 8 or 16
// bits.
bool DtSdiFrame_CodeLine(const DtSdiFrameLayout* Layout, int SymbolBits,
                         const uint8_t* RawLine, int Phase, uint8_t* CodedLine);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Black frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A black frame holds the lines of a standard with nothing in them: every symbol that is
// no timing reference is 200 in the chrominance and 040 in the luminance, which is black
// in the active video and empty blanking elsewhere. The timing references are those of
// the line: EAV and SAV with the field, vertical blanking and protection bits of the
// video standard's field layout, and in HD and 3G, for each channel, the line number and
// SMPTE 292's CRC-18 over that channel's active part of the line before it, then the EAV
// and the line number. For the first line the line before it is the frame's last line,
// which in a black frame is as black as any other.
//

// Writes the coded lines of a black frame of Layout's standard at Lines, Layout->NumLines
// times Layout->Stride bytes, padding bits 0.
void DtSdiFrame_BlackLines(const DtSdiFrameLayout* Layout, uint8_t* Lines);
