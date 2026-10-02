// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtSt2110Video.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - ST 2110-20 video: frames into packets, and packets into frames
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// CDTAPI includes
#include "DtAvPixConv.h" // Pixel conversions.
#include "DtAvStream.h"  // Sinks and streams.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The video packetizer sends a frame, or one field of interlaced or PsF video, as rows
// of pixel groups split over packets. A packet holds segments of at most three rows: a
// long row is split over several packets, short rows share one. The last packet of the
// frame or field has the RTP marker bit set.
//
// The packets are spread over the frame period: the first one leaves the transmit offset
// after the frame's time, and the others follow at equal intervals. With gapped
// scheduling they are spread over only the active part of the frame period. Like audio,
// every packet is scheduled the card's output delay earlier. The second field of PsF
// video gets the same RTP timestamp as the first.
//

// The bytes of a video packet's UDP datagram that are not video: the UDP and RTP headers,
// the extended sequence number, and room for three row headers.
#define DT_ST2110_VIDEO_HEADERS                                                          \
    (DT_AV_UDP_HEADER_SIZE + DT_AV_RTP_HEADER_SIZE + DT_AV_ESN_SIZE + 3 * DT_AV_SRD_SIZE)

typedef struct DtSt2110VideoTx
{
    // Set by Configure.
    bool Is420;                   // Whether the video is 4:2:0, with rows numbered by two
    bool IsInterlaced;            // Whether the video is interlaced
    bool IsPsf;                   // Whether the video is progressive segmented frame
    int NumRows;                  // The rows of a whole frame
    int RowSize;                  // The bytes of a row in the packets: whole pixel groups
    int RowSizeFrame;             // The bytes of a row in the application's frame
    int PgroupBytes;              // The bytes of a pixel group
    int PgroupPixels;             // The pixels in a pixel group
    St2110_VideoPacking Packing;  // How rows are split over packets
    St2110_Scheduling Scheduling; // Whether packets spread over the whole frame period
    FrameRate Rate;               // The rate of what is sent as one frame: the field rate
                                  // for interlaced and PsF video
    Ratio ActiveVideo;            // The active part of the frame period
    int TrOffsetNs;               // The transmit offset, in nanoseconds
    DtAvPixConvFunc Convert;      // Converts the frame's pixels into packets; NULL copies

    // Set by Start.
    int PayloadSize;          // The bytes of video in a packet
    int PacketsPerFrame;      // The packets of a whole frame, both fields
    uint64_t PacketSpacingPs; // The time between two packets, in picoseconds
    uint32_t PrevRtpTime;     // The RTP timestamp of the last first field, for PsF
} DtSt2110VideoTx;

// Sets up a packetizer for 8-bit or 10-bit UYVY video. The transmit offset and the active
// part of the frame period follow from the resolution, scanning and rate. Returns
// DTAPI_OK, or DTAPI_E_INVALID_ARG when the rate or format is not valid, the width is not
// positive and even, the height is not positive, or the transmit offset does not fit in
// an int.
DtapiResult DtSt2110VideoTx_Configure(DtSt2110VideoTx* Tx,
                                      const St2110_TxConfigVideo* Config,
                                      const DtAvPixConvTable* Conv);

// Sets up a packetizer for raw video: rows of pixel groups the application has already
// made, which are sent as they are. Returns DTAPI_OK, or DTAPI_E_INVALID_ARG when the
// pixel group, row size, number of rows, rate or active part is not valid.
DtapiResult DtSt2110VideoTx_ConfigureRaw(DtSt2110VideoTx* Tx,
                                         const St2110_TxConfigRawVideo* Config);

// Prepares the packetizer to send on Stream: chooses the payload size, counts the
// packets of a frame and works out the time between them. A configured payload size of
// -1 takes the most a standard packet holds, also on a pipe with jumbo frames. Returns
// DTAPI_OK, or DTAPI_E_INVALID_ARG when the configured payload size is not positive, not
// a multiple of the pixel group (of 180 bytes for block packing), or larger than a packet
// holds.
DtapiResult DtSt2110VideoTx_Start(DtSt2110VideoTx* Tx, const DtAvTxStream* Stream);

// Returns the most bytes the packets of one frame take in the pipe, padding included.
int DtSt2110VideoTx_FrameBytes(const DtSt2110VideoTx* Tx, const DtAvTxStream* Stream);

// Returns the number of valid bytes a frame must have; for interlaced and PsF video, the
// field Field must have.
int DtSt2110VideoTx_FrameSize(const DtSt2110VideoTx* Tx, int Field);

// Writes the packets of Frame to Sink. Returns DTAPI_OK, or DTAPI_E_INVALID_FORMAT,
// sending nothing, when the frame's number of valid bytes is not what
// DtSt2110VideoTx_FrameSize gives, or larger than the frame.
DtapiResult DtSt2110VideoTx_Packetize(DtSt2110VideoTx* Tx, DtAvTxStream* Stream,
                                      const AvFifo_Frame* Frame, const DtAvTxSink* Sink);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reception +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The video parser does not know the size of the frames in advance; it learns it from
// the stream. Until the first marker it only watches: it counts the bytes and rows of a
// frame that starts at row 0, and reads the highest row number and the length of a row
// from the row headers. A field gets room for one row more than it has.
//
// After that first marker, every packet up to and including the next marker makes one
// frame, with the RTP timestamp and the time of arrival of its first packet. What goes
// wrong is counted in the statistics:
//
//   Problem                                   Counted as        And then
//   Packets missing inside a frame            FramesIncomplete  The frame is skipped
//   Packets missing before a frame's start    Gaps              The frame is received
//   A packet with more than three row         IpPacketErrors    The frame is skipped
//   headers, or headers that do not fit
//   A frame larger than the learned size,     FramesSizeError   The size is learned
//   or a field bit in progressive video                         again
//

typedef struct DtSt2110VideoRx
{
    St2110_RxFrameFormat Format;  // The pixel format the frames are converted to
    const DtAvPixConvTable* Conv; // The conversions used
    DtAvRxSink Sink;              // Where the frames go
    RxStatistics Stats;           // The counts of frames and errors

    // What the parser has learned of the stream.
    int CalculatedFrameSize; // The bytes a frame gets room for, or -1 when not yet known
    int CountedFrameSize;    // The bytes counted of a frame from row 0, or -1
    int CountedNumRows;      // The highest row number counted, plus one
    bool IsInterlaced;       // Whether a field bit was seen
    bool Is420;              // Whether rows are numbered by two
    int RowSizeFrame;        // The bytes of a row, or -1 when not yet known
    int NumRowsFrame;        // The highest row number seen, plus one, or -1
    int PrevNumRows;         // NumRowsFrame of the previous packet, or -1

    // The frame being received.
    uint32_t LastSeqNum;     // The extended sequence number of the last packet
    bool IsWaitingForMarker; // Whether packets are skipped until the next marker
    DtAvFrame* PartialFrame; // The frame being filled, or NULL
    int InputNumBytes;       // The bytes of the packets put into it so far
    int OutputNumBytes;      // The bytes written into it so far, after conversion
} DtSt2110VideoRx;

// Sets up a parser that converts the video to Format with Conv and delivers the frames to
// Target.
void DtSt2110VideoRx_Init(DtSt2110VideoRx* Rx, St2110_RxFrameFormat Format,
                          const DtAvPixConvTable* Conv, const DtAvRxSink* Target);

// Starts the parser afresh: puts the frame being received back in the pool and forgets
// what it learned of the stream.
void DtSt2110VideoRx_Reset(DtSt2110VideoRx* Rx);

// Adds one packet from the pipe to the frame being received, and delivers the frame when
// the packet has the marker bit.
void DtSt2110VideoRx_Parse(DtSt2110VideoRx* Rx, const uint8_t* Packet, int Size);
