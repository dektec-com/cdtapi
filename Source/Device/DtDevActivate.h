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
// all, so a card that has the object neither receives nor sends. The data the device is
// activated with is its own, held in its EEPROM; a device that has no object to activate
// needs none, and gives DTAPI_OK. An object that is already ready is left alone, and
// gives DTAPI_OK too. Otherwise the result is the first failure, such as DTAPI_E_IN_USE
// when exclusive access cannot be had after repeated tries, or DTAPI_E_INVALID when the
// object is not ready afterwards.
//
// Attaching calls this once. Its failure is not a reason to refuse the attach: the device
// is there and answers for itself, so the result is for the caller to log or ignore.
DtapiResult DtDevActivate_OnAttach(OsDrv* Drv);
