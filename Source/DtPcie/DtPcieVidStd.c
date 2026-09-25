// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtPcieVidStd.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - DtPcie driver commands: driver video standards as DTAPI ones - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtPcieVidStd.h" // Interface being implemented.
#include "DtPcieAbi.h"    // The DT_VIDSTD_ codes.
#include "cdtapi.h"       // The DTAPI_VIDSTD_ codes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Video standard +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieVidStd_FromDriver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Each standard of Tables/DtVidStdList.inc has a DT_VIDSTD_ code of the same name, so
// the cases come from that list and cannot fall out of step with it.
//
int DtPcieVidStd_FromDriver(int DrvVidStd)
{
    switch (DrvVidStd)
    {
#define X(Name, FpsNum, FpsDen, NumLines, Scan, LineNumSymHanc, IsLevelB, IoStd,         \
          OneLinkVidStd)                                                                 \
    case DT_VIDSTD_##Name:                                                               \
        return DTAPI_VIDSTD_##Name;
#include "Tables/DtVidStdList.inc"
#undef X
    default:
        return DTAPI_VIDSTD_UNKNOWN;
    }
}
