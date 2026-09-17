// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtSt2110Audio.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - ST 2110-30 audio: frames into packets, and packets into frames
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
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
    int SampleBytes = 0;
    switch (Config->Format)
    {
    case St2110_AudioFormat_L16BE:
        SampleBytes = 2;
        break;
    case St2110_AudioFormat_L24BE:
        SampleBytes = 3;
        break;
    case St2110_AudioFormat_Raw:
        break;
    default:
        return DTAPI_E_INVALID_ARG;
    }
    int64_t PayloadSize =
        (int64_t)SampleBytes * Config->NumChannels * Config->NumSamplesPerIpPacket;
    if (PayloadSize > DT_ST2110_AUDIO_MAX_PAYLOAD)
        return DTAPI_E_INVALID_ARG;

    Tx->Config = *Config;
    Tx->BytesPerSample = SampleBytes * Config->NumChannels;
    Tx->PayloadSize = (int)PayloadSize;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110AudioTx_Reset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSt2110AudioTx_Reset(DtSt2110AudioTx* Tx)
{
    Tx->LeftOver = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NumPackets -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The packets Frame makes, or -1 when it cannot be sent.
//
static int NumPackets(const DtSt2110AudioTx* Tx, const AvFifo_Frame* Frame)
{
    if (Frame->NumValidBytes < 0 || (size_t)Frame->NumValidBytes > Frame->Size)
        return -1;
    if (Tx->BytesPerSample == 0)
        return Frame->NumValidBytes <= DT_ST2110_AUDIO_MAX_PAYLOAD ? 1 : -1;
    if (Frame->NumValidBytes % Tx->BytesPerSample != 0)
        return -1;
    return (int)(((int64_t)Frame->NumValidBytes + Tx->LeftOver) / Tx->PayloadSize);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110AudioTx_PacketBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtSt2110AudioTx_PacketBytes(const DtSt2110AudioTx* Tx, const DtAvTxStream* Stream,
                                const AvFifo_Frame* Frame)
{
    int Packets = NumPackets(Tx, Frame);
    if (Packets < 0)
        return -1;
    int Payload = Tx->BytesPerSample == 0 ? Frame->NumValidBytes : Tx->PayloadSize;
    return Packets * DtAvNet_PacketSize(&Stream->Net, DT_AV_RTP_HEADER_SIZE + Payload);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SendPacket -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sends a packet of Size bytes of samples, of which the first First come from First and
// the rest from Rest.
//
static void SendPacket(DtAvTxStream* Stream, const DtAvSink* Sink, uint32_t RtpTime,
                       uint64_t TodNs, const uint8_t* First, int FirstSize,
                       const uint8_t* Rest, int RestSize)
{
    int Header = DtAvNet_HeaderSize(&Stream->Net);
    int Payload = DT_AV_RTP_HEADER_SIZE + FirstSize + RestSize;
    uint8_t* Packet =
        Sink->Begin(Sink->Context, DtAvNet_PacketSize(&Stream->Net, Payload));

    DtAvRtp Rtp;
    Rtp.Marker = false;
    Rtp.PayloadType = Stream->PayloadType;
    Rtp.SequenceNumber = (uint16_t)Stream->SequenceNumber++;
    Rtp.Timestamp = RtpTime;
    Rtp.Ssrc = Stream->Ssrc;
    DtAvRtp_Write(&Rtp, Packet + Header);
    uint8_t* Samples = Packet + Header + DT_AV_RTP_HEADER_SIZE;
    if (FirstSize > 0)
        memcpy(Samples, First, (size_t)FirstSize);
    if (RestSize > 0)
        memcpy(Samples + FirstSize, Rest, (size_t)RestSize);
    Sink->Commit(Sink->Context, DtAvNet_Finish(&Stream->Net, Packet, Payload, 0, TodNs));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110AudioTx_Packetize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// AudioTx::TransferFrame. The time of a packet is kept in thousandths of a nanosecond, as
// DTAPI keeps it, so that it does not drift from the sample rate.
//
DtapiResult DtSt2110AudioTx_Packetize(DtSt2110AudioTx* Tx, DtAvTxStream* Stream,
                                      const AvFifo_Frame* Frame, const DtAvSink* Sink)
{
    int Packets = NumPackets(Tx, Frame);
    if (Packets < 0)
        return DTAPI_E_INVALID_FORMAT;
    uint64_t FrameTodNs = DtAvTime_ToNs(&Frame->ToD);

    if (Tx->BytesPerSample == 0)
    {
        SendPacket(Stream, Sink, Frame->RtpTime,
                   FrameTodNs - (uint64_t)Stream->OutputDelayNs, Frame->Data,
                   Frame->NumValidBytes, NULL, 0);
        return DTAPI_OK;
    }

    uint64_t FirstTodNs = Tx->LeftOver != 0 ? Tx->LeftOverTodNs : FrameTodNs;
    uint32_t RtpTime = Tx->LeftOver != 0 ? Tx->LeftOverRtp : Frame->RtpTime;
    uint64_t Offset = 0; // In thousandths of a nanosecond
    const uint8_t* Src = Frame->Data;
    int SamplesPerPacket = Tx->PayloadSize / Tx->BytesPerSample;
    for (int i = 0; i < Packets; i++)
    {
        uint64_t TodNs = FirstTodNs - (uint64_t)Stream->OutputDelayNs + Offset / 1000;
        int FromFrame = Tx->PayloadSize - Tx->LeftOver;
        SendPacket(Stream, Sink, RtpTime, TodNs, Tx->LeftOverSamples, Tx->LeftOver, Src,
                   FromFrame);
        Src += FromFrame;
        Tx->LeftOver = 0;
        Offset += DT_AV_NS_PER_SEC * 1000 * (uint64_t)SamplesPerPacket /
                  (uint64_t)Tx->Config.SampleRate;
        RtpTime += (uint32_t)SamplesPerPacket;
    }

    // What remains waits, after any samples that were already waiting.
    int Remaining = (int)(Frame->Data + Frame->NumValidBytes - Src);
    if (Remaining > 0)
    {
        if (Tx->LeftOver == 0)
        {
            Tx->LeftOverTodNs = FirstTodNs + Offset / 1000;
            Tx->LeftOverRtp = RtpTime;
        }
        memcpy(Tx->LeftOverSamples + Tx->LeftOver, Src, (size_t)Remaining);
        Tx->LeftOver += Remaining;
    }
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reception +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110AudioRx_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSt2110AudioRx_Init(DtSt2110AudioRx* Rx, const St2110_RxConfigAudio* Config,
                          const DtAvRxTarget* Target)
{
    memset(Rx, 0, sizeof(*Rx));
    Rx->Config = *Config;
    Rx->Target = *Target;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110AudioRx_Parse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSt2110AudioRx_Parse(DtSt2110AudioRx* Rx, const uint8_t* Packet, int Size)
{
    DtAvRxPacket Udp;
    if (!DtAvRxPacket_Parse(Packet, Size, &Udp) || Udp.UdpSize < DT_AV_RTP_HEADER_SIZE)
    {
        Rx->Stats.IpPacketErrors++;
        return;
    }
    int PayloadSize = Udp.UdpSize - DT_AV_RTP_HEADER_SIZE;
    DtAvFrame* Frame = DtAvFramePool_Get(Rx->Target.Pool, (size_t)PayloadSize);
    if (Frame == NULL)
    {
        Rx->Stats.DroppedFrames++;
        return;
    }
    DtAvRtp Rtp;
    DtAvRtp_Read(Udp.Udp, &Rtp);
    Frame->Frame.RtpTime = Rtp.Timestamp;
    Frame->Frame.ToD = DtAvTime_FromNs(Udp.TodNs);
    memcpy(Frame->Frame.Data, Udp.Udp + DT_AV_RTP_HEADER_SIZE, (size_t)PayloadSize);
    Frame->Frame.NumValidBytes = PayloadSize;
    DtAvRxTarget_Deliver(&Rx->Target, &Rx->Stats, Frame);
}
