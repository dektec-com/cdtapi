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
// counters of a DTA-2178.
//
// After a reset they report what a DTA-2178 without a genlock reference reported:
//
//   genlock     running free, a 625i50 reference configured, none detected
//   time        the time-of-day clock running free on its internal reference
//   clocks      a fractional one at 148.5/1.001 MHz and a non-fractional one at 148.5
//               MHz, each with an offset of 0 and a range of 200 ppm either way
//
// Each counter counts at its clock's frequency, with its offset, from the emulator's
// time of day.
//
// The states are the driver's values. Tests set them to check how the library converts
// them.
//

// Restores the states and offsets of a reset.
void SimClocks_Reset(void);

// Sets what the genlock controller reports: its state, a DT_GENLOCKCTRL_STATE_ value, and
// the reference's configured and detected video standards, DT_VIDSTD_ values. The top of
// frame is valid in every state except no reference and an invalid reference.
void SimClocks_SetGenlock(int State, int RefVidStd, int DetVidStd);

// Sets what the time-of-day clock control reports: its state, a DT_TODCLOCKCTRL_STATE_
// value, its reference, a DT_TODCLOCKCTRL_REF_ value, and its deviation in ppm.
void SimClocks_SetTod(int State, int Reference, int DeviationPpm);

// Sets the type the genlock controller reports for clock ClockIndex, 0 or 1. The type may
// be one the driver does not define.
void SimClocks_SetClockType(int ClockIndex, int Type);

// Returns true for the function codes these objects answer.
bool SimClocks_Handles(int FunctionCode);

// Carries out one command for an object, and returns its DT_STATUS_ result. Type is a
// DT_FUNC_TYPE_ value for a driver function and a DT_BLOCK_TYPE_ value otherwise; Role is
// the object's role. An object that does not take the function code returns
// DT_STATUS_NOT_SUPPORTED.
uint32_t SimClocks_Cmd(int FunctionCode, bool IsDriverFunction, int Type,
                       const char* Role, int Cmd, const void* In, size_t InSize,
                       void* Out, size_t* OutSize);
