// #*#*#*#*#*#*#*#*#*#*#*#* OsAbstractionLayer.h *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The seam between the library and the operating system
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Seam +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Opens a DtPcie card and sends commands (IOCTLs) to its driver. Only the code below
// this layer knows the operating system, or whether there is a card at all.
//
// There are three backends: Linux, Windows, and an emulated device. The emulated device
// lets the library be tested without hardware. It replaces the driver, not the library:
// it takes the same IOCTL codes and structures as the real driver, so the tests check
// those for real.
//
// The device layer uses a device in these steps: OsDrv_Open; OsDrv_Ioctl for each
// command and OsDrv_MapMemory for memory the driver shares; and OsDrv_Close at the end.
//

// The most devices the driver accepts. The device layer tries every index below it; on
// Linux index N is /dev/DtPcieN.
#define DT_MAX_DEVICES 50

typedef struct OsDrv OsDrv;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Device -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

// Opens the device with number Index, from 0 to DT_MAX_DEVICES - 1. On Linux Index is
// the N of /dev/DtPcieN. On Windows it counts the devices present, in the operating
// system's order, which can change when a card is added or removed.
//
// When the environment variable CDTAPI_SIM is set, opens the emulated device instead,
// and no hardware is touched.
//
// Returns the device, or NULL when there is no device at Index or it cannot be opened.
OsDrv* OsDrv_Open(int Index);

// Closes a device. A NULL Drv does nothing.
void OsDrv_Close(OsDrv* Drv);

// Returns whether Drv is the emulated device rather than a card.
//
// The library reads CDTAPI_SIM once, when a device or the network is first used, and
// every handle follows that. A caller that decides later, from its own command line or
// the environment, that there is no card can therefore disagree with the library. Ask
// this function instead.
bool OsDrv_IsEmulated(const OsDrv* Drv);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Control -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Sends one command (IOCTL) to the driver.
//
// Code is the platform's IOCTL code, as the driver's ABI header defines it, for example
// DT_IOCTL_GET_DEV_INFO2. In and InSize are the input structure, Out and OutSize the
// buffer for the answer. Out and OutSize may both be NULL for a command without an
// answer.
//
// What *OutSize holds afterwards differs per platform:
//   Windows   the number of bytes the driver wrote
//   Linux     unchanged, because the Linux driver does not report how much it wrote
//   Emulator  the number of bytes written, as on Windows
// So *OutSize shows a short answer on Windows, but not everywhere.
//
// *DrvStatus gets the driver's DtStatus for OS_IOCTL_DRIVER_STATUS, and DT_STATUS_OK,
// which is zero, otherwise. DrvStatus may be NULL.
//
// Returns one of the OS_IOCTL_ outcomes below.
int OsDrv_Ioctl(OsDrv* Drv, uint32_t Code, const void* In, size_t InSize, void* Out,
                size_t* OutSize, uint32_t* DrvStatus);

// The outcomes of OsDrv_Ioctl.
//
// A driver refuses a command with a DtStatus. Windows delivers it as a GetLastError
// value with the customer bit (bit 29) set; Linux as the negated return value of ioctl.
// Each backend turns its own form into OS_IOCTL_DRIVER_STATUS and the DtStatus, so the
// layer above handles one form. Any other failure is one of the operating system, not of
// the driver, and has an outcome of its own because it maps to a different result.
//
// On Linux, OS_IOCTL_NO_RESOURCES comes only from the backend's own allocation; an ioctl
// that fails with ENOMEM is OS_IOCTL_COMMUNICATION.
//
#define OS_IOCTL_OK 0             // The driver carried out the command.
#define OS_IOCTL_DRIVER_STATUS -1 // The driver refused it; see the DtStatus.
#define OS_IOCTL_NO_RESOURCES -2  // Not enough memory or system resources for the call.
#define OS_IOCTL_COMMUNICATION -3 // Any other failure to reach the driver.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Memory -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

// Maps Size bytes of memory that the driver shares at Offset into the process, for
// reading and writing. Returns the address, or NULL when the mapping fails or the
// platform does not map memory this way. The Windows driver does not: it maps the memory
// itself, in the command that asks for it.
void* OsDrv_MapMemory(OsDrv* Drv, uint64_t Offset, size_t Size);

// Removes a mapping that OsDrv_MapMemory made. A NULL Address does nothing.
void OsDrv_UnmapMemory(OsDrv* Drv, void* Address, size_t Size);

// Returns the error of the last failed call on Drv, for diagnostics: the DtStatus after
// OS_IOCTL_DRIVER_STATUS, and otherwise the platform's error number, errno on Linux and
// GetLastError on Windows. Returns zero when nothing has failed.
uint32_t OsDrv_LastError(const OsDrv* Drv);
