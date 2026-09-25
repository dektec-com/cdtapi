// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtDevClock.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Device layer: the device's genlock, time-of-day and transmit clocks
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtDevice.h" // The device and its objects.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Clocks +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The public functions DtDevice_GetGenlockState, GetTimeOfDayState, GetTxClockCount,
// GetTxClockOffset, GetTxClockProperties and SetTxClockOffset are implemented beside
// this, with the objects it finds.
//

// Looks for the genlock controller, the time-of-day clock control and the two
// transmit-clock counters of a Device just attached, and keeps in Device what it finds,
// as DtDevObject describes. A device without them is attached as well; only the functions
// that need them fail. Returns DTAPI_E_OUT_OF_MEM when looking cannot be done for want of
// memory, and DTAPI_OK otherwise: any other failure to read an object is its absence.
DtapiResult DtDevClock_OnAttach(DtDevice* Device);
