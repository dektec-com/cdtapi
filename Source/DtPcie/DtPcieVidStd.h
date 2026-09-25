// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtPcieVidStd.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - DtPcie driver commands: from a driver video standard to a DTAPI one
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Video standard +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The driver names a video standard with a DT_VIDSTD_ code of its own, which is not the
// DTAPI_VIDSTD_ code of the same standard: DT_VIDSTD_1080I50 is 0x014C, where
// DTAPI_VIDSTD_1080I50 is 67.
//

// The DTAPI_VIDSTD_ code of a driver's DT_VIDSTD_ code, for every standard CDTAPI knows;
// DTAPI_VIDSTD_UNKNOWN for DT_VIDSTD_UNKNOWN, DT_VIDSTD_TS, the driver's standards that
// CDTAPI does not know, and a value that is not a DT_VIDSTD_ code.
int DtPcieVidStd_FromDriver(int DrvVidStd);
