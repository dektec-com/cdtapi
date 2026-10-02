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
#include "AvFifo/DtAvError.h" // The failure text.
#include "cdtapi_avfifo.h"    // The FIFOs' configurations.
#include "cdtapi_constants.h" // Result codes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The types of dtnmos the bridge's functions take, as cdtapi_nmos.h has them through
// dtnmos's headers; here only their names, as only pointers to them are passed.
typedef struct DtNmosFlow DtNmosFlow;

// The declarations of cdtapi_nmos.h, which this build does not install.
CDTAPI_API DtapiResult DtNmosAvFifo_RxConfigFromFlow(const DtNmosFlow* Flow,
                                                     St2110_RxFrameFormat Format,
                                                     St2110_RxConfigVideo* Video,
                                                     St2110_RxConfigAudio* Audio,
                                                     AvFifo_IpPars* IpPars);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NoNmos -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult NoNmos(const char* Where)
{
    return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where,
                         "The library was built without NMOS, CDTAPI_WITH_NMOS");
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Build +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtapiHasNmos -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtapiHasNmos(void)
{
    return 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Flows +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_RxConfigFromFlow -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtNmosAvFifo_RxConfigFromFlow(const DtNmosFlow* Flow,
                                          St2110_RxFrameFormat Format,
                                          St2110_RxConfigVideo* Video,
                                          St2110_RxConfigAudio* Audio,
                                          AvFifo_IpPars* IpPars)
{
    (void)Flow;
    (void)Format;
    (void)Video;
    (void)Audio;
    (void)IpPars;
    return NoNmos("DtNmosAvFifo_RxConfigFromFlow");
}
