// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtlIoConfig.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Translation between I/O configuration codes and driver names
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DTL_IO_CONFIG_H
#define CDTAPILITE_DTL_IO_CONFIG_H

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
int DtlIoConfigCount(void);

// Looks up the code for Name. Sets *Code to -1 first. An empty name succeeds with -1; an
// unknown name fails with DTAPI_E_INVALID_ARG and leaves -1. The match is exact and
// case-sensitive, as it is in the driver.
unsigned int DtlIoConfigGetCode(const char* Name, int* Code);

// Writes the name for Code into Name, which holds Size bytes including the terminator.
// Code -1 writes the empty name. Fails with DTAPI_E_INVALID_ARG for a code out of range,
// and with DTAPI_E_BUF_TOO_SMALL when the name does not fit; Name is then empty.
unsigned int DtlIoConfigGetName(int Code, char* Name, size_t Size);

#endif // CDTAPILITE_DTL_IO_CONFIG_H
