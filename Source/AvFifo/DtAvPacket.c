// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAvPacket.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The packets of a pipe: Ethernet, IP, UDP, RTP and ST 2110-20 headers
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "DtAvPacket.h"     // Interface being implemented.
#include "DtAvTime.h"       // Nanoseconds per second.
#include "DtPcie/DtEthIp.h" // The packet header.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define ETH_TYPE_VLAN 0x8100
#define ETH_TYPE_IPV4 0x0800
#define ETH_TYPE_IPV6 0x86DD
#define IP_PROTOCOL_UDP 17

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasVlan -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool HasVlan(const DtAvNet* Net)
{
    return Net->VlanId != 0 || Net->VlanPriority != 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EthernetSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int EthernetSize(const DtAvNet* Net)
{
    return DT_AV_ETH_HEADER_SIZE + (HasVlan(Net) ? DT_AV_VLAN_TAG_SIZE : 0);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvNet_HeaderSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtAvNet_HeaderSize(const DtAvNet* Net)
{
    return DT_ETHIP_HEADER_SIZE + EthernetSize(Net) +
           (Net->IpV6 ? DT_AV_IPV6_HEADER_SIZE : DT_AV_IPV4_HEADER_SIZE) +
           DT_AV_UDP_HEADER_SIZE;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvNet_PacketSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtAvNet_PacketSize(const DtAvNet* Net, int PayloadSize)
{
    int FrameSize = DtAvNet_HeaderSize(Net) - DT_ETHIP_HEADER_SIZE + PayloadSize;
    return DtEthIp_NumWords(FrameSize, Net->Alignment) * DT_ETHIP_WORD_SIZE;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvNet_Finish -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtAvNet_Finish(DtAvNet* Net, uint8_t* Packet, int PayloadSize, int DstPortOffset,
                   uint64_t TodNs)
{
    int UdpLength = DT_AV_UDP_HEADER_SIZE + PayloadSize;
    int FrameSize = DtAvNet_HeaderSize(Net) - DT_ETHIP_HEADER_SIZE + PayloadSize;

    // Ethernet.
    uint8_t* Eth = Packet + DT_ETHIP_HEADER_SIZE;
    memcpy(Eth, Net->DstMac, 6);
    memcpy(Eth + 6, Net->SrcMac, 6);
    int Offset = 12;
    if (HasVlan(Net))
    {
        DtAvPacket_Put16(ETH_TYPE_VLAN, Eth + Offset);
        uint16_t Tci = (uint16_t)((Net->VlanPriority & 7) << 13 | (Net->VlanId & 0xFFF));
        DtAvPacket_Put16(Tci, Eth + Offset + 2);
        Offset += DT_AV_VLAN_TAG_SIZE;
    }
    DtAvPacket_Put16(Net->IpV6 ? ETH_TYPE_IPV6 : ETH_TYPE_IPV4, Eth + Offset);
    Offset += 2;

    // IP.
    uint8_t* Ip = Eth + Offset;
    int IpAddressOffset = 0;
    if (Net->IpV6)
    {
        Ip[0] = (uint8_t)(0x60 | (Net->DiffServ >> 4 & 0xF));
        Ip[1] = (uint8_t)((Net->DiffServ & 0xF) << 4);
        Ip[2] = 0;
        Ip[3] = 0;
        DtAvPacket_Put16((uint16_t)UdpLength, Ip + 4);
        Ip[6] = IP_PROTOCOL_UDP;
        Ip[7] = (uint8_t)Net->TimeToLive;
        memcpy(Ip + 8, Net->SrcIp, 16);
        memcpy(Ip + 24, Net->DstIp, 16);
        IpAddressOffset = 8;
        Offset += DT_AV_IPV6_HEADER_SIZE;
    }
    else
    {
        Ip[0] = 0x45;
        Ip[1] = (uint8_t)Net->DiffServ;
        DtAvPacket_Put16((uint16_t)(DT_AV_IPV4_HEADER_SIZE + UdpLength), Ip + 2);
        DtAvPacket_Put16(Net->IpIdentification++, Ip + 4);
        Ip[6] = 0x40; // Don't fragment
        Ip[7] = 0;
        Ip[8] = (uint8_t)Net->TimeToLive;
        Ip[9] = IP_PROTOCOL_UDP;
        Ip[10] = 0;
        Ip[11] = 0;
        memcpy(Ip + 12, Net->SrcIp, 4);
        memcpy(Ip + 16, Net->DstIp, 4);
        IpAddressOffset = 12;
        Offset += DT_AV_IPV4_HEADER_SIZE;
    }

    // UDP.
    uint8_t* Udp = Eth + Offset;
    DtAvPacket_Put16(Net->SrcPort, Udp);
    DtAvPacket_Put16((uint16_t)(Net->DstPort + DstPortOffset), Udp + 2);
    DtAvPacket_Put16((uint16_t)UdpLength, Udp + 4);
    DtAvPacket_Put16(0, Udp + 6);

    // The packet header, and the padding.
    DtEthIpFields Header;
    memset(&Header, 0, sizeof(Header));
    Header.Jumbo = Net->Jumbo;
    Header.NumWords = DtEthIp_NumWords(FrameSize, Net->Alignment);
    Header.FrameSize = FrameSize;
    Header.IpAddressOffset = (int)(Ip - Packet) + IpAddressOffset;
    Header.PortOffset = (int)(Udp - Packet);
    Header.Protocol = DT_ETHIP_PROTO_UDP;
    Header.PacketType = Net->IpV6 ? DT_ETHIP_TYPE_IPV6 : DT_ETHIP_TYPE_IPV4;
    Header.TimestampValid = true;
    Header.Seconds = (uint32_t)(TodNs / DT_AV_NS_PER_SEC);
    Header.Nanoseconds = (uint32_t)(TodNs % DT_AV_NS_PER_SEC);
    DtEthIp_Write(&Header, Packet);

    int Size = Header.NumWords * DT_ETHIP_WORD_SIZE;
    int Used = DT_ETHIP_HEADER_SIZE + FrameSize;
    memset(Packet + Used, 0, (size_t)(Size - Used));
    return Size;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reception +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvRxPacket_Parse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtAvRxPacket_Parse(const uint8_t* Packet, int Size, DtAvRxPacket* Rx)
{
    DtEthIpFields Header;

    memset(Rx, 0, sizeof(*Rx));
    if (Size < DT_ETHIP_HEADER_SIZE || !DtEthIp_Read(Packet, &Header))
        return false;
    int End = DtEthIp_HeaderSize(Header.PacketType) + Header.FrameSize;
    if (End > Size || Header.Protocol != DT_ETHIP_PROTO_UDP ||
        (Header.PacketType != DT_ETHIP_TYPE_IPV4 &&
         Header.PacketType != DT_ETHIP_TYPE_IPV6) ||
        Header.PortOffset + DT_AV_UDP_HEADER_SIZE > End)
    {
        return false;
    }
    const uint8_t* Udp = Packet + Header.PortOffset;
    int UdpLength = DtAvPacket_Get16(Udp + 4);
    if (UdpLength < DT_AV_UDP_HEADER_SIZE || Header.PortOffset + UdpLength > End)
        return false;

    Rx->TodNs = (uint64_t)Header.Seconds * DT_AV_NS_PER_SEC + Header.Nanoseconds;
    Rx->SubStream = Header.SubStream;
    Rx->Udp = Udp + DT_AV_UDP_HEADER_SIZE;
    Rx->UdpSize = UdpLength - DT_AV_UDP_HEADER_SIZE;
    return true;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= RTP +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvRtp_Write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtAvRtp_Write(const DtAvRtp* Rtp, uint8_t* Bytes)
{
    Bytes[0] = 0x80;
    Bytes[1] = (uint8_t)((Rtp->Marker ? 0x80 : 0) | (Rtp->PayloadType & 0x7F));
    DtAvPacket_Put16(Rtp->SequenceNumber, Bytes + 2);
    DtAvPacket_Put16((uint16_t)(Rtp->Timestamp >> 16), Bytes + 4);
    DtAvPacket_Put16((uint16_t)Rtp->Timestamp, Bytes + 6);
    DtAvPacket_Put16((uint16_t)(Rtp->Ssrc >> 16), Bytes + 8);
    DtAvPacket_Put16((uint16_t)Rtp->Ssrc, Bytes + 10);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvRtp_Read -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtAvRtp_Read(const uint8_t* Bytes, DtAvRtp* Rtp)
{
    Rtp->Marker = (Bytes[1] & 0x80) != 0;
    Rtp->PayloadType = Bytes[1] & 0x7F;
    Rtp->SequenceNumber = DtAvPacket_Get16(Bytes + 2);
    Rtp->Timestamp =
        (uint32_t)DtAvPacket_Get16(Bytes + 4) << 16 | DtAvPacket_Get16(Bytes + 6);
    Rtp->Ssrc =
        (uint32_t)DtAvPacket_Get16(Bytes + 8) << 16 | DtAvPacket_Get16(Bytes + 10);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= ST 2110-20 rows +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvSrd_Write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtAvSrd_Write(const DtAvSrd* Srd, uint8_t* Bytes)
{
    DtAvPacket_Put16((uint16_t)Srd->Length, Bytes);
    DtAvPacket_Put16((uint16_t)((Srd->Field ? 0x8000 : 0) | (Srd->Row & 0x7FFF)),
                     Bytes + 2);
    DtAvPacket_Put16(
        (uint16_t)((Srd->Continuation ? 0x8000 : 0) | (Srd->Offset & 0x7FFF)), Bytes + 4);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvSrd_Read -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtAvSrd_Read(const uint8_t* Bytes, DtAvSrd* Srd)
{
    uint16_t Row = DtAvPacket_Get16(Bytes + 2);
    uint16_t Offset = DtAvPacket_Get16(Bytes + 4);

    Srd->Length = DtAvPacket_Get16(Bytes);
    Srd->Field = (Row & 0x8000) != 0;
    Srd->Row = Row & 0x7FFF;
    Srd->Continuation = (Offset & 0x8000) != 0;
    Srd->Offset = Offset & 0x7FFF;
}
