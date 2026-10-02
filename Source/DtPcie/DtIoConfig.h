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
// The API gives an I/O configuration as integer codes, such as DTAPI_IOCONFIG_IODIR. The
// driver wants names: DtIoctlIoConfig holds the group, value and sub-value as strings.
// These functions translate between the two. Code -1 means "no value"; its name is the
// empty string.
//

// Returns the number of I/O configuration codes. The codes run from 0 to this number
// minus one.
int DtIoConfig_Count(void);

// Looks up the code of the driver name Name and stores it in *Code. The match is exact
// and case-sensitive, as in the driver.
//
// | Result              | When                                   |
// |---------------------|----------------------------------------|
// | DTAPI_OK            | Name is known, or empty (*Code is -1)  |
// | DTAPI_E_INVALID_ARG | Name is unknown or NULL; *Code is -1   |
DtapiResult DtIoConfig_GetCode(const char* Name, int* Code);

// Writes the driver name of Code into Name, a buffer of Size bytes including the
// terminator. Code -1 gives the empty name.
//
// | Result                | When                                     |
// |-----------------------|------------------------------------------|
// | DTAPI_OK              | The name was written                     |
// | DTAPI_E_INVALID_ARG   | Code is not -1 and not a valid code, or  |
// |                       | Name is NULL, or Size is 0               |
// | DTAPI_E_BUF_TOO_SMALL | The name does not fit; Name is empty     |
DtapiResult DtIoConfig_GetName(int Code, char* Name, size_t Size);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Validation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Checks that a group, value and sub-value fit together before CDTAPI sends them to the
// driver. Whether a port supports the configuration is for the driver to decide.
// Tables/DtIoConfigList.inc says which codes fit under which.
//

// Kinds of code, as flags: a code can be more than one kind. These are the kinds used in
// Tables/DtIoConfigList.inc.
#define DT_IOCONFIG_GROUP 0x1    // A group, such as IODIR
#define DT_IOCONFIG_BOOLIO 0x2   // A boolean I/O capability, or its values TRUE and FALSE
#define DT_IOCONFIG_VALUE 0x4    // A value within a group, such as OUTPUT
#define DT_IOCONFIG_SUBVALUE 0x8 // A sub-value within a value, such as DBLBUF

// Special parents in Tables/DtIoConfigList.inc. DT_IOCONFIG_NONE marks an unused parent
// slot; DT_IOCONFIG_ANY_BOOLIO means "under every boolean I/O capability".
#define DT_IOCONFIG_NONE -1
#define DT_IOCONFIG_ANY_BOOLIO -2

// Checks that Value belongs to Group and SubValue to Value. SubValue must be -1 if, and
// only if, Value has no sub-values. Returns DTAPI_OK when the configuration is valid,
// and DTAPI_E_INVALID_ARG otherwise.
DtapiResult DtIoConfig_CheckConfig(int Group, int Value, int SubValue);

// Checks that Group is a code whose configuration can be read: a group or a boolean I/O
// capability. Returns DTAPI_OK if it is, and DTAPI_E_INVALID_ARG otherwise.
DtapiResult DtIoConfig_CheckGroup(int Group);

// Tells whether a port that has capability Code has Group. The driver reports such a
// capability as property CAP_<name of Code>. For a group, Code must be one of its
// values; for a boolean I/O capability, Code must be Group itself. Returns false for an
// invalid Code or Group.
bool DtIoConfig_IsCapOfGroup(int Code, int Group);
