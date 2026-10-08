// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiAnc.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Finding SMPTE ST 291 ancillary packets in a stream of SDI words
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// CDTAPI includes
#include "cdtapi_sdi.h" // The filter.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Data IDs +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// These are the 8-bit DIDs of the packets that the parser handles itself instead of
// listing them. They are the audio packets of SMPTE ST 299-1 (HD and up) and ST 272
// (SD), and the payload ID of ST 352.
//

#define DT_SDIANC_DID_HD_AUDIO_DATA_G1 0xE7    // Groups 2 to 4: 0xE6, 0xE5, 0xE4
#define DT_SDIANC_DID_HD_AUDIO_CONTROL_G1 0xE3 // Groups 2 to 4: 0xE2, 0xE1, 0xE0
#define DT_SDIANC_DID_SD_AUDIO_DATA_G1 0xFF    // Groups 2 to 4: 0xFD, 0xFB, 0xF9
#define DT_SDIANC_DID_SD_AUDIO_EXT_G1 0xFE     // Groups 2 to 4: 0xFC, 0xFA, 0xF8
#define DT_SDIANC_DID_SD_AUDIO_CONTROL_G1 0xEF // Groups 2 to 4: 0xEE, 0xED, 0xEC
#define DT_SDIANC_DID_PAYLOAD_ID 0x41
#define DT_SDIANC_SDID_PAYLOAD_ID 0x01

// The word written in place of a packet's checksum when the transmitter fills it in. It
// is a legal word, because some transmitters need one before they replace it.
#define DT_SDIANC_NO_CHECKSUM 0x0CC

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Packets +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Describes a packet found in a stream of words.
typedef struct DtSdiAncFound
{
    int Start;             // The index of the packet's first ADF word in the stream
    uint8_t Did;           // The data ID, without parity
    uint8_t SdidOrDbn;     // The secondary data ID or data block number, without parity
    int NumWords;          // The number of user data words
    const uint16_t* Words; // Points to the first user data word, in the stream searched
    bool ChecksumOk;       // Whether the checksum word is correct
} DtSdiAncFound;

// Finds the next packet in a stream's words, from Words[*Pos] up to Words[Count - 1].
// On success, fills in *Packet and moves *Pos past the packet. Returns false when there
// is no further packet, or when the next packet does not lie wholly within the words.
bool DtSdiAnc_Find(const uint16_t* Words, int Count, int* Pos, DtSdiAncFound* Packet);

// Returns whether Did is the DID of an audio packet, for data or control, in HD or SD.
bool DtSdiAnc_IsAudio(uint8_t Did);

// The DIDs of the SD audio control packets (SMPTE ST 272), EF for group 1 down to EC
// for group 4.
#define DT_SDIANC_DID_SD_CONTROL_1 0xEF
#define DT_SDIANC_DID_SD_CONTROL_4 0xEC

// Returns whether the filters can list any packet in one section of line Line,
// counting from 1. The section is the horizontal blanking when InHanc is true, and
// otherwise the vertical blanking. A parser that wants no audio need not read a section
// for which this returns false.
bool DtSdiAnc_IsWanted(const DtSdiAncFilter* Filters, int NumFilters, bool InHanc,
                       int Line);

// Writes a packet into Words, starting at index Pos. The packet consists of:
//   1. The ancillary data flag.
//   2. Did, SdidOrDbn and the data count Count, each with its parity.
//   3. Count user data words from Data, of which only the lower ten bits are kept.
//   4. The checksum. When Checksum is false, the word is DT_SDIANC_NO_CHECKSUM, for the
//      transmitter to replace.
// Returns the index of the word after the packet.
int DtSdiAnc_Put(uint16_t* Words, int Pos, uint8_t Did, uint8_t SdidOrDbn,
                 const uint16_t* Data, int Count, bool Checksum);

// Returns the lower eight bits of Value with even parity in bit 8 and its inverse in
// bit 9. This is how a packet's IDs and data count carry their parity.
uint16_t DtSdiAnc_WithParity8(unsigned Value);

// Returns whether a packet is to be listed. The packet has the IDs Did and SdidOrDbn
// and lies on line Line, counting from 1. InHanc tells whether it lies in the
// horizontal blanking. With filters, the packet is listed when it matches one of the
// NumFilters filters. A filter compares its Sdid only when Did is below 80 (hex),
// because from there on the word is a data block number. With no filters, the packet is
// listed unless it is audio or the payload ID.
bool DtSdiAnc_IsListed(const DtSdiAncFilter* Filters, int NumFilters, uint8_t Did,
                       uint8_t SdidOrDbn, bool InHanc, int Line);
