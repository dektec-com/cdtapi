// #*#*#*#*#*#*#*#*#*#*#*#*#*#* SimDta2110.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - What the emulated DTA-2110 is: its identity, properties and functions
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Identity +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The DTA-2110 is a 10G SmartNIC with one IP port. The PCI IDs, the firmware's build date
// and the port's capabilities come from DekTec's device description, firmware version 1.
//
// The rest is assumed, because no DTA-2110 with DtPcie firmware was at hand: the serial
// number, the hardware revision, the firmware variant, the PCIe link and the MAC address.
// The serial number starts with 9, as the emulated DTA-2178's does. The MAC address lies
// in the device description's MAC range.
//

#define SIM_DTA2110_TYPE_NUMBER 2110
#define SIM_DTA2110_SERIAL 9211000001ULL
#define SIM_DTA2110_HARDWARE_REVISION 1
#define SIM_DTA2110_FIRMWARE_VERSION 1
#define SIM_DTA2110_FIRMWARE_VARIANT 0
#define SIM_DTA2110_DEVICE_ID 0x083E
#define SIM_DTA2110_SUBSYSTEM_VENDOR_ID 0x1A0E
#define SIM_DTA2110_SUBSYSTEM_ID 0x083E
#define SIM_DTA2110_FW_BUILD_YEAR 2025
#define SIM_DTA2110_FW_BUILD_MONTH 8
#define SIM_DTA2110_FW_BUILD_DAY 18
#define SIM_DTA2110_FW_BUILD_HOUR 10
#define SIM_DTA2110_FW_BUILD_MINUTE 26
#define SIM_DTA2110_BUS_NUMBER 5
#define SIM_DTA2110_PCIE_NUM_LANES 4
#define SIM_DTA2110_PCIE_MAX_LANES 4

// The number of ports: one, the IP port.
#define SIM_DTA2110_PORT_COUNT 1

// The MAC address of the port.
#define SIM_DTA2110_MAC_ADDRESS {0x00, 0x14, 0xF4, 0x08, 0x00, 0x01}

// The number of hardware pipes in each direction, from the device description.
#define SIM_DTA2110_HW_PIPES 3

// The packet alignment of the port's data path, in bytes.
#define SIM_DTA2110_PACKET_ALIGNMENT 8

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Model +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The port has one API function, AF_NW#1 with the empty role. Its one object is the
// driver function DF_NW#1. The device has the activation block BC_IPSECG#1. The lookups
// work as those of SimDta2178.h.
//

// Looks up a property by name and port index; -1 is the device. A capability that the
// port does not have is found, with the value false. Returns false only when there is no
// such property; *Type is then left as it was.
bool SimDta2110_GetProperty(const char* Name, int PortIndex, int* Type, uint64_t* Value);

// Looks up a string property in the same way.
bool SimDta2110_GetString(const char* Name, int PortIndex, const char** Str);

// Finds the object a UUID names, and returns its port index, type and role. The bits of
// the UUID above its flags name a pipe of the network function, and are ignored here.
bool SimDta2110_FindFunction(int Uuid, int* PortIndex, int* Type, const char** Role);
