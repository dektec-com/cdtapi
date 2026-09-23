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
// A pipe's shared buffer holds packets, each an 18-byte header followed by an Ethernet
// frame and padded to a whole number of 64-bit words. The header is a row of bit fields
// of 64-bit words, whose layout the C standard leaves to the compiler; here it is written
// and read with shifts, in the layout MSVC and GCC give those bit fields on x86 and ARM,
// least significant bit first in a little-endian word:
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
// A packet's size is its words times 8. Its frame fills the bytes after the header up to
// the size less the padding, which version 1 derives from the frame size it carries. A
// packet of the time stamp type has no alignment bytes: its frame starts at byte 16.
//

// The bytes of the header, and the unit of a packet's size.
#define DT_ETHIP_HEADER_SIZE 18
#define DT_ETHIP_WORD_SIZE 8

// The sync words.
#define DT_ETHIP_SYNC_V1 0xEEEE
#define DT_ETHIP_SYNC_V2 0xEFEF

// Packet types.
#define DT_ETHIP_TYPE_TIMESTAMP 0
#define DT_ETHIP_TYPE_IPV4 1
#define DT_ETHIP_TYPE_IPV6 2
#define DT_ETHIP_TYPE_OTHER 3

// The protocol bit: set for UDP, clear for anything else.
#define DT_ETHIP_PROTO_UDP 1

// The largest frame each version describes: version 1 counts at most 255 words, version 2
// at most 2,047.
#define DT_ETHIP_MAX_FRAME_V1 (255 * DT_ETHIP_WORD_SIZE - DT_ETHIP_HEADER_SIZE)
#define DT_ETHIP_MAX_FRAME_V2 (2047 * DT_ETHIP_WORD_SIZE - DT_ETHIP_HEADER_SIZE)

typedef struct DtEthIpFields
{
    bool Jumbo;          // Version 2
    int NumWords;        // The packet's size in 64-bit words
    int FrameSize;       // Bytes of the Ethernet frame
    int IpAddressOffset; // Bytes from the start of the header to the source IP address
    int PortOffset;      // Bytes from the start of the header to the UDP header
    int Protocol;
    int PacketType; // A DT_ETHIP_TYPE_ value
    int SubStream;  // 0 to 3
    bool IpV4ChecksumError;
    bool UdpChecksumError;
    bool TcpChecksumError;
    bool TimestampRequest;
    bool TimestampValid;
    int Fingerprint;
    uint32_t Seconds;     // Time of day
    uint32_t Nanoseconds; // Time of day, 0 to 999,999,999
} DtEthIpFields;

// The words a packet with a frame of FrameSize bytes takes, padded to Alignment bytes, a
// multiple of 8.
int DtEthIp_NumWords(int FrameSize, int Alignment);

// The bytes before the frame of a packet of PacketType.
int DtEthIp_HeaderSize(int PacketType);

// Writes Header into the DT_ETHIP_HEADER_SIZE bytes at Bytes. Fields wider than their
// bit field are cut to it, and version 1's frame size or version 2's padding is derived
// from NumWords and FrameSize.
void DtEthIp_Write(const DtEthIpFields* Header, uint8_t* Bytes);

// Reads the DT_ETHIP_HEADER_SIZE bytes at Bytes into *Header. False, with *Header filled
// as far as it could be read, for an unknown sync word or a frame that does not fit the
// packet's words.
bool DtEthIp_Read(const uint8_t* Bytes, DtEthIpFields* Header);
