// #*#*#*#*#*#*#*#*#*#*#*#*#* DtPcieCmdIssue.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - DtPcie driver commands: issuing a command, shared by the layer's files
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DT_PCIE_CMD_ISSUE_H
#define CDTAPILITE_DT_PCIE_CMD_ISSUE_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdint.h>

// CDtapiLite includes
#include "DtPcieAbi.h"              // DtIoctlInputDataHdr.
#include "OAL/OsAbstractionLayer.h" // Device handles.

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
// with this UUID in the port with this index.
void DtPcieCmdInitHeader(DtIoctlInputDataHdr* Hdr, int Cmd, int Uuid, int PortIndex);

// Issues a command whose answer has a fixed size, and turns the outcome into a result:
// a refused command into the result its DtStatus stands for, and a failure to reach the
// driver into DTAPI_E_COMMUNICATION or DTAPI_E_OUT_OF_RESOURCES. A command without an
// answer passes Out NULL and OutSize 0.
//
// A driver that answers with fewer bytes than the structure holds is treated as a
// failure: the fields it did not write would otherwise be read as zeroes and trusted.
//
// That check only has teeth on Windows and against the emulator. The Linux driver does
// not report how much it wrote, so there OsDrvIoCtl leaves the size as it was and a
// short answer cannot be detected here.
unsigned int DtPcieCmdIssue(OsDrv* Drv, uint32_t Code, const void* In, size_t InSize,
                            void* Out, size_t OutSize);

#endif // CDTAPILITE_DT_PCIE_CMD_ISSUE_H
