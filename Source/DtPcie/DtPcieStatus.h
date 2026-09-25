// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtPcieStatus.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - DtPcie driver commands: from a driver status to a DTAPI result
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>

// CDTAPI includes
#include "cdtapi.h" // DtapiResult.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Status +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Every driver status maps to the result code that stands for the same failure, so that
// a caller sees one vocabulary of results wherever a failure comes from.
//

// Translates a driver's DtStatus. DT_STATUS_OK becomes DTAPI_OK; a status without a
// counterpart, and one that is not a DtStatus at all, becomes DTAPI_E_DEV_DRIVER.
DtapiResult DtPcieStatus_ToResult(uint32_t Status);

// Translates what OsDrv_Ioctl returned: its outcome, and the DtStatus that goes with
// OS_IOCTL_DRIVER_STATUS.
DtapiResult DtPcieStatus_OutcomeToResult(int Outcome, uint32_t Status);
