// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtEthIp.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The header in front of every Ethernet frame in a pipe's shared buffer
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtEthIp +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Reads and writes the header in front of each Ethernet frame in a pipe's shared buffer.
// The buffer holds packets. Each packet is an 18-byte header, then an Ethernet frame,
// padded to a whole number of 64-bit words.
//
// The driver declares the header as bit fields. The C standard leaves the layout of bit
// fields to the compiler, so CDTAPI reads and writes the header with shifts instead. The
// layout is the one MSVC and GCC give on x86 and ARM: little-endian 64-bit words, least
// significant bit first.
//
//   bytes 0-7    bits 0-15 sync word, EEEEh for version 1 and EFEFh for version 2;
//                version 1: bits 16-23 size in words, 24-34 frame size in bytes;
//                version 2: bits 16-26 size in words, 27-34 padding in bytes;
//                bits 35-39 offset of the IP addresses, 40-47 offset of the UDP ports,
//                both in 4-byte units from the start of the header; bit 48 protocol,
//                49-50 packet type, 51-52 substream, 53 IPv4 header checksum error,
//                54 UDP checksum error, 55 TCP checksum error, 56 time stamp request,
//                57 time stamp valid, 58-63 fingerprint
//   bytes 8-15   bits 0-29 nanoseconds and bits 32-63 seconds of the time of day
//   bytes 16-17  alignment, zero
//
// A packet is 8 bytes times its number of words. The frame starts right after the header
// and ends before the padding. Version 1 gives the frame size, and the padding follows
// from it; version 2 gives the padding, and the frame size follows from that. A time
// stamp packet has no alignment bytes, so its frame starts at byte 16.
//

// The size of the header in bytes, and the size of a word, the unit of a packet's size.
#define DT_ETHIP_HEADER_SIZE 18
#define DT_ETHIP_WORD_SIZE 8

// The sync words, which start each header and give its version.
#define DT_ETHIP_SYNC_V1 0xEEEE
#define DT_ETHIP_SYNC_V2 0xEFEF

// Packet types.
#define DT_ETHIP_TYPE_TIMESTAMP 0
#define DT_ETHIP_TYPE_IPV4 1
#define DT_ETHIP_TYPE_IPV6 2
#define DT_ETHIP_TYPE_OTHER 3

// The value of the protocol bit for UDP. The bit is 0 for any other protocol.
#define DT_ETHIP_PROTO_UDP 1

// The largest frame each version can describe. A version 1 packet has at most 255 words;
// a version 2 packet at most 2,047.
#define DT_ETHIP_MAX_FRAME_V1 (255 * DT_ETHIP_WORD_SIZE - DT_ETHIP_HEADER_SIZE)
#define DT_ETHIP_MAX_FRAME_V2 (2047 * DT_ETHIP_WORD_SIZE - DT_ETHIP_HEADER_SIZE)

// The fields of the driver's DtEthIpHeader, as plain values instead of bits. The struct
// is not called DtEthIpHeader because the ABI already uses that name (CONTRIBUTING,
// rule 12).
typedef struct DtEthIpHeaderFields
{
    bool IsVersion2;        // True for a version 2 header, false for version 1
    int NumWords;           // The packet's size in 64-bit words, header included
    int FrameSize;          // The size of the Ethernet frame in bytes
    int IpAddressOffset;    // Bytes from the start of the header to the source IP address
    int PortOffset;         // Bytes from the start of the header to the UDP header
    int IsUdp;              // DT_ETHIP_PROTO_UDP for UDP, 0 for any other protocol
    int PacketType;         // A DT_ETHIP_TYPE_* value
    int SubStream;          // Which port of the pipe's IP filter matched, 0 to 3
    bool IpV4ChecksumError; // True when the IPv4 header checksum is wrong
    bool UdpChecksumError;  // True when the UDP checksum is wrong
    bool TcpChecksumError;  // True when the TCP checksum is wrong
    bool TimestampRequest;  // True to ask the hardware for a time stamp
    bool TimestampValid;    // True when Seconds and Nanoseconds hold a time stamp
    int Fingerprint;        // A 6-bit value of the hardware; CDTAPI does not use it
    uint32_t Seconds;       // The time stamp: seconds of the time of day
    uint32_t Nanoseconds;   // The time stamp: nanoseconds, 0 to 999,999,999
} DtEthIpHeaderFields;

// Returns the number of 64-bit words of a packet with a frame of FrameSize bytes. The
// packet is padded to a multiple of Alignment bytes, itself a multiple of 8.
int DtEthIp_NumWords(int FrameSize, int Alignment);

// Returns the number of bytes before the frame in a packet of PacketType: 16 for a time
// stamp packet, DT_ETHIP_HEADER_SIZE for any other.
int DtEthIp_HeaderSize(int PacketType);

// Writes Header as DT_ETHIP_HEADER_SIZE bytes to Bytes. A value too large for its bit
// field is cut to fit. Version 1 writes FrameSize; version 2 writes the padding, which it
// computes from NumWords and FrameSize.
void DtEthIp_Write(const DtEthIpHeaderFields* Header, uint8_t* Bytes);

// Reads the DT_ETHIP_HEADER_SIZE bytes at Bytes into *Header. Returns false when the
// sync word is unknown, or when the frame does not fit in the packet; *Header then holds
// what could be read.
bool DtEthIp_Read(const uint8_t* Bytes, DtEthIpHeaderFields* Header);
