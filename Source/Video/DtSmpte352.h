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
// A VPID is the SMPTE ST 352 payload identifier that an SDI signal carries. These
// functions make a VPID and read its fields. A VPID is passed as the SDI receiver
// reports it: four bytes, with byte 1 in the least significant bits.
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

// Marks the VPID of link B of 3G level B: bit 6 of byte 4 (SMPTE ST 372).
#define DT_S352_LEVELB_LINK_B 0x40000000u

// Returns the VPID that a transmitter puts on video standard VidStd, with byte 1 in the
// least significant bits. The four bytes are:
// 1. The payload identifier. It is that of SMPTE ST 259 in SD, ST 292 in HD, and ST 425-1
//    level A or level B in 3G. For 2160p on one 6G or 12G link, it is that of
//    ST 2081-10 or ST 2082-10.
// 2. The picture rate. Bit 7 is set for a progressive transport, and bit 6 for a
//    progressive picture.
// 3. Zero.
// 4. The bit depth, which is 10 bits. On 3G level B this is the VPID of link A; that of
//    link B also has DT_S352_LEVELB_LINK_B.
// Returns 0 for an unknown standard and for 2160p on 3G level B links.
uint32_t DtSmpte352_Make(int VidStd);

// Returns the payload identifier, which is byte 1 of the VPID.
int DtSmpte352_PayloadId(uint32_t Vpid);

// Returns the picture rate as the reduced fraction *Num / *Den. Returns 0/0 for a rate
// code the library does not recognise.
void DtSmpte352_PictureRate(uint32_t Vpid, int* Num, int* Den);

// Returns whether the transport is interlaced.
bool DtSmpte352_IsInterlacedTransport(uint32_t Vpid);

// Returns whether the picture is interlaced. A progressive picture in an interlaced
// transport is PsF.
bool DtSmpte352_IsInterlacedStructure(uint32_t Vpid);

// Returns whether the picture aspect ratio is 16:9; false means 4:3.
bool DtSmpte352_Is16x9(uint32_t Vpid);

// Returns the number of the link that carries this VPID, counting from 0, for a payload
// that has more than one link. Returns 0 for any other payload.
int DtSmpte352_LinkNumber(uint32_t Vpid);
