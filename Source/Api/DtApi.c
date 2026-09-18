// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtApi.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Public API entry points that belong to no single subsystem
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "cdtapi.h" // Public API.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Version +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtapiGetVersion -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* DtapiGetVersion(void)
{
    return CDTAPI_VERSION;
}
