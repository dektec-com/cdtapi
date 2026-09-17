// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtIoConfig.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Translation between I/O configuration codes and driver names
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Code and name +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The API takes I/O configuration as integers, DTAPI_IOCONFIG_IODIR and so on. The driver
// takes names: DtIoctlIoConfig carries its group, value and sub-value as strings. These
// two functions translate between the two, with the same results DTAPI gives
// (DtConfigDefStore.cpp, GetCode and GetName), including its special case: -1 means "no
// value" and corresponds to the empty name.
//
// Both return a DTAPI result code.
//

// The number of I/O configuration codes. Valid codes run from 0 to this number minus one.
int DtIoConfigCount(void);

// Looks up the code for Name. Sets *Code to -1 first. An empty name succeeds with -1; an
// unknown name fails with DTAPI_E_INVALID_ARG and leaves -1. The match is exact and
// case-sensitive, as it is in the driver.
unsigned int DtIoConfigGetCode(const char* Name, int* Code);

// Writes the name for Code into Name, which holds Size bytes including the terminator.
// Code -1 writes the empty name. Fails with DTAPI_E_INVALID_ARG for a code out of range,
// and with DTAPI_E_BUF_TOO_SMALL when the name does not fit; Name is then empty.
unsigned int DtIoConfigGetName(int Code, char* Name, size_t Size);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Validation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Which combinations of group, value and sub-value make a configuration. DTAPI checks a
// configuration against this relation before it sends one to the driver, and so does
// CDtapiLite. Whether a port supports the configuration is for the driver to decide.
//

// What a code can be in a configuration; a code can be more than one. These are the
// kinds in Tables/DtIoConfigList.inc.
#define DT_IOCFG_GROUP 0x1    // A group, such as IODIR
#define DT_IOCFG_BOOLIO 0x2   // A boolean I/O capability, set like a group to TRUE/FALSE
#define DT_IOCFG_VALUE 0x4    // A value within a group, such as OUTPUT
#define DT_IOCFG_SUBVALUE 0x8 // A sub-value within a value, such as DBLBUF

// The parent slots of Tables/DtIoConfigList.inc: an unused slot, and every boolean I/O
// capability.
#define DT_IOCFG_NONE -1
#define DT_IOCFG_ANY_BOOLIO -2

// Returns DTAPI_OK when Value belongs to Group and SubValue to Value, SubValue -1 being
// required exactly when Value has no sub-values, and DTAPI_E_INVALID_ARG otherwise. The
// checks and their order are those of DTAPI's DtConfigDefs::IsValidConfig.
unsigned int DtIoConfigIsValid(int Group, int Value, int SubValue);
