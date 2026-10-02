// #*#*#*#*#*#*#*#*#*#*#*#*#*#* cdtapi_nmos.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Public C API joining the AV FIFO to NMOS through dtnmos
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The bridge is built into the library with CDTAPI_WITH_NMOS, and this header is
// installed with it; it needs dtnmos's headers. A library built without the option
// exports the same functions, which fail with DTAPI_E_NOT_SUPPORTED; DtapiHasNmos says
// which build a program has. The bridge never changes a FIFO by itself: its helpers turn
// a flow, as an SDP or an IS-05 activation describes it, into a FIFO's configuration,
// for the thread that owns the FIFO to apply.

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "cdtapi.h"        // Results.
#include "cdtapi_avfifo.h" // The FIFOs and their configurations.

// dtnmos includes
#include "dtnmos_sdp.h" // Flows.

#ifdef __cplusplus
extern "C"
{
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Flows +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A function that fails returns its error and sets the text GetLastException returns,
// naming what the FIFO cannot do. Addresses are literal: a domain name is
// DTAPI_E_NOT_SUPPORTED, as the bridge looks no name up.
//

// The configuration of a receiving FIFO for Flow, in the frame format Format: *Video for
// a video flow, *Audio for an audio flow, and *IpPars for either. The pointer of the
// other media may be null. The format decides which video flows a FIFO takes: Raw takes
// any uncompressed ST 2110-20 flow, delivering its pixel groups as they come;
// Uyvy422_8b and Yuv422p_8b take YCbCr-4:2:2 of depth 8, and Uyvy422_10b and
// Uyvy422_10b_to_8b of depth 10. Audio of L16 and L24 is L16BE and L24BE, and AM824 is
// Raw, which delivers each packet's bytes. Every RANGE and every TP is taken.
//
// *IpPars are the flow's destination and port, a source filter of its SourceIp with any
// port when it has one, its payload type over RTP, DiffServ 0x88 (AF41), a time to live
// of 64, and gateway and VLAN 0.
//
// DTAPI_E_NOT_SUPPORTED, naming the reason, for the second path of ST 2022-7, for JPEG
// XS, ancillary data and another media, and for a flow the format does not take;
// DTAPI_E_INVALID_ARG for a null flow or IP parameters, a flow whose Size is smaller
// than this header's DtNmosFlow, a missing configuration of the flow's media, and an
// address that is not one.
CDTAPI_API DtapiResult DtNmosAvFifo_RxConfigFromFlow(const DtNmosFlow* Flow,
                                                     St2110_RxFrameFormat Format,
                                                     St2110_RxConfigVideo* Video,
                                                     St2110_RxConfigAudio* Audio,
                                                     AvFifo_IpPars* IpPars);

#ifdef __cplusplus
}
#endif
