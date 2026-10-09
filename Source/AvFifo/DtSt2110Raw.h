// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSt2110Raw.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Raw RTP: a frame for every packet, and a packet for every frame
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// CDTAPI includes
#include "DtAvStream.h" // Sinks and streams.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The raw packetizer sends every frame as one packet. A frame with the program's RTP
// header goes out as it is; another frame gets a header of version 2 in front, with the
// stream's payload type, sequence number and SSRC and the frame's RTP timestamp and
// marker bit. The packet is scheduled the card's output delay earlier than the frame's
// time, so that it leaves the card on time.
//

typedef struct DtSt2110RawTx
{
    St2110_TxConfigRaw Config; // The configuration
} DtSt2110RawTx;

// Sets up a packetizer for a configuration. Returns DTAPI_OK, or DTAPI_E_INVALID_ARG when
// MaxRate is not positive.
DtapiResult DtSt2110RawTx_Configure(DtSt2110RawTx* Tx, const St2110_TxConfigRaw* Config);

// Returns whether Frame can be sent on Stream: its number of valid bytes is not negative
// and not larger than its size, it holds a whole RTP header when it has the program's,
// and its packet fits in a UDP datagram, of a jumbo frame when the stream sends those.
bool DtSt2110RawTx_FrameFits(const DtSt2110RawTx* Tx, const DtAvTxStream* Stream,
                             const AvFifo_Frame* Frame);

// Returns how many bytes the packet of Frame takes in the pipe, padding included, or -1
// when the frame cannot be sent.
int DtSt2110RawTx_PacketBytes(const DtSt2110RawTx* Tx, const DtAvTxStream* Stream,
                              const AvFifo_Frame* Frame);

// Writes the packet of Frame to Sink. Returns DTAPI_OK, or DTAPI_E_INVALID_FORMAT,
// sending nothing, when the frame cannot be sent.
DtapiResult DtSt2110RawTx_Packetize(DtSt2110RawTx* Tx, DtAvTxStream* Stream,
                                    const AvFifo_Frame* Frame, const DtAvTxSink* Sink);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reception +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The raw parser makes a frame of every packet: the whole packet, or its payload without
// the header, the CSRCs, the extension and the padding; with the packet's RTP
// timestamp, marker bit and time of arrival. A sequence number that does not follow the
// one before counts as a gap.
//

typedef struct DtSt2110RawRx
{
    St2110_RxConfigRaw Config;   // The configuration
    DtAvRxSink Sink;             // Where the frames go
    RxStatistics Stats;          // The counts of frames and errors
    bool HasSequenceNumber;      // Whether a packet has been seen
    uint16_t LastSequenceNumber; // The sequence number of the last packet
} DtSt2110RawRx;

// Sets up a parser that delivers its frames to Target.
void DtSt2110RawRx_Init(DtSt2110RawRx* Rx, const St2110_RxConfigRaw* Config,
                        const DtAvRxSink* Target);

// Makes a frame of one packet from the pipe and delivers it. A packet shorter than an RTP
// header, or one whose header, CSRCs, extension and padding do not fit in it when the
// payload alone is asked for, counts as an IP packet error; one for which the pool has no
// memory counts as a dropped frame.
void DtSt2110RawRx_Parse(DtSt2110RawRx* Rx, const uint8_t* Packet, int Size);
