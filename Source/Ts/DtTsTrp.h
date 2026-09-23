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
// packets of 216 bytes: the card's time of day when the packet came, seconds and
// nanoseconds, each 32 bits little endian; 204 bytes of payload; and a trailer, byte 212
// holding a sync nibble 0x5 in its upper half and the packet-sync bit 0x08, byte 213 the
// count of valid payload bytes, bytes 214 and 215 a sequence number, little endian. A
// DTA-2178 showed exactly this (0011, step A).
//
// What a receive channel delivers of each is, per receive mode, the packet or the valid
// bytes, a time stamp before it when asked, and nothing of a packet without its
// packet-sync bit but in DTAPI_RXMODE_STRAW and DTAPI_RXMODE_STTRP.
//

#define DT_TRP_SIZE 216

// The most a packet converts to: DTAPI_RXMODE_STTRP with DTAPI_RXMODE_TIMESTAMP_TOD.
#define DT_TRP_MAX_OUTPUT 216

// The packets in a row that must look right before the stream counts as found.
#define DT_TRP_NUM_SYNC 3

typedef struct DtTsTrp
{
    int RxMode;
    bool SyncErr, SyncErrLatched;
} DtTsTrp;

// Checks a receive mode: DTAPI_RXMODE_ST188, ST204, STMP2, STRAW or STTRP, optionally
// with DTAPI_RXMODE_TIMESTAMP32 or TIMESTAMP_TOD. DTAPI_E_INVALID_MODE for another and
// for DTAPI_RXMODE_TIMESTAMP64.
DtapiResult DtTsTrp_CheckMode(int RxMode);

// Starts converting packets in RxMode, which CheckMode accepted. The synchronisation
// error is kept; ClearFlags clears it.
void DtTsTrp_Start(DtTsTrp* Trp, int RxMode);

// What packet P converts to, into Out, which has room for DT_TRP_MAX_OUTPUT bytes: the
// number of bytes, 0 for a packet that is dropped, or -1 for bytes that are no packet in
// sync, with a sync nibble or a valid count that is wrong, after which the stream must
// be found again. With Out NULL only the number is given, and the flags set. The number
// depends on P and the mode alone.
int DtTsTrp_Convert(DtTsTrp* Trp, const uint8_t* P, uint8_t* Out);

// Searches Size bytes of Buf for DT_TRP_NUM_SYNC packets in a row with the sync nibble, a
// valid count the mode accepts and consecutive sequence numbers. On success *Offset is
// where a whole packet starts: the one found, or the next when the one found started
// before Buf. Returns false when none is found; Buf is then all but its last
// DT_TRP_NUM_SYNC packets' worth of bytes without one.
bool DtTsTrp_FindSync(const DtTsTrp* Trp, const uint8_t* Buf, size_t Size,
                      size_t* Offset);

// DTAPI_RX_SYNC_ERR in *Flags and *Latched when set; ClearFlags clears it when Flags has
// it.
void DtTsTrp_GetFlags(const DtTsTrp* Trp, int* Flags, int* Latched);
void DtTsTrp_ClearFlags(DtTsTrp* Trp, int Flags);
