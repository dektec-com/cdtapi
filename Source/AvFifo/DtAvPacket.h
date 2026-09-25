// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAvPacket.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The packets of a pipe: Ethernet, IP, UDP, RTP and ST 2110-20 headers
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Sizes +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define DT_AV_ETH_HEADER_SIZE 14
#define DT_AV_VLAN_TAG_SIZE 4
#define DT_AV_IPV4_HEADER_SIZE 20
#define DT_AV_IPV6_HEADER_SIZE 40
#define DT_AV_UDP_HEADER_SIZE 8
#define DT_AV_RTP_HEADER_SIZE 12

// ST 2110-20's payload header: the extended sequence number, then a sample row data
// header per row segment in the packet.
#define DT_AV_ESN_SIZE 2
#define DT_AV_SRD_SIZE 6

// UDP datagrams of a standard and of a jumbo frame, their headers included.
#define DT_AV_UDP_STANDARD 1460
#define DT_AV_UDP_JUMBO 8960

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A transmitted packet: the DtEthIp header with the time to send it, an Ethernet header
// with a VLAN tag when an ID or priority is given, an IPv4 header of 20 bytes that may
// not be fragmented or an IPv6 header, and a UDP header, all checksums zero for the card
// to insert; then the UDP payload, and zero bytes up to the pipe's alignment.
//

// Where a stream goes and how it gets there.
typedef struct DtAvNet
{
    bool IsVersion2; // Version 2 packet headers, for a pipe with jumbo frames
    int Alignment;   // Bytes a packet pads to: the pipe's data width in bytes
    uint8_t SrcMac[6];
    uint8_t DstMac[6];
    int VlanId;       // 0, with VlanPriority 0: no VLAN tag
    int VlanPriority; // 0 to 7
    bool IpV6;
    uint8_t SrcIp[16]; // 4 bytes for IPv4
    uint8_t DstIp[16]; // 4 bytes for IPv4
    int DiffServ;
    int TimeToLive;
    uint16_t SrcPort;
    uint16_t DstPort;
    uint16_t IpIdentification; // Of the next IPv4 packet
} DtAvNet;

// The bytes before a UDP payload: the DtEthIp, Ethernet, IP and UDP headers.
int DtAvNet_HeaderSize(const DtAvNet* Net);

// The bytes of a packet with a UDP payload of PayloadSize bytes, padded.
int DtAvNet_PacketSize(const DtAvNet* Net, int PayloadSize);

// Writes the headers of a packet whose UDP payload of PayloadSize bytes is in place after
// them, to be sent at TodNs nanoseconds, to destination port DstPort plus DstPortOffset,
// and zeroes the padding. Advances the IPv4 identification. Returns the packet's size.
int DtAvNet_WriteHeaders(DtAvNet* Net, uint8_t* Packet, int PayloadSize,
                         int DstPortOffset, uint64_t TodNs);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reception +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A received UDP packet.
typedef struct DtAvRxPacket
{
    uint64_t TodNs;         // Time of arrival
    int SubStream;          // Which of the filter's destination ports it arrived on
    const uint8_t* Payload; // The UDP payload
    int PayloadSize;        // Its bytes, from the UDP header
} DtAvRxPacket;

// Reads the packet of Size bytes at Packet. False when its DtEthIp header does not check,
// it is not a UDP packet, or its UDP header does not fit its frame or claims more bytes
// than the frame holds. A frame from the card may end in the Ethernet checksum, which the
// UDP length leaves out.
bool DtAvRxPacket_Parse(const uint8_t* Packet, int Size, DtAvRxPacket* Rx);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= RTP +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The fields of an RTP header without CSRC list or extension.
typedef struct DtAvRtp
{
    bool Marker;
    int PayloadType; // 0 to 127
    uint16_t SequenceNumber;
    uint32_t Timestamp;
    uint32_t Ssrc;
} DtAvRtp;

// Writes the DT_AV_RTP_HEADER_SIZE bytes of version 2 without padding at Bytes.
void DtAvRtp_Write(const DtAvRtp* Rtp, uint8_t* Bytes);

// Reads the DT_AV_RTP_HEADER_SIZE bytes at Bytes.
void DtAvRtp_Read(const uint8_t* Bytes, DtAvRtp* Rtp);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= ST 2110-20 rows +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A sample row data header of ST 2110-20 (RFC 4175).
typedef struct DtAvSrd
{
    int Length;        // Bytes of the row segment
    bool Field;        // The second field
    int Row;           // 15 bits
    bool Continuation; // Another header follows
    int Offset;        // Pixel offset in the row, 15 bits
} DtAvSrd;

// Writes and reads the DT_AV_SRD_SIZE bytes of a header.
void DtAvSrd_Write(const DtAvSrd* Srd, uint8_t* Bytes);
void DtAvSrd_Read(const uint8_t* Bytes, DtAvSrd* Srd);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Byte order +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static inline uint16_t DtAvPacket_Get16(const uint8_t* Bytes)
{
    return (uint16_t)(Bytes[0] << 8 | Bytes[1]);
}

static inline void DtAvPacket_Put16(uint16_t Value, uint8_t* Bytes)
{
    Bytes[0] = (uint8_t)(Value >> 8);
    Bytes[1] = (uint8_t)Value;
}
