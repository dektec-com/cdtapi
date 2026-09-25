// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# SimClocks.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The emulated card's genlock, time-of-day clock control and transmit clocks
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Clocks +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The genlock controller, the time-of-day clock control and the two transmit-clock
// counters of a DTA-2178. After a reset they are as a DTA-2178 without a genlock
// reference reported them: the genlock running free with a 625i50 reference configured
// and none detected, the time-of-day clock running free on its internal reference, and
// two clocks, fractional at 148.5/1.001 MHz and non-fractional at 148.5 MHz, each with
// an offset of 0 in a range of 200 ppm either way. The counters count at their clock's
// frequency with its offset, from the emulator's time of day.
//
// The states are the driver's values, which tests set to see them converted.
//

// Forgets the states and offsets a test set.
void SimClocks_Reset(void);

// Sets the genlock state, a DT_GENLOCKCTRL_STATE_ value, and the reference's configured
// and detected video standards, DT_VIDSTD_ values. The top of frame is valid in any state
// but no reference and an invalid one.
void SimClocks_SetGenlock(int State, int RefVidStd, int DetVidStd);

// Sets the time-of-day clock control's state, a DT_TODCLOCKCTRL_STATE_ value, its
// reference, a DT_TODCLOCKCTRL_REF_ value, and the deviation it reports in ppm.
void SimClocks_SetTod(int State, int Reference, int DeviationPpm);

// Sets the type the genlock controller reports for clock ClockIndex, 0 or 1, including
// one the driver does not define.
void SimClocks_SetClockType(int ClockIndex, int Type);

// True for the function codes these objects answer.
bool SimClocks_Handles(int FunctionCode);

// Handles one command for an object of type Type, a DT_FUNC_TYPE_ for a driver function
// and a DT_BLOCK_TYPE_ otherwise, with role Role, and gives its DT_STATUS_ outcome. An
// object that does not take the function code refuses with DT_STATUS_NOT_SUPPORTED.
uint32_t SimClocks_Cmd(int FunctionCode, bool IsDriverFunction, int Type,
                       const char* Role, int Cmd, const void* In, size_t InSize,
                       void* Out, size_t* OutSize);
