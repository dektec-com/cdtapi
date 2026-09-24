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
// Every translation unit that talks to the driver includes this header rather than
// Abi/DtCommon.h, so that the base types and the WINBUILD spelling are set up in exactly
// one place.
//
// DtCommon.h keys its Windows branches off WINBUILD, which is the driver build's own
// spelling rather than a compiler predefine, so it is derived here from _WIN32.
//

#if defined(_WIN32) || defined(_WIN64)
    #ifndef WINBUILD
        #define WINBUILD 1
    #endif

    // DtCommon.h names GUID and builds IOCTL numbers with CTL_CODE, and it pulls in
    // winioctl.h only when DTAPI is defined. Supply both here instead.
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <winioctl.h>

    // DtStatusCodes.h builds Windows driver statuses from two NT status definitions
    // that come from kernel headers and are not available to user mode. Their values
    // are fixed by the NTSTATUS layout: success is zero and the error severity is 3.
    // The severity is unsigned because DT_STATUS_ERROR shifts it left by 30 bits, which
    // would overflow a signed int.
    #ifndef STATUS_SUCCESS
        #define STATUS_SUCCESS 0
    #endif
    #ifndef STATUS_SEVERITY_ERROR
        #define STATUS_SEVERITY_ERROR 3U
    #endif
#else
    // _IOWR and _IOC_NR, used by the Linux half of the IOCTL definitions.
    #include <sys/ioctl.h>
#endif

// The vendored headers write ASSERT_SIZE(Type, Size); with a semicolon, and on compilers
// other than MSVC the macro already ends in one. That leaves an empty declaration at file
// scope, which -Wpedantic reports and -Werror then turns into a failed build of every
// file that includes the driver ABI. The headers stay byte-identical to the SDK, so the
// diagnostic is silenced for exactly these vendored includes instead.
//
// Visual Studio 2022's compiler reports the flexible array members of those headers, such
// as m_Buf[] at the end of a command's output, as C4200, a zero-sized array, although C99
// and C11 have them; later versions do not. /W4 reports it and /WX fails the build, so it
// is silenced for the same includes.
#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wpedantic"
#elif defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4200)
#endif

#include "Abi/DtCommon.h"

// The DtPcie driver's own header carries the device interface GUID that the Windows
// backend enumerates by. Including it for every platform keeps one include order.
#include "Abi/DtPcieCommon.h"

// The status codes a driver command fails with, DT_STATUS_IN_USE and the like.
#include "Abi/DtStatusCodes.h"

#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#elif defined(_MSC_VER)
    #pragma warning(pop)
#endif
