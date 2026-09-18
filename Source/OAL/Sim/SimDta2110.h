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
// The DTA-2110, a 10G SmartNIC with one IP port, as DekTec's device description has it
// for firmware version 1: the PCI IDs, the firmware's build date and the port's
// capabilities. No DTA-2110 with DtPcie firmware has been at hand, so the rest is
// assumed: the serial number, starting with 9 as the emulated DTA-2178's does, the
// hardware revision, the firmware variant, the PCIe link and the MAC address, which
// follows the device description's MAC range.
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

// Its one port, the IP port.
#define SIM_DTA2110_PORT_COUNT 1

// The MAC address of the port.
#define SIM_DTA2110_MAC_ADDRESS {0x00, 0x14, 0xF4, 0x08, 0x00, 0x01}

// The number of hardware pipes each way, as the device description lists them.
#define SIM_DTA2110_HW_PIPES 3

// The packet alignment of the port's data path, in bytes.
#define SIM_DTA2110_PACKET_ALIGNMENT 8

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Model +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The port has one API function, AF_NW#1 with the empty role, whose one part is the
// driver function DF_NW#1. The lookups follow those of SimDta2178.h.
//

// Looks up a property by name and port index, -1 for the device. A capability the port
// does not have is found with the value false. Returns false only when there is no such
// property; *Type is then untouched.
bool SimDta2110_GetProperty(const char* Name, int PortIndex, int* Type, uint64_t* Value);

// Looks up a string property the same way.
bool SimDta2110_GetString(const char* Name, int PortIndex, const char** Str);

// Finds the part a UUID names, with its port index, type and role. The UUID is compared
// without the bits above its flags, which name a pipe of the network function.
bool SimDta2110_FindFunction(int Uuid, int* PortIndex, int* Type, const char** Role);
