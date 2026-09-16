// #*#*#*#*#*#*#*#*#*#*#*#* OsAbstractionLayer.h *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - The seam between the library and the operating system
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_OS_ABSTRACTION_LAYER_H
#define CDTAPILITE_OS_ABSTRACTION_LAYER_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Seam +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Everything below this line talks to a DtPcie card; everything above it does not know
// which operating system it is on, or whether there is a card at all.
//
// Three backends sit behind it: Linux, Windows, and an emulated device. The emulator is
// what makes the rest of the library testable without hardware, and it is the reason
// this layer exists as a named boundary rather than as scattered #ifdefs.
//
// The emulator replaces the driver, not the library. It receives the same IOCTL codes
// and the same structures the real driver would, so the wire format is exercised for
// real rather than mocked away. That is where most of the defects are going to be.
//

// The driver accepts up to this many devices, and the Linux backend probes /dev/DtPcie0
// through /dev/DtPcie49 to find them.
#define DTL_MAX_DEVICES 50

typedef struct OsDrv OsDrv;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Device -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

// Opens the device at Index, or returns NULL when there is none. Index runs from zero
// to DTL_MAX_DEVICES - 1.
//
// When CDTAPILITE_SIM is set in the environment, the emulated device is opened instead
// and no real hardware is touched.
OsDrv* OsDrvOpen(int Index);

// Closes a device. Passing NULL does nothing.
void OsDrvClose(OsDrv* Drv);

// True when this handle is the emulated device rather than a card.
//
// It answers what actually opened, not what was asked for. A build made without the
// emulator still accepts CDTAPILITE_SIM in the environment and then opens real hardware,
// and a caller that decides "no card" from its own command line rather than from this
// would go on to read a real device with the emulator's assumptions.
bool OsDrvIsEmulated(const OsDrv* Drv);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Control -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Issues one IOCTL.
//
// Code is the platform's own code, taken straight from the vendored driver ABI header,
// so the caller writes DT_IOCTL_GET_DEV_INFO2 and this layer does not need to know what
// that expands to.
//
// In and InSize describe the input structure. Out and OutSize describe the output
// buffer. Out may be NULL with OutSize NULL for a command that returns nothing.
//
// On return *OutSize means different things per platform, because the drivers differ:
//
//   Windows  the number of bytes the driver actually wrote.
//   Linux    unchanged. The Linux driver writes its answer over the shared buffer and
//            never reports how much of it is valid, so there is nothing to return.
//   Emulator the number of bytes written, as on Windows.
//
// So a caller can use *OutSize to detect a short answer on Windows, but must not rely on
// it to do so everywhere.
//
// Returns 0 on success and -1 on failure.
int OsDrvIoCtl(OsDrv* Drv, unsigned long Code, const void* In, size_t InSize, void* Out,
               size_t* OutSize);

// The error the last failed call on this handle reported, as the platform's own error
// number: errno on Linux, GetLastError on Windows. Zero when nothing has failed.
unsigned long OsDrvLastError(const OsDrv* Drv);

#endif // CDTAPILITE_OS_ABSTRACTION_LAYER_H
