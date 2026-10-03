// #*#*#*#*#*#*#*#*#*#*#*#*#* DtServiceProxy.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The proxy to DtapiService's services: what the library uses besides the API
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// CDTAPI includes
#include "cdtapi_service.h" // The proxy.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Proxy +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// DtServiceProxy_Attach checks the device and the port and calls DtServiceProxy_AttachTo,
// which the tests call themselves, with a pipe of their own.
//

// Attaches a proxy to the service Service of the port at PortIndex, counted from 0, of
// the card with serial number Serial, through the pipe PipeName: NULL for the service,
// which is the emulated one when CDTAPI_SIM asks for the emulator. *Proxy is NULL after
// a failure.
DtapiResult DtServiceProxy_AttachTo(const char* PipeName, int64_t Serial, int PortIndex,
                                    DtServiceType Service, bool Exclusive,
                                    DtServiceProxy** Proxy);

// Returns the result of the service's exception Exception, as cdtapi_service.h lists
// them; DTAPI_OK for DT_SERVICE_EXC_NONE.
DtapiResult DtServiceProxy_ExceptionResult(DtServiceExc Exception);
