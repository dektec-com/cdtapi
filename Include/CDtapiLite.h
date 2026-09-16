// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* CDtapiLite.h *#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
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
#include "CDtapiLite_Version.h"

#ifdef __cplusplus
extern "C"
{
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Symbol export +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Global functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Returns the library version as a string, for example "1.0.0". The returned pointer is
// static storage owned by the library and must not be freed.
CDTAPILITE_API const char* DtapiLiteGetVersion(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // CDTAPILITE_H
