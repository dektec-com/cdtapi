// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtlDrvAbi.h *#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Single entry point for the vendored DtPcie driver ABI
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DTL_DRV_ABI_H
#define CDTAPILITE_DTL_DRV_ABI_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "DtlAbiTypes.h" // Base types the vendored header expects.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Vendored ABI +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
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

#include "Abi/DtCommon.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Linux IOCTL dispatch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// On Linux the size of the argument structure is encoded into the IOCTL number, so the
// number changes whenever a structure grows. Match on the function code alone, exactly
// as the driver does. DtCommon.h documents this at its DT_IOCTL_TO_FUNCTION definition.
//
#if !defined(WINBUILD) && !defined(DT_IOCTL_TO_FUNCTION)
    #define DT_IOCTL_TO_FUNCTION(IoctlCode) ((UInt32)_IOC_NR(IoctlCode))
#endif

#endif // CDTAPILITE_DTL_DRV_ABI_H
