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
// The 8-bit DIDs of the packets the parser treats itself rather than list: the audio of
// SMPTE ST 299-1 (HD and up) and ST 272 (SD), and the payload ID of ST 352.
//

#define DT_SDIANC_DID_HD_AUDIO_DATA_G1 0xE7    // Groups 2 to 4: 0xE6, 0xE5, 0xE4
#define DT_SDIANC_DID_HD_AUDIO_CONTROL_G1 0xE3 // Groups 2 to 4: 0xE2, 0xE1, 0xE0
#define DT_SDIANC_DID_SD_AUDIO_DATA_G1 0xFF    // Groups 2 to 4: 0xFD, 0xFB, 0xF9
#define DT_SDIANC_DID_SD_AUDIO_EXT_G1 0xFE     // Groups 2 to 4: 0xFC, 0xFA, 0xF8
#define DT_SDIANC_DID_SD_AUDIO_CONTROL_G1 0xEF // Groups 2 to 4: 0xEE, 0xED, 0xEC
#define DT_SDIANC_DID_PAYLOAD_ID 0x41
#define DT_SDIANC_SDID_PAYLOAD_ID 0x01

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Packets +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A packet found in a stream: where it starts, its IDs and its user data words.
typedef struct DtSdiAncFound
{
    int Start;             // The index of its first ADF word
    uint8_t Did;           // The data ID, without parity
    uint8_t SdidOrDbn;     // The secondary data ID or data block number, without parity
    int NumWords;          // The user data words
    const uint16_t* Words; // The user data words, in the stream searched
    bool ChecksumOk;       // Whether the checksum word held
} DtSdiAncFound;

// Finds the next packet in Words[*Pos] to Words[Count - 1], a stream's words, and moves
// *Pos past it. A packet must lie wholly within the words. Returns false when there is
// none.
bool DtSdiAnc_Find(const uint16_t* Words, int Count, int* Pos, DtSdiAncFound* Packet);

// Returns whether Did is one of audio's, data or control, in HD or SD.
bool DtSdiAnc_IsAudio(uint8_t Did);

// Returns whether any packet in the horizontal blanking (InHanc) or the vertical
// blanking of line Line (from 1) can be listed, so that a parser that wants no audio
// need not read the rest.
bool DtSdiAnc_IsWanted(const DtSdiAncFilter* Filters, int NumFilters, bool InHanc,
                       int Line);

// Returns whether a packet with Did and SdidOrDbn on line Line (from 1), in the
// horizontal blanking or not, is to be listed: when it matches one of the NumFilters
// filters, or with no filters, when it is neither audio nor the payload ID.
bool DtSdiAnc_IsListed(const DtSdiAncFilter* Filters, int NumFilters, uint8_t Did,
                       uint8_t SdidOrDbn, bool InHanc, int Line);
