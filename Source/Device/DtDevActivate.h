// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtDevActivate.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Activating a device at attach
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "OAL/OsAbstractionLayer.h" // Device handles.
#include "cdtapi.h"                 // DtapiResult.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Activation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Some cards must be activated before their firmware carries any data: until then such a
// card neither receives nor sends. The card activates itself with data from its own
// EEPROM, when the library asks it to.
//

// Activates the device behind Drv, if it has an object to activate. Attaching calls this
// once. A failure is no reason to refuse the attach, as the device is there and answers
// for itself; the caller logs or ignores the result.
//
// Returns DTAPI_OK when the device has no such object, when it was ready already, or when
// it is activated now; otherwise the first failure, e.g.:
//   DTAPI_E_IN_USE   exclusive access could not be had after repeated tries
//   DTAPI_E_INVALID  the object is still not ready afterwards
DtapiResult DtDevActivate_OnAttach(OsDrv* Drv);
