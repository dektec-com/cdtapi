// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtNmosAvFifo.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The NMOS bridge of the AV FIFO, built with CDTAPI_WITH_NMOS
//
// SPDX-License-Identifier: BSD-3-Clause
//
// A build without the option compiles DtNmosAvFifoStub.c instead, which exports the same
// functions, so that every build of the library has the same exports. The mapping of a
// flow and a FIFO's configuration follows gst-dektec's, so that the two write the same
// SDP for the same stream.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <string.h>

// CDTAPI includes
#include "AvFifo/DtAvError.h"  // The failure text.
#include "AvFifo/DtAvTxFifo.h" // What a transmit FIFO is configured with.
#include "Device/DtDevice.h"   // A device's ports.
#include "DtNmosAddr.h"        // Addresses.
#include "cdtapi_constants.h"  // Result codes.
#include "cdtapi_nmos.h"       // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The DiffServ field of every flow: AF41, as DTAPI and the examples set it.
#define DT_NMOS_DIFFSERV 0x88

// The time to live of every flow, as dtnmos writes it into the c= line of an SDP.
#define DT_NMOS_TIME_TO_LIVE 64

// The packet time of an audio flow whose SDP gives none: 1 ms, level A of ST 2110-30.
#define DT_NMOS_DEFAULT_PACKET_TIME_NS 1000000

#define DT_NMOS_NS_PER_SEC UINT64_C(1000000000)

// The only sampling a FIFO converts or sends.
#define DT_NMOS_SAMPLING_422 "YCbCr-4:2:2"

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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckFlow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// What no FIFO takes, receiving or sending: a flow smaller than the bridge's, the
// second path of ST 2022-7, and a media other than video and audio.
//
static DtapiResult CheckFlow(const DtNmosFlow* Flow, const char* Where)
{
    if (Flow->Size < sizeof(DtNmosFlow))
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where,
                             "The flow's Size is smaller than sizeof(DtNmosFlow)");
    if (Flow->Leg != 0)
        return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where,
                             "The flow is the second path of ST 2022-7, which the FIFO "
                             "does not receive or send");
    char Why[160];
    switch (Flow->Media)
    {
    case DTNMOS_MEDIA_VIDEO:
    case DTNMOS_MEDIA_AUDIO:
        return DTAPI_OK;
    case DTNMOS_MEDIA_COMPRESSED_VIDEO:
        return DtAvError_Set(
            DTAPI_E_NOT_SUPPORTED, Where,
            "The flow is ST 2110-22, which the FIFO does not encode or decode");
    case DTNMOS_MEDIA_ANC:
        return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where,
                             "The flow is ST 2110-40, which the FIFO does not receive or "
                             "send");
    default:
        snprintf(Why, sizeof(Why),
                 "The flow's encoding %.64s is one the FIFO does not know",
                 Flow->Format.Other.Encoding);
        return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where, Why);
    }
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FailDtNmos -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Records a failure of dtnmos, Result, with dtnmos's message, and returns the result of
// CDTAPI that matches it. Both texts belong to the calling thread.
//
static DtapiResult FailDtNmos(DtNmosResult Result, const char* Where)
{
    DtapiResult Mapped = DTAPI_E_INTERNAL;
    switch (Result)
    {
    case DTNMOS_E_INVALID_ARGUMENT:
    case DTNMOS_E_PARSE:
        Mapped = DTAPI_E_INVALID_ARG;
        break;
    case DTNMOS_E_NOT_FOUND:
        Mapped = DTAPI_E_NOT_FOUND;
        break;
    case DTNMOS_E_STATE:
        Mapped = DTAPI_E_STATE;
        break;
    case DTNMOS_E_NO_MEMORY:
        Mapped = DTAPI_E_OUT_OF_MEM;
        break;
    case DTNMOS_E_TIMEOUT:
        Mapped = DTAPI_E_TIMEOUT;
        break;
    case DTNMOS_E_HTTP:
    case DTNMOS_E_NETWORK:
        Mapped = DTAPI_E_COMMUNICATION;
        break;
    case DTNMOS_E_BUFFER_TOO_SMALL:
        Mapped = DTAPI_E_BUF_TOO_SMALL;
        break;
    default:
        break;
    }
    return DtAvError_Set(Mapped, Where, DtNmos_GetLastError());
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
    if (strcmp(Video->Sampling, DT_NMOS_SAMPLING_422) != 0)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TxAudioOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The configuration of a transmitting FIFO for the audio of Flow, or the failure.
//
static DtapiResult TxAudioOf(const DtNmosFlow* Flow, St2110_TxConfigAudio* Audio,
                             const char* Where)
{
    const DtNmosAudioFormat* Format = &Flow->Format.Audio;
    char Why[160];
    St2110_AudioFormat AudioFormat = St2110_AudioFormat_Raw;
    if (!AudioFormatOf(Format->Encoding, &AudioFormat) ||
        AudioFormat == St2110_AudioFormat_Raw)
    {
        snprintf(Why, sizeof(Why), "The FIFO sends L16 and L24, and the flow is %.16s",
                 Format->Encoding);
        return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where, Why);
    }
    if (Format->SampleRate == 0 || Format->Channels == 0)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where,
                             "The flow has no sample rate or no channels");
    const uint64_t PacketTimeNs =
        Format->PacketTimeNs != 0 ? Format->PacketTimeNs : DT_NMOS_DEFAULT_PACKET_TIME_NS;
    const uint64_t Product = (uint64_t)Format->SampleRate * PacketTimeNs;
    if (Product % DT_NMOS_NS_PER_SEC != 0 || Product / DT_NMOS_NS_PER_SEC == 0)
    {
        snprintf(Why, sizeof(Why),
                 "A packet time of %llu ns at %u Hz is no whole number of samples",
                 (unsigned long long)PacketTimeNs, Format->SampleRate);
        return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where, Why);
    }
    memset(Audio, 0, sizeof(*Audio));
    Audio->Format = AudioFormat;
    Audio->NumChannels = (int)Format->Channels;
    Audio->NumSamplesPerIpPacket = (int)(Product / DT_NMOS_NS_PER_SEC);
    Audio->SampleRate = (int)Format->SampleRate;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TxVideoOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The configuration of a transmitting FIFO for the video of Flow, or the failure. The
// frame rate of interlaced and PsF video is doubled into the field rate CDTAPI has.
//
static DtapiResult TxVideoOf(const DtNmosFlow* Flow, St2110_TxConfigVideo* Video,
                             const char* Where)
{
    const DtNmosVideoFormat* Format = &Flow->Format.Video;
    char Why[160];
    if (strcmp(Format->Sampling, DT_NMOS_SAMPLING_422) != 0 ||
        (Format->Depth != 8 && Format->Depth != 10))
    {
        snprintf(Why, sizeof(Why),
                 "The FIFO sends YCbCr-4:2:2 of depth 8 or 10, and the flow is %.32s of "
                 "depth %u",
                 Format->Sampling, Format->Depth);
        return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where, Why);
    }
    if (Format->Width == 0 || Format->Height == 0 || Format->RateNumerator == 0 ||
        Format->RateDenominator == 0)
    {
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where,
                             "The flow has no width, height or frame rate");
    }

    St2110_PackingMode Packing = St2110_PackingMode_General;
    if (strcmp(Format->PackingMode, "2110BPM") == 0)
        Packing = St2110_PackingMode_Block;
    else if (Format->PackingMode[0] != '\0' &&
             strcmp(Format->PackingMode, "2110GPM") != 0)
    {
        snprintf(Why, sizeof(Why), "The flow's PM %.16s is no packing mode of the FIFO",
                 Format->PackingMode);
        return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where, Why);
    }

    St2110_Scheduling Scheduling = St2110_Scheduling_Gapped;
    if (strcmp(Format->TransmitterType, "2110TPNL") == 0)
        Scheduling = St2110_Scheduling_Linear;
    else if (Format->TransmitterType[0] != '\0' &&
             strcmp(Format->TransmitterType, "2110TPN") != 0 &&
             strcmp(Format->TransmitterType, "2110TPW") != 0)
    {
        snprintf(Why, sizeof(Why), "The flow's TP %.16s is no sender type of the FIFO",
                 Format->TransmitterType);
        return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where, Why);
    }

    St2110_VideoScanning Scanning = St2110_VideoScanning_Progressive;
    if (Format->Segmented)
        Scanning = St2110_VideoScanning_PsF;
    else if (Format->Interlaced)
        Scanning = St2110_VideoScanning_Interlaced;
    const uint32_t Fields = Scanning == St2110_VideoScanning_Progressive ? 1 : 2;

    memset(Video, 0, sizeof(*Video));
    Video->Format = Format->Depth == 10 ? St2110_TxFrameFormat_Uyvy422_10b
                                        : St2110_TxFrameFormat_Uyvy422_8b;
    Video->Packing.PackingMode = Packing;
    Video->Packing.PayloadSize = -1;
    Video->Resolution.Width = (int)Format->Width;
    Video->Resolution.Height = (int)Format->Height;
    Video->Timing.Rate.Numerator = (int)(Format->RateNumerator * Fields);
    Video->Timing.Rate.Denominator = (int)Format->RateDenominator;
    Video->Timing.Scheduling = Scheduling;
    Video->Timing.VideoScanning = Scanning;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Build +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtapiHasNmos -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtapiHasNmos(void)
{
    return true;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Nodes +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_AddDevice -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The ID the bridge makes is a name-based UUID, version 5 of RFC 9562: the SHA-1 of the
// node's ID and "device/cdtapi/<serial>:<port>", which gives the same ID for the same
// node, card and port every time, as dtcore derives its own. The label puts the serial
// number between the type and the port of the port's name, "DTA-2110 port 1", so that
// two cards of a type tell apart.
//
DtapiResult DtNmosAvFifo_AddDevice(DtNmosNode* Node, const DtDevice* Device, int Port,
                                   const DtNmosDeviceConfig* Config, DtNmosId* Id)
{
    static const char* const Where = "DtNmosAvFifo_AddDevice";
    if (Node == NULL || Id == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No node or ID");
    if (Device == NULL || Device->Drv == NULL)
        return DtAvError_Set(DTAPI_E_DEVICE, Where, "No device, or one not attached");
    if (Port < 1 || Port > Device->NumPublicPorts)
        return DtAvError_Set(DTAPI_E_NO_SUCH_PORT, Where, "The device has no such port");
    if (!DtDevice_PortHasAllCaps(Device, Port, DT_CAP_AVFIFO))
        return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where, "The port has no AV FIFO");
    if (Config != NULL && Config->Size < sizeof(DtNmosDeviceConfig))
        return DtAvError_Set(
            DTAPI_E_INVALID_ARG, Where,
            "The config's Size is smaller than sizeof(DtNmosDeviceConfig)");

    DtNmosDeviceConfig Filled;
    memset(&Filled, 0, sizeof(Filled));
    if (Config != NULL)
        Filled = *Config;
    Filled.Size = sizeof(Filled);
    DtNmosResult Result = DTNMOS_OK;
    if (Filled.Id.Text[0] == '\0')
    {
        DtNmosId NodeId;
        Result = DtNmosNode_Id(Node, &NodeId);
        if (Result != DTNMOS_OK)
            return FailDtNmos(Result, Where);
        char Name[64];
        snprintf(Name, sizeof(Name), "device/cdtapi/%lld:%d",
                 (long long)Device->Info.Serial, Port);
        Result = DtNmosId_FromName(&NodeId, Name, &Filled.Id);
        if (Result != DTNMOS_OK)
            return FailDtNmos(Result, Where);
    }

    DtHwFuncDesc Desc;
    DtDevice_DescribeHwFunc(Device, Port, &Desc);
    char Label[96];
    const char* PortText = strstr(Desc.Description, " port ");
    const int TypeLength = PortText != NULL ? (int)(PortText - Desc.Description)
                                            : (int)strlen(Desc.Description);
    snprintf(Label, sizeof(Label), "%.*s %lld port %d", TypeLength, Desc.Description,
             (long long)Device->Info.Serial, Port);
    if (Filled.Label == NULL || Filled.Label[0] == '\0')
        Filled.Label = Label;
    if (Filled.Description == NULL)
        Filled.Description = "";

    Result = DtNmosNode_AddDevice(Node, &Filled);
    if (Result != DTNMOS_OK)
        return FailDtNmos(Result, Where);
    *Id = Filled.Id;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Flows +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_FlowFromTxFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The frame rate of interlaced and PsF video is half the field rate CDTAPI has: the
// numerator halved when it is even, the denominator doubled when not.
//
DtapiResult DtNmosAvFifo_FlowFromTxFifo(AvFifo_TxFifo* Fifo, DtNmosFlow* Flow)
{
    static const char* const Where = "DtNmosAvFifo_FlowFromTxFifo";
    if (Fifo == NULL || Flow == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO or flow");
    DtAvTxFifoDescription Description;
    DtapiResult Result = DtAvTxFifo_Describe(Fifo, &Description, Where);
    if (Result != DTAPI_OK)
        return Result;
    if (Description.Kind == DT_AV_KIND_AUDIO &&
        Description.Audio.Format == St2110_AudioFormat_Raw)
        return DtAvError_Set(
            DTAPI_E_NOT_SUPPORTED, Where,
            "The FIFO sends audio of Raw, whose encoding it does not know");

    DtNmosFlow Made;
    memset(&Made, 0, sizeof(Made));
    Made.Size = sizeof(Made);
    const bool IpV6 = Description.IpPars.IpVersion == IpProtocolVersion_IPv6;
    DtNmosAddr_Format(IpV6, Description.IpPars.IpAddr, Made.DestinationIp,
                      sizeof(Made.DestinationIp));
    DtNmosAddr_Format(IpV6, Description.SourceIp, Made.SourceIp, sizeof(Made.SourceIp));
    Made.DestinationPort = (uint16_t)Description.IpPars.Port;
    Made.PayloadType = (uint8_t)Description.IpPars.RtpPayloadType;
    Made.RefClock.Kind = DTNMOS_REFCLOCK_LOCALMAC;
    Made.RefClock.Domain = -1;
    const uint8_t* Mac = Description.Mac;
    snprintf(Made.RefClock.LocalMac, sizeof(Made.RefClock.LocalMac),
             "%02X-%02X-%02X-%02X-%02X-%02X", Mac[0], Mac[1], Mac[2], Mac[3], Mac[4],
             Mac[5]);
    Made.MediaClockDirect = true;

    if (Description.Kind == DT_AV_KIND_VIDEO)
    {
        const St2110_TxConfigVideo* Config = &Description.Video;
        DtNmosVideoFormat* Video = &Made.Format.Video;
        Made.Media = DTNMOS_MEDIA_VIDEO;
        Made.ClockRate = 90000;
        Video->Width = (uint32_t)Config->Resolution.Width;
        Video->Height = (uint32_t)Config->Resolution.Height;
        uint32_t Numerator = (uint32_t)Config->Timing.Rate.Numerator;
        uint32_t Denominator = (uint32_t)Config->Timing.Rate.Denominator;
        const St2110_VideoScanning Scanning = Config->Timing.VideoScanning;
        if (Scanning != St2110_VideoScanning_Progressive)
        {
            if (Numerator % 2 == 0)
                Numerator /= 2;
            else
                Denominator *= 2;
            Video->Interlaced = true;
            Video->Segmented = Scanning == St2110_VideoScanning_PsF;
        }
        Video->RateNumerator = Numerator;
        Video->RateDenominator = Denominator;
        Video->Depth = Config->Format == St2110_TxFrameFormat_Uyvy422_10b ? 10 : 8;
        snprintf(Video->Sampling, sizeof(Video->Sampling), DT_NMOS_SAMPLING_422);
        snprintf(Video->PackingMode, sizeof(Video->PackingMode), "%s",
                 Config->Packing.PackingMode == St2110_PackingMode_Block ? "2110BPM"
                                                                         : "2110GPM");
        snprintf(Video->TransmitterType, sizeof(Video->TransmitterType), "%s",
                 Config->Timing.Scheduling == St2110_Scheduling_Linear ? "2110TPNL"
                                                                       : "2110TPN");
        snprintf(Video->Ssn, sizeof(Video->Ssn), "ST2110-20:2017");
        DtNmosVideoFormat_SetDefaults(Video);
    }
    else
    {
        const St2110_TxConfigAudio* Config = &Description.Audio;
        DtNmosAudioFormat* Audio = &Made.Format.Audio;
        Made.Media = DTNMOS_MEDIA_AUDIO;
        Made.ClockRate = (uint32_t)Config->SampleRate;
        snprintf(Audio->Encoding, sizeof(Audio->Encoding), "%s",
                 Config->Format == St2110_AudioFormat_L16BE ? "L16" : "L24");
        Audio->SampleRate = (uint32_t)Config->SampleRate;
        Audio->Channels = (uint32_t)Config->NumChannels;
        Audio->PacketTimeNs =
            (uint32_t)((uint64_t)Config->NumSamplesPerIpPacket * DT_NMOS_NS_PER_SEC /
                       (uint64_t)Config->SampleRate);
    }
    *Flow = Made;
    return DTAPI_OK;
}

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
    DtapiResult Result = CheckFlow(Flow, Where);
    if (Result != DTAPI_OK)
        return Result;

    char Why[160];
    St2110_AudioFormat AudioFormat = St2110_AudioFormat_Raw;
    if (Flow->Media == DTNMOS_MEDIA_VIDEO)
    {
        if (Video == NULL)
            return DtAvError_Set(DTAPI_E_INVALID_ARG, Where,
                                 "A video flow and no video configuration");
        if ((int)Format < 0 || (int)Format > (int)St2110_RxFrameFormat_Yuv422p_8b)
            return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "Invalid frame format");
        if (!RxVideoTakes(Format, &Flow->Format.Video, Why, sizeof(Why)))
            return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where, Why);
    }
    else
    {
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
    }

    AvFifo_IpPars Pars;
    Result = IpParsOf(Flow, &Pars, Where);
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_TxConfigFromFlow -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A sender's SourceIp is its own address, which the FIFO chooses, so the IP parameters
// get no source filter.
//
DtapiResult DtNmosAvFifo_TxConfigFromFlow(const DtNmosFlow* Flow,
                                          St2110_TxConfigVideo* Video,
                                          St2110_TxConfigAudio* Audio,
                                          AvFifo_IpPars* IpPars)
{
    static const char* const Where = "DtNmosAvFifo_TxConfigFromFlow";
    if (Flow == NULL || IpPars == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No flow or IP parameters");
    DtapiResult Result = CheckFlow(Flow, Where);
    if (Result != DTAPI_OK)
        return Result;

    St2110_TxConfigVideo VideoConfig = {0};
    St2110_TxConfigAudio AudioConfig = {0};
    if (Flow->Media == DTNMOS_MEDIA_VIDEO)
    {
        if (Video == NULL)
            return DtAvError_Set(DTAPI_E_INVALID_ARG, Where,
                                 "A video flow and no video configuration");
        Result = TxVideoOf(Flow, &VideoConfig, Where);
    }
    else
    {
        if (Audio == NULL)
            return DtAvError_Set(DTAPI_E_INVALID_ARG, Where,
                                 "An audio flow and no audio configuration");
        Result = TxAudioOf(Flow, &AudioConfig, Where);
    }
    if (Result != DTAPI_OK)
        return Result;

    DtNmosFlow WithoutSource = *Flow;
    WithoutSource.SourceIp[0] = '\0';
    AvFifo_IpPars Pars;
    Result = IpParsOf(&WithoutSource, &Pars, Where);
    if (Result != DTAPI_OK)
        return Result;

    // Nothing is written before everything has been checked.
    if (Flow->Media == DTNMOS_MEDIA_VIDEO)
        *Video = VideoConfig;
    else
        *Audio = AudioConfig;
    *IpPars = Pars;
    return DTAPI_OK;
}
