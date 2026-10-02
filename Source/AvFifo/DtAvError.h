// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtAvError.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The text of a thread's last failure in the AV FIFO
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "cdtapi.h" // DtapiResult.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Failures +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Each thread keeps the text of its last failure, which GetLastException returns, in
// the form "<Where>: <What> (<result name>)". It holds at most DT_AV_ERROR_SIZE - 1
// characters.
//

#define DT_AV_ERROR_SIZE 256

// Records a failure as the calling thread's last one, and returns Result, so that a
// function can write "return DtAvError_Set(...)". Where names the function, e.g.
// "AvFifo_RxFifo_Start"; What says what failed.
DtapiResult DtAvError_Set(DtapiResult Result, const char* Where, const char* What);
