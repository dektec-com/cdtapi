// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtTsTrp.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The transparent packets a DtPcie card receives ASI into
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "cdtapi.h" // DtapiResult and the receive modes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transparent packets +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A DtPcie card writes what it receives over ASI into its DMA buffer as transparent
// packets of 216 bytes:
//   bytes 0-7     the card's time of day when the packet arrived: seconds, then
//                 nanoseconds, each 32 bits little endian
//   bytes 8-211   204 bytes of payload
//   byte 212      a sync nibble 0x5 in the upper half, and the packet-sync bit 0x08
//   byte 213      the number of valid payload bytes
//   bytes 214-215 a sequence number, little endian
// A DTA-2178 showed exactly this (plan 0011, step A).
//
// These functions turn each packet into what a receive channel delivers in its receive
// mode: the whole packet or only the valid bytes, with a time stamp before it if the
// mode asks for one. A packet without its packet-sync bit delivers nothing, except in
// DTAPI_RXMODE_STRAW and DTAPI_RXMODE_STTRP.
//

// The size of a transparent packet.
#define DT_TRP_SIZE 216

// The most bytes one packet decodes to: in DTAPI_RXMODE_STTRP with
// DTAPI_RXMODE_TIMESTAMP_TOD.
#define DT_TRP_MAX_OUTPUT_BYTES 216

// The number of correct packets in a row needed before the stream counts as found.
#define DT_TRP_NUM_SYNC 3

// The state of decoding: the receive mode and the synchronisation error.
typedef struct DtTsTrp
{
    int RxMode;                   // The receive mode, a DTAPI_RXMODE_ value
    bool SyncErr, SyncErrLatched; // The synchronisation error, now and latched
} DtTsTrp;

// Checks that RxMode is a receive mode these functions support: DTAPI_RXMODE_ST188,
// ST204, STMP2, STRAW or STTRP, optionally with DTAPI_RXMODE_TIMESTAMP32 or
// TIMESTAMP_TOD.
//
// Returns DTAPI_OK, or DTAPI_E_INVALID_MODE for any other mode, DTAPI_RXMODE_TIMESTAMP64
// included.
DtapiResult DtTsTrp_CheckMode(int RxMode);

// Starts decoding packets in RxMode, which DtTsTrp_CheckMode must have accepted. The
// synchronisation error is kept; DtTsTrp_ClearFlags clears it.
void DtTsTrp_Start(DtTsTrp* Trp, int RxMode);

// Decodes Packet into Out, which has room for DT_TRP_MAX_OUTPUT_BYTES bytes, and sets
// the flags. With Out NULL, it only returns the size and sets the flags. The size
// depends only on Packet and the mode.
//
// Returns the number of bytes written; 0 for a packet that is dropped; or -1 when the
// bytes are not a packet in sync: the sync nibble or the valid count is wrong. After -1,
// the caller must find the stream again (see DtTsTrp_FindSync).
int DtTsTrp_Decode(DtTsTrp* Trp, const uint8_t* Packet, uint8_t* Out);

// Finds the stream in the Size bytes at Buf: DT_TRP_NUM_SYNC packets in a row with the
// sync nibble, consecutive sequence numbers, and a valid count the mode accepts (the
// first packet's may be anything up to 204). On success, *Offset is where a whole packet
// starts: the first one found, or the next when the first one started before Buf.
//
// Returns false when no such run starts in the first
// Size - DT_TRP_NUM_SYNC * DT_TRP_SIZE bytes of Buf.
bool DtTsTrp_FindSync(const DtTsTrp* Trp, const uint8_t* Buf, size_t Size,
                      size_t* Offset);

// DtTsTrp_GetFlags sets DTAPI_RX_SYNC_ERR in *Flags and *Latched when the
// synchronisation error is set. DtTsTrp_ClearFlags clears it when Flags has
// DTAPI_RX_SYNC_ERR.
void DtTsTrp_GetFlags(const DtTsTrp* Trp, int* Flags, int* Latched);
void DtTsTrp_ClearFlags(DtTsTrp* Trp, int Flags);
