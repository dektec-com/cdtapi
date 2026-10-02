// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtAvInput.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Device layer: detecting the video standard on an input port
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// CDTAPI includes
#include "DtDevice.h" // The device object.
#include "cdtapi.h"   // DtDetVidStd.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Input status +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A DtAvInput detects the video standard on an input port of a DtPcie card, through the
// card's Matrix API. Attaching finds the port's SDI receiver once; detection then reads
// it as often as the caller asks. The receiver is the SDI receiver driver function inside
// the port's ASI/SDI receiver API function.
//

// An input port, attached for detection.
typedef struct DtAvInput
{
    DtDevice* Device;  // The card
    int PortIndex;     // The port, from 0
    uint64_t Caps;     // The port's DT_CAP_ flags
    DtDrvObject SdiRx; // The port's SDI receiver
} DtAvInput;

// Attaches Input to port Port (from 1) of Device, which must be attached, and finds the
// port's SDI receiver. That is the SDI receiver driver function, with the empty role, in
// the port's ASI/SDI receiver API function, with the empty role (see DtFunc_Find).
//
// Returns DTAPI_OK, or, in this order:
//   DTAPI_E_DEVICE         Device is not attached
//   DTAPI_E_OBSOLETE_FW    the card's firmware is obsolete
//   DTAPI_E_TAINTED_FW     the card's firmware is an unsupported version
//   DTAPI_E_NO_SUCH_PORT   the card has no such port
//   DTAPI_E_NOT_SUPPORTED  the port is not an input or internal input, has none of the
//                          SDI receiver, HDMI or Matrix API capabilities, or lacks the
//                          Matrix API one
//   DTAPI_E_NOT_FOUND      the API function has no SDI receiver
// and the errors of DtFunc_Find().
DtapiResult DtAvInput_Attach(DtAvInput* Input, DtDevice* Device, int Port);

// Sets every field of *Info to unknown: the standards to DTAPI_VIDSTD_UNKNOWN, the link
// standards and link number to -1, the VPIDs to 0 and the aspect ratio to DT_AR_UNKNOWN.
void DtDetVidStd_SetUnknown(DtDetVidStd* Info);

// Detects the video standard on an attached input, and fills *Info with it. *Info is set
// to unknown first, and stays so when the receiver has no valid, locked signal, or one
// that matches no standard.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_DRIVER_INCOMP  the driver is older than the SDI receiver needs (see
//                          DtFunc_CheckDriverVersion)
// and the errors of reading the port's down-scaling setting and the receiver's status.
DtapiResult DtAvInput_DetectVidStd(const DtAvInput* Input, DtDetVidStd* Info);
