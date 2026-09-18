// #*#*#*#*#*#*#*#*#*#*#*#*#* DtPcieCmdIssue.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - DtPcie driver commands: issuing a command, shared by the layer's files
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "DtPcieAbi.h"              // DtIoctlInputDataHdr.
#include "DtPcieCmd.h"              // DtPartRef.
#include "OAL/OsAbstractionLayer.h" // Device handles.
#include "cdtapi.h"                 // DtapiResult.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Not for use outside the DtPcie command layer, whose other headers keep the driver's
// structures out of the layers above.
//

// The IOCTL codes come from CTL_CODE on Windows, which the SDK evaluates as int. The
// device type DekTec uses puts the value above INT_MAX, so it is converted to the
// unsigned 32 bits it is once, here, rather than at every call.
#define DT_IOCTL(Code) ((uint32_t)(Code))

// Fills the header every command starts with, for the driver function or building block
// Part.
void DtPcieCmd_InitHeader(DtIoctlInputDataHdr* Hdr, int Cmd, DtPartRef Part);

// Issues a command whose answer has a fixed size, and turns the outcome into a result:
// a refused command into the result its DtStatus stands for, and a failure to reach the
// driver into DTAPI_E_COMMUNICATION or DTAPI_E_OUT_OF_RESOURCES. A command without an
// answer passes Out NULL and OutSize 0.
//
// A driver that answers with fewer bytes than the structure holds is treated as a
// failure: the fields it did not write would otherwise be read as zeroes and trusted.
//
// That check only has teeth on Windows and against the emulator. The Linux driver does
// not report how much it wrote, so there OsDrv_IoCtl leaves the size as it was and a
// short answer cannot be detected here.
DtapiResult DtPcieCmd_Issue(OsDrv* Drv, uint32_t Code, const void* In, size_t InSize,
                            void* Out, size_t OutSize);

// Issues a command that is only its header, for Part, answered with Out of OutSize bytes,
// which are cleared first, or with nothing when Out is NULL. Gives DTAPI_E_INVALID_ARG
// for a Drv of NULL.
DtapiResult DtPcieCmd_IssuePlain(OsDrv* Drv, uint32_t Code, int Cmd, DtPartRef Part,
                                 void* Out, size_t OutSize);
