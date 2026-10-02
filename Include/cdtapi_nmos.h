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

// The flow of a transmit FIFO that is configured and has its IP parameters, started or
// not, for its SDP and its NMOS sender: its format, its destination, port and payload
// type, and as its source the port's address Start sends from, which the program need
// not give. A video flow is YCbCr-4:2:2 of the FIFO's depth, its packing mode, 2110TPN
// for gapped and 2110TPNL for linear scheduling, SSN ST2110-20:2017, and the
// colorimetry and SDR of DtNmosVideoFormat_SetDefaults; RANGE is left out, narrow. Every
// flow has a=ts-refclk localmac with the port's MAC address and a=mediaclk direct=0.
// What the FIFO does not know is the program's to overwrite in *Flow: HDR, another
// range, a reference clock of PTP, an audio flow's channel order. *Flow owns no strings.
//
// DTAPI_E_NOT_ATTACHED, DTAPI_E_CONFIG before Configure, DTAPI_E_NO_IPPARS before
// SetIpPars, the failures of checking the port's network, and DTAPI_E_NOT_SUPPORTED for
// audio of St2110_AudioFormat_Raw, whose encoding the FIFO does not know.
CDTAPI_API DtapiResult DtNmosAvFifo_FlowFromTxFifo(AvFifo_TxFifo* Fifo, DtNmosFlow* Flow);

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

// The configuration of a transmitting FIFO for Flow, as DtNmosAvFifo_FlowFromTxFifo
// writes it: *Video for a video flow, *Audio for an audio flow, and *IpPars for either;
// the pointer of the other media may be null. Video is YCbCr-4:2:2 of depth 8 or 10,
// which gives the frame format; exactframerate is doubled into the field rate of
// interlaced and PsF video; PM 2110GPM, or none, is General and 2110BPM Block; TP
// 2110TPNL is Linear, and 2110TPN, 2110TPW or none Gapped. Audio is L16 or L24, with
// NumSamplesPerIpPacket from a=ptime, 1 ms when the flow has none. *IpPars are those of
// DtNmosAvFifo_RxConfigFromFlow without a source filter: a sender's SourceIp is its own
// address, which the FIFO chooses.
//
// DTAPI_E_NOT_SUPPORTED, naming the reason, for what DtNmosAvFifo_RxConfigFromFlow
// refuses, for another sampling or depth, AM824, and a PM or TP the FIFO does not have;
// DTAPI_E_INVALID_ARG as DtNmosAvFifo_RxConfigFromFlow, and for video without width,
// height or frame rate and audio without sample rate or channels.
CDTAPI_API DtapiResult DtNmosAvFifo_TxConfigFromFlow(const DtNmosFlow* Flow,
                                                     St2110_TxConfigVideo* Video,
                                                     St2110_TxConfigAudio* Audio,
                                                     AvFifo_IpPars* IpPars);

#ifdef __cplusplus
}
#endif
