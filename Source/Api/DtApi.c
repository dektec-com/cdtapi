// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtApi.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Public API entry points that belong to no single subsystem
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "CDtapiLite.h" // Public API.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Version +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtapiLiteGetVersion -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* DtapiLiteGetVersion(void)
{
    return CDTAPILITE_VERSION;
}
