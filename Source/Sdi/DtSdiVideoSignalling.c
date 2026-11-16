// #*#*#*#*#*#*#*#*#*#*#*# DtSdiVideoSignalling.c *#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The signalling of an SDI frame, read from a VPID - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "Video/DtSmpte352.h" // Reading the payload ID.
#include "cdtapi_sdi.h"       // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Signalling +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiVideoSignalling_FromVpid -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtSdiVideoSignalling_FromVpid(uint32_t Vpid, DtSdiVideoSignalling* Signalling)
{
    if (Signalling == NULL || !DtSmpte352_IsSignallingSize(Signalling->Size))
        return DTAPI_E_INVALID_ARG;
    DtSdiVideoSignalling Whole;
    const bool Known = DtSmpte352_ReadSignalling(Vpid, &Whole);
    DtSmpte352_GiveSignalling(&Whole, Signalling);
    return Known ? DTAPI_OK : DTAPI_E_NOT_SUPPORTED;
}
