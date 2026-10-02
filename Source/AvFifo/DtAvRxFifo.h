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

// Describes the port Fifo is attached to, as DtapiHwFuncScan describes it: its name, its
// capabilities, and its addresses and MAC address. DTAPI_E_NOT_ATTACHED, with the failure
// text naming Where.
DtapiResult DtAvRxFifo_DescribePort(AvFifo_RxFifo* Fifo, DtHwFuncDesc* Desc,
                                    const char* Where);
