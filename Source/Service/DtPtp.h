// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtPtp.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The PTP clock slave: what the library uses besides the API
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "cdtapi_service.h" // The proxy and the slave's status.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= PTP slave +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Reads the status of the PTP clock slave that Proxy is attached to into *Status, as
// DtDevice_GetPtpStatus does with a proxy of its own. The parameters are found by name.
// *Status is all zero after a failure.
// Returns:
//   DTAPI_OK
//   DTAPI_E_SERVICE_INCOMP  the service lacks a parameter it needs, or has it of another
//                           type
//   and the results of the proxy's functions
DtapiResult DtPtp_ReadStatus(DtServiceProxy* Proxy, DtPtpStatus* Status);
