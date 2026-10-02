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
#include "DtTest.h"           // Test framework.
#include "cdtapi_constants.h" // Result codes.
#include "cdtapi_nmos.h"      // Functions under test.

// dtnmos includes
#include "dtnmos_node.h" // DtNmos_HasServer, through the library's own link.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The session of every SDP here, from the sender 192.168.39.10.
#define SESSION                                                                          \
    "v=0\r\n"                                                                            \
    "o=- 1 1 IN IP4 192.168.39.10\r\n"                                                   \
    "s=Test\r\n"                                                                         \
    "t=0 0\r\n"

// A video flow to 239.1.2.3:5004 from 192.168.39.10, of the sampling and depth given.
#define VIDEO(Sampling, Depth)                                                           \
    SESSION "m=video 5004 RTP/AVP 96\r\n"                                                \
            "c=IN IP4 239.1.2.3/64\r\n"                                                  \
            "a=source-filter: incl IN IP4 239.1.2.3 192.168.39.10\r\n"                   \
            "a=rtpmap:96 raw/90000\r\n"                                                  \
            "a=fmtp:96 sampling=" Sampling "; width=1920; height=1080; "                 \
            "exactframerate=25; depth=" Depth "; TCS=SDR; colorimetry=BT709; "           \
            "PM=2110GPM; SSN=ST2110-20:2017; TP=2110TPN\r\n"                             \
            "a=mediaclk:direct=0\r\n"

// An audio flow of two channels at 48 kHz to 239.1.2.4:5006, of the encoding given.
#define AUDIO(Encoding)                                                                  \
    SESSION "m=audio 5006 RTP/AVP 97\r\n"                                                \
            "c=IN IP4 239.1.2.4/64\r\n"                                                  \
            "a=rtpmap:97 " Encoding "/48000/2\r\n"                                       \
            "a=ptime:1\r\n"                                                              \
            "a=mediaclk:direct=0\r\n"

typedef struct Fixture
{
    DtNmosSdp* Sdp;
} Fixture;

static void FreeFixture(void* Context)
{
    Fixture* Fix = (Fixture*)Context;
    DtNmosSdp_Free(Fix->Sdp);
    Fix->Sdp = NULL;
}

// Parses Text into the fixture and returns its flow at Index, or NULL.
static const DtNmosFlow* FlowOf(Fixture* Fix, const char* Text, size_t Index)
{
    FreeFixture(Fix);
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
    DT_ASSERT_EQ(DtapiHasNmos(), 1);
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
    snprintf(Flow.Format.Audio.Encoding, sizeof(Flow.Format.Audio.Encoding), "opus");
    DT_ASSERT_EQ(DtNmosAvFifo_RxConfigFromFlow(&Flow, St2110_RxFrameFormat_Raw, &Video,
                                               &Audio, &IpPars),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT(strstr(GetLastException(), "opus") != NULL);
}

// Arguments that are wrong: no flow or IP parameters, a flow smaller than the bridge's,
// no configuration of the flow's media, a name or no address, sources of another IP
// version, and no port.
DT_TEST(RxArguments)
{
    St2110_RxConfigVideo Video;
    AvFifo_IpPars IpPars;
    DtNmosFlow Flow = HandFlow(DTNMOS_MEDIA_VIDEO);
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

DT_TEST_MAIN("NmosAvFifo", DT_RUN(LinksDtnmos), DT_RUN(RxVideo), DT_RUN(RxVideoFormats),
             DT_RUN(RxAudio), DT_RUN(RxRefused), DT_RUN(RxArguments), DT_RUN(RxIpV6))
