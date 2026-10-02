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
#include "AvFifo/DtAvRxFifo.h" // The port of a receive FIFO.
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AudioFormatOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The FIFO's format of the audio encoding Encoding, and false for one it does not know.
// A receiving FIFO delivers AM824 as it comes, as Raw.
//
static bool AudioFormatOf(DtNmosAudioEncoding Encoding, St2110_AudioFormat* Format)
{
    switch (Encoding)
    {
    case DTNMOS_AUDIO_ENCODING_L16:
        *Format = St2110_AudioFormat_L16BE;
        return true;
    case DTNMOS_AUDIO_ENCODING_L24:
        *Format = St2110_AudioFormat_L24BE;
        return true;
    case DTNMOS_AUDIO_ENCODING_AM824:
        *Format = St2110_AudioFormat_Raw;
        return true;
    case DTNMOS_AUDIO_ENCODING_NONE:
    case DTNMOS_AUDIO_ENCODING_OTHER:
        break;
    }
    return false;
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
    case DTNMOS_MEDIA_NONE:
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "The flow has no Media");
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ChildId -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The ID the bridge makes for a sender or receiver, Kind, of the device DeviceId with the
// label Label: the name-based UUID of "<kind>/<label>" in the device's ID, as dtcore
// makes its own, the same each time.
//
static DtNmosResult ChildId(const DtNmosId* DeviceId, const char* Kind, const char* Label,
                            DtNmosId* Id)
{
    char Name[256];
    snprintf(Name, sizeof(Name), "%s/%s", Kind, Label);
    return DtNmosId_FromName(DeviceId, Name, Id);
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NameOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The name of a value of a flow in a failure text: the enum's spelling Known, or, when
// the enum is _OTHER, Other, the words for a value dtnmos does not know, which the flow
// keeps in its OtherParameters; "none" for _NONE.
//
static const char* NameOf(const char* Known, bool Other)
{
    return Other ? "one dtnmos does not know" : Known[0] != '\0' ? Known : "none";
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PortAddress -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The address of a port Desc describes as text into Text, of Size bytes: its IPv4
// address when it has one, else its first IPv6 address. DTAPI_E_NO_ADAPTER_IP_ADDR when
// it has neither.
//
static DtapiResult PortAddress(const DtHwFuncDesc* Desc, char* Text, size_t Size,
                               const char* Where)
{
    static const uint8_t None[16] = {0};
    if (memcmp(Desc->Ip, None, sizeof(Desc->Ip)) != 0)
    {
        uint8_t Ip[16] = {0};
        memcpy(Ip, Desc->Ip, sizeof(Desc->Ip));
        return DtNmosAddr_Format(false, Ip, Text, Size);
    }
    for (int i = 0; i < MAX_IPV6_ADDR; i++)
    {
        if (memcmp(Desc->IpV6[i], None, 16) != 0)
            return DtNmosAddr_Format(true, Desc->IpV6[i], Text, Size);
    }
    return DtAvError_Set(DTAPI_E_NO_ADAPTER_IP_ADDR, Where,
                         "The FIFO's port has no IP address");
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
    if (Video->Sampling != DTNMOS_SAMPLING_YCBCR_422)
    {
        snprintf(Why, Size, "%s takes YCbCr-4:2:2, and the flow is %.32s; use Raw",
                 RxFormatName(Format),
                 NameOf(DtNmosSampling_Text(Video->Sampling),
                        Video->Sampling == DTNMOS_SAMPLING_OTHER));
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
        snprintf(Why, sizeof(Why), "The FIFO sends L16 and L24, and the flow is %s",
                 NameOf(DtNmosAudioEncoding_Text(Format->Encoding),
                        Format->Encoding == DTNMOS_AUDIO_ENCODING_OTHER));
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
    if (Format->Sampling != DTNMOS_SAMPLING_YCBCR_422 ||
        (Format->Depth != 8 && Format->Depth != 10))
    {
        snprintf(Why, sizeof(Why),
                 "The FIFO sends YCbCr-4:2:2 of depth 8 or 10, and the flow is %.32s of "
                 "depth %u",
                 NameOf(DtNmosSampling_Text(Format->Sampling),
                        Format->Sampling == DTNMOS_SAMPLING_OTHER),
                 Format->Depth);
        return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where, Why);
    }
    if (Format->Width == 0 || Format->Height == 0 || Format->RateNumerator == 0 ||
        Format->RateDenominator == 0)
    {
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where,
                             "The flow has no width, height or frame rate");
    }

    // A PM not given is General, as ST 2110-20 has it.
    St2110_PackingMode Packing = St2110_PackingMode_General;
    if (Format->PackingMode == DTNMOS_PACKING_MODE_BLOCK)
        Packing = St2110_PackingMode_Block;
    else if (Format->PackingMode == DTNMOS_PACKING_MODE_OTHER)
    {
        return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where,
                             "The flow's PM is one dtnmos does not know, and no packing "
                             "mode of the FIFO");
    }

    // A TP not given is gapped, and a wide sender's is sent narrow, gapped.
    St2110_Scheduling Scheduling = St2110_Scheduling_Gapped;
    if (Format->TransmitterType == DTNMOS_TRANSMITTER_TYPE_NARROW_LINEAR)
        Scheduling = St2110_Scheduling_Linear;
    else if (Format->TransmitterType == DTNMOS_TRANSMITTER_TYPE_OTHER)
    {
        return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where,
                             "The flow's TP is one dtnmos does not know, and no sender "
                             "type of the FIFO");
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_AddReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtNmosAvFifo_AddReceiver(DtNmosNode* Node, AvFifo_RxFifo* Fifo,
                                     const DtNmosReceiverConfig* Config,
                                     DtNmosReceiverActivateFunc Activate, void* User,
                                     DtNmosId* Id)
{
    static const char* const Where = "DtNmosAvFifo_AddReceiver";
    if (Node == NULL || Fifo == NULL || Config == NULL || Id == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No node, FIFO, config or ID");
    if (Config->Size < sizeof(DtNmosReceiverConfig))
        return DtAvError_Set(
            DTAPI_E_INVALID_ARG, Where,
            "The config's Size is smaller than sizeof(DtNmosReceiverConfig)");
    if (Config->DeviceId.Text[0] == '\0')
        return DtAvError_Set(
            DTAPI_E_INVALID_ARG, Where,
            "The config has no DeviceId, as DtNmosAvFifo_AddDevice gives");
    const bool HasLabel = Config->Label != NULL && Config->Label[0] != '\0';
    if (Config->Id.Text[0] == '\0' && !HasLabel)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where,
                             "A receiver without an ID needs a label to make one from");

    DtNmosReceiverConfig Filled = *Config;
    Filled.Size = sizeof(Filled);
    if (Filled.Id.Text[0] == '\0')
    {
        const DtNmosResult Made =
            ChildId(&Filled.DeviceId, "receiver", Filled.Label, &Filled.Id);
        if (Made != DTNMOS_OK)
            return FailDtNmos(Made, Where);
    }
    DtapiResult Result = DTAPI_OK;
    char InterfaceIp[DT_NMOS_ADDR_SIZE];
    if (Filled.InterfaceIp == NULL || Filled.InterfaceIp[0] == '\0')
    {
        DtHwFuncDesc Desc;
        Result = DtAvRxFifo_DescribePort(Fifo, &Desc, Where);
        if (Result == DTAPI_OK)
            Result = PortAddress(&Desc, InterfaceIp, sizeof(InterfaceIp), Where);
        Filled.InterfaceIp = InterfaceIp;
    }
    if (Result != DTAPI_OK)
        return Result;
    if (Filled.Description == NULL)
        Filled.Description = "";

    const DtNmosResult Added = DtNmosNode_AddReceiver(Node, &Filled, Activate, User);
    if (Added != DTNMOS_OK)
        return FailDtNmos(Added, Where);
    *Id = Filled.Id;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_AddSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtNmosAvFifo_AddSender(DtNmosNode* Node, AvFifo_TxFifo* Fifo,
                                   const DtNmosSenderConfig* Config,
                                   DtNmosSenderActivateFunc Activate, void* User,
                                   DtNmosId* Id)
{
    static const char* const Where = "DtNmosAvFifo_AddSender";
    if (Node == NULL || Fifo == NULL || Config == NULL || Id == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No node, FIFO, config or ID");
    if (Config->Size < sizeof(DtNmosSenderConfig))
        return DtAvError_Set(
            DTAPI_E_INVALID_ARG, Where,
            "The config's Size is smaller than sizeof(DtNmosSenderConfig)");
    if (Config->DeviceId.Text[0] == '\0')
        return DtAvError_Set(
            DTAPI_E_INVALID_ARG, Where,
            "The config has no DeviceId, as DtNmosAvFifo_AddDevice gives");
    const bool HasLabel = Config->Label != NULL && Config->Label[0] != '\0';
    if (Config->Id.Text[0] == '\0' && !HasLabel)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where,
                             "A sender without an ID needs a label to make one from");

    DtNmosSenderConfig Filled = *Config;
    Filled.Size = sizeof(Filled);
    if (Filled.Id.Text[0] == '\0')
    {
        const DtNmosResult Made =
            ChildId(&Filled.DeviceId, "sender", Filled.Label, &Filled.Id);
        if (Made != DTNMOS_OK)
            return FailDtNmos(Made, Where);
    }

    // The flow of the FIFO, which gives the address Start sends from as its source.
    DtNmosFlow Flow;
    const bool MakesFlow = Filled.Flow == NULL;
    const bool MakesSource = Filled.SourceIp == NULL || Filled.SourceIp[0] == '\0';
    DtapiResult Result = DTAPI_OK;
    if (MakesFlow || MakesSource)
        Result = DtNmosAvFifo_FlowFromTxFifo(Fifo, &Flow);
    if (Result != DTAPI_OK)
        return Result;
    if (MakesFlow)
        Filled.Flow = &Flow;
    if (MakesSource)
        Filled.SourceIp = Flow.SourceIp;
    if (Filled.Description == NULL)
        Filled.Description = "";

    const DtNmosResult Added = DtNmosNode_AddSender(Node, &Filled, Activate, User);
    if (Added != DTNMOS_OK)
        return FailDtNmos(Added, Where);
    *Id = Filled.Id;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Activations +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_ApplyRxChange -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtNmosAvFifo_ApplyRxChange(AvFifo_RxFifo* Fifo,
                                       const DtNmosAvFifoRxChange* Change)
{
    static const char* const Where = "DtNmosAvFifo_ApplyRxChange";
    if (Fifo == NULL || Change == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO or change");
    if (Change->Size < sizeof(DtNmosAvFifoRxChange))
        return DtAvError_Set(
            DTAPI_E_INVALID_ARG, Where,
            "The change's Size is smaller than sizeof(DtNmosAvFifoRxChange)");
    if (Change->MasterEnable && Change->HasConfig &&
        Change->Media != DTNMOS_MEDIA_VIDEO && Change->Media != DTNMOS_MEDIA_AUDIO)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where,
                             "The change's configuration is of neither video nor audio");

    DtapiResult Result = AvFifo_RxFifo_Stop(Fifo);
    if (Result != DTAPI_OK || !Change->MasterEnable)
        return Result;
    if (Change->HasConfig)
    {
        Result = Change->Media == DTNMOS_MEDIA_VIDEO
                     ? AvFifo_RxFifo_ConfigureVideo(Fifo, &Change->Video)
                     : AvFifo_RxFifo_ConfigureAudio(Fifo, &Change->Audio);
    }
    if (Result == DTAPI_OK)
        Result = AvFifo_RxFifo_SetIpPars(Fifo, &Change->IpPars);
    if (Result == DTAPI_OK)
        Result = AvFifo_RxFifo_Start(Fifo);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_ApplyTxChange -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The FIFO's IP parameters are read before it is stopped, so that a FIFO without them
// keeps sending what it sends.
//
DtapiResult DtNmosAvFifo_ApplyTxChange(AvFifo_TxFifo* Fifo,
                                       const DtNmosAvFifoTxChange* Change)
{
    static const char* const Where = "DtNmosAvFifo_ApplyTxChange";
    if (Fifo == NULL || Change == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO or change");
    if (Change->Size < sizeof(DtNmosAvFifoTxChange))
        return DtAvError_Set(
            DTAPI_E_INVALID_ARG, Where,
            "The change's Size is smaller than sizeof(DtNmosAvFifoTxChange)");
    if (!Change->MasterEnable)
        return AvFifo_TxFifo_Stop(Fifo);

    AvFifo_IpPars IpPars;
    DtapiResult Result = DtAvTxFifo_GetIpPars(Fifo, &IpPars, Where);
    if (Result != DTAPI_OK)
        return Result;
    memcpy(IpPars.IpAddr, Change->DestinationIp, sizeof(IpPars.IpAddr));
    IpPars.IpVersion = Change->IpVersion;
    IpPars.Port = Change->DestinationPort;
    Result = AvFifo_TxFifo_Stop(Fifo);
    if (Result == DTAPI_OK)
        Result = AvFifo_TxFifo_SetIpPars(Fifo, &IpPars);
    if (Result == DTAPI_OK)
        Result = AvFifo_TxFifo_Start(Fifo);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_RxChangeFromActivation -.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult
DtNmosAvFifo_RxChangeFromActivation(const DtNmosReceiverActivation* Activation,
                                    St2110_RxFrameFormat Format,
                                    DtNmosAvFifoRxChange* Change)
{
    static const char* const Where = "DtNmosAvFifo_RxChangeFromActivation";
    if (Activation == NULL || Change == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No activation or change");
    DtNmosAvFifoRxChange Made;
    memset(&Made, 0, sizeof(Made));
    Made.Size = sizeof(Made);
    Made.MasterEnable = Activation->MasterEnable;
    Made.AtNs = Activation->AtNs;
    if (Activation->MasterEnable)
    {
        const DtNmosFlow* Flow = &Activation->Flow;
        DtapiResult Result = DTAPI_OK;
        if (Activation->HasFlow)
        {
            // The configuration of the flow's media alone is made; the other stays zero.
            Result = DtNmosAvFifo_RxConfigFromFlow(Flow, Format, &Made.Video, &Made.Audio,
                                                   &Made.IpPars);
            Made.HasConfig = true;
            Made.Media = Flow->Media;
        }
        else
        {
            Result = CheckFlow(Flow, Where);
            if (Result == DTAPI_OK)
                Result = IpParsOf(Flow, &Made.IpPars, Where);
        }
        if (Result != DTAPI_OK)
            return Result;
    }
    *Change = Made;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_TxChangeFromActivation -.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtNmosAvFifo_TxChangeFromActivation(const DtNmosSenderActivation* Activation,
                                                DtNmosAvFifoTxChange* Change)
{
    static const char* const Where = "DtNmosAvFifo_TxChangeFromActivation";
    if (Activation == NULL || Change == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No activation or change");
    DtNmosAvFifoTxChange Made;
    memset(&Made, 0, sizeof(Made));
    Made.Size = sizeof(Made);
    Made.MasterEnable = Activation->MasterEnable;
    Made.AtNs = Activation->AtNs;
    if (Activation->MasterEnable)
    {
        bool IpV6 = false;
        const DtapiResult Result =
            DtNmosAddr_Parse(Activation->DestinationIp, Made.DestinationIp, &IpV6);
        if (Result != DTAPI_OK)
            return FailAddress(Result, Where, "destination", Activation->DestinationIp);
        if (Activation->DestinationPort == 0)
            return DtAvError_Set(DTAPI_E_INVALID_ARG, Where,
                                 "The activation has no destination port");
        Made.IpVersion = IpV6 ? IpProtocolVersion_IPv6 : IpProtocolVersion_IPv4;
        Made.DestinationPort = Activation->DestinationPort;
    }
    *Change = Made;
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
        Video->Sampling = DTNMOS_SAMPLING_YCBCR_422;
        Video->PackingMode = Config->Packing.PackingMode == St2110_PackingMode_Block
                                 ? DTNMOS_PACKING_MODE_BLOCK
                                 : DTNMOS_PACKING_MODE_GENERAL;
        Video->TransmitterType = Config->Timing.Scheduling == St2110_Scheduling_Linear
                                     ? DTNMOS_TRANSMITTER_TYPE_NARROW_LINEAR
                                     : DTNMOS_TRANSMITTER_TYPE_NARROW;
        snprintf(Video->Ssn, sizeof(Video->Ssn), "ST2110-20:2017");
        DtNmosVideoFormat_SetDefaults(Video);
    }
    else
    {
        const St2110_TxConfigAudio* Config = &Description.Audio;
        DtNmosAudioFormat* Audio = &Made.Format.Audio;
        Made.Media = DTNMOS_MEDIA_AUDIO;
        Made.ClockRate = (uint32_t)Config->SampleRate;
        Audio->Encoding = Config->Format == St2110_AudioFormat_L16BE
                              ? DTNMOS_AUDIO_ENCODING_L16
                              : DTNMOS_AUDIO_ENCODING_L24;
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
                     "The flow's encoding %s is one the FIFO does not know",
                     NameOf(DtNmosAudioEncoding_Text(Flow->Format.Audio.Encoding),
                            Flow->Format.Audio.Encoding == DTNMOS_AUDIO_ENCODING_OTHER));
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_UpdateSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtNmosAvFifo_UpdateSender(DtNmosNode* Node, const DtNmosId* Id,
                                      AvFifo_TxFifo* Fifo)
{
    static const char* const Where = "DtNmosAvFifo_UpdateSender";
    if (Node == NULL || Id == NULL || Fifo == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No node, ID or FIFO");
    DtNmosFlow Flow;
    const DtapiResult Result = DtNmosAvFifo_FlowFromTxFifo(Fifo, &Flow);
    if (Result != DTAPI_OK)
        return Result;
    const DtNmosResult Updated = DtNmosNode_UpdateSender(Node, Id, &Flow);
    return Updated == DTNMOS_OK ? DTAPI_OK : FailDtNmos(Updated, Where);
}
