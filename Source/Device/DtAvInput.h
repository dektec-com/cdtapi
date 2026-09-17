// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtAvInput.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Device layer: detecting the video standard on an input port
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// CDtapiLite includes
#include "CDtapiLite.h" // DtDetVidStd.
#include "DtDevice.h"   // The device object.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Input status +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// What DTAPI's DtAvInputStatus holds once attached to a port of a DtPcie card, for the
// high-level Matrix API route it takes there: the port, its capabilities, and the SDI
// receiver driver function the port's ASI/SDI receiver API function names. Attaching
// finds that function once; detecting reads it as often as the caller asks.
//

typedef struct DtAvInput
{
    DtDevice* Device;
    int PortIndex;
    uint32_t Caps; // DT_CAP_ flags of the port
    int SdiRxUuid; // The UUID of the port's SDI receiver
} DtAvInput;

// Attaches Input to a port of an attached Device, numbered from 1, with the checks of
// DtAvInputStatus::AttachToPort and AvInputStatusProxy::Init in their order:
// DTAPI_E_DEVICE for a detached device, DTAPI_E_OBSOLETE_FW or DTAPI_E_TAINTED_FW, then
// DTAPI_E_NO_SUCH_PORT beyond all ports, then DTAPI_E_NOT_SUPPORTED for a port that is no
// input, has no SDI receiver, HDMI or Matrix API capability, or lacks the Matrix API one.
//
// Then the port's ASI/SDI receiver API function with the empty role is found with
// DtFunc_Find, whose failures are returned, and in it the SDI receiver driver function
// with the empty role; DTAPI_E_NOT_FOUND when there is none, which DTAPI only finds out
// when it detects.
DtapiResult DtAvInput_Attach(DtAvInput* Input, DtDevice* Device, int Port);

// Sets every field of *Info to unknown: the standards to DTAPI_VIDSTD_UNKNOWN, the link
// standards and link number to -1, the VPIDs to 0 and the aspect ratio to DT_AR_UNKNOWN.
void DtAvInput_SetUnknown(DtDetVidStd* Info);

// Detects the video standard on an attached input, as DtAvInputStatus::DetectVidStd and
// DtPalSDIRX::DetectVidStd do. Sets every field of *Info to unknown first; returns
// DTAPI_OK with the standard, or with it unknown when the receiver reports no valid,
// locked signal or one that matches no standard. Fails with the result of reading the
// port's down-scaling configuration or the receiver's status, and with
// DTAPI_E_DRIVER_INCOMP for a driver older than 1.4.0.111.
DtapiResult DtAvInput_DetectVidStd(const DtAvInput* Input, DtDetVidStd* Info);
