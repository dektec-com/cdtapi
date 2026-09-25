// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtSt2110Audio.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - ST 2110-30 audio: frames into packets, and packets into frames
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "DtAvTime.h"      // Nanoseconds and 128-bit arithmetic.
#include "DtSt2110Audio.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110AudioTx_Configure -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtSt2110AudioTx_Configure(DtSt2110AudioTx* Tx,
                                      const St2110_TxConfigAudio* Config)
{
    memset(Tx, 0, sizeof(*Tx));
    if (Config->NumSamplesPerIpPacket <= 0 || Config->NumChannels <= 0 ||
        Config->SampleRate <= 0)
    {
        return DTAPI_E_INVALID_ARG;
    }
    int BytesPerChannelSample = 0;
    switch (Config->Format)
    {
    case St2110_AudioFormat_L16BE:
        BytesPerChannelSample = 2;
        break;
    case St2110_AudioFormat_L24BE:
        BytesPerChannelSample = 3;
        break;
    case St2110_AudioFormat_Raw:
        break;
    default:
        return DTAPI_E_INVALID_ARG;
    }
    int64_t PayloadSize = (int64_t)BytesPerChannelSample * Config->NumChannels *
                          Config->NumSamplesPerIpPacket;
    if (PayloadSize > DT_ST2110_AUDIO_MAX_PAYLOAD)
        return DTAPI_E_INVALID_ARG;

    Tx->Config = *Config;
    Tx->BytesPerSamplePeriod = BytesPerChannelSample * Config->NumChannels;
    Tx->PayloadSize = (int)PayloadSize;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110AudioTx_Reset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSt2110AudioTx_Reset(DtSt2110AudioTx* Tx)
{
    Tx->LeftOverBytes = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PacketsForFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The packets Frame makes, or -1 when it cannot be sent.
//
static int PacketsForFrame(const DtSt2110AudioTx* Tx, const AvFifo_Frame* Frame)
{
    if (Frame->NumValidBytes < 0 || (size_t)Frame->NumValidBytes > Frame->Size)
        return -1;
    if (Tx->BytesPerSamplePeriod == 0)
        return Frame->NumValidBytes <= DT_ST2110_AUDIO_MAX_PAYLOAD ? 1 : -1;
    if (Frame->NumValidBytes % Tx->BytesPerSamplePeriod != 0)
        return -1;
    return (int)(((int64_t)Frame->NumValidBytes + Tx->LeftOverBytes) / Tx->PayloadSize);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110AudioTx_PacketBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtSt2110AudioTx_PacketBytes(const DtSt2110AudioTx* Tx, const DtAvTxStream* Stream,
                                const AvFifo_Frame* Frame)
{
    int Packets = PacketsForFrame(Tx, Frame);
    if (Packets < 0)
        return -1;
    int Payload = Tx->BytesPerSamplePeriod == 0 ? Frame->NumValidBytes : Tx->PayloadSize;
    return Packets * DtAvNet_PacketSize(&Stream->Net, DT_AV_RTP_HEADER_SIZE + Payload);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WritePacket -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes a packet of samples: LeftOverBytes bytes from LeftOver, then FromFrameBytes
// bytes from FromFrame.
//
static void WritePacket(DtAvTxStream* Stream, const DtAvTxSink* Sink, uint32_t RtpTime,
                        uint64_t TodNs, const uint8_t* LeftOver, int LeftOverBytes,
                        const uint8_t* FromFrame, int FromFrameBytes)
{
    int Header = DtAvNet_HeaderSize(&Stream->Net);
    int Payload = DT_AV_RTP_HEADER_SIZE + LeftOverBytes + FromFrameBytes;
    uint8_t* Packet =
        Sink->ReserveRoom(Sink->Context, DtAvNet_PacketSize(&Stream->Net, Payload));

    DtAvRtp Rtp;
    Rtp.Marker = false;
    Rtp.PayloadType = Stream->PayloadType;
    Rtp.SequenceNumber = (uint16_t)Stream->NextSequenceNumber++;
    Rtp.Timestamp = RtpTime;
    Rtp.Ssrc = Stream->Ssrc;
    DtAvRtp_Write(&Rtp, Packet + Header);
    uint8_t* Samples = Packet + Header + DT_AV_RTP_HEADER_SIZE;
    if (LeftOverBytes > 0)
        memcpy(Samples, LeftOver, (size_t)LeftOverBytes);
    if (FromFrameBytes > 0)
        memcpy(Samples + LeftOverBytes, FromFrame, (size_t)FromFrameBytes);
    Sink->CommitPacket(Sink->Context,
                       DtAvNet_WriteHeaders(&Stream->Net, Packet, Payload, 0, TodNs));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110AudioTx_Packetize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The time of a packet is kept in thousandths of a nanosecond, so that it does not drift
// from the sample rate.
//
DtapiResult DtSt2110AudioTx_Packetize(DtSt2110AudioTx* Tx, DtAvTxStream* Stream,
                                      const AvFifo_Frame* Frame, const DtAvTxSink* Sink)
{
    int Packets = PacketsForFrame(Tx, Frame);
    if (Packets < 0)
        return DTAPI_E_INVALID_FORMAT;
    uint64_t FrameTodNs = DtAvTime_ToNs(&Frame->ToD);

    if (Tx->BytesPerSamplePeriod == 0)
    {
        WritePacket(Stream, Sink, Frame->RtpTime,
                    FrameTodNs - (uint64_t)Stream->OutputDelayNs, Frame->Data,
                    Frame->NumValidBytes, NULL, 0);
        return DTAPI_OK;
    }

    uint64_t FirstTodNs = Tx->LeftOverBytes != 0 ? Tx->LeftOverTodNs : FrameTodNs;
    uint32_t RtpTime = Tx->LeftOverBytes != 0 ? Tx->LeftOverRtpTime : Frame->RtpTime;
    uint64_t Offset = 0; // In thousandths of a nanosecond
    const uint8_t* Src = Frame->Data;
    int SamplesPerPacket = Tx->PayloadSize / Tx->BytesPerSamplePeriod;
    for (int i = 0; i < Packets; i++)
    {
        uint64_t TodNs =
            FirstTodNs - (uint64_t)Stream->OutputDelayNs + Offset / DT_AV_PS_PER_NS;
        int FromFrame = Tx->PayloadSize - Tx->LeftOverBytes;
        WritePacket(Stream, Sink, RtpTime, TodNs, Tx->LeftOverSamples, Tx->LeftOverBytes,
                    Src, FromFrame);
        Src += FromFrame;
        Tx->LeftOverBytes = 0;
        Offset += DT_AV_NS_PER_SEC * DT_AV_PS_PER_NS * (uint64_t)SamplesPerPacket /
                  (uint64_t)Tx->Config.SampleRate;
        RtpTime += (uint32_t)SamplesPerPacket;
    }

    // What remains waits, after any samples that were already waiting.
    int Remaining = (int)(Frame->Data + Frame->NumValidBytes - Src);
    if (Remaining > 0)
    {
        if (Tx->LeftOverBytes == 0)
        {
            Tx->LeftOverTodNs = FirstTodNs + Offset / DT_AV_PS_PER_NS;
            Tx->LeftOverRtpTime = RtpTime;
        }
        memcpy(Tx->LeftOverSamples + Tx->LeftOverBytes, Src, (size_t)Remaining);
        Tx->LeftOverBytes += Remaining;
    }
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reception +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110AudioRx_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSt2110AudioRx_Init(DtSt2110AudioRx* Rx, const St2110_RxConfigAudio* Config,
                          const DtAvRxSink* Target)
{
    memset(Rx, 0, sizeof(*Rx));
    Rx->Config = *Config;
    Rx->Sink = *Target;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110AudioRx_Parse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSt2110AudioRx_Parse(DtSt2110AudioRx* Rx, const uint8_t* Packet, int Size)
{
    DtAvRxPacket Udp;
    if (!DtAvRxPacket_Parse(Packet, Size, &Udp) ||
        Udp.PayloadSize < DT_AV_RTP_HEADER_SIZE)
    {
        Rx->Stats.IpPacketErrors++;
        return;
    }
    int PayloadSize = Udp.PayloadSize - DT_AV_RTP_HEADER_SIZE;
    DtAvFrame* Frame = DtAvFramePool_Get(Rx->Sink.Pool, (size_t)PayloadSize);
    if (Frame == NULL)
    {
        Rx->Stats.DroppedFrames++;
        return;
    }
    DtAvRtp Rtp;
    DtAvRtp_Read(Udp.Payload, &Rtp);
    Frame->Frame.RtpTime = Rtp.Timestamp;
    Frame->Frame.ToD = DtAvTime_FromNs(Udp.TodNs);
    memcpy(Frame->Frame.Data, Udp.Payload + DT_AV_RTP_HEADER_SIZE, (size_t)PayloadSize);
    Frame->Frame.NumValidBytes = PayloadSize;
    DtAvRxSink_Deliver(&Rx->Sink, &Rx->Stats, Frame);
}
