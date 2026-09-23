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

// Activates the device behind Drv: until this is done its firmware carries no data at
// all, so the card neither receives nor sends, whatever kind of card it is. The data the
// device is activated with is its own, held in its EEPROM; a device that has no object to
// activate needs none, and gives DTAPI_OK.
//
// Attaching calls this once. Its failure is not a reason to refuse the attach: the device
// is there and answers for itself, so the result is for the caller to log or ignore.
DtapiResult DtDevActivate_OnAttach(OsDrv* Drv);
