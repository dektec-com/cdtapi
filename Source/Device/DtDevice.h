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
// A DtDevice holds what the library keeps of an attached card: the driver handle and
// version, the card's identity, its port counts, and the capabilities of each port. All
// of it is read once, when the card is attached. The capabilities do not depend on the
// I/O configuration.
//
// A port's channel type is not kept here: it follows the I/O direction, so it would have
// to be read again after every configuration change.
//

// The SDI rates, the AV FIFO and the direction.
#define DT_CAP_12GSDI UINT64_C(0x01)
#define DT_CAP_3GSDI UINT64_C(0x02)
#define DT_CAP_6GSDI UINT64_C(0x04)
#define DT_CAP_HDSDI UINT64_C(0x08)
#define DT_CAP_SDI UINT64_C(0x10)
#define DT_CAP_AVFIFO UINT64_C(0x20)
#define DT_CAP_INPUT UINT64_C(0x40)
#define DT_CAP_OUTPUT UINT64_C(0x80)

// The receiver: internal inputs, the Matrix API, SDI, HDMI and 12G-to-3G scaling.
#define DT_CAP_INTINPUT                                                                  \
    UINT64_C(0x100) // Internal input, e.g. one link of a quad-link input
#define DT_CAP_MATRIX2 UINT64_C(0x200) // The high-level Matrix API can use the port
#define DT_CAP_SDIRX UINT64_C(0x400)   // SDI receiver
#define DT_CAP_HDMI UINT64_C(0x800)    // HDMI
#define DT_CAP_SCALE_12GTO3G UINT64_C(0x1000) // The port can scale 12G-SDI down to 3G-SDI

// Transport stream over IP.
#define DT_CAP_IP UINT64_C(0x2000) // Transport-stream-over-IP port

// ASI, the transport-stream receive modes, and per-port hardware: relay, SPI, quad link.
#define DT_CAP_ASI UINT64_C(0x4000)      // ASI; an ASI/SDI receiver has it too
#define DT_CAP_MATRIX UINT64_C(0x8000)   // The frame-buffer Matrix API of older cards
#define DT_CAP_TS UINT64_C(0x10000)      // Transport-stream receive modes
#define DT_CAP_HUFFMAN UINT64_C(0x20000) // Compressed SDI
#define DT_CAP_L3MODE UINT64_C(0x40000)  // L.3 receive modes
#define DT_CAP_TRPMODE UINT64_C(0x80000) // Transparent-packet receive mode
#define DT_CAP_TIMESTAMP64 UINT64_C(0x100000) // 64-bit time stamps
#define DT_CAP_SDI10BNBO UINT64_C(0x200000)   // 10-bit SDI in network byte order
#define DT_CAP_DMATESTMODE UINT64_C(0x400000) // A DMA-rate test mode, to switch off
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

// An object of the card that a public function needs, such as the genlock controller.
// The library looks for it once, when the card is attached.
typedef struct DtDevObject
{
    // DTAPI_OK               found; Object is where its commands go
    // DTAPI_E_NOT_SUPPORTED  the card does not have it, or it was not looked for
    // DTAPI_E_DRIVER_INCOMP  the driver is too old for it
    DtapiResult LookupResult;
    DtDrvObject Object; // Where commands to the object go, when found
} DtDevObject;

struct DtDevice
{
    OsDrv* Drv;                    // The driver handle; NULL while detached
    int DriverIndex;               // The number the driver gives the card
    DtDriverVersion DriverVersion; // The driver's version
    DtDeviceInfo Info;             // The card's identity, as the driver reports it
    int NumPorts;                  // All ports, PORT_COUNT
    int NumPublicPorts;            // The ports an application sees, MAIN_PORT_COUNT
    uint64_t* PortCaps; // DT_CAP_ flags per port index, for the larger of the two counts

    // The clocks, which DtDevClock_OnAttach looks for.
    DtDevObject Genlock;    // The genlock controller
    DtDevObject TodClkCtrl; // The time-of-day clock control
    DtDevObject ClkCnt[2];  // The transmit-clock counters, per DTAPI_TXCLK_ type
};

// Attaches Device, which must be detached, to the card the driver numbers Index. When
// MatchSerial is true, the card must also have serial number Serial. Then activates the
// card (see DtDevActivate_OnAttach), which can take tens of milliseconds; whether that
// works does not change the result.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_NO_SUCH_DEVICE  there is no such card, its serial number differs, or it
//                           cannot be read
//   DTAPI_E_DRIVER_INCOMP   the driver is too old
//   DTAPI_E_OUT_OF_MEM      not enough memory
DtapiResult DtDevice_AttachToIndex(DtDevice* Device, int Index, bool MatchSerial,
                                   int64_t Serial);

// Frees what an attached Device holds, and leaves it detached.
void DtDevice_Release(DtDevice* Device);

// Checks that the firmware of an attached Device can be used.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_OBSOLETE_FW  the card's firmware is obsolete
//   DTAPI_E_TAINTED_FW   the card's firmware is an unsupported version
DtapiResult DtDevice_CheckFirmware(const DtDevice* Device);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Port capabilities +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A port can do what its capabilities say. Code that needs a port to do something asks
// these functions. The flags of DtHwFuncDesc are filled from them, for the application;
// the library itself does not read those flags.
//

// Returns whether port Port (from 1) of an attached Device has every capability in Caps,
// a set of DT_CAP_ flags. Returns false for a port the card does not have, or one with
// no capabilities at all.
bool DtDevice_PortHasAllCaps(const DtDevice* Device, int Port, uint64_t Caps);

// Returns whether the port has at least one capability in Caps. Returns false for a port
// the card does not have, and for an empty Caps.
bool DtDevice_PortHasAnyCap(const DtDevice* Device, int Port, uint64_t Caps);

// Returns whether the port can receive or send ASI: it has DT_CAP_ASI, and DT_CAP_INPUT
// or DT_CAP_OUTPUT.
bool DtDevice_PortHasAsiCaps(const DtDevice* Device, int Port);

// Returns whether the port can carry I/O standard IoStd, a DTAPI_IOCONFIG_IOSTD value:
// - DTAPI_IOCONFIG_ASI: as DtDevice_PortHasAsiCaps;
// - DTAPI_IOCONFIG_SDI, HDSDI, 3GSDI, 6GSDI and 12GSDI: that SDI rate, and the other
//   capabilities SDI needs (see DtDevice_PortHasSdiCaps).
// Returns false for any other standard, as the input and output channels carry no other.
bool DtDevice_PortHasIoStdCaps(const DtDevice* Device, int Port, int IoStd);

// Returns whether the port can receive or send SDI: it has one of the SDI rates,
// DT_CAP_MATRIX2, and DT_CAP_INPUT or DT_CAP_OUTPUT.
bool DtDevice_PortHasSdiCaps(const DtDevice* Device, int Port);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Hardware functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Writes the name of a port into Buf, e.g. "DTA-2178 port 1" or "DTA-2172A port 3": the
// type number, the sub-type as a letter, and the port number. A DTA-2178 with sub-type 1
// is written as "DTA-2178-ASI".
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_BUF    Buf is NULL or Size is 0
//   DTAPI_E_BUF_TOO_SMALL  the name does not fit in Size bytes; Buf is then empty
DtapiResult DtDevice_FormatPortName(int TypeNumber, int SubType, int Port, char* Buf,
                                    size_t Size);

// Fills *Desc with the description of port Port (from 1) of an attached Device.
void DtDevice_DescribeHwFunc(const DtDevice* Device, int Port, DtHwFuncDesc* Desc);

// Fills *Desc with the description of an attached Device. To count the input and output
// channels, it reads the I/O direction of each public port that can be both input and
// output and is not an IP port. Counting stops at the first port whose direction cannot
// be read.
void DtDevice_DescribeDevice(const DtDevice* Device, DtDeviceDesc* Desc);
