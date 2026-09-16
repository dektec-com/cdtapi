// #*#*#*#*#*#*#*#*#*#*#*#*#*#* CDtapiLite.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Public C API for DekTec SDI interfaces
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_H
#define CDTAPILITE_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>

// CDtapiLite includes
#include "CDtapiLite_Constants.h" // Result codes and configuration constants.
#include "CDtapiLite_Version.h"   // Library version.

#ifdef __cplusplus
extern "C"
{
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Symbol export +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A consumer that links the shared library on Windows needs the import declaration, so
// the default here is "importing"; the library build itself defines CDTAPILITE_EXPORTS.
// A static build defines CDTAPILITE_STATIC and gets neither.
//

#if defined(CDTAPILITE_STATIC)
    #define CDTAPILITE_API
#elif defined(_WIN32) || defined(_WIN64)
    #if defined(CDTAPILITE_EXPORTS)
        #define CDTAPILITE_API __declspec(dllexport)
    #else
        #define CDTAPILITE_API __declspec(dllimport)
    #endif
#else
    #define CDTAPILITE_API __attribute__((visibility("default")))
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Global functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Returns the library version as a string, for example "1.0.0". The returned pointer is
// static storage owned by the library and must not be freed.
CDTAPILITE_API const char* DtapiLiteGetVersion(void);

// Converts a video standard to the I/O standard group value and sub-value that select it,
// for use with SetIoConfig and DTAPI_IOCONFIG_IOSTD. LinkStandard is -1 for anything but
// 4K; for 4K it says how the picture is carried: 0 and 1 are four 3G links per SMPTE 425
// level A and annex B, 2 is one 6G link, 3 is one 12G link.
//
// Sets *Value and *SubValue to -1 before anything can fail. Returns DTAPI_E_INVALID_ARG
// for a null output pointer, DTAPI_E_INVALID_LINKSTD for a link standard that does not
// suit the video standard, and DTAPI_E_INVALID_VIDSTD for an unknown video standard.
// 4K over four links returns DTAPI_E_NOT_IMPLEMENTED in this version.
CDTAPILITE_API unsigned int DtapiVidStd2IoStd(int VideoStandard, int LinkStandard,
                                              int* Value, int* SubValue);

// Returns the name of a result code's macro, for example "DTAPI_E_IN_USE", or "???" for a
// value that is no result code. The names are those DTAPI gives: of each pair of names
// for one value the first, DTAPI_E_NO_DT_INPUT and DTAPI_E_NO_DT_OUTPUT, and "???" for
// DTAPI_E_INVALID_NUM_INPUTS, DTAPI_E_DISABLED and DTAPI_E_EXCEPTION, which DTAPI does
// not name. The returned string is static and must not be freed.
CDTAPILITE_API const char* DtapiResult2Str(unsigned int Result);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Time of day +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A time from a device's time-of-day clock.
typedef struct DtTimeOfDay
{
    unsigned int Seconds;     // Integer number of seconds part of the TOD time
    unsigned int Nanoseconds; // Number of nanoseconds part of the TOD time
} DtTimeOfDay;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtHwFuncDesc +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// One hardware function: a public port of a device.
//

// The sizes of its two strings, MAX_DEVICE_NAME_SIZE and MAX_DEVICE_DESC_SIZE, are in
// CDtapiLite_Constants.h.

typedef struct DtHwFuncDesc
{
    char DeviceName[MAX_DEVICE_NAME_SIZE];  // Serial and port, as "<serial>:<port>"
    char Description[MAX_DEVICE_DESC_SIZE]; // Type and port, as "DTA-2178 port 1"
    int64_t SerialNumber;
    int Port;     // Port number, from 1
    int IsSdi;    // 1 when the port can carry SD-, HD-, 3G-, 6G- or 12G-SDI
    int IsAvFifo; // 1 when the port has an AV FIFO
    int IsInput;  // 1 when the port can be an input
    int IsOutput; // 1 when the port can be an output
} DtHwFuncDesc;

// Describes the public ports of every device, in the order the driver numbers the
// devices. HwFuncs holds NumEntries descriptors; *NumEntriesResult receives how many
// ports there are, also when they do not all fit. A device that cannot be attached, for
// example because its driver is too old, is left out.
//
// Returns DTAPI_OK and fills all NumEntries descriptors; those beyond the last port are
// all zero apart from DeviceName "0:0" and Description "DTA-0 port 0", as CDTAPI fills
// them. Returns DTAPI_E_BUF_TOO_SMALL when there are more ports than NumEntries, leaving
// HwFuncs untouched; with NumEntries 0 and HwFuncs NULL this asks for the count. Returns
// DTAPI_E_INVALID_ARG for a null NumEntriesResult or a negative NumEntries,
// DTAPI_E_INVALID_BUF for a null HwFuncs with NumEntries not 0, and DTAPI_E_OUT_OF_MEM.
CDTAPILITE_API unsigned int DtapiHwFuncScan(int NumEntries, int* NumEntriesResult,
                                            DtHwFuncDesc* HwFuncs);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtDevice +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A device object, attached to one DekTec device at a time. Every function taking a
// DtDevice returns DTAPI_E_INVALID_ARG for a null pointer, and DTAPI_E_NOT_ATTACHED when
// it needs a device and the object is not attached to one.
//

typedef struct DtDeviceC DtDevice;

// Allocates a detached device object. Returns NULL when memory runs out.
CDTAPILITE_API DtDevice* DtDevice_Alloc(void);

// Detaches the device object if it is attached, and frees it. NULL is allowed.
CDTAPILITE_API void DtDevice_Free(DtDevice* Device);

// Frees *Device as DtDevice_Free does and sets *Device to NULL. NULL is allowed.
CDTAPILITE_API void DtDevice_Freep(DtDevice** Device);

// Attaches to the device with this serial number. Returns DTAPI_E_ATTACHED when already
// attached, DTAPI_E_DRIVER_INCOMP for a driver that is too old, and
// DTAPI_E_NO_SUCH_DEVICE when no device has the serial number. Succeeds with
// DTAPI_OK_OBSOLETE_FW or DTAPI_OK_TAINTED_FW when the device's firmware is obsolete or
// tainted.
CDTAPILITE_API unsigned int DtDevice_AttachToSerial(DtDevice* Device,
                                                    int64_t SerialNumber);

// Detaches from the device.
CDTAPILITE_API unsigned int DtDevice_Detach(DtDevice* Device);

// Sets one I/O configuration of a port, numbered from 1. Returns DTAPI_E_OBSOLETE_FW or
// DTAPI_E_TAINTED_FW for a device whose firmware is, DTAPI_E_NO_SUCH_PORT for a port the
// device does not have, and DTAPI_E_INVALID_ARG for a combination of group, value and
// sub-value that is no configuration; otherwise the driver's result.
CDTAPILITE_API unsigned int DtDevice_SetIoConfig(DtDevice* Device, int Port, int Group,
                                                 int Value, int SubValue);

// Makes a port an output: DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT.
CDTAPILITE_API unsigned int DtDevice_SetToOutput(DtDevice* Device, int Port);

// Makes a port an input: DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT.
CDTAPILITE_API unsigned int DtDevice_SetToInput(DtDevice* Device, int Port);

// Reads the device's time-of-day clock. *TimeOfDay is zero when this fails.
CDTAPILITE_API unsigned int DtDevice_GetTimeOfDay(const DtDevice* Device,
                                                  DtTimeOfDay* TimeOfDay);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // CDTAPILITE_H
