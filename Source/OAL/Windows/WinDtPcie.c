// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# WinDtPcie.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - DtPcie driver backend for Windows
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes: DtlDrvAbi.h brings in windows.h and the device interface GUID.
#include "Core/DtlAlloc.h"      // Allocation seam.
#include "DtlDrvAbi.h"          // Driver ABI and GUID_DEVINTERFACE_DTPCIE.
#include "OAL/OsBackend.h"      // Backend interface being implemented.
#include "OAL/OsIoctlOutcome.h" // Classifies a failed DeviceIoControl.

// Windows includes, after windows.h.
#include <setupapi.h>

// The classifier is built without windows.h, so it carries its own copy of this value.
_Static_assert(OS_WIN_ERROR_NO_SYSTEM_RESOURCES == ERROR_NO_SYSTEM_RESOURCES,
               "OS_WIN_ERROR_NO_SYSTEM_RESOURCES must match winerror.h");

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct WinDevice
{
    HANDLE Handle;
    unsigned long LastError;
} WinDevice;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Discovery +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OpenInterface -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Opens the Index-th present DtPcie device interface. Index counts interfaces that
// SetupAPI reports as present, in its own order; it is not a fixed slot number.
//
// Returns INVALID_HANDLE_VALUE when there is no interface at that index.
//
static HANDLE OpenInterface(int Index)
{
    SP_DEVICE_INTERFACE_DATA InterfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_A Detail = NULL;
    HDEVINFO DevInfo;
    HANDLE Handle = INVALID_HANDLE_VALUE;
    DWORD Size = 0;

    DevInfo = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_DTPCIE, NULL, NULL,
                                   DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (DevInfo == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;

    InterfaceData.cbSize = sizeof(InterfaceData);
    if (!SetupDiEnumDeviceInterfaces(DevInfo, NULL, &GUID_DEVINTERFACE_DTPCIE,
                                     (DWORD)Index, &InterfaceData))
    {
        goto Cleanup;
    }

    // The first call only measures. It is expected to fail with a buffer that is too
    // small; any other failure means the interface cannot be read.
    if (!SetupDiGetDeviceInterfaceDetailA(DevInfo, &InterfaceData, NULL, 0, &Size,
                                          NULL) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        goto Cleanup;
    }

    Detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)DtlMalloc(Size);
    if (Detail == NULL)
        goto Cleanup;

    // cbSize is the size of the fixed part, not of the whole allocation.
    Detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
    if (!SetupDiGetDeviceInterfaceDetailA(DevInfo, &InterfaceData, Detail, Size, NULL,
                                          NULL))
    {
        goto Cleanup;
    }

    // Shared, so that another tool such as DtInfo can still open the same card while
    // this library holds it. Exclusive use of a port is arbitrated by the driver, per
    // channel, not by who opened the device first.
    Handle = CreateFileA(Detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);

Cleanup:
    DtlFree(Detail);
    SetupDiDestroyDeviceInfoList(DevInfo);
    return Handle;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Backend +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WinOpen -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void* WinOpen(int Index)
{
    WinDevice* Dev;
    HANDLE Handle = OpenInterface(Index);

    if (Handle == INVALID_HANDLE_VALUE)
        return NULL;

    Dev = (WinDevice*)DtlMalloc(sizeof(WinDevice));
    if (Dev == NULL)
    {
        CloseHandle(Handle);
        return NULL;
    }

    Dev->Handle = Handle;
    Dev->LastError = 0;
    return Dev;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WinClose -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void WinClose(void* State)
{
    WinDevice* Dev = (WinDevice*)State;

    CloseHandle(Dev->Handle);
    DtlFree(Dev);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WinIoCtl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The driver defines its IOCTLs as METHOD_OUT_DIRECT, so the output buffer is a separate
// argument that the I/O manager locks for the duration of the call.
//
// A driver-specific failure comes back from GetLastError with the customer bit, bit 29,
// set. OsIoctlClassifyWindows separates it from errors of Windows itself; translating it
// into a DTAPI result is the driver ABI layer's job, not this one's.
//
static int WinIoCtl(void* State, unsigned long Code, const void* In, size_t InSize,
                    void* Out, size_t* OutSize, uint32_t* DrvStatus)
{
    WinDevice* Dev = (WinDevice*)State;
    DWORD Returned = 0;
    DWORD OutCapacity = (Out != NULL && OutSize != NULL) ? (DWORD)*OutSize : 0;

    if (!DeviceIoControl(Dev->Handle, (DWORD)Code, (LPVOID)In, (DWORD)InSize, Out,
                         OutCapacity, &Returned, NULL))
    {
        Dev->LastError = GetLastError();
        return OsIoctlClassifyWindows(Dev->LastError, DrvStatus);
    }

    if (OutSize != NULL)
        *OutSize = Returned;

    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WinLastError -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static unsigned long WinLastError(const void* State)
{
    return ((const WinDevice*)State)->LastError;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPlatformBackend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const OsBackend* OsPlatformBackend(void)
{
    static const OsBackend Backend = {WinOpen, WinClose, WinIoCtl, WinLastError};
    return &Backend;
}
