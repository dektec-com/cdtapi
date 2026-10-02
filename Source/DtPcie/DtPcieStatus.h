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
// The driver reports a failure as a DtStatus. CDTAPI translates each one into the
// DTAPI_E_* result for the same failure, so that a program sees the same results whether
// a failure comes from the driver or from CDTAPI itself.
//

// Translates the driver's DtStatus Status into a DTAPI result.
//
// | Status                          | Result             |
// |---------------------------------|--------------------|
// | DT_STATUS_OK                    | DTAPI_OK           |
// | A status with a DTAPI result    | That result        |
// | Any other value                 | DTAPI_E_DEV_DRIVER |
DtapiResult DtPcieStatus_ToResult(uint32_t Status);

// Translates the result of OsDrv_Ioctl into a DTAPI result. Outcome is the OS_IOCTL_*
// value it returned; Status is the driver's DtStatus, used only when Outcome is
// OS_IOCTL_DRIVER_STATUS.
//
// | Outcome                | Result                         |
// |------------------------|--------------------------------|
// | OS_IOCTL_OK            | DTAPI_OK                       |
// | OS_IOCTL_DRIVER_STATUS | DtPcieStatus_ToResult(Status)  |
// | OS_IOCTL_NO_RESOURCES  | DTAPI_E_OUT_OF_RESOURCES       |
// | Any other value        | DTAPI_E_COMMUNICATION          |
DtapiResult DtPcieStatus_OutcomeToResult(int Outcome, uint32_t Status);
