// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtSt2110Audio.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - ST 2110-30 audio: frames into packets, and packets into frames
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>

// CDtapiLite includes
#include "DtAvStream.h" // Sinks, streams and targets.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// DTAPI's AudioTx. A frame of L16 or L24 samples is cut into packets of the configured
// number of samples; the samples that do not fill a packet wait for the next frame, and
// that packet gets the time of day and RTP timestamp of its first sample. Each packet's
// time and timestamp advance by its samples, and its time is sent earlier by the card's
// output delay. A raw frame is one packet of its bytes, at the frame's own time.
//

// The audio bytes a packet holds at most: a standard UDP datagram less the UDP and RTP
// headers.
#define DT_ST2110_AUDIO_MAX_PAYLOAD                                                      \
    (DT_AV_UDP_STANDARD - DT_AV_UDP_HEADER_SIZE - DT_AV_RTP_HEADER_SIZE)

typedef struct DtSt2110AudioTx
{
    St2110_TxConfigAudio Config;
    int BytesPerSample; // For all channels; 0 for raw audio
    int PayloadSize;    // Bytes of samples per packet
    int LeftOver;       // Bytes of samples waiting for the next frame
    uint8_t LeftOverSamples[DT_ST2110_AUDIO_MAX_PAYLOAD];
    uint64_t LeftOverTodNs;
    uint32_t LeftOverRtp;
} DtSt2110AudioTx;

// Configures a packetizer. DTAPI_E_INVALID_ARG for a format that is none of the three,
// no channels, samples or sample rate, or packets of more than
// DT_ST2110_AUDIO_MAX_PAYLOAD bytes.
DtapiResult DtSt2110AudioTx_Configure(DtSt2110AudioTx* Tx,
                                      const St2110_TxConfigAudio* Config);

// Forgets the samples waiting for the next frame.
void DtSt2110AudioTx_Reset(DtSt2110AudioTx* Tx);

// The bytes the packets of Frame take in the pipe, padding included, or -1 when the frame
// cannot be sent: DTAPI_E_INVALID_FORMAT's cases below.
int DtSt2110AudioTx_PacketBytes(const DtSt2110AudioTx* Tx, const DtAvTxStream* Stream,
                                const AvFifo_Frame* Frame);

// Hands the packets of Frame to Sink. DTAPI_E_INVALID_FORMAT, sending nothing, for a raw
// frame of more than DT_ST2110_AUDIO_MAX_PAYLOAD bytes or a frame of samples that is not
// a whole number of samples.
DtapiResult DtSt2110AudioTx_Packetize(DtSt2110AudioTx* Tx, DtAvTxStream* Stream,
                                      const AvFifo_Frame* Frame, const DtAvSink* Sink);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reception +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// DTAPI's AudioRx: every packet is a frame of its payload, with its RTP timestamp and its
// time of arrival, whatever the format.
//

typedef struct DtSt2110AudioRx
{
    St2110_RxConfigAudio Config;
    DtAvRxTarget Target;
    RxStatistics Stats;
} DtSt2110AudioRx;

// Sets up a parser that delivers to Target.
void DtSt2110AudioRx_Init(DtSt2110AudioRx* Rx, const St2110_RxConfigAudio* Config,
                          const DtAvRxTarget* Target);

// Parses one packet of the pipe. A packet whose headers do not check counts an IP packet
// error; one for which the pool has no memory a dropped frame.
void DtSt2110AudioRx_Parse(DtSt2110AudioRx* Rx, const uint8_t* Packet, int Size);
