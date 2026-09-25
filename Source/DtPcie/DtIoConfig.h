// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtIoConfig.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - I/O configuration codes: their driver names, and which combinations are valid
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>

// CDTAPI includes
#include "cdtapi.h" // DtapiResult.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Code and name +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The API takes I/O configuration as integers, DTAPI_IOCONFIG_IODIR and so on. The driver
// takes names: DtIoctlIoConfig carries its group, value and sub-value as strings.
// DtIoConfig_GetCode and DtIoConfig_GetName translate between the two and return a DTAPI
// result code, with one special case: -1 means "no value" and corresponds to the empty
// name.
//

// The number of I/O configuration codes. Valid codes run from 0 to this number minus one.
int DtIoConfig_Count(void);

// Looks up the code for Name. Sets *Code to -1 first. An empty name succeeds with -1; an
// unknown name fails with DTAPI_E_INVALID_ARG and leaves -1. The match is exact and
// case-sensitive, as it is in the driver.
DtapiResult DtIoConfig_GetCode(const char* Name, int* Code);

// Writes the name for Code into Name, which holds Size bytes including the terminator.
// Code -1 writes the empty name. Fails with DTAPI_E_INVALID_ARG for a code out of range,
// and with DTAPI_E_BUF_TOO_SMALL when the name does not fit; Name is then empty.
DtapiResult DtIoConfig_GetName(int Code, char* Name, size_t Size);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Validation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Which combinations of group, value and sub-value make a configuration. A configuration
// is checked against this relation before it is sent to the driver. Whether a port
// supports the configuration is for the driver to decide.
//

// What a code can be in a configuration; a code can be more than one. These are the
// kinds in Tables/DtIoConfigList.inc.
#define DT_IOCONFIG_GROUP 0x1    // A group, such as IODIR
#define DT_IOCONFIG_BOOLIO 0x2   // A boolean I/O capability, or TRUE/FALSE, its values
#define DT_IOCONFIG_VALUE 0x4    // A value within a group, such as OUTPUT
#define DT_IOCONFIG_SUBVALUE 0x8 // A sub-value within a value, such as DBLBUF

// The parent slots of Tables/DtIoConfigList.inc: an unused slot, and every boolean I/O
// capability.
#define DT_IOCONFIG_NONE -1
#define DT_IOCONFIG_ANY_BOOLIO -2

// Returns DTAPI_OK when Value belongs to Group and SubValue to Value, SubValue -1 being
// required exactly when Value has no sub-values, and DTAPI_E_INVALID_ARG otherwise. The
// group is checked first, then the value, then the sub-value.
DtapiResult DtIoConfig_CheckConfig(int Group, int Value, int SubValue);

// Returns DTAPI_OK when Group is a group or a boolean I/O capability, which is what
// configurations can be read of, and DTAPI_E_INVALID_ARG otherwise.
DtapiResult DtIoConfig_CheckGroup(int Group);

// True when capability CAP_<name of Code> on a port means that the port has Group. A
// boolean I/O capability is its own capability; a group has those of its values.
bool DtIoConfig_IsCapOfGroup(int Code, int Group);
