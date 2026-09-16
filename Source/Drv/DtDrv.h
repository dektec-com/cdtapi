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
#include <stddef.h>
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

    // When the firmware was built.
    int FwBuildYear;
    int FwBuildMonth;
    int FwBuildDay;
    int FwBuildHour;
    int FwBuildMinute;

    // Where the device is and how its PCIe link runs.
    int BusNumber;
    int SlotNumber;
    int PcieNumLanes;
    int PcieMaxLanes;
    int PcieLinkSpeed;          // PCIe generation of the link
    int PcieMaxSpeed;           // PCIe generation the link can reach
    int PcieMaxPayloadSize;     // Bytes
    int PcieMaxReadRequestSize; // Bytes
    int PcieMaxSlotPower;       // Milliwatts; 0 from a driver without GET_DEV_INFO2
} DtDeviceInfo;

// Reads the version of the driver behind Drv.
unsigned int DtDrvGetDriverVersion(OsDrv* Drv, DtDriverVersion* Version);

// True when a DtPcie driver of this version is new enough: 1.3.1 or later, the minimum
// DTAPI accepts (Utility.h, DtPcieMin*). The build number does not count.
bool DtDrvVersionIsSupported(const DtDriverVersion* Version);

// True when Version is Major.Minor.Micro.Build or later, comparing the four numbers in
// that order, as DTAPI's DtVersion does.
bool DtDrvVersionAtLeast(const DtDriverVersion* Version, int Major, int Minor, int Micro,
                         int Build);

// Reads the identity of the device behind Drv. Uses GET_DEV_INFO2, and falls back to the
// original GET_DEV_INFO for a driver that predates it, whose PCIe part has no slot power.
unsigned int DtDrvGetDeviceInfo(OsDrv* Drv, DtDeviceInfo* Info);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Properties -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A property is a named value the driver holds for the device, such as PORT_COUNT, or
// for one of its ports, such as the capability 3GSDI. It is always read for the device
// behind Drv, with its own hardware revision and firmware, as DTAPI's Device class does.
//
// The functions clear their output first, and fail with DTAPI_E_BUF_TOO_SMALL for a name
// longer than the driver accepts and with DTAPI_E_NOT_FOUND for a property the device
// does not have.
//

// The port index of a property that belongs to the device rather than to a port.
#define DT_PROPERTY_DEVICE -1

// Reads an integer property. PortIndex counts from zero, or is DT_PROPERTY_DEVICE.
unsigned int DtDrvGetPropertyInt(OsDrv* Drv, const char* Name, int PortIndex, int* Value);

// Reads a boolean property. PortIndex counts from zero, or is DT_PROPERTY_DEVICE.
unsigned int DtDrvGetPropertyBool(OsDrv* Drv, const char* Name, int PortIndex,
                                  bool* Value);

// The size of a buffer that holds every string property, terminator included: the
// driver answers with at most 96 characters.
#define DT_PROPERTY_STR_SIZE 97

// Reads a string property into Str, which holds Size bytes. PortIndex counts from zero,
// or is DT_PROPERTY_DEVICE. Fails with DTAPI_E_BUF_TOO_SMALL, leaving Str empty, when
// the string does not fit.
unsigned int DtDrvGetPropertyStr(OsDrv* Drv, const char* Name, int PortIndex, char* Str,
                                 size_t Size);

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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SDI receiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A command for a driver function rather than for the device goes to the function's UUID,
// which the device layer reads from the function's properties, and to the index of the
// port the function belongs to.
//

// What an SDI receiver reports of its input, converted as DTAPI's SDIRX proxy does.
typedef struct DtSdiRxStatus
{
    bool CarrierDetect;
    bool SdiLock;       // Locked to the SDI stream
    bool LineLock;      // Locked to the lines
    bool Valid;         // The counters below describe the input
    int NumSymsHanc;    // Symbols per line in HANC, EAV and SAV included
    int NumSymsVidVanc; // Symbols per line in the active part
    int NumLinesF1;
    int NumLinesF2;
    bool IsLevelB;      // 3G level B
    uint32_t PayloadId; // The SMPTE 352 VPID, 0 for none
    double FrameRate;   // Frames per second, 0 when the driver reports no frame period
    int SdiRate;        // -1 unknown, 0 SD, 1 HD, 2 3G, 3 6G, 4 12G
} DtSdiRxStatus;

// Reads the status of the SDI receiver with this UUID in the port with this index.
// Clears *Status first.
unsigned int DtDrvSdiRxGetStatus(OsDrv* Drv, int Uuid, int PortIndex,
                                 DtSdiRxStatus* Status);

#endif // CDTAPILITE_DT_DRV_H
