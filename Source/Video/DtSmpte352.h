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
// These functions read the fields of a VPID, the SMPTE ST 352 payload identifier that
// an SDI signal carries. The VPID is passed as the SDI receiver reports it: four bytes,
// with byte 1 in the least significant bits.
//

// The payload identifiers (byte 1) that the library recognises.
#define DT_S352_ID_S259 0x81          // 525 and 625 lines, SMPTE ST 259
#define DT_S352_ID_S292_720 0x84      // 720 lines, SMPTE ST 292
#define DT_S352_ID_S292_1080 0x85     // 1080 lines, SMPTE ST 292
#define DT_S352_ID_S425_1080_A 0x89   // 1080 lines on 3G level A, SMPTE ST 425-1
#define DT_S352_ID_S425_1080_B 0x8A   // 1080 lines on 3G level B, SMPTE ST 425-1
#define DT_S352_ID_S425_5_2160_A 0x97 // 2160 lines on four 3G level A links, ST 425-5
#define DT_S352_ID_S425_5_2160_B 0x98 // 2160 lines on four 3G level B links, ST 425-5
#define DT_S352_ID_S2081_2160 0xC0    // 2160 lines on 6G, SMPTE ST 2081-10
#define DT_S352_ID_S2082_2160 0xCE    // 2160 lines on 12G, SMPTE ST 2082-10

// Returns the payload identifier: byte 1 of the VPID.
int DtSmpte352_PayloadId(uint32_t Vpid);

// Returns the picture rate in *Num / *Den, as a reduced fraction. Returns 0/0 for a rate
// code the library does not recognise.
void DtSmpte352_PictureRate(uint32_t Vpid, int* Num, int* Den);

// Return whether the transport is interlaced, and whether the picture is. A progressive
// picture in an interlaced transport is PsF.
bool DtSmpte352_IsInterlacedTransport(uint32_t Vpid);
bool DtSmpte352_IsInterlacedStructure(uint32_t Vpid);

// Returns whether the picture aspect ratio is 16:9; false means 4:3.
bool DtSmpte352_Is16x9(uint32_t Vpid);

// Returns the number, from 0, of the link that carries this VPID, for a payload that has
// more than one link. Returns 0 for any other payload.
int DtSmpte352_LinkNumber(uint32_t Vpid);
