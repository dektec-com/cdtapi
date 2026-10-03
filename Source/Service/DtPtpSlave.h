// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtPtpSlave.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The typed PTP clock slave: what the library uses besides the API
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// CDTAPI includes
#include "cdtapi_service.h" // The slave.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= PTP slave +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Attaches to the PTP clock slave of the port at PortIndex, counted from 0, of the card
// with serial number Serial, through the pipe PipeName, as DtServiceProxy_AttachTo
// attaches a proxy, and finds its parameters. DtPtpSlave_Attach checks the device and
// the port and calls this with a NULL PipeName; the tests call it with a pipe of their
// own. *Slave is NULL after a failure.
DtapiResult DtPtpSlave_AttachTo(const char* PipeName, int64_t Serial, int PortIndex,
                                bool Exclusive, DtPtpSlave** Slave);
