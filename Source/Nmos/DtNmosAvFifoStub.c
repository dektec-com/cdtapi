// #*#*#*#*#*#*#*#*#*#*#*#*# DtNmosAvFifoStub.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The NMOS bridge of the AV FIFO in a build without CDTAPI_WITH_NMOS
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The bridge's functions, exported in every build so that every build has the same
// exports: a program built against a library with NMOS loads against one without, and
// learns from DtapiHasNmos, or from DTAPI_E_NOT_SUPPORTED, that NMOS is not there. They
// take pointers only, so nothing here needs a header of dtnmos's.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "cdtapi.h" // DtapiHasNmos.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Build +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtapiHasNmos -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtapiHasNmos(void)
{
    return 0;
}
