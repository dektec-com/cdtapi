// #*#*#*#*#*#*#*#*#*#*#*#*#*#* SimDta2178.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - What the emulated DTA-2178 is: its properties and default configuration
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Model +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The card description is taken from DekTec's device description of the DTA-2178,
// firmware variant 1. Only the description lives here; the commands that read and change
// it are in SimDtPcie.c.
//

// Looks up a property as the DtPcie driver does (DtPropertiesGet): by name and port
// index, -1 for the device. A capability, a name starting with CAP_, that the port does
// not have is found with the value false rather than not found. Returns false only when
// there is no such property; *Type is then untouched.
bool SimDta2178_GetProperty(const char* Name, int PortIndex, int* Type, uint64_t* Value);

// Looks up a string property the same way. Returns false when there is no such string
// property; *Str is then untouched.
bool SimDta2178_GetString(const char* Name, int PortIndex, const char** Str);

// Finds the driver function or building block a UUID names, with the index of its port,
// its type and its role. The type is a DT_FUNC_TYPE_ for a UUID with DT_UUID_DF_FLAG and
// a DT_BLOCK_TYPE_ for one with DT_UUID_BC_FLAG. Returns false for a UUID the card does
// not have. The index part of the UUIDs of the card's parts runs from 1 to
// SimDta2178_PartCount without gaps.
bool SimDta2178_FindFunction(int Uuid, int* PortIndex, int* Type, const char** Role);

// The number of parts of all API functions of all ports.
int SimDta2178_PartCount(void);

// The power-on configuration of Group on the port at PortIndex, as I/O configuration
// codes, -1 for none.
void SimDta2178_DefaultConfig(int PortIndex, int Group, int* Value, int* SubValue);
