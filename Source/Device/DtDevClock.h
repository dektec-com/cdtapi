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
// The clock functions of the public API (DtDevice_GetGenlockState, GetTimeOfDayState,
// GetTxClockCount, GetTxClockOffset, GetTxClockProperties and SetTxClockOffset) are in
// the same source file. They use the objects this function finds.
//

// Finds the clock objects of a Device that was just attached: the genlock controller,
// the time-of-day clock control and the two transmit-clock counters. Keeps what it finds
// in Device, as DtDevObject describes. A device without them is attached all the same;
// only the functions that need them fail.
//
// Returns DTAPI_OK, or DTAPI_E_OUT_OF_MEM when there is not enough memory to look. Any
// other failure to read an object counts as the object not being there.
DtapiResult DtDevClock_OnAttach(DtDevice* Device);
