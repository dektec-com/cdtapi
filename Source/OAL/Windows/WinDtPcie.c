// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# WinDtPcie.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - DtPcie driver backend for Windows
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes: DtPcieAbi.h brings in windows.h and the device interface GUID.
#include "Core/DtAlloc.h"       // Allocation seam.
#include "DtPcieAbi.h"          // Driver ABI and GUID_DEVINTERFACE_DTPCIE.
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
    uint32_t LastError;
} WinDevice;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Discovery +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OpenListedInterface -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Opens the Index-th interface of the list DevInfo. Returns INVALID_HANDLE_VALUE when
// there is no interface at that index or it cannot be opened.
//
static HANDLE OpenListedInterface(HDEVINFO DevInfo, int Index)
{
    SP_DEVICE_INTERFACE_DATA InterfaceData;
    InterfaceData.cbSize = sizeof(InterfaceData);
    if (!SetupDiEnumDeviceInterfaces(DevInfo, NULL, &GUID_DEVINTERFACE_DTPCIE,
                                     (DWORD)Index, &InterfaceData))
    {
        return INVALID_HANDLE_VALUE;
    }

    // The first call only measures. It is expected to fail with a buffer that is too
    // small; any other failure means the interface cannot be read.
    DWORD Size = 0;
    if (!SetupDiGetDeviceInterfaceDetailA(DevInfo, &InterfaceData, NULL, 0, &Size,
                                          NULL) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        return INVALID_HANDLE_VALUE;
    }

    PSP_DEVICE_INTERFACE_DETAIL_DATA_A Detail =
        (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)DtAlloc_Malloc(Size);
    if (Detail == NULL)
        return INVALID_HANDLE_VALUE;

    // cbSize is the size of the fixed part, not of the whole allocation.
    Detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
    HANDLE Handle = INVALID_HANDLE_VALUE;
    if (SetupDiGetDeviceInterfaceDetailA(DevInfo, &InterfaceData, Detail, Size, NULL,
                                         NULL))
    {
        // Shared, so that another tool such as DtInfo can still open the same card while
        // this library holds it. Exclusive use of a port is arbitrated by the driver, per
        // channel, not by who opened the device first.
        Handle = CreateFileA(Detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    }
    DtAlloc_Free(Detail);
    return Handle;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OpenInterface -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Opens the Index-th present DtPcie device interface. Index counts interfaces that
// SetupAPI reports as present, in its own order; it is not a fixed slot number.
//
// Returns INVALID_HANDLE_VALUE when there is no interface at that index.
//
static HANDLE OpenInterface(int Index)
{
    HDEVINFO DevInfo = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_DTPCIE, NULL, NULL,
                                            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (DevInfo == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;

    HANDLE Handle = OpenListedInterface(DevInfo, Index);
    SetupDiDestroyDeviceInfoList(DevInfo);
    return Handle;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Backend +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WinOpen -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void* WinOpen(int Index)
{
    HANDLE Handle = OpenInterface(Index);

    if (Handle == INVALID_HANDLE_VALUE)
        return NULL;

    WinDevice* Dev = (WinDevice*)DtAlloc_Malloc(sizeof(WinDevice));
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
    DtAlloc_Free(Dev);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WinIoCtl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The driver defines its IOCTLs as METHOD_OUT_DIRECT, so the output buffer is a separate
// argument that the I/O manager locks for the duration of the call.
//
// A driver-specific failure comes back from GetLastError with the customer bit, bit 29,
// set. OsIoctlOutcome_ClassifyWindows separates it from errors of Windows itself;
// translating it into a DTAPI result is the DtPcie command layer's job, not this one's.
//
static int WinIoCtl(void* State, uint32_t Code, const void* In, size_t InSize, void* Out,
                    size_t* OutSize, uint32_t* DrvStatus)
{
    WinDevice* Dev = (WinDevice*)State;
    DWORD Returned = 0;
    DWORD OutCapacity = (Out != NULL && OutSize != NULL) ? (DWORD)*OutSize : 0;

    if (!DeviceIoControl(Dev->Handle, (DWORD)Code, (LPVOID)In, (DWORD)InSize, Out,
                         OutCapacity, &Returned, NULL))
    {
        Dev->LastError = (uint32_t)GetLastError();
        return OsIoctlOutcome_ClassifyWindows(Dev->LastError, DrvStatus);
    }

    if (OutSize != NULL)
        *OutSize = Returned;

    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WinLastError -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static uint32_t WinLastError(const void* State)
{
    return ((const WinDevice*)State)->LastError;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPlatform_Backend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const OsBackend* OsPlatform_Backend(void)
{
    static const OsBackend Backend = {WinOpen,      WinClose, WinIoCtl,
                                      WinLastError, NULL,     NULL};
    return &Backend;
}
