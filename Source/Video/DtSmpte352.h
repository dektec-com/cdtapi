// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSmpte352.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The fields of a SMPTE ST 352 payload identifier (VPID)
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Payload fields +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A VPID is the four bytes of the payload identifier as the SDI receiver reports them,
// the first byte in the least significant bits.
//

// The payload identifiers, byte 1, that are recognised.
#define DT_S352_ID_S259 0x81          // 525 and 625 lines, SMPTE ST 259
#define DT_S352_ID_S292_720 0x84      // 720 lines, SMPTE ST 292
#define DT_S352_ID_S292_1080 0x85     // 1080 lines, SMPTE ST 292
#define DT_S352_ID_S425_1080_A 0x89   // 1080 lines on 3G level A, SMPTE ST 425-1
#define DT_S352_ID_S425_1080_B 0x8A   // 1080 lines on 3G level B, SMPTE ST 425-1
#define DT_S352_ID_S425_5_2160_A 0x97 // 2160 lines on four 3G level A links, ST 425-5
#define DT_S352_ID_S425_5_2160_B 0x98 // 2160 lines on four 3G level B links, ST 425-5
#define DT_S352_ID_S2081_2160 0xC0    // 2160 lines on 6G, SMPTE ST 2081-10
#define DT_S352_ID_S2082_2160 0xCE    // 2160 lines on 12G, SMPTE ST 2082-10

// The payload identifier.
int DtSmpte352_PayloadId(uint32_t Vpid);

// The picture rate as a reduced fraction; 0/0 for a rate code that is not recognised.
void DtSmpte352_PictureRate(uint32_t Vpid, int* Num, int* Den);

// Whether the transport is interlaced, and whether the picture structure is. Progressive
// pictures in an interlaced transport are PsF.
bool DtSmpte352_IsInterlacedTransport(uint32_t Vpid);
bool DtSmpte352_IsInterlacedStructure(uint32_t Vpid);

// Whether the picture aspect ratio is 16:9; otherwise it is 4:3.
bool DtSmpte352_Is16x9(uint32_t Vpid);

// The zero-based number of the link that carries this VPID, for the payloads that have
// more than one link; 0 for all others.
int DtSmpte352_LinkNumber(uint32_t Vpid);
