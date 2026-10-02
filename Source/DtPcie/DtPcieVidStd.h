// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtPcieVidStd.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - DtPcie driver commands: from a driver video standard to a DTAPI one
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Video standard +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The driver has its own codes for video standards, DT_VIDSTD_*. They differ from the
// DTAPI_VIDSTD_* codes of the API: DT_VIDSTD_1080I50 is 0x014C, but DTAPI_VIDSTD_1080I50
// is 67. A standard the driver reports must be translated before CDTAPI returns it.
//

// Translates the driver's video standard DrvVidStd into a DTAPI_VIDSTD_* code.
// Returns DTAPI_VIDSTD_UNKNOWN for DT_VIDSTD_UNKNOWN, for DT_VIDSTD_TS, for a standard
// CDTAPI does not know, and for a value that is not a DT_VIDSTD_* code.
int DtPcieVidStd_FromDriver(int DrvVidStd);
