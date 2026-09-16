// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtDevice.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Device layer: the object behind DtDevice, and its hardware functions
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DT_DEVICE_H
#define CDTAPILITE_DT_DEVICE_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdint.h>

// CDtapiLite includes
#include "CDtapiLite.h"             // DtDevice and DtHwFuncDesc.
#include "DtDrv.h"                  // Driver commands and DtDeviceInfo.
#include "OAL/OsAbstractionLayer.h" // Device handles.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Device +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// What DTAPI's DtDevice keeps of an attached device and CDtapiLite needs: the driver
// handle, the device's identity, its port counts and, per public port, the capabilities
// the hardware functions report. Everything is read once, at attach, as DTAPI does; the
// capabilities do not depend on the I/O configuration.
//
// DTAPI also caches each port's channel type, which follows the I/O direction, and
// re-reads it after a configuration change. Nothing CDTAPI exposes uses it; it belongs
// to the channels of a later milestone.
//

// The capabilities of a port that a hardware function description reports.
#define DT_CAP_12GSDI 0x01
#define DT_CAP_3GSDI 0x02
#define DT_CAP_6GSDI 0x04
#define DT_CAP_HDSDI 0x08
#define DT_CAP_SDI 0x10
#define DT_CAP_AVFIFO 0x20
#define DT_CAP_INPUT 0x40
#define DT_CAP_OUTPUT 0x80

// Any of the SDI rates.
#define DT_CAP_ANY_SDI                                                                   \
    (DT_CAP_12GSDI | DT_CAP_3GSDI | DT_CAP_6GSDI | DT_CAP_HDSDI | DT_CAP_SDI)

struct DtDeviceC
{
    OsDrv* Drv; // NULL while detached
    DtDeviceInfo Info;
    int NumPorts;       // All ports, PORT_COUNT
    int NumPublicPorts; // The ports an application sees, MAIN_PORT_COUNT
    uint32_t* PortCaps; // DT_CAP_ flags per public port, NumPublicPorts long
};

// Attaches Device, which must be detached, to the device the driver numbers Index, when
// MatchSerial is false or the device's serial number is Serial. Returns DTAPI_OK,
// DTAPI_E_NO_SUCH_DEVICE when there is no such device or it cannot be read,
// DTAPI_E_DRIVER_INCOMP for a driver that is too old, and DTAPI_E_OUT_OF_MEM.
unsigned int DtDeviceAttachIndex(DtDevice* Device, int Index, bool MatchSerial,
                                 int64_t Serial);

// Releases what an attached Device holds and leaves it detached.
void DtDeviceRelease(DtDevice* Device);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Hardware functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Writes DTAPI's description of a port, its DTAPI_HWF2STR_TYPE_AND_PORT2 format for a PCI
// device: "DTA-" and the type number, the sub-type as a letter, and " port " with the
// port number, as "DTA-2178 port 1" or "DTA-2172A port 3". For a DTA-2178 with sub-type
// 1, DTAPI writes the full name after the type number: "DTA-2178DTA-2178-ASI port 1".
// Returns DTAPI_E_BUF_TOO_SMALL, with an empty Buf, when Size cannot hold it.
unsigned int DtDeviceDescribe(int TypeNumber, int SubType, int Port, char* Buf,
                              size_t Size);

// Fills Desc for a port of an attached Device, numbered from 1, as CDTAPI converts
// DTAPI's hardware function descriptor.
void DtDeviceHwFunc(const DtDevice* Device, int Port, DtHwFuncDesc* Desc);

#endif // CDTAPILITE_DT_DEVICE_H
