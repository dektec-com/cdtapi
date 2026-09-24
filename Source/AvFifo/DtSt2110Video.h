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
#include "DtAvStream.h"  // Sinks, streams and targets.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A frame, or a field of interlaced and PsF video, is cut into rows of pixel groups and
// the rows into packets: one row or less per packet, or up to three rows when the
// payload holds them. The last packet carries the marker. The first packet is sent the
// transmit offset after the frame's time, less the card's output delay, and the others
// follow at equal spacing over the frame period, or over its active part for gapped
// scheduling. The second field of PsF video repeats the first field's RTP timestamp.
//

// The bytes of video a packet holds at most: a UDP datagram less the UDP and RTP headers
// and a payload header of three rows.
#define DT_ST2110_VIDEO_HEADERS                                                          \
    (DT_AV_UDP_HEADER_SIZE + DT_AV_RTP_HEADER_SIZE + DT_AV_ESN_SIZE + 3 * DT_AV_SRD_SIZE)

typedef struct DtSt2110VideoTx
{
    // From the configuration.
    bool Is420;
    bool Interlaced;
    bool Psf;
    int NumRows;      // Rows of a frame
    int RowSize;      // Bytes of a row in the packets, whole pixel groups
    int RowSizeFrame; // Bytes of a row in the application's frame
    int PgroupBytes;
    int PgroupPixels;
    St2110_VideoPacking Packing;
    St2110_Scheduling Scheduling;
    FrameRate Rate;    // Of frames: half the field rate of interlaced and PsF video
    Ratio ActiveVideo; // The active part of the frame period
    int TrOffsetNs;
    DtAvPixConvFunc Convert; // From the frame to the packets, or NULL to copy

    // From the start.
    int PayloadSize;     // Bytes of video per packet
    int PacketsPerFrame; // Of a whole frame, both fields
    uint64_t Spacing;    // Between packets, in thousandths of a nanosecond
    uint32_t PrevRtpTime;
} DtSt2110VideoTx;

// Configures a packetizer for 8-bit or 10-bit UYVY, with the transmit offset and active
// part for the resolution, scanning and rate. DTAPI_E_INVALID_ARG for a rate or
// format that is not valid, or a resolution that is not a positive even width and
// positive height.
DtapiResult DtSt2110VideoTx_Configure(DtSt2110VideoTx* Tx,
                                      const St2110_TxConfigVideo* Config,
                                      const DtAvPixConv* Conv);

// Configures a packetizer for rows of pixel groups as the application gives them.
// DTAPI_E_INVALID_ARG for a pixel group, row, row count or rate that is not valid.
DtapiResult DtSt2110VideoTx_ConfigureRaw(DtSt2110VideoTx* Tx,
                                         const St2110_TxConfigRawVideo* Config);

// Sizes the packets for the stream, jumbo frames or not. DTAPI_E_INVALID_ARG for a
// configured payload size that is not a positive multiple of the pixel group, of 180
// bytes for block packing, or larger than a packet holds.
DtapiResult DtSt2110VideoTx_Start(DtSt2110VideoTx* Tx, const DtAvTxStream* Stream);

// The bytes the packets of a frame take in the pipe at most, padding included.
int DtSt2110VideoTx_FrameBytes(const DtSt2110VideoTx* Tx, const DtAvTxStream* Stream);

// The valid bytes a frame, or a field, must have.
int DtSt2110VideoTx_FrameSize(const DtSt2110VideoTx* Tx, int Field);

// Hands the packets of Frame to Sink. DTAPI_E_INVALID_FORMAT, sending nothing, when its
// valid bytes are not those of DtSt2110VideoTx_FrameSize.
DtapiResult DtSt2110VideoTx_Packetize(DtSt2110VideoTx* Tx, DtAvTxStream* Stream,
                                      const AvFifo_Frame* Frame, const DtAvSink* Sink);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reception +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The parser learns the frame's size from the stream: waiting for a marker, it counts
// the rows and bytes of a frame that starts at row 0, and reads the last row number and
// a row's length from the row headers; an interlaced frame, a field, gets room for one
// row more. From the first marker on, a frame is the packets up to the next marker, with
// the RTP timestamp and time of arrival of its first packet.
//
//   a gap in the sequence numbers inside a frame    an incomplete frame, skipped
//   a gap before a frame's first packet             a gap, and the frame is received
//   more than three row headers in a packet         an IP packet error, and the frame
//                                                   skipped
//   more bytes than the learned size, or the        a size error, and the size learned
//   field bit appearing once the size is known      again
//

typedef struct DtSt2110VideoRx
{
    St2110_RxFrameFormat Format;
    const DtAvPixConv* Conv;
    DtAvRxTarget Target;
    RxStatistics Stats;

    // What the stream taught.
    int CalculatedFrameSize; // Bytes of pixel groups a frame gets room for, or -1
    int CountedFrameSize;    // Bytes of a frame from row 0 counted, or -1
    int CountedNumLines;
    bool Interlaced;
    bool Is420;
    int LineSizeFrame; // Bytes of pixel groups of a row, or -1
    int NumRowsFrame;  // Last row number plus one, or -1
    int PrevRowNum;

    // The frame being received.
    uint32_t LastSeqNum;
    bool WaitForEndFrame;
    DtAvFrame* PartialFrame;
    int InputNumBytes;  // Bytes of pixel groups taken into it
    int OutputNumBytes; // Bytes written into it
} DtSt2110VideoRx;

// Sets up a parser that converts to Format with Conv and delivers to Target.
void DtSt2110VideoRx_Init(DtSt2110VideoRx* Rx, St2110_RxFrameFormat Format,
                          const DtAvPixConv* Conv, const DtAvRxTarget* Target);

// Returns the frame being received to the pool and forgets what the stream taught.
void DtSt2110VideoRx_Reset(DtSt2110VideoRx* Rx);

// Parses one packet of the pipe.
void DtSt2110VideoRx_Parse(DtSt2110VideoRx* Rx, const uint8_t* Packet, int Size);
