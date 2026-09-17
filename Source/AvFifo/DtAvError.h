// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtAvError.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - The text of a thread's last failure in the AV FIFO
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "CDtapiLite.h" // DtapiResult.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Failures +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Each thread has a text of at most DT_AV_ERROR_SIZE - 1 characters, which
// GetLastException returns: "<Where>: <What> (<result name>)".
//

#define DT_AV_ERROR_SIZE 256

// Records a failure on the calling thread and returns Result. Where names the function,
// such as "AvFifo_RxFifo_Start"; What says what failed.
DtapiResult DtAvError_Set(DtapiResult Result, const char* Where, const char* What);
