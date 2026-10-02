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

// The sizes in bytes of the headers of a packet.
#define DT_AV_ETH_HEADER_SIZE 14
#define DT_AV_VLAN_TAG_SIZE 4
#define DT_AV_IPV4_HEADER_SIZE 20
#define DT_AV_IPV6_HEADER_SIZE 40
#define DT_AV_UDP_HEADER_SIZE 8
#define DT_AV_RTP_HEADER_SIZE 12

// The sizes of the parts of ST 2110-20's payload header: the extended sequence number,
// which comes first, and a sample row data header (SRD), one for each row segment in the
// packet.
#define DT_AV_ESN_SIZE 2
#define DT_AV_SRD_SIZE 6

// The largest UDP datagram sent in a standard and in a jumbo frame, including the UDP
// header.
#define DT_AV_UDP_STANDARD 1460
#define DT_AV_UDP_JUMBO 8960

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A packet to transmit, as the card expects it in a pipe's buffer:
//
//   - A DtEthIp header, which tells the card the packet's size and when to send it.
//   - An Ethernet header, with a VLAN tag when a VLAN ID or priority is set.
//   - An IPv4 header of 20 bytes, marked as not to be fragmented, or an IPv6 header.
//   - A UDP header.
//   - The UDP payload.
//   - Zero bytes up to the pipe's data width.
//
// The IP and UDP checksums are left zero; the card fills them in.
//

// Where a stream is sent and how: the addresses, ports and header fields of its packets.
typedef struct DtAvNet
{
    bool IsVersion2;   // Whether to write version 2 packet headers, for jumbo frames
    int Alignment;     // The pipe's data width in bytes, to which a packet is padded
    uint8_t SrcMac[6]; // The source MAC address
    uint8_t DstMac[6]; // The destination MAC address
    int VlanId;        // The VLAN ID; with VlanPriority 0, 0 means no VLAN tag
    int VlanPriority;  // The VLAN priority, 0 to 7
    bool IpV6;         // Whether the packets are IPv6 rather than IPv4
    uint8_t SrcIp[16]; // The source IP address; only the first 4 bytes for IPv4
    uint8_t DstIp[16]; // The destination IP address; only the first 4 bytes for IPv4
    int DiffServ;      // The DSCP and ECN bits: IPv4's TOS byte, IPv6's traffic class
    int TimeToLive;    // IPv4's time to live, IPv6's hop limit
    uint16_t SrcPort;  // The source UDP port
    uint16_t DstPort;  // The destination UDP port
    uint16_t IpIdentification; // The identification of the next IPv4 packet
} DtAvNet;

// Returns the size of the headers before the UDP payload: the DtEthIp, Ethernet, IP and
// UDP headers.
int DtAvNet_HeaderSize(const DtAvNet* Net);

// Returns the size of a packet with a UDP payload of PayloadSize bytes, padding included.
int DtAvNet_PacketSize(const DtAvNet* Net, int PayloadSize);

// Writes the headers of a packet in front of its UDP payload, which must already be in
// place, and zeroes the padding after it. The packet is sent at TodNs nanoseconds, to
// UDP port DstPort plus DstPortOffset. Also increments the IPv4 identification. Returns
// the size of the packet.
int DtAvNet_WriteHeaders(DtAvNet* Net, uint8_t* Packet, int PayloadSize,
                         int DstPortOffset, uint64_t TodNs);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reception +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// What matters of a received UDP packet.
typedef struct DtAvRxPacket
{
    uint64_t TodNs;         // When the packet arrived, in nanoseconds
    int SubStream;          // Which of the filter's destination ports it arrived on
    const uint8_t* Payload; // The UDP payload
    int PayloadSize;        // The size of the payload, from the UDP header
} DtAvRxPacket;

// Finds the UDP payload of a received packet of Size bytes. Returns false when the
// packet is not a valid IPv4 or IPv6 UDP packet: it is shorter than a DtEthIp header,
// that header is not valid, or the UDP header or the length it gives do not fit in the
// Ethernet frame. The frame may end in the Ethernet checksum; the UDP length leaves it
// out.
bool DtAvRxPacket_Parse(const uint8_t* Packet, int Size, DtAvRxPacket* Rx);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= RTP +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The fields of an RTP header, as CDTAPI uses it: without CSRCs, extension or padding.
typedef struct DtAvRtp
{
    bool Marker;             // The marker bit
    int PayloadType;         // The payload type, 0 to 127
    uint16_t SequenceNumber; // The sequence number
    uint32_t Timestamp;      // The RTP timestamp
    uint32_t Ssrc;           // The synchronisation source
} DtAvRtp;

// Writes an RTP header of version 2, DT_AV_RTP_HEADER_SIZE bytes, at Bytes.
void DtAvRtp_Write(const DtAvRtp* Rtp, uint8_t* Bytes);

// Reads the RTP header of DT_AV_RTP_HEADER_SIZE bytes at Bytes.
void DtAvRtp_Read(const uint8_t* Bytes, DtAvRtp* Rtp);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= ST 2110-20 rows +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A sample row data header of ST 2110-20 (RFC 4175), which says where in the picture a
// row segment of the packet goes.
typedef struct DtAvSrd
{
    int Length;        // The size of the row segment in bytes
    bool Field;        // Whether the segment is in the second field
    int Row;           // The row number, 15 bits
    bool Continuation; // Whether another header follows this one
    int Offset;        // The position of the segment's first pixel in the row, 15 bits
} DtAvSrd;

// Write and read a header of DT_AV_SRD_SIZE bytes at Bytes.
void DtAvSrd_Write(const DtAvSrd* Srd, uint8_t* Bytes);
void DtAvSrd_Read(const uint8_t* Bytes, DtAvSrd* Srd);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Byte order +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Network headers are big-endian: the most significant byte comes first.
//

// Reads a 16-bit big-endian value at Bytes.
static inline uint16_t DtAvPacket_Get16(const uint8_t* Bytes)
{
    return (uint16_t)(Bytes[0] << 8 | Bytes[1]);
}

// Writes Value at Bytes as a 16-bit big-endian value.
static inline void DtAvPacket_Put16(uint16_t Value, uint8_t* Bytes)
{
    Bytes[0] = (uint8_t)(Value >> 8);
    Bytes[1] = (uint8_t)Value;
}
