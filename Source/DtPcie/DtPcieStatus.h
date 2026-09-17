// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtPcieStatus.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - DtPcie driver commands: from a driver status to a DTAPI result
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Status +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The results match DTAPI's own translation, so that a failing command reports the same
// code through CDtapiLite as through CDTAPI.
//

// Translates a driver's DtStatus. DT_STATUS_OK becomes DTAPI_OK; a status without a
// counterpart, and one that is not a DtStatus at all, becomes DTAPI_E_DEV_DRIVER.
unsigned int DtPcieStatusToResult(uint32_t Status);

// Translates what OsDrvIoCtl returned: its outcome, and the DtStatus that goes with
// OS_IOCTL_DRIVER_STATUS.
unsigned int DtPcieOutcomeToResult(int Outcome, uint32_t Status);
