// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtSt2110Audio.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - ST 2110-30 audio: frames into packets, and packets into frames
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>

// CDTAPI includes
#include "DtAvStream.h" // Sinks and streams.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The audio packetizer splits a frame of L16 or L24 samples into packets of the
// configured number of samples. Samples left at the end of a frame that do not fill a
// packet are kept and sent at the start of the next frame's first packet. Each packet
// gets the time of day and RTP timestamp of its first sample, so they advance by the
// samples of a packet from one packet to the next.
//
// A raw frame is sent as it is, in one packet, with the frame's time and RTP timestamp.
//
// Every packet is scheduled the card's output delay earlier than its time, so that it
// leaves the card on time.
//

// The most audio bytes a packet holds: a standard UDP datagram less the UDP and RTP
// headers.
#define DT_ST2110_AUDIO_MAX_PAYLOAD                                                      \
    (DT_AV_UDP_STANDARD - DT_AV_UDP_HEADER_SIZE - DT_AV_RTP_HEADER_SIZE)

typedef struct DtSt2110AudioTx
{
    St2110_TxConfigAudio Config; // The configuration
    int BytesPerSamplePeriod; // The size of one sample of all channels; 0 for raw audio
    int PayloadSize;          // The bytes of samples in a packet
    int LeftOverBytes;        // The bytes of samples waiting for the next frame
    uint8_t LeftOverSamples[DT_ST2110_AUDIO_MAX_PAYLOAD]; // The samples waiting
    uint64_t LeftOverTodNs;   // The time of day of the first waiting sample
    uint32_t LeftOverRtpTime; // The RTP timestamp of the first waiting sample
} DtSt2110AudioTx;

// Sets up a packetizer for a configuration. Returns DTAPI_OK, or DTAPI_E_INVALID_ARG when
// the format is not L16, L24 or raw, the number of channels, samples per packet or the
// sample rate is not positive, or a packet would hold more than
// DT_ST2110_AUDIO_MAX_PAYLOAD bytes.
DtapiResult DtSt2110AudioTx_Configure(DtSt2110AudioTx* Tx,
                                      const St2110_TxConfigAudio* Config);

// Throws away the samples waiting for the next frame, for when the stream restarts.
void DtSt2110AudioTx_Reset(DtSt2110AudioTx* Tx);

// Returns how many bytes the packets of Frame take in the pipe, padding included, or -1
// when the frame cannot be sent, for the reasons DtSt2110AudioTx_Packetize gives.
int DtSt2110AudioTx_PacketBytes(const DtSt2110AudioTx* Tx, const DtAvTxStream* Stream,
                                const AvFifo_Frame* Frame);

// Writes the packets of Frame to Sink. Returns DTAPI_OK, or DTAPI_E_INVALID_FORMAT,
// sending nothing, when the frame's number of valid bytes is negative or larger than its
// size, a raw frame is larger than DT_ST2110_AUDIO_MAX_PAYLOAD, or a frame of samples
// ends in part of a sample.
DtapiResult DtSt2110AudioTx_Packetize(DtSt2110AudioTx* Tx, DtAvTxStream* Stream,
                                      const AvFifo_Frame* Frame, const DtAvTxSink* Sink);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reception +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The audio parser makes a frame of every packet, whatever the format: the payload, with
// the packet's RTP timestamp and its time of arrival.
//

typedef struct DtSt2110AudioRx
{
    St2110_RxConfigAudio Config; // The configuration
    DtAvRxSink Sink;             // Where the frames go
    RxStatistics Stats;          // The counts of frames and errors
} DtSt2110AudioRx;

// Sets up a parser that delivers its frames to Target.
void DtSt2110AudioRx_Init(DtSt2110AudioRx* Rx, const St2110_RxConfigAudio* Config,
                          const DtAvRxSink* Target);

// Makes a frame of one packet from the pipe and delivers it. A packet that is not a
// valid RTP packet counts as an IP packet error; one for which the pool has no memory
// counts as a dropped frame.
void DtSt2110AudioRx_Parse(DtSt2110AudioRx* Rx, const uint8_t* Packet, int Size);
