// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSt2110Raw.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Raw RTP: a frame for every packet, and a packet for every frame
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "DtAvTime.h"    // Nanoseconds.
#include "DtSt2110Raw.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110RawTx_Configure -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtSt2110RawTx_Configure(DtSt2110RawTx* Tx, const St2110_TxConfigRaw* Config)
{
    memset(Tx, 0, sizeof(*Tx));
    if (Config->MaxRate <= 0)
        return DTAPI_E_INVALID_ARG;
    Tx->Config = *Config;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- UdpPayload -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns the size of the UDP payload of Frame's packet: the frame, with an RTP header
// in front when the program did not write one.
//
static int UdpPayload(const DtSt2110RawTx* Tx, const AvFifo_Frame* Frame)
{
    return Frame->NumValidBytes +
           (Tx->Config.CustomRtpHeader ? 0 : DT_AV_RTP_HEADER_SIZE);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110RawTx_FrameFits -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtSt2110RawTx_FrameFits(const DtSt2110RawTx* Tx, const DtAvTxStream* Stream,
                             const AvFifo_Frame* Frame)
{
    if (Frame->NumValidBytes < 0 || (size_t)Frame->NumValidBytes > Frame->Size)
        return false;
    if (Tx->Config.CustomRtpHeader && Frame->NumValidBytes < DT_AV_RTP_HEADER_SIZE)
        return false;
    const int Udp = Stream->Net.IsVersion2 ? DT_AV_UDP_JUMBO : DT_AV_UDP_STANDARD;
    return UdpPayload(Tx, Frame) <= Udp - DT_AV_UDP_HEADER_SIZE;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110RawTx_PacketBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtSt2110RawTx_PacketBytes(const DtSt2110RawTx* Tx, const DtAvTxStream* Stream,
                              const AvFifo_Frame* Frame)
{
    if (!DtSt2110RawTx_FrameFits(Tx, Stream, Frame))
        return -1;
    return DtAvNet_PacketSize(&Stream->Net, UdpPayload(Tx, Frame));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110RawTx_Packetize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtSt2110RawTx_Packetize(DtSt2110RawTx* Tx, DtAvTxStream* Stream,
                                    const AvFifo_Frame* Frame, const DtAvTxSink* Sink)
{
    if (!DtSt2110RawTx_FrameFits(Tx, Stream, Frame))
        return DTAPI_E_INVALID_FORMAT;
    const int Header = DtAvNet_HeaderSize(&Stream->Net);
    const int Payload = UdpPayload(Tx, Frame);
    uint8_t* Packet =
        Sink->ReserveRoom(Sink->Context, DtAvNet_PacketSize(&Stream->Net, Payload));

    uint8_t* Data = Packet + Header;
    if (!Tx->Config.CustomRtpHeader)
    {
        DtAvRtp Rtp;
        Rtp.Marker = Frame->Marker;
        Rtp.PayloadType = Stream->PayloadType;
        Rtp.SequenceNumber = (uint16_t)Stream->NextSequenceNumber++;
        Rtp.Timestamp = Frame->RtpTime;
        Rtp.Ssrc = Stream->Ssrc;
        DtAvRtp_Write(&Rtp, Data);
        Data += DT_AV_RTP_HEADER_SIZE;
    }
    if (Frame->NumValidBytes > 0)
        memcpy(Data, Frame->Data, (size_t)Frame->NumValidBytes);
    const uint64_t TodNs = DtAvTime_ToNs(&Frame->ToD) - (uint64_t)Stream->OutputDelayNs;
    Sink->CommitPacket(Sink->Context,
                       DtAvNet_WriteHeaders(&Stream->Net, Packet, Payload, 0, TodNs));
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reception +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindPayload -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Finds the payload of the RTP packet of Size bytes at Rtp: after the fixed header, the
// CSRCs and the extension, and before the padding (RFC 3550, 5.1 and 5.3.1). Returns
// false when they do not fit in the packet.
//
static bool FindPayload(const uint8_t* Rtp, int Size, int* Offset, int* Length)
{
    int Start = DT_AV_RTP_HEADER_SIZE + 4 * (Rtp[0] & 0x0F);
    if ((Rtp[0] & 0x10) != 0)
    {
        if (Start + 4 > Size)
            return false;
        Start += 4 + 4 * DtAvPacket_Get16(Rtp + Start + 2);
    }
    int End = Size;
    if ((Rtp[0] & 0x20) != 0 && Size > 0)
        End -= Rtp[Size - 1];
    if (Start > End)
        return false;
    *Offset = Start;
    *Length = End - Start;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110RawRx_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSt2110RawRx_Init(DtSt2110RawRx* Rx, const St2110_RxConfigRaw* Config,
                        const DtAvRxSink* Target)
{
    memset(Rx, 0, sizeof(*Rx));
    Rx->Config = *Config;
    Rx->Sink = *Target;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110RawRx_Parse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSt2110RawRx_Parse(DtSt2110RawRx* Rx, const uint8_t* Packet, int Size)
{
    DtAvRxPacket Udp;
    if (!DtAvRxPacket_Parse(Packet, Size, &Udp) ||
        Udp.PayloadSize < DT_AV_RTP_HEADER_SIZE)
    {
        Rx->Stats.IpPacketErrors++;
        return;
    }
    int Offset = 0;
    int Length = Udp.PayloadSize;
    if (!Rx->Config.IncludeRtpHeader &&
        !FindPayload(Udp.Payload, Udp.PayloadSize, &Offset, &Length))
    {
        Rx->Stats.IpPacketErrors++;
        return;
    }

    DtAvRtp Rtp;
    DtAvRtp_Read(Udp.Payload, &Rtp);
    if (Rx->HasSequenceNumber &&
        Rtp.SequenceNumber != (uint16_t)(Rx->LastSequenceNumber + 1))
    {
        Rx->Stats.Gaps++;
    }
    Rx->HasSequenceNumber = true;
    Rx->LastSequenceNumber = Rtp.SequenceNumber;

    DtAvFrame* Frame = DtAvFramePool_Get(Rx->Sink.Pool, (size_t)Length);
    if (Frame == NULL)
    {
        Rx->Stats.DroppedFrames++;
        return;
    }
    Frame->Frame.RtpTime = Rtp.Timestamp;
    Frame->Frame.Marker = Rtp.Marker;
    Frame->Frame.ToD = DtAvTime_FromNs(Udp.TodNs);
    if (Length > 0)
        memcpy(Frame->Frame.Data, Udp.Payload + Offset, (size_t)Length);
    Frame->Frame.NumValidBytes = Length;
    DtAvRxSink_Deliver(&Rx->Sink, &Rx->Stats, Frame);
}
