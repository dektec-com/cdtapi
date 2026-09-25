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
// The object a card carries that its firmware waits for before it does any work. It is
// ready once it has been given the data the card's own EEPROM holds behind its sections,
// unless those were never written, and each later check decides again.
//

// Forgets that the object was given anything, as a card does when it loses power, and
// the busy count a test set.
void SimActivate_Reset(void);

// True once the object has what it needs.
bool SimActivate_IsReady(void);

// How long the object says it is busy after it is given its data: that many status
// requests answer busy before one answers ready. None unless a test asks for it.
void SimActivate_SetBusyCount(int Count);

// True for the function code the object answers.
bool SimActivate_Handles(int FunctionCode);

// Handles one command, and gives its DT_STATUS_ outcome.
uint32_t SimActivate_Cmd(int Cmd, const void* In, size_t InSize, void* Out,
                         size_t* OutSize);
