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

// CDTAPI includes
#include "cdtapi.h"     // DtAspectRatio and results.
#include "cdtapi_sdi.h" // DtSdiVideoSignalling.

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
// least significant bits, with the defaults of the standard's signalling. It is
// DtSmpte352_MakeSignalling() with every field of the signalling at its default.
uint32_t DtSmpte352_Make(int VidStd);

// Returns the VPID that a transmitter puts on video standard VidStd, with byte 1 in the
// least significant bits, carrying Signalling. Signalling must hold no _DEFAULT field and
// only values the standard can carry, as DtSmpte352_ResolveSignalling() gives them. The
// four bytes are:
// 1. The payload identifier. It is that of SMPTE ST 259 in SD, ST 292 in HD, and ST 425-1
//    level A or level B in 3G. For 2160p on one 6G or 12G link, it is that of
//    ST 2081-10 or ST 2082-10.
// 2. The picture rate. Bit 7 is set for a progressive transport, and bit 6 for a
//    progressive picture. From 1080 lines up, bits 5 and 4 hold the transfer
//    characteristic.
// 3. The aspect ratio, and from 1080 lines up the colorimetry, on the bits of the
//    payload's standard.
// 4. The bit depth, which is 10 bits, and from 1080 lines up its range and bit 4 for
//    ICtCp. On 3G level B this is the VPID of link A; that of link B also has
//    DT_S352_LEVELB_LINK_B.
// Returns 0 for an unknown standard and for 2160p on 3G level B links.
uint32_t DtSmpte352_MakeSignalling(int VidStd, const DtSdiVideoSignalling* Signalling);

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

// Returns the aspect ratio the VPID gives, from the bit its payload's standard puts it
// in: 4:3 or 16:9 in SD; 16:9 in 720p, which has no such bit; 16:9 or unknown from 1080
// lines up. Returns DT_AR_UNKNOWN for a payload the library does not know.
DtAspectRatio DtSmpte352_AspectRatio(uint32_t Vpid);

// Returns the number of the link that carries this VPID, counting from 0, for a payload
// that has more than one link. Returns 0 for any other payload.
int DtSmpte352_LinkNumber(uint32_t Vpid);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Signalling +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The signalling of a payload ID, as DtSdiVideoSignalling holds it. Inside the library
// signalling is always whole: its Size is sizeof(DtSdiVideoSignalling). These functions
// take a program's signalling in and give one back out, and work out what goes into a
// frame.
//

// Returns whether Size ends at one of DtSdiVideoSignalling's fields. 0 does not.
bool DtSmpte352_IsSignallingSize(int Size);

// Takes a program's signalling into *Out, whole: the fields that In->Size covers, and
// _DEFAULT for the rest. NULL, or a Size of 0, gives every field _DEFAULT.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG    In->Size is not 0 and does not end at a field, or a field is
//                          not a value of its enum
//   DTAPI_E_NOT_SUPPORTED  the colorimetry is DT_SDI_COLORIMETRY_COLOR_VANC
// After a failure *Out is unchanged.
DtapiResult DtSmpte352_TakeSignalling(const DtSdiVideoSignalling* In,
                                      DtSdiVideoSignalling* Out);

// Copies the fields of the whole signalling Signalling that Out->Size covers into *Out.
// Out->Size must end at a field.
void DtSmpte352_GiveSignalling(const DtSdiVideoSignalling* Signalling,
                               DtSdiVideoSignalling* Out);

// Fills *Signalling with the defaults of video standard VidStd, as cdtapi_sdi.h lists
// them. An unknown standard gets those of 1080 lines.
void DtSmpte352_DefaultSignalling(int VidStd, DtSdiVideoSignalling* Signalling);

// Works out the signalling of a frame of video standard VidStd into *Out, field by field:
// the field of Frame where it is not _DEFAULT, then that of Builder where the standard
// can carry it, then the standard's default. Frame and Builder are whole.
//
// Returns DTAPI_OK, or DTAPI_E_NOT_SUPPORTED when a field of Frame is a value the
// standard cannot carry.
DtapiResult DtSmpte352_ResolveSignalling(int VidStd, const DtSdiVideoSignalling* Frame,
                                         const DtSdiVideoSignalling* Builder,
                                         DtSdiVideoSignalling* Out);

// Reads the signalling of a VPID into the whole signalling *Signalling. A field the
// payload does not carry gets the value its standard implies. Returns false for a payload
// the library does not know; *Signalling then holds unspecified and unknown fields.
bool DtSmpte352_ReadSignalling(uint32_t Vpid, DtSdiVideoSignalling* Signalling);
