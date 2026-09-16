// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# SimDtPcie.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - The emulated DtPcie device, and the values it reports
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_SIM_DT_PCIE_H
#define CDTAPILITE_SIM_DT_PCIE_H

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Identity +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// What the emulated card says about itself. Tests compare against these rather than
// against literals, so that changing the emulated card is a one-line edit here.
//
// It presents as a DTA-2178. DekTec PCI device IDs are the type number in hexadecimal,
// so 2178 becomes 0x0882, the same way the DTA-2110 is 0x083E.
//
// The serial number starts with 9 because DekTec serials start with the type number,
// and no card has type number 9: a serial seen in a log is recognisably emulated.
//

#define SIM_TYPE_NUMBER 2178
#define SIM_SERIAL 9217800001ULL
#define SIM_HARDWARE_REVISION 1
#define SIM_FIRMWARE_VERSION 7
#define SIM_FIRMWARE_VARIANT 0
#define SIM_VENDOR_ID 0x1A0E
#define SIM_DEVICE_ID 0x0882

#define SIM_DRIVER_MAJOR 1
#define SIM_DRIVER_MINOR 99
#define SIM_DRIVER_MICRO 0
#define SIM_DRIVER_BUILD 0

// The emulator presents exactly one device, at index zero. A scan returns just that one,
// which replaces the hardware rather than adding to it.
#define SIM_DEVICE_INDEX 0

#endif // CDTAPILITE_SIM_DT_PCIE_H
