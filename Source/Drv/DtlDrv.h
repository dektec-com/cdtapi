// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtlDrv.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Driver ABI layer: typed commands on top of the OS abstraction
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DTL_DRV_H
#define CDTAPILITE_DTL_DRV_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>

// CDtapiLite includes
#include "CDtapiLite.h"             // DTAPI result codes.
#include "OAL/OsAbstractionLayer.h" // Device handles and IOCTL transport.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Results +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// This layer returns the public DTAPI result codes from CDtapiLite.h, so that a result
// can travel up to the API unchanged.
//

// True for DTAPI_OK and for the DTAPI_OK_* successes that carry a warning.
#define DTL_SUCCEEDED(Result) ((Result) < DTAPI_E)

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// One function per driver command, each taking and returning plain C types. The driver
// structures stay inside this layer, so nothing above it depends on their layout.
//

typedef struct DtlDriverVersion
{
    int Major;
    int Minor;
    int Micro;
    int Build;
} DtlDriverVersion;

typedef struct DtlDeviceInfo
{
    int TypeNumber; // 2178 for a DTA-2178
    int SubType;    // 0 for none, 1 for A, and so on
    int64_t Serial;
    int HardwareRevision;
    int FirmwareVersion;
    int FirmwareVariant;
    int FirmwareStatus; // One of the DT_FWSTATUS_* values
    uint16_t VendorId;
    uint16_t DeviceId;
    uint16_t SubVendorId;
    uint16_t SubSystemId;
} DtlDeviceInfo;

// Reads the version of the driver behind Drv.
unsigned int DtlDrvGetDriverVersion(OsDrv* Drv, DtlDriverVersion* Version);

// Reads the identity of the device behind Drv. Uses GET_DEV_INFO2, and falls back to the
// original GET_DEV_INFO for a driver that predates it.
unsigned int DtlDrvGetDeviceInfo(OsDrv* Drv, DtlDeviceInfo* Info);

#endif // CDTAPILITE_DTL_DRV_H
