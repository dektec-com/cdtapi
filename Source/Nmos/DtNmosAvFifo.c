// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtNmosAvFifo.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The NMOS bridge of the AV FIFO, built with CDTAPI_WITH_NMOS
//
// SPDX-License-Identifier: BSD-3-Clause
//
// A build without the option compiles DtNmosAvFifoStub.c instead, which exports the same
// functions, so that every build of the library has the same exports.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <string.h>

// CDTAPI includes
#include "AvFifo/DtAvError.h" // The failure text.
#include "DtNmosAddr.h"       // Addresses.
#include "cdtapi_constants.h" // Result codes.
#include "cdtapi_nmos.h"      // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The DiffServ field of every flow: AF41, as DTAPI and the examples set it.
#define DT_NMOS_DIFFSERV 0x88

// The time to live of every flow, as dtnmos writes it into the c= line of an SDP.
#define DT_NMOS_TIME_TO_LIVE 64

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AudioFormatOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The FIFO's format of the audio encoding Encoding, and false for one it does not know.
// A receiving FIFO delivers AM824 as it comes, as Raw.
//
static bool AudioFormatOf(const char* Encoding, St2110_AudioFormat* Format)
{
    if (strcmp(Encoding, "L16") == 0)
        *Format = St2110_AudioFormat_L16BE;
    else if (strcmp(Encoding, "L24") == 0)
        *Format = St2110_AudioFormat_L24BE;
    else if (strcmp(Encoding, "AM824") == 0)
        *Format = St2110_AudioFormat_Raw;
    else
        return false;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FailAddress -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Records the failure Result of reading the address Text, which What names.
//
static DtapiResult FailAddress(DtapiResult Result, const char* Where, const char* What,
                               const char* Text)
{
    char Message[160];
    if (Result == DTAPI_E_NOT_SUPPORTED)
        snprintf(Message, sizeof(Message),
                 "The %s %.64s is a domain name, which the bridge does not look up", What,
                 Text);
    else
        snprintf(Message, sizeof(Message), "The %s %.64s is no IP address", What, Text);
    return DtAvError_Set(Result, Where, Message);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IpParsOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The IP parameters of Flow: its destination and port, a source filter of its source
// with any port, its payload type over RTP, and the DiffServ and time to live every flow
// has.
//
static DtapiResult IpParsOf(const DtNmosFlow* Flow, AvFifo_IpPars* IpPars,
                            const char* Where)
{
    memset(IpPars, 0, sizeof(*IpPars));
    bool IpV6 = false;
    DtapiResult Result = DtNmosAddr_Parse(Flow->DestinationIp, IpPars->IpAddr, &IpV6);
    if (Result != DTAPI_OK)
        return FailAddress(Result, Where, "destination", Flow->DestinationIp);
    if (Flow->DestinationPort == 0)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where,
                             "The flow has no destination port");
    IpPars->IpVersion = IpV6 ? IpProtocolVersion_IPv6 : IpProtocolVersion_IPv4;
    IpPars->Port = Flow->DestinationPort;

    if (Flow->SourceIp[0] != '\0')
    {
        bool SourceV6 = false;
        Result = DtNmosAddr_Parse(Flow->SourceIp, IpPars->SrcFlt[0].IpAddr, &SourceV6);
        if (Result != DTAPI_OK)
            return FailAddress(Result, Where, "source", Flow->SourceIp);
        if (SourceV6 != IpV6)
        {
            return DtAvError_Set(
                DTAPI_E_INVALID_ARG, Where,
                "The flow's source and destination differ in IP version");
        }
        IpPars->SrcFlt[0].Port = -1;
        IpPars->NSrcFlt = 1;
    }

    IpPars->RtpPayloadType = Flow->PayloadType;
    IpPars->TransportProtocol = IpTransportProtocol_Rtp;
    IpPars->DiffServ = DT_NMOS_DIFFSERV;
    IpPars->TimeToLive = DT_NMOS_TIME_TO_LIVE;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RxFormatName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static const char* RxFormatName(St2110_RxFrameFormat Format)
{
    switch (Format)
    {
    case St2110_RxFrameFormat_Raw:
        return "Raw";
    case St2110_RxFrameFormat_Uyvy422_8b:
        return "Uyvy422_8b";
    case St2110_RxFrameFormat_Uyvy422_10b:
        return "Uyvy422_10b";
    case St2110_RxFrameFormat_Uyvy422_10b_to_8b:
        return "Uyvy422_10b_to_8b";
    case St2110_RxFrameFormat_Yuv422p_8b:
        return "Yuv422p_8b";
    }
    return "?";
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RxVideoTakes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether a receiving FIFO in Format takes the video Video, as its conversions read the
// pixel groups of 4:2:2 of one depth; Raw takes any. When not, Why says why and which
// formats would.
//
static bool RxVideoTakes(St2110_RxFrameFormat Format, const DtNmosVideoFormat* Video,
                         char* Why, size_t Size)
{
    uint32_t Depth = 0;
    switch (Format)
    {
    case St2110_RxFrameFormat_Raw:
        return true;
    case St2110_RxFrameFormat_Uyvy422_8b:
    case St2110_RxFrameFormat_Yuv422p_8b:
        Depth = 8;
        break;
    case St2110_RxFrameFormat_Uyvy422_10b:
    case St2110_RxFrameFormat_Uyvy422_10b_to_8b:
        Depth = 10;
        break;
    }
    if (strcmp(Video->Sampling, "YCbCr-4:2:2") != 0)
    {
        snprintf(Why, Size, "%s takes YCbCr-4:2:2, and the flow is %.32s; use Raw",
                 RxFormatName(Format), Video->Sampling);
        return false;
    }
    if (Video->Depth != Depth)
    {
        const char* Others = Video->Depth == 8 ? "Uyvy422_8b, Yuv422p_8b or Raw"
                             : Video->Depth == 10
                                 ? "Uyvy422_10b, Uyvy422_10b_to_8b or Raw"
                                 : "Raw";
        snprintf(Why, Size, "%s takes depth %u, and the flow has %u; use %s",
                 RxFormatName(Format), Depth, Video->Depth, Others);
        return false;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SizeOfFlowIsKnown -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether the caller's DtNmosFlow is at least as large as the one the bridge was built
// with, so that every field it reads is there.
//
static bool SizeOfFlowIsKnown(const DtNmosFlow* Flow)
{
    return Flow->Size >= sizeof(DtNmosFlow);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Build +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtapiHasNmos -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtapiHasNmos(void)
{
    return 1;
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
    static const char* const Where = "DtNmosAvFifo_RxConfigFromFlow";
    if (Flow == NULL || IpPars == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No flow or IP parameters");
    if (!SizeOfFlowIsKnown(Flow))
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where,
                             "The flow's Size is smaller than sizeof(DtNmosFlow)");
    if (Flow->Leg != 0)
        return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where,
                             "The flow is the second path of ST 2022-7, which the FIFO "
                             "does not receive");

    char Why[160];
    St2110_AudioFormat AudioFormat = St2110_AudioFormat_Raw;
    switch (Flow->Media)
    {
    case DTNMOS_MEDIA_VIDEO:
        if (Video == NULL)
            return DtAvError_Set(DTAPI_E_INVALID_ARG, Where,
                                 "A video flow and no video configuration");
        if ((int)Format < 0 || (int)Format > (int)St2110_RxFrameFormat_Yuv422p_8b)
            return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "Invalid frame format");
        if (!RxVideoTakes(Format, &Flow->Format.Video, Why, sizeof(Why)))
            return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where, Why);
        break;
    case DTNMOS_MEDIA_AUDIO:
        if (Audio == NULL)
            return DtAvError_Set(DTAPI_E_INVALID_ARG, Where,
                                 "An audio flow and no audio configuration");
        if (!AudioFormatOf(Flow->Format.Audio.Encoding, &AudioFormat))
        {
            snprintf(Why, sizeof(Why),
                     "The flow's encoding %.16s is one the FIFO does not know",
                     Flow->Format.Audio.Encoding);
            return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where, Why);
        }
        break;
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
        return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where,
                             "The flow is ST 2110-22, which the FIFO does not decode");
    case DTNMOS_MEDIA_ANC:
        return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where,
                             "The flow is ST 2110-40, which the FIFO does not receive");
    default:
        snprintf(Why, sizeof(Why),
                 "The flow's encoding %.64s is one the FIFO does not know",
                 Flow->Format.Other.Encoding);
        return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where, Why);
    }

    AvFifo_IpPars Pars;
    DtapiResult Result = IpParsOf(Flow, &Pars, Where);
    if (Result != DTAPI_OK)
        return Result;

    // Nothing is written before everything has been checked.
    if (Flow->Media == DTNMOS_MEDIA_VIDEO)
    {
        Video->Format = Format;
    }
    else
    {
        Audio->Format = AudioFormat;
        Audio->SampleRate = (int)Flow->Format.Audio.SampleRate;
    }
    *IpPars = Pars;
    return DTAPI_OK;
}
