// #*#*#*#*#*#*#*#*#*#*#*#*#*#* SimActivate.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The emulated DTA-2110's activation object
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Activation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The card's firmware does no work until its activation object is ready. The object
// becomes ready when a check gives it the data stored in the card's EEPROM behind the VPD
// sections. On a card where that data was never written, no check succeeds. Every check
// decides again: a wrong one makes the object not ready.
//

// Makes the object forget what it was given, as a card does when it loses power, and
// clears the busy count a test set.
void SimActivate_Reset(void);

// Returns true when the object is ready.
bool SimActivate_IsReady(void);

// Makes the object busy for a while after a successful check: the next Count status
// requests answer busy, and only then does one answer ready. After a reset, Count is 0.
void SimActivate_SetBusyCount(int Count);

// Returns true for the function code the object answers.
bool SimActivate_Handles(int FunctionCode);

// Carries out one command, and returns its DT_STATUS_ result.
uint32_t SimActivate_Cmd(int Cmd, const void* In, size_t InSize, void* Out,
                         size_t* OutSize);
