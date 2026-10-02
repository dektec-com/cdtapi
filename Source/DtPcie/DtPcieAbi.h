// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtPcieAbi.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Single entry point for the vendored DtPcie driver ABI
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtAbiTypes.h" // Base types the vendored header expects.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Vendored ABI +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Includes the driver's ABI headers, with everything they need. Code that talks to the
// driver includes this header, never Abi/DtCommon.h itself, so that the set-up below is
// in one place.
//
// DtCommon.h selects its Windows code with WINBUILD. The driver's build defines that
// macro; a compiler does not. So this header defines it when _WIN32 is defined.
//

#if defined(_WIN32) || defined(_WIN64)
    #ifndef WINBUILD
        #define WINBUILD 1
    #endif

    // DtCommon.h uses GUID, and CTL_CODE to build IOCTL codes. It includes winioctl.h
    // only when DTAPI is defined, so this header includes both itself.
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <winioctl.h>

    // Defines two NTSTATUS values that DtStatusCodes.h uses. They come from kernel
    // headers, which a user-mode program cannot include. Their values are fixed: success
    // is 0 and the error severity is 3. The severity is unsigned because DT_STATUS_ERROR
    // shifts it left by 30 bits, which would overflow a signed int.
    #ifndef STATUS_SUCCESS
        #define STATUS_SUCCESS 0
    #endif
    #ifndef STATUS_SEVERITY_ERROR
        #define STATUS_SEVERITY_ERROR 3U
    #endif
#else
    // Provides _IOWR and _IOC_NR, which the Linux IOCTL definitions use.
    #include <sys/ioctl.h>
#endif

// Turns off two warnings for the driver's headers only. The headers stay identical to the
// SDK's, so they cannot be fixed.
//
// - The headers write "ASSERT_SIZE(Type, Size);". Except on MSVC, the macro already ends
//   in a semicolon, which leaves an empty declaration. -Wpedantic warns about it, and
//   -Werror would fail the build.
// - Visual Studio 2022 reports flexible array members, such as m_Buf[] at the end of a
//   command's output, as warning C4200, although C99 and C11 allow them. Later versions
//   do not. /W4 reports it, and /WX would fail the build.
#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wpedantic"
#elif defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4200)
#endif

#include "Abi/DtCommon.h"

// Includes the GUID of the DtPcie device interface, which the Windows backend uses to
// find the devices. It is included on every platform, so that the include order is the
// same everywhere.
#include "Abi/DtPcieCommon.h"

// Includes the DtStatus codes a driver command can fail with, such as DT_STATUS_IN_USE.
#include "Abi/DtStatusCodes.h"

#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#elif defined(_MSC_VER)
    #pragma warning(pop)
#endif
