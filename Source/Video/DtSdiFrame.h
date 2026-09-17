// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiFrame.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - The frame format a receive channel writes, and DTAPI's raw SDI frame
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DT_SDI_FRAME_H
#define CDTAPILITE_DT_SDI_FRAME_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Coded frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A CHSDIRX channel writes the firmware's "SDI RX simple" format into its DMA ring
// (DTAPI: MxChannelMemlessRx::SdiRxSimpleProps). Per frame:
//
//   header      16 bytes, padded to the stream alignment
//                 32 bits  sync word 0xFFEFFBFE
//                 32 bits  protocol version (bits 0-3), format (bits 4-7), reserved
//                          (bits 8-15), frame ID (bits 16-31)
//                 64 bits  PTP time of arrival: seconds, then nanoseconds
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

// The header's size before padding.
#define DT_SDIFRAME_HEADER_BYTES 16

// The formats a header names.
#define DT_SDIFRAME_FORMAT_UNCOMPRESSED 0
#define DT_SDIFRAME_FORMAT_UNCOMPRESSED_4K 1

typedef struct DtSdiFrameLayout
{
    int VidStd;         // DTAPI_VIDSTD_ code
    int Alignment;      // Bytes every part is padded to
    int HeaderBytes;    // The header with its padding
    int NumLines;       // Coded lines per frame
    int LineSymsHanc;   // Symbols in the HANC section
    int LineBytesHanc;  // Bytes of the HANC section with its padding
    int LineSymsVideo;  // Symbols in the video section
    int LineBytesVideo; // Bytes of the video section with its padding
    int Stride;         // Bytes per coded line
    int Format;         // The format a header of this standard names
} DtSdiFrameLayout;

// Fills Layout for a video standard and a stream alignment in bits, as
// SdiRxSimpleProps::Init. Returns false for an unknown standard, a 4K standard, and an
// alignment that is not a positive number of whole bytes.
bool DtSdiFrameLayoutInit(DtSdiFrameLayout* Layout, int VidStd, int AlignmentBits);

// The bytes one coded frame takes: its header and its lines.
size_t DtSdiFrameCodedSize(const DtSdiFrameLayout* Layout);

// A header, decoded.
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
void DtSdiFrameDecodeHeader(const uint8_t* Bytes, DtSdiFrameHeader* Header);

// Encodes Header into DT_SDIFRAME_HEADER_BYTES bytes at Bytes. The reserved bits are 0.
void DtSdiFrameEncodeHeader(const DtSdiFrameHeader* Header, uint8_t* Bytes);

// Checks a header as MxChannelMemlessRx::CheckFrameHeader does: DTAPI_E_OUT_OF_SYNC for
// a wrong sync word; DTAPI_E_INVALID when ExpectedId is not -1 and the frame ID differs;
// DTAPI_E_INVALID_FORMAT for a format the layout does not expect; otherwise DTAPI_OK.
unsigned int DtSdiFrameCheckHeader(const DtSdiFrameLayout* Layout,
                                   const DtSdiFrameHeader* Header, int ExpectedId);

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
unsigned int DtSdiFrameCheckLines(const DtSdiFrameLayout* Layout,
                                  const uint8_t* FirstLine, const uint8_t* LastLine);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Raw frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// What DTAPI's ReadFrame delivers in DTAPI_RXMODE_SDI_FULL: every line of the frame, EAV
// first, as one stream of symbols, padded with zero bits to a 64-bit boundary
// (SdiRxImpl_Bb2::RxIdle2Receive, PxCnvTaskRaw::Run). A symbol takes
//
//   8 bits    its upper eight bits
//   10 bits   packed, least significant bit first, as in the coded frame
//   16 bits   its value unshifted, little endian
//

// The bytes of a raw frame whose symbols take SymbolBits, 8, 10 or 16, padding included;
// 0 for any other symbol size.
size_t DtSdiFrameRawSize(const DtSdiFrameLayout* Layout, int SymbolBits);

// Converts the coded line at CodedLine, the line with index LineIndex from 0, into its
// place in the raw frame at Raw, whose symbols take SymbolBits, 8, 10 or 16. With 10 bits
// a line can share a byte with the line before or after it, so the raw frame must be
// cleared beforehand; the lines can then be converted in any order. Padding bits of the
// coded line are not copied.
void DtSdiFrameConvertLine(const DtSdiFrameLayout* Layout, int SymbolBits,
                           const uint8_t* CodedLine, int LineIndex, uint8_t* Raw);

#endif // CDTAPILITE_DT_SDI_FRAME_H
