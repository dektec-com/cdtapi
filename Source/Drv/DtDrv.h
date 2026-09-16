// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtDrv.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Driver ABI layer: typed commands on top of the OS abstraction
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DT_DRV_H
#define CDTAPILITE_DT_DRV_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
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
#define DT_SUCCEEDED(Result) ((Result) < DTAPI_E)

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// One function per driver command, each taking and returning plain C types. The driver
// structures stay inside this layer, so nothing above it depends on their layout.
//

typedef struct DtDriverVersion
{
    int Major;
    int Minor;
    int Micro;
    int Build;
} DtDriverVersion;

typedef struct DtDeviceInfo
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
} DtDeviceInfo;

// Reads the version of the driver behind Drv.
unsigned int DtDrvGetDriverVersion(OsDrv* Drv, DtDriverVersion* Version);

// True when a DtPcie driver of this version is new enough: 1.3.1 or later, the minimum
// DTAPI accepts (Utility.h, DtPcieMin*). The build number does not count.
bool DtDrvVersionIsSupported(const DtDriverVersion* Version);

// Reads the identity of the device behind Drv. Uses GET_DEV_INFO2, and falls back to the
// original GET_DEV_INFO for a driver that predates it.
unsigned int DtDrvGetDeviceInfo(OsDrv* Drv, DtDeviceInfo* Info);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Properties -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A property is a named value the driver holds for the device, such as PORT_COUNT, or
// for one of its ports, such as the capability 3GSDI. It is always read for the device
// behind Drv, with its own hardware revision and firmware, as DTAPI's Device class does.
//
// Both functions set *Value to zero or false first, and fail with DTAPI_E_BUF_TOO_SMALL
// for a name longer than the driver accepts and with DTAPI_E_NOT_FOUND for a property
// the device does not have.
//

// The port index of a property that belongs to the device rather than to a port.
#define DT_PROPERTY_DEVICE -1

// Reads an integer property. PortIndex counts from zero, or is DT_PROPERTY_DEVICE.
unsigned int DtDrvGetPropertyInt(OsDrv* Drv, const char* Name, int PortIndex, int* Value);

// Reads a boolean property. PortIndex counts from zero, or is DT_PROPERTY_DEVICE.
unsigned int DtDrvGetPropertyBool(OsDrv* Drv, const char* Name, int PortIndex,
                                  bool* Value);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- I/O configuration -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The fields are as DTAPI's DtIoConfig has them: a port number from 1, and the group,
// value and sub-value as DTAPI_IOCONFIG_ codes, -1 for none. This layer converts to what
// the driver takes: a port index from 0, the codes as names, and, for the I/O direction
// values that name another port in ParXtra[0], that port as an index as well.
//

typedef struct DtIoConfig
{
    int Port;
    int Group;
    int Value;
    int SubValue;
    int64_t ParXtra[2];
} DtIoConfig;

// Reads the configuration of Config->Group on Config->Port, and fills in the other
// fields.
unsigned int DtDrvGetIoConfig(OsDrv* Drv, DtIoConfig* Config);

// Applies one configuration. The driver validates it; this layer only converts it, and
// refuses a LOOPS2TS output whose ParXtra[1], the ISI, is outside 0 to 255 with
// DTAPI_E_INVALID_ISI, as DTAPI does before sending it.
unsigned int DtDrvSetIoConfig(OsDrv* Drv, const DtIoConfig* Config);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Time of day -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Reads the device's time-of-day clock.
unsigned int DtDrvGetTimeOfDay(OsDrv* Drv, uint32_t* Seconds, uint32_t* Nanoseconds);

#endif // CDTAPILITE_DT_DRV_H
