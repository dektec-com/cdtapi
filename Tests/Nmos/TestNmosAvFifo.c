// #*#*#*#*#*#*#*#*#*#*#*#*#* TestNmosAvFifo.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The NMOS bridge of the AV FIFO, in a build with CDTAPI_WITH_NMOS
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The flows are read from SDPs by dtnmos's parser, as a program gets them, except those
// of a media the parser takes and the FIFO does not, which are made by hand.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtTest.h"             // Test framework.
#include "OAL/OsThread.h"       // The owner of a FIFO and its mailbox.
#include "OAL/Sim/SimDtPcie.h"  // The emulated devices.
#include "OAL/Sim/SimDta2110.h" // The emulated DTA-2110's serial number.
#include "cdtapi_constants.h"   // Result codes.
#include "cdtapi_nmos.h"        // Functions under test.

// dtnmos includes
#include "dtnmos_node.h" // DtNmos_HasServer, through the library's own link.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The session of every SDP here, from the sender 192.168.39.10.
#define SESSION                                                                          \
    "v=0\r\n"                                                                            \
    "o=- 1 1 IN IP4 192.168.39.10\r\n"                                                   \
    "s=Test\r\n"                                                                         \
    "t=0 0\r\n"

// A video flow to 239.1.2.3:5004 from 192.168.39.10, with the a=fmtp given.
#define VIDEO_FMTP(Fmtp)                                                                 \
    SESSION "m=video 5004 RTP/AVP 96\r\n"                                                \
            "c=IN IP4 239.1.2.3/64\r\n"                                                  \
            "a=source-filter: incl IN IP4 239.1.2.3 192.168.39.10\r\n"                   \
            "a=rtpmap:96 raw/90000\r\n"                                                  \
            "a=fmtp:96 " Fmtp "\r\n"                                                     \
            "a=mediaclk:direct=0\r\n"

// A 1080p25 video flow of the sampling and depth given.
#define VIDEO(Sampling, Depth)                                                           \
    VIDEO_FMTP("sampling=" Sampling "; width=1920; height=1080; exactframerate=25; "     \
               "depth=" Depth "; TCS=SDR; colorimetry=BT709; PM=2110GPM; "               \
               "SSN=ST2110-20:2017; TP=2110TPN")

// An audio flow of two channels at 48 kHz to 239.1.2.4:5006, of the encoding given.
#define AUDIO(Encoding)                                                                  \
    SESSION "m=audio 5006 RTP/AVP 97\r\n"                                                \
            "c=IN IP4 239.1.2.4/64\r\n"                                                  \
            "a=rtpmap:97 " Encoding "/48000/2\r\n"                                       \
            "a=ptime:1\r\n"                                                              \
            "a=mediaclk:direct=0\r\n"

// What a case holds: an SDP, and the emulated DTA-2110 with a transmit FIFO.
typedef struct Fixture
{
    DtNmosSdp* Sdp;
    DtNmosNode* Node;
    DtDevice* Device;
    AvFifo_TxFifo* Tx;
    AvFifo_RxFifo* Rx;
} Fixture;

static void FreeFixture(void* Context)
{
    Fixture* Fix = (Fixture*)Context;
    DtNmosSdp_Free(Fix->Sdp);
    Fix->Sdp = NULL;
    DtNmosNode_Freep(&Fix->Node);
    AvFifo_TxFifo_Freep(&Fix->Tx);
    AvFifo_RxFifo_Freep(&Fix->Rx);
    DtDevice_Freep(&Fix->Device);
}

// The ID of the nodes here.
#define NODE_ID "aaaaaaaa-0000-4000-8000-000000000001"

// The registry of the nodes here: it takes every registration and every heartbeat.
static DtNmosResult RegistryHttp(void* User, const DtNmosHttpRequest* Request,
                                 DtNmosHttpResponse* Response)
{
    (void)User;
    const bool Posts =
        strcmp(Request->Method, "POST") == 0 && strstr(Request->Url, "/resource") != NULL;
    DtNmosHttpResponse_SetStatus(Response, Posts ? 201 : 200);
    return DTNMOS_OK;
}

// Opens the fixture's node on the registry stub; false, failing the case, when not.
static bool OpenNode(Fixture* Fix, int* DtFailures)
{
    DtNmosNodeConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    snprintf(Config.Id.Text, sizeof(Config.Id.Text), "%s", NODE_ID);
    Config.Label = "bridge node";
    Config.ApiHost = "192.168.1.10";
    Config.ApiPort = 8080;
    Config.RegistrationUrl = "http://registry.test";
    Config.Http = RegistryHttp;
    Fix->Node = DtNmosNode_Alloc();
    if (Fix->Node == NULL || DtNmosNode_Open(Fix->Node, &Config) != DTNMOS_OK)
    {
        printf("    FAIL: no node: %s\n", DtNmos_GetLastError());
        (*DtFailures)++;
        return false;
    }
    return true;
}

// The label of the device Id of Node, as the Node API answers it, into Label.
static void DeviceLabel(DtNmosNode* Node, const DtNmosId* Id, char* Label, size_t Size)
{
    char Url[128];
    snprintf(Url, sizeof(Url), "/x-nmos/node/v1.3/devices/%s", Id->Text);
    DtNmosHttpRequest Request;
    memset(&Request, 0, sizeof(Request));
    Request.Size = sizeof(Request);
    Request.Method = "GET";
    Request.Url = Url;
    DtNmosHttpResponse* Response = DtNmosHttpResponse_Alloc();
    Label[0] = '\0';
    if (Response != NULL && DtNmosNode_Handle(Node, &Request, Response) == DTNMOS_OK)
    {
        size_t Length = 0;
        const char* Body = DtNmosHttpResponse_Body(Response, &Length);
        const char* Start = Body != NULL ? strstr(Body, "\"label\": \"") : NULL;
        if (Start != NULL)
        {
            Start += strlen("\"label\": \"");
            const char* End = strchr(Start, '"');
            if (End != NULL)
                snprintf(Label, Size, "%.*s", (int)(End - Start), Start);
        }
    }
    DtNmosHttpResponse_Free(Response);
}

// Parses Text into the fixture and returns its flow at Index, or NULL.
static const DtNmosFlow* FlowOf(Fixture* Fix, const char* Text, size_t Index)
{
    DtNmosSdp_Free(Fix->Sdp);
    Fix->Sdp = NULL;
    if (DtNmosSdp_Parse(Text, strlen(Text), &Fix->Sdp) != DTNMOS_OK)
        return NULL;
    return DtNmosSdp_Flow(Fix->Sdp, Index);
}

// A flow made by hand: Size set and the rest zero, to 239.1.2.3:5004.
static DtNmosFlow HandFlow(DtNmosMedia Media)
{
    DtNmosFlow Flow;
    memset(&Flow, 0, sizeof(Flow));
    Flow.Size = sizeof(Flow);
    Flow.Media = Media;
    snprintf(Flow.DestinationIp, sizeof(Flow.DestinationIp), "239.1.2.3");
    Flow.DestinationPort = 5004;
    return Flow;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Build +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The library has the bridge, and its interface brings dtnmos: its header and its
// library, which a program gets from linking CDTAPI alone.
DT_TEST(LinksDtnmos)
{
    DT_ASSERT(DtapiHasNmos());
    DT_ASSERT(DtNmos_HasServer() == 0 || DtNmos_HasServer() == 1);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Receive: video +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// A 10-bit 4:2:2 flow for a FIFO that converts it, and the IP parameters every flow
// gets: its group, port, source and payload type, RTP, AF41 and a time to live of 64.
DT_TEST(RxVideo)
{
    static const uint8_t Group[16] = {239, 1, 2, 3};
    static const uint8_t Source[16] = {192, 168, 39, 10};
    Fixture Fix = {NULL};
    DtTest_SetCleanup(FreeFixture, &Fix);
    const DtNmosFlow* Flow = FlowOf(&Fix, VIDEO("YCbCr-4:2:2", "10"), 0);
    DT_ASSERT(Flow != NULL);

    St2110_RxConfigVideo Video;
    AvFifo_IpPars IpPars;
    DT_ASSERT_OK(DtNmosAvFifo_RxConfigFromFlow(Flow, St2110_RxFrameFormat_Uyvy422_10b,
                                               &Video, NULL, &IpPars));
    DT_ASSERT_EQ(Video.Format, St2110_RxFrameFormat_Uyvy422_10b);
    DT_ASSERT_EQ(IpPars.IpVersion, IpProtocolVersion_IPv4);
    DT_ASSERT_MEM(IpPars.IpAddr, Group, 16);
    DT_ASSERT_EQ(IpPars.Port, 5004);
    DT_ASSERT_EQ(IpPars.NSrcFlt, 1);
    DT_ASSERT_MEM(IpPars.SrcFlt[0].IpAddr, Source, 16);
    DT_ASSERT_EQ(IpPars.SrcFlt[0].Port, -1);
    DT_ASSERT_EQ(IpPars.RtpPayloadType, 96);
    DT_ASSERT_EQ(IpPars.TransportProtocol, IpTransportProtocol_Rtp);
    DT_ASSERT_EQ(IpPars.DiffServ, 0x88);
    DT_ASSERT_EQ(IpPars.TimeToLive, 64);

    DT_ASSERT_OK(DtNmosAvFifo_RxConfigFromFlow(
        Flow, St2110_RxFrameFormat_Uyvy422_10b_to_8b, &Video, NULL, &IpPars));
    DT_ASSERT_OK(DtNmosAvFifo_RxConfigFromFlow(Flow, St2110_RxFrameFormat_Raw, &Video,
                                               NULL, &IpPars));
    FreeFixture(&Fix);
    DtTest_SetCleanup(NULL, NULL);
}

// A conversion takes 4:2:2 of its depth only, says which formats would take the flow,
// and writes nothing; Raw takes any uncompressed flow.
DT_TEST(RxVideoFormats)
{
    Fixture Fix = {NULL};
    DtTest_SetCleanup(FreeFixture, &Fix);
    St2110_RxConfigVideo Video = {St2110_RxFrameFormat_Yuv422p_8b};
    AvFifo_IpPars IpPars;
    memset(&IpPars, 0xAB, sizeof(IpPars));

    const DtNmosFlow* Flow = FlowOf(&Fix, VIDEO("YCbCr-4:2:2", "10"), 0);
    DT_ASSERT(Flow != NULL);
    DT_ASSERT_EQ(DtNmosAvFifo_RxConfigFromFlow(Flow, St2110_RxFrameFormat_Uyvy422_8b,
                                               &Video, NULL, &IpPars),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT(strstr(GetLastException(), "Uyvy422_10b_to_8b") != NULL);
    DT_ASSERT_EQ(Video.Format, St2110_RxFrameFormat_Yuv422p_8b);
    DT_ASSERT_EQ(IpPars.Port, (int)0xABABABAB);

    Flow = FlowOf(&Fix, VIDEO("YCbCr-4:2:2", "8"), 0);
    DT_ASSERT(Flow != NULL);
    DT_ASSERT_OK(DtNmosAvFifo_RxConfigFromFlow(Flow, St2110_RxFrameFormat_Yuv422p_8b,
                                               &Video, NULL, &IpPars));
    DT_ASSERT_EQ(DtNmosAvFifo_RxConfigFromFlow(Flow, St2110_RxFrameFormat_Uyvy422_10b,
                                               &Video, NULL, &IpPars),
                 DTAPI_E_NOT_SUPPORTED);

    Flow = FlowOf(&Fix, VIDEO("YCbCr-4:4:4", "12"), 0);
    DT_ASSERT(Flow != NULL);
    DT_ASSERT_EQ(DtNmosAvFifo_RxConfigFromFlow(Flow, St2110_RxFrameFormat_Uyvy422_10b,
                                               &Video, NULL, &IpPars),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT(strstr(GetLastException(), "use Raw") != NULL);
    DT_ASSERT_OK(DtNmosAvFifo_RxConfigFromFlow(Flow, St2110_RxFrameFormat_Raw, &Video,
                                               NULL, &IpPars));
    FreeFixture(&Fix);
    DtTest_SetCleanup(NULL, NULL);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Receive: audio +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// L16 and L24 as they are, and AM824 as Raw, at the flow's sample rate; a flow without a
// source filter gets none.
DT_TEST(RxAudio)
{
    Fixture Fix = {NULL};
    DtTest_SetCleanup(FreeFixture, &Fix);
    St2110_RxConfigAudio Audio;
    AvFifo_IpPars IpPars;
    static const struct
    {
        const char* Sdp;
        St2110_AudioFormat Format;
    } Cases[] = {{AUDIO("L16"), St2110_AudioFormat_L16BE},
                 {AUDIO("L24"), St2110_AudioFormat_L24BE},
                 {AUDIO("AM824"), St2110_AudioFormat_Raw}};
    for (size_t i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        const DtNmosFlow* Flow = FlowOf(&Fix, Cases[i].Sdp, 0);
        DT_ASSERT(Flow != NULL);
        DT_ASSERT_OK(DtNmosAvFifo_RxConfigFromFlow(Flow, St2110_RxFrameFormat_Raw, NULL,
                                                   &Audio, &IpPars));
        DT_ASSERT_EQ(Audio.Format, Cases[i].Format);
        DT_ASSERT_EQ(Audio.SampleRate, 48000);
        DT_ASSERT_EQ(IpPars.Port, 5006);
        DT_ASSERT_EQ(IpPars.NSrcFlt, 0);
    }
    FreeFixture(&Fix);
    DtTest_SetCleanup(NULL, NULL);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Receive: refused +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// What the FIFO does not receive, each with its reason.
DT_TEST(RxRefused)
{
    St2110_RxConfigVideo Video;
    St2110_RxConfigAudio Audio;
    AvFifo_IpPars IpPars;
    static const struct
    {
        DtNmosMedia Media;
        uint32_t Leg;
        const char* Reason;
    } Cases[] = {{DTNMOS_MEDIA_VIDEO, 1, "ST 2022-7"},
                 {DTNMOS_MEDIA_COMPRESSED_VIDEO, 0, "ST 2110-22"},
                 {DTNMOS_MEDIA_ANC, 0, "ST 2110-40"},
                 {DTNMOS_MEDIA_OTHER, 0, "does not know"}};
    for (size_t i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        DtNmosFlow Flow = HandFlow(Cases[i].Media);
        Flow.Leg = Cases[i].Leg;
        DT_ASSERT_EQ(DtNmosAvFifo_RxConfigFromFlow(&Flow, St2110_RxFrameFormat_Raw,
                                                   &Video, &Audio, &IpPars),
                     DTAPI_E_NOT_SUPPORTED);
        DT_ASSERT(strstr(GetLastException(), Cases[i].Reason) != NULL);
    }

    DtNmosFlow Flow = HandFlow(DTNMOS_MEDIA_AUDIO);
    Flow.Format.Audio.Encoding = DTNMOS_AUDIO_ENCODING_OTHER;
    DT_ASSERT_EQ(DtNmosAvFifo_RxConfigFromFlow(&Flow, St2110_RxFrameFormat_Raw, &Video,
                                               &Audio, &IpPars),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT(strstr(GetLastException(), "does not know") != NULL);
}

// Arguments that are wrong: no flow or IP parameters, a flow without a media or smaller
// than the bridge's, no configuration of the flow's media, a name or no address, sources
// of another IP version, and no port.
DT_TEST(RxArguments)
{
    St2110_RxConfigVideo Video;
    AvFifo_IpPars IpPars;
    DtNmosFlow Flow = HandFlow(DTNMOS_MEDIA_NONE);
    DT_ASSERT_EQ(DtNmosAvFifo_RxConfigFromFlow(&Flow, St2110_RxFrameFormat_Raw, &Video,
                                               NULL, &IpPars),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT(strstr(GetLastException(), "no Media") != NULL);
    Flow = HandFlow(DTNMOS_MEDIA_VIDEO);
    DT_ASSERT_EQ(DtNmosAvFifo_RxConfigFromFlow(NULL, St2110_RxFrameFormat_Raw, &Video,
                                               NULL, &IpPars),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtNmosAvFifo_RxConfigFromFlow(&Flow, St2110_RxFrameFormat_Raw, &Video,
                                               NULL, NULL),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtNmosAvFifo_RxConfigFromFlow(&Flow, St2110_RxFrameFormat_Raw, NULL,
                                               NULL, &IpPars),
                 DTAPI_E_INVALID_ARG);

    Flow.Size = sizeof(Flow) - 1;
    DT_ASSERT_EQ(DtNmosAvFifo_RxConfigFromFlow(&Flow, St2110_RxFrameFormat_Raw, &Video,
                                               NULL, &IpPars),
                 DTAPI_E_INVALID_ARG);

    Flow = HandFlow(DTNMOS_MEDIA_VIDEO);
    snprintf(Flow.DestinationIp, sizeof(Flow.DestinationIp), "cam1.studio.local");
    DT_ASSERT_EQ(DtNmosAvFifo_RxConfigFromFlow(&Flow, St2110_RxFrameFormat_Raw, &Video,
                                               NULL, &IpPars),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT(strstr(GetLastException(), "domain name") != NULL);
    Flow.DestinationIp[0] = '\0';
    DT_ASSERT_EQ(DtNmosAvFifo_RxConfigFromFlow(&Flow, St2110_RxFrameFormat_Raw, &Video,
                                               NULL, &IpPars),
                 DTAPI_E_INVALID_ARG);

    Flow = HandFlow(DTNMOS_MEDIA_VIDEO);
    snprintf(Flow.SourceIp, sizeof(Flow.SourceIp), "fe80::1");
    DT_ASSERT_EQ(DtNmosAvFifo_RxConfigFromFlow(&Flow, St2110_RxFrameFormat_Raw, &Video,
                                               NULL, &IpPars),
                 DTAPI_E_INVALID_ARG);

    Flow = HandFlow(DTNMOS_MEDIA_VIDEO);
    Flow.DestinationPort = 0;
    DT_ASSERT_EQ(DtNmosAvFifo_RxConfigFromFlow(&Flow, St2110_RxFrameFormat_Raw, &Video,
                                               NULL, &IpPars),
                 DTAPI_E_INVALID_ARG);
}

// An IPv6 flow: group and source in all 16 bytes.
DT_TEST(RxIpV6)
{
    static const uint8_t Group[16] = {0xFF, 0x3E, 0, 0, 0, 0, 0, 0,
                                      0,    0,    0, 0, 0, 0, 0, 1};
    St2110_RxConfigVideo Video;
    AvFifo_IpPars IpPars;
    DtNmosFlow Flow = HandFlow(DTNMOS_MEDIA_VIDEO);
    snprintf(Flow.DestinationIp, sizeof(Flow.DestinationIp), "ff3e::1");
    snprintf(Flow.SourceIp, sizeof(Flow.SourceIp), "2001:db8::10");
    DT_ASSERT_OK(DtNmosAvFifo_RxConfigFromFlow(&Flow, St2110_RxFrameFormat_Raw, &Video,
                                               NULL, &IpPars));
    DT_ASSERT_EQ(IpPars.IpVersion, IpProtocolVersion_IPv6);
    DT_ASSERT_MEM(IpPars.IpAddr, Group, 16);
    DT_ASSERT_EQ(IpPars.NSrcFlt, 1);
    DT_ASSERT_EQ(IpPars.SrcFlt[0].IpAddr[15], 0x10);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmit +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// 1080i50 of depth 8 in block packing and linear scheduling: the frame rate doubled into
// the field rate, the frame format of the depth, no source filter.
DT_TEST(TxVideo)
{
    Fixture Fix = {NULL};
    DtTest_SetCleanup(FreeFixture, &Fix);
    const DtNmosFlow* Flow =
        FlowOf(&Fix,
               VIDEO_FMTP("sampling=YCbCr-4:2:2; width=1920; height=1080; "
                          "exactframerate=25; depth=8; interlace; PM=2110BPM; "
                          "TP=2110TPNL"),
               0);
    DT_ASSERT(Flow != NULL);
    St2110_TxConfigVideo Video;
    AvFifo_IpPars IpPars;
    DT_ASSERT_OK(DtNmosAvFifo_TxConfigFromFlow(Flow, &Video, NULL, &IpPars));
    DT_ASSERT_EQ(Video.Format, St2110_TxFrameFormat_Uyvy422_8b);
    DT_ASSERT_EQ(Video.Resolution.Width, 1920);
    DT_ASSERT_EQ(Video.Resolution.Height, 1080);
    DT_ASSERT_EQ(Video.Timing.Rate.Numerator, 50);
    DT_ASSERT_EQ(Video.Timing.Rate.Denominator, 1);
    DT_ASSERT_EQ(Video.Timing.VideoScanning, St2110_VideoScanning_Interlaced);
    DT_ASSERT_EQ(Video.Timing.Scheduling, St2110_Scheduling_Linear);
    DT_ASSERT_EQ(Video.Packing.PackingMode, St2110_PackingMode_Block);
    DT_ASSERT_EQ(Video.Packing.PayloadSize, -1);
    DT_ASSERT_EQ(IpPars.Port, 5004);
    DT_ASSERT_EQ(IpPars.NSrcFlt, 0);

    // A wide sender's type is sent narrow, gapped.
    Flow = FlowOf(&Fix,
                  VIDEO_FMTP("sampling=YCbCr-4:2:2; width=1280; height=720; "
                             "exactframerate=60000/1001; depth=10; TP=2110TPW"),
                  0);
    DT_ASSERT(Flow != NULL);
    DT_ASSERT_OK(DtNmosAvFifo_TxConfigFromFlow(Flow, &Video, NULL, &IpPars));
    DT_ASSERT_EQ(Video.Format, St2110_TxFrameFormat_Uyvy422_10b);
    DT_ASSERT_EQ(Video.Timing.Rate.Numerator, 60000);
    DT_ASSERT_EQ(Video.Timing.Rate.Denominator, 1001);
    DT_ASSERT_EQ(Video.Timing.Scheduling, St2110_Scheduling_Gapped);
    DT_ASSERT_EQ(Video.Packing.PackingMode, St2110_PackingMode_General);
    FreeFixture(&Fix);
    DtTest_SetCleanup(NULL, NULL);
}

// L24 and L16 at the packet times of ST 2110-30, 1 ms when the SDP gives none.
DT_TEST(TxAudio)
{
    Fixture Fix = {NULL};
    DtTest_SetCleanup(FreeFixture, &Fix);
    St2110_TxConfigAudio Audio;
    AvFifo_IpPars IpPars;
    const DtNmosFlow* Flow = FlowOf(&Fix, AUDIO("L24"), 0);
    DT_ASSERT(Flow != NULL);
    DT_ASSERT_OK(DtNmosAvFifo_TxConfigFromFlow(Flow, NULL, &Audio, &IpPars));
    DT_ASSERT_EQ(Audio.Format, St2110_AudioFormat_L24BE);
    DT_ASSERT_EQ(Audio.NumChannels, 2);
    DT_ASSERT_EQ(Audio.SampleRate, 48000);
    DT_ASSERT_EQ(Audio.NumSamplesPerIpPacket, 48);

    DtNmosFlow Hand = *Flow;
    Hand.Format.Audio.PacketTimeNs = 125000;
    Hand.Format.Audio.Encoding = DTNMOS_AUDIO_ENCODING_L16;
    DT_ASSERT_OK(DtNmosAvFifo_TxConfigFromFlow(&Hand, NULL, &Audio, &IpPars));
    DT_ASSERT_EQ(Audio.Format, St2110_AudioFormat_L16BE);
    DT_ASSERT_EQ(Audio.NumSamplesPerIpPacket, 6);
    Hand.Format.Audio.PacketTimeNs = 0;
    DT_ASSERT_OK(DtNmosAvFifo_TxConfigFromFlow(&Hand, NULL, &Audio, &IpPars));
    DT_ASSERT_EQ(Audio.NumSamplesPerIpPacket, 48);
    Hand.Format.Audio.PacketTimeNs = 333333;
    DT_ASSERT_EQ(DtNmosAvFifo_TxConfigFromFlow(&Hand, NULL, &Audio, &IpPars),
                 DTAPI_E_NOT_SUPPORTED);
    FreeFixture(&Fix);
    DtTest_SetCleanup(NULL, NULL);
}

// What the FIFO does not send: another sampling or depth, AM824, a PM or TP it has not,
// and a flow without a raster.
DT_TEST(TxRefused)
{
    Fixture Fix = {NULL};
    DtTest_SetCleanup(FreeFixture, &Fix);
    St2110_TxConfigVideo Video;
    St2110_TxConfigAudio Audio;
    AvFifo_IpPars IpPars;
    static const char* const Sdps[] = {
        VIDEO("YCbCr-4:4:4", "10"), VIDEO("YCbCr-4:2:2", "12"),
        VIDEO_FMTP("sampling=YCbCr-4:2:2; width=1920; height=1080; exactframerate=25; "
                   "depth=10; PM=2110XPM"),
        VIDEO_FMTP("sampling=YCbCr-4:2:2; width=1920; height=1080; exactframerate=25; "
                   "depth=10; TP=2110TPX"),
        AUDIO("AM824")};
    for (size_t i = 0; i < sizeof(Sdps) / sizeof(Sdps[0]); i++)
    {
        const DtNmosFlow* Flow = FlowOf(&Fix, Sdps[i], 0);
        DT_ASSERT(Flow != NULL);
        if (DtNmosAvFifo_TxConfigFromFlow(Flow, &Video, &Audio, &IpPars) !=
            DTAPI_E_NOT_SUPPORTED)
        {
            DT_FAIL("case %zu was not refused: %s", i, GetLastException());
        }
    }
    DtNmosFlow Hand = HandFlow(DTNMOS_MEDIA_VIDEO);
    Hand.Format.Video.Sampling = DTNMOS_SAMPLING_YCBCR_422;
    Hand.Format.Video.Depth = 10;
    DT_ASSERT_EQ(DtNmosAvFifo_TxConfigFromFlow(&Hand, &Video, NULL, &IpPars),
                 DTAPI_E_INVALID_ARG);
    FreeFixture(&Fix);
    DtTest_SetCleanup(NULL, NULL);
}

// Whether the answer of Node to a GET of Url has Text in its body.
static bool Answers(DtNmosNode* Node, const char* Url, const char* Text)
{
    DtNmosHttpRequest Request;
    memset(&Request, 0, sizeof(Request));
    Request.Size = sizeof(Request);
    Request.Method = "GET";
    Request.Url = Url;
    DtNmosHttpResponse* Response = DtNmosHttpResponse_Alloc();
    bool Has = false;
    if (Response != NULL && DtNmosNode_Handle(Node, &Request, Response) == DTNMOS_OK)
    {
        size_t Length = 0;
        const char* Body = DtNmosHttpResponse_Body(Response, &Length);
        Has = Body != NULL && strstr(Body, Text) != NULL;
    }
    DtNmosHttpResponse_Free(Response);
    return Has;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- From a FIFO, and back -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// The emulated DTA-2110 attached in the fixture, with a transmit FIFO on its port.
static bool OpenTx(Fixture* Fix, int* DtFailures)
{
    SimDtPcie_Reset();
    SimDtPcie_SetDta2110Index(1);
    Fix->Device = DtDevice_Alloc();
    Fix->Tx = AvFifo_TxFifo_Alloc();
    if (Fix->Device == NULL || Fix->Tx == NULL ||
        DtDevice_AttachToSerial(Fix->Device, (int64_t)SIM_DTA2110_SERIAL) != DTAPI_OK ||
        AvFifo_TxFifo_Attach(Fix->Tx, Fix->Device, 1) != DTAPI_OK)
    {
        printf("    FAIL: no emulated DTA-2110; is CDTAPI_SIM=1 set?\n");
        (*DtFailures)++;
        return false;
    }
    return true;
}

// A flow from a configured FIFO, written as an SDP and read back, gives the FIFO's
// configuration again; its source is the port's address, its reference clock the port's
// MAC address, and its colorimetry that of its raster.
static void RoundTripVideo(Fixture* Fix, const St2110_TxConfigVideo* Config,
                           DtNmosColorimetry Colorimetry, int* DtFailures)
{
    AvFifo_IpPars IpPars;
    memset(&IpPars, 0, sizeof(IpPars));
    IpPars.IpAddr[0] = 239;
    IpPars.IpAddr[1] = 1;
    IpPars.IpAddr[2] = 2;
    IpPars.IpAddr[3] = 3;
    IpPars.Port = 5004;
    IpPars.RtpPayloadType = 96;
    IpPars.TimeToLive = 64;
    IpPars.DiffServ = 0x88;
    DT_ASSERT_OK(AvFifo_TxFifo_ConfigureVideo(Fix->Tx, Config));
    DT_ASSERT_OK(AvFifo_TxFifo_SetIpPars(Fix->Tx, &IpPars));

    DtNmosFlow Flow;
    DT_ASSERT_OK(DtNmosAvFifo_FlowFromTxFifo(Fix->Tx, &Flow));
    DT_ASSERT_STR(Flow.DestinationIp, "239.1.2.3");
    DT_ASSERT_STR(Flow.SourceIp, "192.168.1.10");
    DT_ASSERT_EQ(Flow.RefClock.Kind, DTNMOS_REFCLOCK_LOCALMAC);
    DT_ASSERT(strlen(Flow.RefClock.LocalMac) == 17);
    DT_ASSERT_EQ(Flow.Format.Video.Colorimetry, Colorimetry);
    DT_ASSERT_EQ(Flow.Format.Video.Tcs, DTNMOS_TCS_SDR);
    DT_ASSERT_EQ(Flow.Format.Video.Range, DTNMOS_RANGE_NONE);

    DtNmosSession Session;
    memset(&Session, 0, sizeof(Session));
    Session.Size = sizeof(Session);
    Session.Name = "RoundTrip";
    snprintf(Session.OriginIp, sizeof(Session.OriginIp), "%s", Flow.SourceIp);
    char Text[2048];
    size_t Size = sizeof(Text);
    DT_ASSERT(DtNmosSdp_Write(&Session, &Flow, 1, Text, &Size) == DTNMOS_OK);
    const DtNmosFlow* Read = FlowOf(Fix, Text, 0);
    DT_ASSERT(Read != NULL);

    St2110_TxConfigVideo Back;
    AvFifo_IpPars BackPars;
    DT_ASSERT_OK(DtNmosAvFifo_TxConfigFromFlow(Read, &Back, NULL, &BackPars));
    DT_ASSERT_EQ(Back.Format, Config->Format);
    DT_ASSERT_EQ(Back.Resolution.Width, Config->Resolution.Width);
    DT_ASSERT_EQ(Back.Resolution.Height, Config->Resolution.Height);
    DT_ASSERT_EQ(Back.Timing.Rate.Numerator * Config->Timing.Rate.Denominator,
                 Config->Timing.Rate.Numerator * Back.Timing.Rate.Denominator);
    DT_ASSERT_EQ(Back.Timing.VideoScanning, Config->Timing.VideoScanning);
    DT_ASSERT_EQ(Back.Timing.Scheduling, Config->Timing.Scheduling);
    DT_ASSERT_EQ(Back.Packing.PackingMode, Config->Packing.PackingMode);
    DT_ASSERT_MEM(BackPars.IpAddr, IpPars.IpAddr, 16);
    DT_ASSERT_EQ(BackPars.Port, IpPars.Port);
    DT_ASSERT_EQ(BackPars.RtpPayloadType, IpPars.RtpPayloadType);
}

// Video of each scanning, both depths and both packing modes and schedules, around the
// SDP and back, of SD, HD and UHD.
DT_TEST(TxRoundTripVideo)
{
    Fixture Fix = {NULL};
    DtTest_SetCleanup(FreeFixture, &Fix);
    if (!OpenTx(&Fix, DtFailures))
        return;
    static const struct
    {
        St2110_TxFrameFormat Format;
        int Width, Height, Numerator, Denominator;
        St2110_VideoScanning Scanning;
        St2110_Scheduling Scheduling;
        St2110_PackingMode Packing;
        DtNmosColorimetry Colorimetry;
    } Cases[] = {
        {St2110_TxFrameFormat_Uyvy422_10b, 1920, 1080, 25, 1,
         St2110_VideoScanning_Progressive, St2110_Scheduling_Gapped,
         St2110_PackingMode_General, DTNMOS_COLORIMETRY_BT709},
        {St2110_TxFrameFormat_Uyvy422_8b, 1920, 1080, 50, 1,
         St2110_VideoScanning_Interlaced, St2110_Scheduling_Linear,
         St2110_PackingMode_Block, DTNMOS_COLORIMETRY_BT709},
        {St2110_TxFrameFormat_Uyvy422_10b, 1920, 1080, 50, 1, St2110_VideoScanning_PsF,
         St2110_Scheduling_Gapped, St2110_PackingMode_General, DTNMOS_COLORIMETRY_BT709},
        {St2110_TxFrameFormat_Uyvy422_10b, 720, 486, 60000, 1001,
         St2110_VideoScanning_Interlaced, St2110_Scheduling_Gapped,
         St2110_PackingMode_General, DTNMOS_COLORIMETRY_BT601},
        {St2110_TxFrameFormat_Uyvy422_10b, 3840, 2160, 50, 1,
         St2110_VideoScanning_Progressive, St2110_Scheduling_Gapped,
         St2110_PackingMode_General, DTNMOS_COLORIMETRY_BT2020},
    };
    for (size_t i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        St2110_TxConfigVideo Config;
        memset(&Config, 0, sizeof(Config));
        Config.Format = Cases[i].Format;
        Config.Packing.PackingMode = Cases[i].Packing;
        Config.Packing.PayloadSize = -1;
        Config.Resolution.Width = Cases[i].Width;
        Config.Resolution.Height = Cases[i].Height;
        Config.Timing.Rate.Numerator = Cases[i].Numerator;
        Config.Timing.Rate.Denominator = Cases[i].Denominator;
        Config.Timing.Scheduling = Cases[i].Scheduling;
        Config.Timing.VideoScanning = Cases[i].Scanning;
        RoundTripVideo(&Fix, &Config, Cases[i].Colorimetry, DtFailures);
    }
    FreeFixture(&Fix);
    DtTest_SetCleanup(NULL, NULL);
}

// Audio around the SDP and back; audio of Raw has no encoding to name.
DT_TEST(TxRoundTripAudio)
{
    Fixture Fix = {NULL};
    DtTest_SetCleanup(FreeFixture, &Fix);
    if (!OpenTx(&Fix, DtFailures))
        return;
    AvFifo_IpPars IpPars;
    memset(&IpPars, 0, sizeof(IpPars));
    IpPars.IpAddr[0] = 239;
    IpPars.IpAddr[3] = 4;
    IpPars.Port = 5006;
    IpPars.RtpPayloadType = 97;
    const St2110_TxConfigAudio Config = {St2110_AudioFormat_L16BE, 8, 6, 48000};
    DT_ASSERT_OK(AvFifo_TxFifo_ConfigureAudio(Fix.Tx, &Config));
    DT_ASSERT_OK(AvFifo_TxFifo_SetIpPars(Fix.Tx, &IpPars));

    DtNmosFlow Flow;
    DT_ASSERT_OK(DtNmosAvFifo_FlowFromTxFifo(Fix.Tx, &Flow));
    DT_ASSERT_EQ(Flow.Media, DTNMOS_MEDIA_AUDIO);
    DT_ASSERT_EQ(Flow.Format.Audio.Encoding, DTNMOS_AUDIO_ENCODING_L16);
    DT_ASSERT_EQ(Flow.Format.Audio.Channels, 8);
    DT_ASSERT_EQ(Flow.Format.Audio.PacketTimeNs, 125000);
    St2110_TxConfigAudio Back;
    AvFifo_IpPars BackPars;
    DT_ASSERT_OK(DtNmosAvFifo_TxConfigFromFlow(&Flow, NULL, &Back, &BackPars));
    DT_ASSERT_EQ(Back.Format, Config.Format);
    DT_ASSERT_EQ(Back.NumChannels, Config.NumChannels);
    DT_ASSERT_EQ(Back.NumSamplesPerIpPacket, Config.NumSamplesPerIpPacket);
    DT_ASSERT_EQ(Back.SampleRate, Config.SampleRate);

    const St2110_TxConfigAudio Raw = {St2110_AudioFormat_Raw, 2, 48, 48000};
    DT_ASSERT_OK(AvFifo_TxFifo_ConfigureAudio(Fix.Tx, &Raw));
    DT_ASSERT_EQ(DtNmosAvFifo_FlowFromTxFifo(Fix.Tx, &Flow), DTAPI_E_NOT_SUPPORTED);
    FreeFixture(&Fix);
    DtTest_SetCleanup(NULL, NULL);
}

// A FIFO not configured, or without IP parameters, has no flow yet.
DT_TEST(TxFifoNotReady)
{
    Fixture Fix = {NULL};
    DtTest_SetCleanup(FreeFixture, &Fix);
    if (!OpenTx(&Fix, DtFailures))
        return;
    DtNmosFlow Flow;
    DT_ASSERT_EQ(DtNmosAvFifo_FlowFromTxFifo(Fix.Tx, &Flow), DTAPI_E_CONFIG);
    const St2110_TxConfigAudio Config = {St2110_AudioFormat_L24BE, 2, 48, 48000};
    DT_ASSERT_OK(AvFifo_TxFifo_ConfigureAudio(Fix.Tx, &Config));
    DT_ASSERT_EQ(DtNmosAvFifo_FlowFromTxFifo(Fix.Tx, &Flow), DTAPI_E_NO_IPPARS);
    DT_ASSERT_EQ(DtNmosAvFifo_FlowFromTxFifo(NULL, &Flow), DTAPI_E_INVALID_ARG);
    FreeFixture(&Fix);
    DtTest_SetCleanup(NULL, NULL);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EnableSlave -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Switches the emulated slave of the DTA-2110's port on, in domain Domain, so that it
// locks to the emulated grandmaster at once.
//
static bool EnableSlave(const DtDevice* Device, int Domain)
{
    DtPtpSlave* Slave = NULL;
    if (DtPtpSlave_Attach(Device, 1, true, &Slave) != DTAPI_OK)
        return false;
    DtPtpConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Fields = DT_PTP_CONFIG_ENABLE | DT_PTP_CONFIG_DOMAIN;
    Config.Enable = true;
    Config.Domain = Domain;
    DtapiResult Result = DtPtpSlave_SetConfig(Slave, &Config);
    DtPtpSlave_Detach(Slave);
    return Result == DTAPI_OK;
}

// A sender's reference clock is the port's MAC address while the port's PTP slave is
// off, and its grandmaster, named in full, once the slave is locked to it.
DT_TEST(TxPtpRefClock)
{
    Fixture Fix = {NULL};
    DtTest_SetCleanup(FreeFixture, &Fix);
    if (!OpenTx(&Fix, DtFailures))
        return;
    const St2110_TxConfigAudio Config = {St2110_AudioFormat_L24BE, 2, 48, 48000};
    DT_ASSERT_OK(AvFifo_TxFifo_ConfigureAudio(Fix.Tx, &Config));
    AvFifo_IpPars IpPars;
    memset(&IpPars, 0, sizeof(IpPars));
    IpPars.IpVersion = IpProtocolVersion_IPv4;
    IpPars.IpAddr[0] = 239;
    IpPars.IpAddr[1] = 1;
    IpPars.IpAddr[2] = 2;
    IpPars.IpAddr[3] = 4;
    IpPars.Port = 5004;
    IpPars.RtpPayloadType = 97;
    IpPars.TimeToLive = 64;
    DT_ASSERT_OK(AvFifo_TxFifo_SetIpPars(Fix.Tx, &IpPars));

    DtNmosFlow Flow;
    DT_ASSERT_OK(DtNmosAvFifo_FlowFromTxFifo(Fix.Tx, &Flow));
    DT_ASSERT_EQ(Flow.RefClock.Kind, DTNMOS_REFCLOCK_LOCALMAC);

    DT_ASSERT(EnableSlave(Fix.Device, 5));
    DT_ASSERT_OK(DtNmosAvFifo_FlowFromTxFifo(Fix.Tx, &Flow));
    DT_ASSERT_EQ(Flow.RefClock.Kind, DTNMOS_REFCLOCK_PTP);
    DT_ASSERT_STR(Flow.RefClock.PtpVersion, "IEEE1588-2008");
    DT_ASSERT_STR(Flow.RefClock.Grandmaster, "00-1B-19-FF-FE-00-00-01");
    DT_ASSERT_EQ(Flow.RefClock.Domain, 5);
    DT_ASSERT(!Flow.RefClock.Traceable);

    DtNmosSession Session;
    memset(&Session, 0, sizeof(Session));
    Session.Size = sizeof(Session);
    Session.Name = "Ptp";
    snprintf(Session.OriginIp, sizeof(Session.OriginIp), "%s", Flow.SourceIp);
    char Text[2048];
    size_t Size = sizeof(Text);
    DT_ASSERT(DtNmosSdp_Write(&Session, &Flow, 1, Text, &Size) == DTNMOS_OK);
    DT_ASSERT(
        strstr(Text, "a=ts-refclk:ptp=IEEE1588-2008:00-1B-19-FF-FE-00-00-01:5\r\n") !=
        NULL);
    FreeFixture(&Fix);
    DtTest_SetCleanup(NULL, NULL);
}

// The node's clock from the port's PTP slave: internal while it has no grandmaster, and
// the grandmaster, in lower case, traceable and locked, once it has; a node registers
// with it.
DT_TEST(NodeClockFromPort)
{
    Fixture Fix = {NULL};
    DtTest_SetCleanup(FreeFixture, &Fix);
    if (!OpenTx(&Fix, DtFailures))
        return;
    DtNmosClock Clock;
    DT_ASSERT_OK(DtNmosAvFifo_ClockFromPort(Fix.Device, 1, &Clock));
    DT_ASSERT_EQ(Clock.Size, sizeof(Clock));
    DT_ASSERT_EQ(Clock.Kind, DTNMOS_CLOCK_INTERNAL);

    DT_ASSERT(EnableSlave(Fix.Device, 0));
    DT_ASSERT_OK(DtNmosAvFifo_ClockFromPort(Fix.Device, 1, &Clock));
    DT_ASSERT_EQ(Clock.Kind, DTNMOS_CLOCK_PTP);
    DT_ASSERT_STR(Clock.Grandmaster, "00-1b-19-ff-fe-00-00-01");
    DT_ASSERT(Clock.Traceable);
    DT_ASSERT(Clock.Locked);

    // A node takes it as it is.
    if (!OpenNode(&Fix, DtFailures))
        return;
    DT_ASSERT(DtNmosNode_SetClock(Fix.Node, &Clock) == DTNMOS_OK);
    DT_ASSERT(Answers(Fix.Node, "/x-nmos/node/v1.3/self",
                      "\"gmid\": \"00-1b-19-ff-fe-00-00-01\", \"locked\": true"));

    // A port without a slave fails, and leaves the clock internal.
    DT_ASSERT_EQ(DtNmosAvFifo_ClockFromPort(Fix.Device, 2, &Clock), DTAPI_E_NO_SUCH_PORT);
    DT_ASSERT_EQ(Clock.Kind, DTNMOS_CLOCK_INTERNAL);
    DT_ASSERT_EQ(DtNmosAvFifo_ClockFromPort(Fix.Device, 1, NULL), DTAPI_E_INVALID_ARG);
    FreeFixture(&Fix);
    DtTest_SetCleanup(NULL, NULL);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Nodes +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The device of the DTA-2110's port: without a config, its ID derived from the node's,
// the serial number and the port, and its label with the serial number; once only, and
// only of a port with an AV FIFO that the device has. A config's ID and label are kept.
DT_TEST(AddDevice)
{
    Fixture Fix = {NULL};
    DtTest_SetCleanup(FreeFixture, &Fix);
    if (!OpenTx(&Fix, DtFailures) || !OpenNode(&Fix, DtFailures))
        return;
    DtNmosId Id;
    DT_ASSERT_OK(DtNmosAvFifo_AddDevice(Fix.Node, Fix.Device, 1, NULL, &Id));
    DtNmosId NodeId = {NODE_ID};
    DtNmosId Want;
    char Name[64];
    snprintf(Name, sizeof(Name), "device/cdtapi/%lld:1", (long long)SIM_DTA2110_SERIAL);
    DT_ASSERT(DtNmosId_FromName(&NodeId, Name, &Want) == DTNMOS_OK);
    DT_ASSERT_STR(Id.Text, Want.Text);
    char Label[96];
    DeviceLabel(Fix.Node, &Id, Label, sizeof(Label));
    char WantLabel[96];
    snprintf(WantLabel, sizeof(WantLabel), "DTA-2110 %lld port 1",
             (long long)SIM_DTA2110_SERIAL);
    DT_ASSERT_STR(Label, WantLabel);

    DT_ASSERT_EQ(DtNmosAvFifo_AddDevice(Fix.Node, Fix.Device, 1, NULL, &Id),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT(strstr(GetLastException(), "already") != NULL);
    DT_ASSERT_EQ(DtNmosAvFifo_AddDevice(Fix.Node, Fix.Device, 0, NULL, &Id),
                 DTAPI_E_NO_SUCH_PORT);
    DT_ASSERT_EQ(DtNmosAvFifo_AddDevice(Fix.Node, Fix.Device, 99, NULL, &Id),
                 DTAPI_E_NO_SUCH_PORT);
    DT_ASSERT_EQ(DtNmosAvFifo_AddDevice(Fix.Node, NULL, 1, NULL, &Id), DTAPI_E_DEVICE);
    DT_ASSERT_EQ(DtNmosAvFifo_AddDevice(NULL, Fix.Device, 1, NULL, &Id),
                 DTAPI_E_INVALID_ARG);

    // A config's own ID and label, once the device with the derived ID is removed.
    DT_ASSERT(DtNmosNode_Remove(Fix.Node, &Id) == DTNMOS_OK);
    DtNmosDeviceConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    snprintf(Config.Id.Text, sizeof(Config.Id.Text), "%s",
             "aaaaaaaa-0000-4000-8000-0000000000d1");
    Config.Label = "studio camera input";
    DT_ASSERT_OK(DtNmosAvFifo_AddDevice(Fix.Node, Fix.Device, 1, &Config, &Id));
    DT_ASSERT_STR(Id.Text, "aaaaaaaa-0000-4000-8000-0000000000d1");
    DeviceLabel(Fix.Node, &Id, Label, sizeof(Label));
    DT_ASSERT_STR(Label, "studio camera input");
    Config.Size = sizeof(Config) - 1;
    DT_ASSERT_EQ(DtNmosAvFifo_AddDevice(Fix.Node, Fix.Device, 1, &Config, &Id),
                 DTAPI_E_INVALID_ARG);
    FreeFixture(&Fix);
    DtTest_SetCleanup(NULL, NULL);
}

// The fixture's node with the device of the DTA-2110's port 1 and a transmit FIFO
// configured for 1080p25 of 10 bits to 239.1.2.3:5004, and a receive FIFO attached to
// the same port; false, failing the case, when not.
static bool OpenPort(Fixture* Fix, DtNmosId* DeviceId, int* DtFailures)
{
    if (!OpenTx(Fix, DtFailures) || !OpenNode(Fix, DtFailures))
        return false;
    St2110_TxConfigVideo Config;
    memset(&Config, 0, sizeof(Config));
    Config.Format = St2110_TxFrameFormat_Uyvy422_10b;
    Config.Resolution.Width = 1920;
    Config.Resolution.Height = 1080;
    Config.Timing.Rate.Numerator = 25;
    Config.Timing.Rate.Denominator = 1;
    Config.Packing.PayloadSize = -1;
    AvFifo_IpPars IpPars;
    memset(&IpPars, 0, sizeof(IpPars));
    IpPars.IpAddr[0] = 239;
    IpPars.IpAddr[1] = 1;
    IpPars.IpAddr[2] = 2;
    IpPars.IpAddr[3] = 3;
    IpPars.Port = 5004;
    IpPars.RtpPayloadType = 96;
    Fix->Rx = AvFifo_RxFifo_Alloc();
    if (AvFifo_TxFifo_ConfigureVideo(Fix->Tx, &Config) != DTAPI_OK ||
        AvFifo_TxFifo_SetIpPars(Fix->Tx, &IpPars) != DTAPI_OK || Fix->Rx == NULL ||
        AvFifo_RxFifo_Attach(Fix->Rx, Fix->Device, 1) != DTAPI_OK ||
        DtNmosAvFifo_AddDevice(Fix->Node, Fix->Device, 1, NULL, DeviceId) != DTAPI_OK)
    {
        printf("    FAIL: no port: %s\n", GetLastException());
        (*DtFailures)++;
        return false;
    }
    return true;
}

// Takes every activation.
static DtNmosResult TakeRx(void* User, const DtNmosId* Receiver,
                           const DtNmosReceiverActivation* Activation)
{
    (void)User;
    (void)Receiver;
    (void)Activation;
    return DTNMOS_OK;
}

static DtNmosResult TakeTx(void* User, const DtNmosId* Sender,
                           const DtNmosSenderActivation* Activation)
{
    (void)User;
    (void)Sender;
    (void)Activation;
    return DTNMOS_OK;
}

// A receiver of the FIFO: its ID made from the device's and the label, the same each
// time, or the config's; its interface the port's. A config without a device, without
// an ID or label, or too small, and a FIFO not attached, are refused.
DT_TEST(AddReceiver)
{
    Fixture Fix = {NULL};
    DtTest_SetCleanup(FreeFixture, &Fix);
    DtNmosId DeviceId;
    if (!OpenPort(&Fix, &DeviceId, DtFailures))
        return;
    DtNmosReceiverConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    Config.DeviceId = DeviceId;
    Config.Label = "camera 1";
    Config.Media = DTNMOS_MEDIA_VIDEO;
    DtNmosId Id;
    DT_ASSERT_OK(DtNmosAvFifo_AddReceiver(Fix.Node, Fix.Rx, &Config, TakeRx, NULL, &Id));
    DtNmosId Want;
    DT_ASSERT(DtNmosId_FromName(&DeviceId, "receiver/camera 1", &Want) == DTNMOS_OK);
    DT_ASSERT_STR(Id.Text, Want.Text);
    char Url[128];
    snprintf(Url, sizeof(Url), "/x-nmos/node/v1.3/receivers/%s", Id.Text);
    DT_ASSERT(Answers(Fix.Node, Url, "camera 1"));
    DT_ASSERT_EQ(DtNmosAvFifo_AddReceiver(Fix.Node, Fix.Rx, &Config, TakeRx, NULL, &Id),
                 DTAPI_E_INVALID_ARG);

    snprintf(Config.Id.Text, sizeof(Config.Id.Text), "%s",
             "aaaaaaaa-0000-4000-8000-0000000000e1");
    Config.Label = NULL;
    DT_ASSERT_OK(DtNmosAvFifo_AddReceiver(Fix.Node, Fix.Rx, &Config, TakeRx, NULL, &Id));
    DT_ASSERT_STR(Id.Text, "aaaaaaaa-0000-4000-8000-0000000000e1");

    Config.Id.Text[0] = '\0';
    DT_ASSERT_EQ(DtNmosAvFifo_AddReceiver(Fix.Node, Fix.Rx, &Config, TakeRx, NULL, &Id),
                 DTAPI_E_INVALID_ARG);
    Config.Label = "camera 2";
    Config.Size = sizeof(Config) - 1;
    DT_ASSERT_EQ(DtNmosAvFifo_AddReceiver(Fix.Node, Fix.Rx, &Config, TakeRx, NULL, &Id),
                 DTAPI_E_INVALID_ARG);
    Config.Size = sizeof(Config);
    Config.DeviceId.Text[0] = '\0';
    DT_ASSERT_EQ(DtNmosAvFifo_AddReceiver(Fix.Node, Fix.Rx, &Config, TakeRx, NULL, &Id),
                 DTAPI_E_INVALID_ARG);
    Config.DeviceId = DeviceId;
    AvFifo_RxFifo* Loose = AvFifo_RxFifo_Alloc();
    DT_ASSERT_EQ(DtNmosAvFifo_AddReceiver(Fix.Node, Loose, &Config, TakeRx, NULL, &Id),
                 DTAPI_E_NOT_ATTACHED);
    AvFifo_RxFifo_Freep(&Loose);
    DT_ASSERT_EQ(DtNmosAvFifo_AddReceiver(Fix.Node, NULL, &Config, TakeRx, NULL, &Id),
                 DTAPI_E_INVALID_ARG);
    FreeFixture(&Fix);
    DtTest_SetCleanup(NULL, NULL);
}

// A sender of the FIFO: its flow and source the FIFO's, in the SDP the node serves, or a
// flow the program gives; a FIFO not configured has no flow to give. After the FIFO's
// format changed, UpdateSender gives the node the new flow.
DT_TEST(AddSender)
{
    Fixture Fix = {NULL};
    DtTest_SetCleanup(FreeFixture, &Fix);
    DtNmosId DeviceId;
    if (!OpenPort(&Fix, &DeviceId, DtFailures))
        return;
    DtNmosSenderConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    Config.DeviceId = DeviceId;
    Config.Label = "program out";
    DtNmosId Id;
    DT_ASSERT_OK(DtNmosAvFifo_AddSender(Fix.Node, Fix.Tx, &Config, TakeTx, NULL, &Id));
    DtNmosId Want;
    DT_ASSERT(DtNmosId_FromName(&DeviceId, "sender/program out", &Want) == DTNMOS_OK);
    DT_ASSERT_STR(Id.Text, Want.Text);
    char Url[160];
    snprintf(Url, sizeof(Url), "/x-nmos/connection/v1.1/single/senders/%s/transportfile",
             Id.Text);
    DT_ASSERT(
        Answers(Fix.Node, Url, "a=source-filter: incl IN IP4 239.1.2.3 192.168.1.10"));
    DT_ASSERT(Answers(Fix.Node, Url, "depth=10"));

    St2110_TxConfigVideo Video;
    memset(&Video, 0, sizeof(Video));
    Video.Format = St2110_TxFrameFormat_Uyvy422_8b;
    Video.Resolution.Width = 1920;
    Video.Resolution.Height = 1080;
    Video.Timing.Rate.Numerator = 25;
    Video.Timing.Rate.Denominator = 1;
    DT_ASSERT_OK(AvFifo_TxFifo_ConfigureVideo(Fix.Tx, &Video));
    DT_ASSERT_OK(DtNmosAvFifo_UpdateSender(Fix.Node, &Id, Fix.Tx));
    DT_ASSERT(Answers(Fix.Node, Url, "depth=8"));

    // A flow of the program's, with HDR the FIFO does not know.
    DtNmosFlow Flow;
    DT_ASSERT_OK(DtNmosAvFifo_FlowFromTxFifo(Fix.Tx, &Flow));
    Flow.Format.Video.Tcs = DTNMOS_TCS_HLG;
    Config.Label = "program out HLG";
    Config.Flow = &Flow;
    DT_ASSERT_OK(DtNmosAvFifo_AddSender(Fix.Node, Fix.Tx, &Config, TakeTx, NULL, &Id));
    snprintf(Url, sizeof(Url), "/x-nmos/connection/v1.1/single/senders/%s/transportfile",
             Id.Text);
    DT_ASSERT(Answers(Fix.Node, Url, "TCS=HLG"));

    AvFifo_TxFifo* Bare = AvFifo_TxFifo_Alloc();
    DT_ASSERT_OK(AvFifo_TxFifo_Attach(Bare, Fix.Device, 1));
    Config.Flow = NULL;
    Config.Label = "not configured";
    DT_ASSERT_EQ(DtNmosAvFifo_AddSender(Fix.Node, Bare, &Config, TakeTx, NULL, &Id),
                 DTAPI_E_CONFIG);
    DT_ASSERT_EQ(DtNmosAvFifo_UpdateSender(Fix.Node, &Id, Bare), DTAPI_E_CONFIG);
    AvFifo_TxFifo_Freep(&Bare);
    FreeFixture(&Fix);
    DtTest_SetCleanup(NULL, NULL);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Activations +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Whether Fifo is started: SetIpPars, which a stopped FIFO takes, is refused by a started
// one. IpPars are the FIFO's own, so that a stopped FIFO is left as it was.
static bool RxStarted(AvFifo_RxFifo* Fifo, const AvFifo_IpPars* IpPars)
{
    return AvFifo_RxFifo_SetIpPars(Fifo, IpPars) == DTAPI_E_STARTED;
}

static bool TxStarted(AvFifo_TxFifo* Fifo, const AvFifo_IpPars* IpPars)
{
    return AvFifo_TxFifo_SetIpPars(Fifo, IpPars) == DTAPI_E_STARTED;
}

// A receiver's activation becomes a change, which the FIFO takes: with the flow of a
// transport file it is configured, set and started; with the transport alone it keeps
// its configuration and moves to the new stream; disabled, it stops. A flow the format
// cannot take is refused when the change is made, before the FIFO is asked.
DT_TEST(RxChange)
{
    Fixture Fix = {NULL};
    DtTest_SetCleanup(FreeFixture, &Fix);
    DtNmosId DeviceId;
    if (!OpenPort(&Fix, &DeviceId, DtFailures))
        return;
    DtNmosReceiverActivation Activation;
    memset(&Activation, 0, sizeof(Activation));
    Activation.MasterEnable = true;
    Activation.HasFlow = true;
    Activation.Flow = *FlowOf(&Fix, VIDEO("YCbCr-4:2:2", "10"), 0);
    Activation.AtNs = 1234;
    DtNmosAvFifoRxChange Change;
    DT_ASSERT_OK(DtNmosAvFifo_RxChangeFromActivation(
        &Activation, St2110_RxFrameFormat_Uyvy422_10b, &Change));
    DT_ASSERT_EQ(Change.Size, sizeof(Change));
    DT_ASSERT(Change.MasterEnable && Change.HasConfig);
    DT_ASSERT_EQ(Change.Media, DTNMOS_MEDIA_VIDEO);
    DT_ASSERT_EQ(Change.Video.Format, St2110_RxFrameFormat_Uyvy422_10b);
    DT_ASSERT_EQ(Change.IpPars.Port, 5004);
    DT_ASSERT_EQ(Change.AtNs, 1234);
    DT_ASSERT_OK(DtNmosAvFifo_ApplyRxChange(Fix.Rx, &Change));
    DT_ASSERT(RxStarted(Fix.Rx, &Change.IpPars));

    // The transport alone: the FIFO keeps its configuration and moves.
    DtNmosAvFifoRxChange Moved;
    Activation.HasFlow = false;
    memset(&Activation.Flow.Format, 0, sizeof(Activation.Flow.Format));
    snprintf(Activation.Flow.DestinationIp, sizeof(Activation.Flow.DestinationIp),
             "239.1.2.9");
    Activation.Flow.DestinationPort = 5010;
    DT_ASSERT_OK(DtNmosAvFifo_RxChangeFromActivation(
        &Activation, St2110_RxFrameFormat_Uyvy422_10b, &Moved));
    DT_ASSERT(Moved.MasterEnable && !Moved.HasConfig);
    DT_ASSERT_EQ(Moved.Media, DTNMOS_MEDIA_NONE);
    DT_ASSERT_EQ(Moved.IpPars.IpAddr[3], 9);
    DT_ASSERT_EQ(Moved.IpPars.Port, 5010);
    DT_ASSERT_OK(DtNmosAvFifo_ApplyRxChange(Fix.Rx, &Moved));
    DT_ASSERT(RxStarted(Fix.Rx, &Moved.IpPars));

    // Disabled: the FIFO stops, whatever the flow.
    DtNmosAvFifoRxChange Parked;
    Activation.MasterEnable = false;
    Activation.Flow.DestinationIp[0] = '\0';
    DT_ASSERT_OK(DtNmosAvFifo_RxChangeFromActivation(
        &Activation, St2110_RxFrameFormat_Uyvy422_10b, &Parked));
    DT_ASSERT(!Parked.MasterEnable && !Parked.HasConfig);
    DT_ASSERT_OK(DtNmosAvFifo_ApplyRxChange(Fix.Rx, &Parked));
    DT_ASSERT(!RxStarted(Fix.Rx, &Moved.IpPars));

    // Refused before the FIFO is asked: a flow the format does not take.
    Activation.MasterEnable = true;
    Activation.HasFlow = true;
    Activation.Flow = *FlowOf(&Fix, VIDEO("YCbCr-4:2:2", "8"), 0);
    DT_ASSERT_EQ(DtNmosAvFifo_RxChangeFromActivation(
                     &Activation, St2110_RxFrameFormat_Uyvy422_10b, &Change),
                 DTAPI_E_NOT_SUPPORTED);
    Parked.Size = sizeof(Parked) - 1;
    DT_ASSERT_EQ(DtNmosAvFifo_ApplyRxChange(Fix.Rx, &Parked), DTAPI_E_INVALID_ARG);
    FreeFixture(&Fix);
    DtTest_SetCleanup(NULL, NULL);
}

// A sender's activation becomes a change of its destination and port, which the FIFO
// takes keeping its payload type and its other IP parameters; disabled, it stops. A FIFO
// without IP parameters has none to change.
DT_TEST(TxChange)
{
    Fixture Fix = {NULL};
    DtTest_SetCleanup(FreeFixture, &Fix);
    DtNmosId DeviceId;
    if (!OpenPort(&Fix, &DeviceId, DtFailures))
        return;
    DtNmosSenderActivation Activation;
    memset(&Activation, 0, sizeof(Activation));
    Activation.MasterEnable = true;
    snprintf(Activation.DestinationIp, sizeof(Activation.DestinationIp), "239.9.8.7");
    Activation.DestinationPort = 6000;
    snprintf(Activation.SourceIp, sizeof(Activation.SourceIp), "192.168.1.10");
    DtNmosAvFifoTxChange Change;
    DT_ASSERT_OK(DtNmosAvFifo_TxChangeFromActivation(&Activation, &Change));
    DT_ASSERT_EQ(Change.Size, sizeof(Change));
    DT_ASSERT_EQ(Change.IpVersion, IpProtocolVersion_IPv4);
    DT_ASSERT_EQ(Change.DestinationPort, 6000);
    DT_ASSERT_OK(DtNmosAvFifo_ApplyTxChange(Fix.Tx, &Change));
    DtNmosFlow Flow;
    DT_ASSERT_OK(DtNmosAvFifo_FlowFromTxFifo(Fix.Tx, &Flow));
    DT_ASSERT_STR(Flow.DestinationIp, "239.9.8.7");
    DT_ASSERT_EQ(Flow.DestinationPort, 6000);
    DT_ASSERT_EQ(Flow.PayloadType, 96);
    AvFifo_IpPars IpPars;
    memset(&IpPars, 0, sizeof(IpPars));
    DT_ASSERT(TxStarted(Fix.Tx, &IpPars));

    Activation.MasterEnable = false;
    DT_ASSERT_OK(DtNmosAvFifo_TxChangeFromActivation(&Activation, &Change));
    DT_ASSERT_OK(DtNmosAvFifo_ApplyTxChange(Fix.Tx, &Change));
    DT_ASSERT_OK(DtNmosAvFifo_FlowFromTxFifo(Fix.Tx, &Flow));
    DT_ASSERT_STR(Flow.DestinationIp, "239.9.8.7");

    Activation.MasterEnable = true;
    snprintf(Activation.DestinationIp, sizeof(Activation.DestinationIp), "camera.local");
    DT_ASSERT_EQ(DtNmosAvFifo_TxChangeFromActivation(&Activation, &Change),
                 DTAPI_E_NOT_SUPPORTED);
    snprintf(Activation.DestinationIp, sizeof(Activation.DestinationIp), "239.9.8.7");
    Activation.DestinationPort = 0;
    DT_ASSERT_EQ(DtNmosAvFifo_TxChangeFromActivation(&Activation, &Change),
                 DTAPI_E_INVALID_ARG);

    Activation.DestinationPort = 6000;
    DT_ASSERT_OK(DtNmosAvFifo_TxChangeFromActivation(&Activation, &Change));
    AvFifo_TxFifo* Bare = AvFifo_TxFifo_Alloc();
    DT_ASSERT_OK(AvFifo_TxFifo_Attach(Bare, Fix.Device, 1));
    DT_ASSERT_EQ(DtNmosAvFifo_ApplyTxChange(Bare, &Change), DTAPI_E_NO_IPPARS);
    AvFifo_TxFifo_Freep(&Bare);
    FreeFixture(&Fix);
    DtTest_SetCleanup(NULL, NULL);
}

// The mailbox of a FIFO's owner: the callback posts a change and waits for the result of
// applying it, which the owner posts back.
typedef struct Mailbox
{
    OsMutex* Lock;
    OsEvent* Posted;
    OsEvent* Done;
    AvFifo_RxFifo* Fifo;
    bool Quit;
    DtNmosAvFifoRxChange Change;
    DtapiResult Result;
    int Applied;
} Mailbox;

// The FIFO's owner: applies each change it is given, between the frames it would read.
static void Owner(void* Context)
{
    Mailbox* Box = (Mailbox*)Context;
    for (;;)
    {
        OsEvent_Wait(Box->Posted, -1);
        OsMutex_Lock(Box->Lock);
        const bool Quit = Box->Quit;
        const DtNmosAvFifoRxChange Change = Box->Change;
        OsMutex_Unlock(Box->Lock);
        if (Quit)
            return;
        const DtapiResult Result = DtNmosAvFifo_ApplyRxChange(Box->Fifo, &Change);
        OsMutex_Lock(Box->Lock);
        Box->Result = Result;
        Box->Applied++;
        OsMutex_Unlock(Box->Lock);
        OsEvent_Set(Box->Done);
    }
}

// The receiver's callback: makes the change, posts it, and answers with the owner's
// result.
static DtNmosResult HandToOwner(void* User, const DtNmosId* Receiver,
                                const DtNmosReceiverActivation* Activation)
{
    (void)Receiver;
    Mailbox* Box = (Mailbox*)User;
    DtNmosAvFifoRxChange Change;
    if (DtNmosAvFifo_RxChangeFromActivation(Activation, St2110_RxFrameFormat_Raw,
                                            &Change) != DTAPI_OK)
        return DtNmos_SetLastError(DTNMOS_E_INVALID_ARGUMENT, GetLastException());
    OsMutex_Lock(Box->Lock);
    Box->Change = Change;
    OsMutex_Unlock(Box->Lock);
    OsEvent_Set(Box->Posted);
    if (OsEvent_Wait(Box->Done, 5000) != OS_WAIT_SIGNALLED)
        return DtNmos_SetLastError(DTNMOS_E_TIMEOUT, "The FIFO's owner did not answer");
    OsMutex_Lock(Box->Lock);
    const DtapiResult Result = Box->Result;
    OsMutex_Unlock(Box->Lock);
    return Result == DTAPI_OK ? DTNMOS_OK
                              : DtNmos_SetLastError(DTNMOS_E_STATE, "The FIFO refused");
}

// The status of a PATCH of the staged parameters of receiver Id with Body.
static int Patch(DtNmosNode* Node, const DtNmosId* Id, const char* Body)
{
    char Url[160];
    snprintf(Url, sizeof(Url), "/x-nmos/connection/v1.1/single/receivers/%s/staged",
             Id->Text);
    DtNmosHttpRequest Request;
    memset(&Request, 0, sizeof(Request));
    Request.Size = sizeof(Request);
    Request.Method = "PATCH";
    Request.Url = Url;
    Request.ContentType = "application/json";
    Request.Body = Body;
    Request.BodyLength = strlen(Body);
    DtNmosHttpResponse* Response = DtNmosHttpResponse_Alloc();
    int Status = 0;
    if (Response != NULL && DtNmosNode_Handle(Node, &Request, Response) == DTNMOS_OK)
        Status = DtNmosHttpResponse_Status(Response);
    DtNmosHttpResponse_Free(Response);
    return Status;
}

// A controller's activation reaches the FIFO through its owner: the callback, on the
// node's thread, posts the change and waits; the owner, a thread of its own, applies it.
// A flow the FIFO cannot take is refused in the callback, and the owner is not asked.
DT_TEST(ActivationReachesTheOwner)
{
    Fixture Fix = {NULL};
    DtTest_SetCleanup(FreeFixture, &Fix);
    DtNmosId DeviceId;
    if (!OpenPort(&Fix, &DeviceId, DtFailures))
        return;
    Mailbox Box;
    memset(&Box, 0, sizeof(Box));
    Box.Lock = OsMutex_Create();
    Box.Posted = OsEvent_Create();
    Box.Done = OsEvent_Create();
    Box.Fifo = Fix.Rx;
    OsThread* Thread = OsThread_Start(Owner, &Box);
    DT_ASSERT(Box.Lock != NULL && Box.Posted != NULL && Box.Done != NULL &&
              Thread != NULL);

    DtNmosReceiverConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    Config.DeviceId = DeviceId;
    Config.Label = "monitor";
    Config.Media = DTNMOS_MEDIA_VIDEO;
    DtNmosId Id;
    DT_ASSERT_OK(
        DtNmosAvFifo_AddReceiver(Fix.Node, Fix.Rx, &Config, HandToOwner, &Box, &Id));
    DT_ASSERT_EQ(Patch(Fix.Node, &Id,
                       "{\"master_enable\": true, "
                       "\"activation\": {\"mode\": \"activate_immediate\"}, "
                       "\"transport_file\": {\"type\": \"application/sdp\", \"data\": "
                       "\"v=0\\no=- 1 1 IN IP4 192.168.1.7\\ns=peer\\nt=0 0\\n"
                       "m=video 5000 RTP/AVP 96\\nc=IN IP4 239.1.1.1/64\\n"
                       "a=rtpmap:96 raw/90000\\na=fmtp:96 sampling=YCbCr-4:2:2; "
                       "width=1920; height=1080; exactframerate=25; depth=10\\n\"}}"),
                 200);
    DT_ASSERT_EQ(Box.Applied, 1);
    DT_ASSERT_EQ(Box.Change.IpPars.Port, 5000);
    DT_ASSERT(RxStarted(Fix.Rx, &Box.Change.IpPars));

    DT_ASSERT_EQ(Patch(Fix.Node, &Id,
                       "{\"master_enable\": false, "
                       "\"activation\": {\"mode\": \"activate_immediate\"}}"),
                 200);
    DT_ASSERT_EQ(Box.Applied, 2);
    DT_ASSERT(!RxStarted(Fix.Rx, &Box.Change.IpPars));

    // ST 2110-22, which the FIFO does not decode: refused before the owner is asked.
    DT_ASSERT(Patch(Fix.Node, &Id,
                    "{\"master_enable\": true, "
                    "\"activation\": {\"mode\": \"activate_immediate\"}, "
                    "\"transport_file\": {\"type\": \"application/sdp\", \"data\": "
                    "\"v=0\\no=- 1 1 IN IP4 192.168.1.7\\ns=peer\\nt=0 0\\n"
                    "m=video 5000 RTP/AVP 98\\nc=IN IP4 239.1.1.1/64\\n"
                    "a=rtpmap:98 jxsv/90000\\n\"}}") != 200);
    DT_ASSERT_EQ(Box.Applied, 2);

    OsMutex_Lock(Box.Lock);
    Box.Quit = true;
    OsMutex_Unlock(Box.Lock);
    OsEvent_Set(Box.Posted);
    OsThread_Join(Thread);
    OsEvent_Destroy(Box.Done);
    OsEvent_Destroy(Box.Posted);
    OsMutex_Destroy(Box.Lock);
    FreeFixture(&Fix);
    DtTest_SetCleanup(NULL, NULL);
}

DT_TEST_MAIN("NmosAvFifo", DT_RUN(LinksDtnmos), DT_RUN(RxVideo), DT_RUN(RxVideoFormats),
             DT_RUN(RxAudio), DT_RUN(RxRefused), DT_RUN(RxArguments), DT_RUN(RxIpV6),
             DT_RUN(TxVideo), DT_RUN(TxAudio), DT_RUN(TxRefused),
             DT_RUN(TxRoundTripVideo), DT_RUN(TxRoundTripAudio), DT_RUN(TxFifoNotReady),
             DT_RUN(TxPtpRefClock), DT_RUN(NodeClockFromPort), DT_RUN(AddDevice),
             DT_RUN(AddReceiver), DT_RUN(AddSender), DT_RUN(RxChange), DT_RUN(TxChange),
             DT_RUN(ActivationReachesTheOwner))
