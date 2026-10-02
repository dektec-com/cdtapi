// #*#*#*#*#*#*#*#*#*#*#*#*#*#* SimDta2178.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - What the emulated DTA-2178 is: its properties and default configuration
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Model +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The description of the emulated DTA-2178. It comes from DekTec's device description of
// the DTA-2178, firmware variant 1, with the changes SimDta2178.c lists. The commands
// that read and change it are in SimDtPcie.c.
//

// Looks up a property as the DtPcie driver does, by name and port index; -1 is the
// device. A capability (a name starting with CAP_) that the port does not have is found,
// with the value false. Returns false only when there is no such property; *Type is then
// left as it was.
bool SimDta2178_GetProperty(const char* Name, int PortIndex, int* Type, uint64_t* Value);

// Looks up a string property in the same way. Returns false when there is no such string
// property; *Str is then left as it was.
bool SimDta2178_GetString(const char* Name, int PortIndex, const char** Str);

// Finds the driver function or building block a UUID names. Returns false when the card
// has no such object. Otherwise, returns:
//
//   *PortIndex  the index of its port, -1 for an object of the device
//   *Type       a DT_FUNC_TYPE_ value for a UUID with DT_UUID_DF_FLAG, and a
//               DT_BLOCK_TYPE_ value for one with DT_UUID_BC_FLAG
//   *Role       its role
//
// The index part of the UUIDs of the card's objects runs from 1 to
// SimDta2178_ObjectCount(), without gaps.
bool SimDta2178_FindFunction(int Uuid, int* PortIndex, int* Type, const char** Role);

// Returns the number of objects of all API functions of the device and its ports.
int SimDta2178_ObjectCount(void);

// Returns in *Value and *SubValue the power-on configuration of Group on the port at
// PortIndex, as I/O configuration codes; -1 is none.
void SimDta2178_DefaultConfig(int PortIndex, int Group, int* Value, int* SubValue);
