// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAvRxFifo.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - What the library itself reads of a receive FIFO
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "cdtapi.h"        // The description of a port.
#include "cdtapi_avfifo.h" // The FIFO.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Description +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Describes the port the FIFO is attached to, the same way DtapiHwFuncScan does: its
// name, its capabilities, its IP addresses and its MAC address. Where names the calling
// function for the failure text. Returns:
//
//   DTAPI_OK                The description is filled in
//   DTAPI_E_INVALID_ARG     Fifo or Desc is NULL
//   DTAPI_E_NOT_ATTACHED    The FIFO is not attached to a port
//
DtapiResult DtAvRxFifo_DescribePort(AvFifo_RxFifo* Fifo, DtHwFuncDesc* Desc,
                                    const char* Where);
