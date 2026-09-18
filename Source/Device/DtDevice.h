// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtDevice.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Device layer: the object behind DtDevice, and its hardware functions
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "DtPcieCmd.h"              // Driver commands and DtDeviceInfo.
#include "OAL/OsAbstractionLayer.h" // Device handles.
#include "cdtapi.h"                 // DtDevice and DtHwFuncDesc.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Device +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// What DTAPI's DtDevice keeps of an attached device and CDTAPI needs: the driver
// handle and its version, the device's identity, its port counts and, per port, the
// capabilities CDTAPI looks at. Everything is read once, at attach, as DTAPI does;
// the capabilities do not depend on the I/O configuration.
//
// DTAPI also caches each port's channel type, which follows the I/O direction, and
// re-reads it after a configuration change; this does not.
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

// The capabilities video standard detection looks at.
#define DT_CAP_INTINPUT 0x100       // Internal input, such as a link of a quad-link input
#define DT_CAP_MATRIX2 0x200        // The high-level Matrix API can use the port
#define DT_CAP_SDIRX 0x400          // SDI receiver
#define DT_CAP_HDMI 0x800           // HDMI
#define DT_CAP_SCALE_12GTO3G 0x1000 // The port can scale 12G-SDI down to 3G-SDI

// The capability the device descriptor looks at besides the direction.
#define DT_CAP_IP 0x2000 // Transport-stream-over-IP port

// The capabilities an input channel looks at.
#define DT_CAP_ASI 0x4000           // ASI, which DTAPI's ASI/SDI receiver implies
#define DT_CAP_MATRIX 0x8000        // The frame-buffer Matrix API of older cards
#define DT_CAP_TS 0x10000           // Transport-stream receive modes
#define DT_CAP_HUFFMAN 0x20000      // Compressed SDI
#define DT_CAP_L3MODE 0x40000       // L.3 receive modes
#define DT_CAP_TRPMODE 0x80000      // Transparent-packet receive mode
#define DT_CAP_TIMESTAMP64 0x100000 // 64-bit time stamps
#define DT_CAP_SDI10BNBO 0x200000   // 10-bit SDI in network byte order
#define DT_CAP_DMATESTMODE 0x400000 // A DMA-rate test mode to switch off
#define DT_CAP_FAILSAFE 0x800000    // A fail-safe relay
#define DT_CAP_SPI 0x1000000        // SPI
#define DT_CAP_SPISDI 0x2000000     // SDI over SPI

// Any of the SDI rates.
#define DT_CAP_ANY_SDI                                                                   \
    (DT_CAP_12GSDI | DT_CAP_3GSDI | DT_CAP_6GSDI | DT_CAP_HDSDI | DT_CAP_SDI)

struct DtDeviceC
{
    OsDrv* Drv; // NULL while detached
    int Index;  // The index the driver numbers the device by
    DtDriverVersion DriverVersion;
    DtDeviceInfo Info;
    int NumPorts;       // All ports, PORT_COUNT
    int NumPublicPorts; // The ports an application sees, MAIN_PORT_COUNT
    uint32_t* PortCaps; // DT_CAP_ flags per port index, for NumPorts and NumPublicPorts
};

// Attaches Device, which must be detached, to the device the driver numbers Index, when
// MatchSerial is false or the device's serial number is Serial. Returns DTAPI_OK,
// DTAPI_E_NO_SUCH_DEVICE when there is no such device or it cannot be read,
// DTAPI_E_DRIVER_INCOMP for a driver that is too old, and DTAPI_E_OUT_OF_MEM.
DtapiResult DtDevice_AttachIndex(DtDevice* Device, int Index, bool MatchSerial,
                                 int64_t Serial);

// Releases what an attached Device holds and leaves it detached.
void DtDevice_Release(DtDevice* Device);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Hardware functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Writes DTAPI's description of a port, its DTAPI_HWF2STR_TYPE_AND_PORT2 format for a PCI
// device: "DTA-" and the type number, the sub-type as a letter, and " port " with the
// port number, as "DTA-2178 port 1" or "DTA-2172A port 3". For a DTA-2178 with sub-type
// 1, DTAPI writes the full name after the type number: "DTA-2178DTA-2178-ASI port 1".
// Returns DTAPI_E_BUF_TOO_SMALL, with an empty Buf, when Size cannot hold it.
DtapiResult DtDevice_Describe(int TypeNumber, int SubType, int Port, char* Buf,
                              size_t Size);

// Fills Desc for a port of an attached Device, numbered from 1, as CDTAPI converts
// DTAPI's hardware function descriptor.
void DtDevice_HwFunc(const DtDevice* Device, int Port, DtHwFuncDesc* Desc);

// Fills Desc for an attached Device, as DTAPI's Device::GetDescriptor and
// PcieDevice::GetDescriptor do. Reads the I/O direction of each port that can be both an
// input and an output.
void DtDevice_DescribeDevice(const DtDevice* Device, DtDeviceDesc* Desc);
