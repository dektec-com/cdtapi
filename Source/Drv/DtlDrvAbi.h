// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtlDrvAbi.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Single entry point for the vendored DtPcie driver ABI
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DTL_DRV_ABI_H
#define CDTAPILITE_DTL_DRV_ABI_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "DtlAbiTypes.h" // Base types the vendored header expects.

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
#else
    // _IOWR and _IOC_NR, used by the Linux half of the IOCTL definitions.
    #include <sys/ioctl.h>
#endif

// The vendored headers write ASSERT_SIZE(Type, Size); with a semicolon, and on compilers
// other than MSVC the macro already ends in one. That leaves an empty declaration at file
// scope, which -Wpedantic reports and -Werror then turns into a failed build of every
// file that includes the driver ABI. The headers stay byte-identical to the SDK, so the
// diagnostic is silenced for exactly these two includes instead.
#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include "Abi/DtCommon.h"

// The DtPcie driver's own header carries the device interface GUID that the Windows
// backend enumerates by. Including it for every platform keeps one include order.
#include "Abi/DtPcieCommon.h"

#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

#endif // CDTAPILITE_DTL_DRV_ABI_H
