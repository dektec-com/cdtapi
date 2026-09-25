// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtDevice.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Device layer: the object behind DtDevice, and its hardware functions
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "DtPcieCmd.h"              // Driver commands and DtDeviceInfo.
#include "OAL/OsAbstractionLayer.h" // Device handles.
#include "cdtapi.h"                 // DtDevice and DtHwFuncDesc.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Device +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// What is kept of an attached device: the driver handle and its version, the device's
// identity, its port counts and, per port, the capabilities CDTAPI looks at. Everything
// is read once, at attach; the capabilities do not depend on the I/O configuration.
//
// A port's channel type, which follows the I/O direction and would have to be re-read
// after every configuration change, is not kept here.
//

// The capabilities of a port that a hardware function description reports.
#define DT_CAP_12GSDI UINT64_C(0x01)
#define DT_CAP_3GSDI UINT64_C(0x02)
#define DT_CAP_6GSDI UINT64_C(0x04)
#define DT_CAP_HDSDI UINT64_C(0x08)
#define DT_CAP_SDI UINT64_C(0x10)
#define DT_CAP_AVFIFO UINT64_C(0x20)
#define DT_CAP_INPUT UINT64_C(0x40)
#define DT_CAP_OUTPUT UINT64_C(0x80)

// The capabilities video standard detection looks at.
#define DT_CAP_INTINPUT                                                                  \
    UINT64_C(0x100) // Internal input, such as a link of a quad-link input
#define DT_CAP_MATRIX2 UINT64_C(0x200) // The high-level Matrix API can use the port
#define DT_CAP_SDIRX UINT64_C(0x400)   // SDI receiver
#define DT_CAP_HDMI UINT64_C(0x800)    // HDMI
#define DT_CAP_SCALE_12GTO3G UINT64_C(0x1000) // The port can scale 12G-SDI down to 3G-SDI

// The capability the device descriptor looks at besides the direction.
#define DT_CAP_IP UINT64_C(0x2000) // Transport-stream-over-IP port

// The capabilities the input and output channels look at. A hardware function description
// reports DT_CAP_ASI as well.
#define DT_CAP_ASI UINT64_C(0x4000)      // ASI, which the ASI/SDI receiver implies
#define DT_CAP_MATRIX UINT64_C(0x8000)   // The frame-buffer Matrix API of older cards
#define DT_CAP_TS UINT64_C(0x10000)      // Transport-stream receive modes
#define DT_CAP_HUFFMAN UINT64_C(0x20000) // Compressed SDI
#define DT_CAP_L3MODE UINT64_C(0x40000)  // L.3 receive modes
#define DT_CAP_TRPMODE UINT64_C(0x80000) // Transparent-packet receive mode
#define DT_CAP_TIMESTAMP64 UINT64_C(0x100000) // 64-bit time stamps
#define DT_CAP_SDI10BNBO UINT64_C(0x200000)   // 10-bit SDI in network byte order
#define DT_CAP_DMATESTMODE UINT64_C(0x400000) // A DMA-rate test mode to switch off
#define DT_CAP_FAILSAFE UINT64_C(0x800000)    // A fail-safe relay
#define DT_CAP_SPI UINT64_C(0x1000000)        // SPI
#define DT_CAP_SPISDI UINT64_C(0x2000000)     // SDI over SPI
#define DT_CAP_QUADLINK UINT64_C(0x4000000)   // Quad-link 4K with the next three ports

// The capabilities of an SMPTE ST 2110 port.
#define DT_CAP_PTP UINT64_C(0x8000000)     // PTP time from the network
#define DT_CAP_SFP10G UINT64_C(0x10000000) // An SFP+ cage for 10 Gbit/s
#define DT_CAP_SFP25G UINT64_C(0x20000000) // An SFP28 cage for 25 Gbit/s
#define DT_CAP_ST2110 UINT64_C(0x40000000) // SMPTE ST 2110 streams

// Any of the SDI rates.
#define DT_CAP_ANY_SDI                                                                   \
    (DT_CAP_12GSDI | DT_CAP_3GSDI | DT_CAP_6GSDI | DT_CAP_HDSDI | DT_CAP_SDI)

// An object of the device's own that a public function needs, looked for once, when the
// application attaches: Found is DTAPI_OK with Ref what its commands go to,
// DTAPI_E_NOT_SUPPORTED when the device does not have it or it was not looked for, and
// DTAPI_E_DRIVER_INCOMP when the driver is too old for it.
typedef struct DtDevObject
{
    DtapiResult Found;
    DtDrvObject Ref;
} DtDevObject;

struct DtDevice
{
    OsDrv* Drv; // NULL while detached
    int Index;  // The index the driver numbers the device by
    DtDriverVersion DriverVersion;
    DtDeviceInfo Info;
    int NumPorts;       // All ports, PORT_COUNT
    int NumPublicPorts; // The ports an application sees, MAIN_PORT_COUNT
    uint64_t* PortCaps; // DT_CAP_ flags per port index, for the larger count

    // The clocks, which DtDevClock_OnAttach looks for.
    DtDevObject Genlock;    // The genlock controller
    DtDevObject TodClkCtrl; // The time-of-day clock control
    DtDevObject ClkCnt[2];  // The transmit-clock counters, per DTAPI_TXCLK_ type
};

// Attaches Device, which must be detached, to the device the driver numbers Index, when
// MatchSerial is false or the device's serial number is Serial. Returns DTAPI_OK,
// DTAPI_E_NO_SUCH_DEVICE when there is no such device or it cannot be read,
// DTAPI_E_DRIVER_INCOMP for a driver that is too old, and DTAPI_E_OUT_OF_MEM. An attached
// device is activated too, as DtDevActivate_OnAttach describes, which can take tens of
// milliseconds; the result of that does not decide the attach.
DtapiResult DtDevice_AttachIndex(DtDevice* Device, int Index, bool MatchSerial,
                                 int64_t Serial);

// Releases what an attached Device holds and leaves it detached.
void DtDevice_Release(DtDevice* Device);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Port capabilities +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// What a port can do is what its capabilities say. Code that needs a port to be able to
// do something asks these functions, and the flags of DtHwFuncDesc are filled from
// them for the application's convenience; the library does not read those flags.
//

// Whether port Port, numbered from 1, of an attached Device has every capability in
// Caps, a set of DT_CAP_ flags. False for a port the device does not have.
bool DtDevice_PortHasAllCaps(const DtDevice* Device, int Port, uint64_t Caps);

// Whether the port has at least one capability in Caps. False for a port the device
// does not have, and for an empty Caps.
bool DtDevice_PortHasAnyCap(const DtDevice* Device, int Port, uint64_t Caps);

// Whether the port has the capabilities ASI needs in CDTAPI: DT_CAP_ASI, and
// DT_CAP_INPUT or DT_CAP_OUTPUT, so that it can receive or send it.
bool DtDevice_PortHasAsiCaps(const DtDevice* Device, int Port);

// Whether the port has the capabilities of I/O standard IoStd, a value of
// DTAPI_IOCONFIG_IOSTD: those of ASI for DTAPI_IOCONFIG_ASI, and for DTAPI_IOCONFIG_SDI,
// HDSDI, 3GSDI, 6GSDI and 12GSDI the capability of that rate with the others SDI needs.
// False for any other standard, which the input and output channels do not carry.
bool DtDevice_PortHasIoStdCaps(const DtDevice* Device, int Port, int IoStd);

// Whether the port has the capabilities SDI needs in CDTAPI: one of the SDI rates,
// DT_CAP_MATRIX2, and DT_CAP_INPUT or DT_CAP_OUTPUT, so that it can receive or send it.
bool DtDevice_PortHasSdiCaps(const DtDevice* Device, int Port);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Hardware functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Writes the description of a port in the type-and-port format of a PCI device: "DTA-"
// and the type number, the sub-type as a letter, and " port " with the port number, as
// "DTA-2178 port 1" or "DTA-2172A port 3". A DTA-2178 with sub-type 1 is the
// DTA-2178-ASI: "DTA-2178-ASI port 1". Returns DTAPI_E_BUF_TOO_SMALL, with an empty Buf,
// when Size cannot hold it.
DtapiResult DtDevice_Describe(int TypeNumber, int SubType, int Port, char* Buf,
                              size_t Size);

// Fills Desc for a port of an attached Device, numbered from 1.
void DtDevice_HwFunc(const DtDevice* Device, int Port, DtHwFuncDesc* Desc);

// Fills Desc for an attached Device. Reads the I/O direction of each public port that is
// not only an input, only an output or an IP port; the channel counts stop at the first
// port whose direction cannot be read.
void DtDevice_DescribeDevice(const DtDevice* Device, DtDeviceDesc* Desc);
