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
#include "DtPcieCmd.h"              // DtDrvObject.
#include "OAL/OsAbstractionLayer.h" // Device handles.
#include "cdtapi.h"                 // DtapiResult.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Sends commands to the driver. Only the files of the DtPcie command layer use these
// functions; the layers above call the functions of DtPcieCmd.h, which keep the driver's
// structures hidden.
//

// Converts an IOCTL code to uint32_t. On Windows, CTL_CODE gives an int, and DekTec's
// device type makes the value larger than INT_MAX. Converting once here saves a cast at
// every call.
#define DT_IOCTL(Code) ((uint32_t)(Code))

// Fills Hdr, the header that starts every command. Cmd is the command; Object is the
// driver function or building block it is for.
void DtPcieCmd_InitHeader(DtIoctlInputDataHdr* Hdr, int Cmd, DtDrvObject Object);

// Fills Hdr for a command to the device as a whole, not to one function. It sets the
// UUID to 0 and the port index to DT_PROPERTY_DEVICE, which the driver reads as "the
// device".
void DtPcieCmd_InitDeviceHeader(DtIoctlInputDataHdr* Hdr, int Cmd);

// Sends command Code with the input In of InSize bytes, and receives the answer into Out
// of OutSize bytes. For a command without an answer, pass Out NULL and OutSize 0.
//
// | Result                   | When                                          |
// |--------------------------|-----------------------------------------------|
// | DTAPI_OK                 | The driver carried out the command            |
// | DTAPI_E_DEV_DRIVER       | The driver answered with fewer than OutSize   |
// |                          | bytes                                         |
// | DTAPI_E_COMMUNICATION    | The command did not reach the driver          |
// | DTAPI_E_OUT_OF_RESOURCES | The OS had no resources to send the command   |
// | Other                    | The driver refused the command; the result    |
// |                          | for its DtStatus                              |
//
// A short answer counts as a failure, because the fields the driver did not write would
// read as zeroes and be trusted. Only Windows and the emulator report how much the
// driver wrote. The Linux driver does not, so on Linux a short answer goes unnoticed.
DtapiResult DtPcieCmd_Issue(OsDrv* Drv, uint32_t Code, const void* In, size_t InSize,
                            void* Out, size_t OutSize);

// Sends a command that consists of only its header: command Cmd for Object, with IOCTL
// code Code. Clears Out, of OutSize bytes, and receives the answer into it; pass Out
// NULL for a command without an answer. Returns DTAPI_E_INVALID_ARG when Drv is NULL,
// and otherwise the results of DtPcieCmd_Issue().
DtapiResult DtPcieCmd_IssueHeaderOnly(OsDrv* Drv, uint32_t Code, int Cmd,
                                      DtDrvObject Object, void* Out, size_t OutSize);
