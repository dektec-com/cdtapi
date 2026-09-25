// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestSimAvFifo.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The receive and transmit FIFOs against the emulated DTA-2110
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPI_SIM=1. The card's clock stands still unless a test
// advances it, so that packets leave and arrive when the test says. Every case ends with
// no handle, pipe, socket, membership or allocation left.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "AvFifo/DtAvError.h"   // The failure text's size.
#include "AvFifo/DtAvPixConv.h" // The reference conversions.
#include "AvFifo/DtAvTime.h"    // Times of day.
#include "Core/DtAlloc.h"       // Live allocations.
#include "Device/DtDevice.h"    // The port's capabilities.
#include "DtPcieAbi.h"          // Pipe modes and filter flags.
#include "DtTest.h"             // Test framework.
#include "OAL/OsThread.h"       // Sleeping and a second thread.
#include "OAL/Sim/SimDtPcie.h"  // The emulated devices and their controls.
#include "OAL/Sim/SimDta2110.h" // The DTA-2110's identity.
#include "OAL/Sim/SimNet.h"     // The emulated network.
#include "OAL/Sim/SimNw.h"      // The network function's controls.
#include "cdtapi.h"             // Devices.
#include "cdtapi_avfifo.h"      // Functions under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define INDEX 1
#define T0 (UINT64_C(1800000000) * DT_AV_NS_PER_SEC)
#define MS UINT64_C(1000000)
#define WIDTH 320
#define HEIGHT 240

static const uint8_t Group[4] = {239, 1, 2, 3};

typedef struct Fixture
{
    DtDevice* Device;
    int Live;
} Fixture;

// The DTA-2110 attached, its clock stopped at T0 and its loopback on.
static bool Open(Fixture* Fix, int* DtFailures)
{
    SimDtPcie_Reset();
    Fix->Live = DtAlloc_NumLive();
    SimDtPcie_SetDta2110Index(INDEX);
    SimDtPcie_SetNwTime(T0);
    SimDtPcie_SetNwLoopback(true);
    Fix->Device = DtDevice_Alloc();
    if (Fix->Device == NULL ||
        DtDevice_AttachToSerial(Fix->Device, (int64_t)SIM_DTA2110_SERIAL) != DTAPI_OK)
    {
        printf("    FAIL: no emulated DTA-2110; is CDTAPI_SIM=1 set?\n");
        (*DtFailures)++;
        DtDevice_Free(Fix->Device);
        return false;
    }
    return true;
}

// Frees the device and checks that nothing is left.
#define FINISH(Fix)                                                                      \
    do                                                                                   \
    {                                                                                    \
        DtDevice_Free((Fix).Device);                                                     \
        DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);                                    \
        DT_ASSERT_EQ(SimDtPcie_OpenNetSocketCount(), 0);                                 \
        DT_ASSERT_EQ(SimDtPcie_NetMembershipCount(), 0);                                 \
        SimDtPcie_Reset();                                                               \
        DT_ASSERT_EQ(DtAlloc_NumLive(), (Fix).Live);                                     \
    } while (0)

// IP parameters of the multicast stream, with one source of 192.168.1.50 when Source.
static AvFifo_IpPars Pars(int Port, bool Source, IpSrcFlt* Sources)
{
    static const uint8_t SourceIp[4] = {192, 168, 1, 50};
    AvFifo_IpPars P;
    memset(&P, 0, sizeof(P));
    memcpy(P.IpAddr, Group, 4);
    P.Port = Port;
    P.TimeToLive = 32;
    P.DiffServ = 0x88;
    P.RtpPayloadType = 98;
    if (Source)
    {
        memset(Sources, 0, sizeof(*Sources));
        memcpy(Sources[0].IpAddr, SourceIp, 4);
        Sources[0].Port = -1;
        P.SrcFlt = Sources;
        P.NSrcFlt = 1;
    }
    return P;
}

// The video both FIFOs use.
static St2110_TxConfigVideo VideoConfig(St2110_TxFrameFormat Format)
{
    St2110_TxConfigVideo Config;
    memset(&Config, 0, sizeof(Config));
    Config.Format = Format;
    Config.Packing.PayloadSize = -1;
    Config.Resolution.Width = WIDTH;
    Config.Resolution.Height = HEIGHT;
    Config.Timing.Rate.Numerator = 25;
    Config.Timing.Rate.Denominator = 1;
    return Config;
}

// Advances the clock in 5 ms steps, giving the FIFOs' threads time, until Done says so
// or two seconds of the clock have passed.
typedef bool (*Condition)(void* Context);
static bool RunUntil(Condition Done, void* Context)
{
    for (int Step = 0; Step < 400; Step++)
    {
        if (Done(Context))
            return true;
        SimDtPcie_AdvanceNwTime(5 * MS);
        OsTime_SleepMs(3);
    }
    return Done(Context);
}

typedef struct LoadWanted
{
    AvFifo_RxFifo* Fifo;
    int Load;
} LoadWanted;

static bool HasLoad(void* Context)
{
    const LoadWanted* W = (const LoadWanted*)Context;
    return AvFifo_RxFifo_GetFifoLoad(W->Fifo) >= W->Load;
}

// The first pipe from First to Last that is in use, with its state in *State; 0 when
// none is.
static int PipeInUse(int First, int Last, SimNwPipeState* State)
{
    for (int Id = First; Id <= Last; Id++)
    {
        SimDtPcie_GetNwPipeState(Id, State);
        if (State->InUse)
            return Id;
    }
    return 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Lifecycle +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(ResultsOfTheLifecycle)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;
    AvFifo_RxFifo* Rx = AvFifo_RxFifo_Alloc();
    AvFifo_TxFifo* Tx = AvFifo_TxFifo_Alloc();
    DT_ASSERT(Rx != NULL && Tx != NULL);
    IpSrcFlt Sources[3];
    AvFifo_IpPars P = Pars(5004, false, Sources);
    const St2110_RxConfigVideo RxVideo = {St2110_RxFrameFormat_Uyvy422_8b};
    const St2110_TxConfigVideo TxVideo = VideoConfig(St2110_TxFrameFormat_Uyvy422_8b);

    DT_ASSERT_EQ(AvFifo_RxFifo_Start(Rx), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT(strstr(GetLastException(), "AvFifo_RxFifo_Start") != NULL);
    DT_ASSERT_EQ(AvFifo_RxFifo_ConfigureVideo(Rx, &RxVideo), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(AvFifo_RxFifo_Attach(Rx, NULL, 1), DTAPI_E_DEVICE);
    DT_ASSERT_EQ(AvFifo_RxFifo_Attach(Rx, Fix.Device, 0), DTAPI_E_NO_SUCH_PORT);
    DT_ASSERT_EQ(AvFifo_RxFifo_Attach(Rx, Fix.Device, 2), DTAPI_E_NO_SUCH_PORT);
    DT_ASSERT_EQ(AvFifo_RxFifo_Attach2(Rx, Fix.Device, 1, (HwOrSwPipe)9),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_OK(AvFifo_RxFifo_Attach(Rx, Fix.Device, 1));
    DT_ASSERT_EQ(AvFifo_RxFifo_Attach(Rx, Fix.Device, 1), DTAPI_E_ATTACHED);
    DT_ASSERT_EQ(AvFifo_RxFifo_Start(Rx), DTAPI_E_CONFIG);
    DT_ASSERT_OK(AvFifo_RxFifo_ConfigureVideo(Rx, &RxVideo));
    DT_ASSERT_EQ(AvFifo_RxFifo_Start(Rx), DTAPI_E_NO_IPPARS);
    int UsesHw = -1;
    DT_ASSERT_EQ(AvFifo_RxFifo_UsesHwPipe(Rx, &UsesHw), DTAPI_E_NOT_STARTED);

    // IP parameters that are refused.
    IpSrcFlt Four[4];
    memset(Four, 0, sizeof(Four));
    P.SrcFlt = Four;
    P.NSrcFlt = 4;
    DT_ASSERT_EQ(AvFifo_RxFifo_SetIpPars(Rx, &P), DTAPI_E_INVALID_ARG);
    P.NSrcFlt = 2;
    Four[1].IpAddr[3] = 1;
    DT_ASSERT_EQ(AvFifo_RxFifo_SetIpPars(Rx, &P), DTAPI_E_INVALID_ARG);
    P.NSrcFlt = 0;
    P.Port = 70000;
    DT_ASSERT_EQ(AvFifo_RxFifo_SetIpPars(Rx, &P), DTAPI_E_INVALID_ARG);
    P.Port = 5004;
    DT_ASSERT_OK(AvFifo_RxFifo_SetIpPars(Rx, &P));

    // The transmit side.
    DT_ASSERT(AvFifo_TxFifo_GetFromMemPool(Tx, 100) == NULL);
    DT_ASSERT_OK(AvFifo_TxFifo_Attach2(Tx, Fix.Device, 1, HwOrSwPipe_UseSwPipe));
    DT_ASSERT_OK(AvFifo_TxFifo_UsesHwPipe(Tx, &UsesHw));
    DT_ASSERT_EQ(UsesHw, 0);
    DT_ASSERT_OK(AvFifo_TxFifo_ConfigureVideo(Tx, &TxVideo));
    AvFifo_Frame* Frame = AvFifo_TxFifo_GetFromMemPool(Tx, WIDTH * 2 * HEIGHT);
    DT_ASSERT(Frame != NULL);
    DT_ASSERT_EQ(AvFifo_TxFifo_Write(Tx, Frame), DTAPI_E_NOT_STARTED);
    P.RtpPayloadType = 128;
    DT_ASSERT_EQ(AvFifo_TxFifo_SetIpPars(Tx, &P), DTAPI_E_INVALID_ARG);
    P.RtpPayloadType = 98;
    St2110_TxConfigVideo Odd = TxVideo;
    Odd.Resolution.Width = 321;
    DT_ASSERT_EQ(AvFifo_TxFifo_ConfigureVideo(Tx, &Odd), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(AvFifo_TxFifo_Stop(Tx), DTAPI_OK);

    DT_ASSERT_OK(AvFifo_RxFifo_Detach(Rx));
    DT_ASSERT_EQ(AvFifo_RxFifo_Detach(Rx), DTAPI_E_NOT_ATTACHED);
    AvFifo_RxFifo_Freep(&Rx);
    DT_ASSERT(Rx == NULL);
    AvFifo_TxFifo_Free(Tx);
    AvFifo_RxFifo_Free(NULL);
    DT_ASSERT_EQ(AvFifo_RxFifo_GetFifoLoad(NULL), 0);
    DT_ASSERT(AvFifo_RxFifo_Read(NULL) == NULL);
    FINISH(Fix);
}

// The DTA-2178's SDI ports have no A/V FIFO.
DT_TEST(SdiPortIsRefused)
{
    SimDtPcie_Reset();
    int Live = DtAlloc_NumLive();
    DtDevice* Device = DtDevice_Alloc();
    AvFifo_RxFifo* Rx = AvFifo_RxFifo_Alloc();
    DT_ASSERT(Device != NULL && Rx != NULL);
    DT_ASSERT_OK(DtDevice_AttachToSerial(Device, (int64_t)SIM_SERIAL));
    DT_ASSERT_EQ(AvFifo_RxFifo_Attach(Rx, Device, 1), DTAPI_E_NOT_SUPPORTED);
    AvFifo_RxFifo_Free(Rx);
    DtDevice_Free(Device);
    DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);
    SimDtPcie_Reset();
    DT_ASSERT_EQ(DtAlloc_NumLive(), Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Starting +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Starts a receive FIFO of 8-bit video on the stream, or gives the failure.
static DtapiResult StartRx(AvFifo_RxFifo* Rx, DtDevice* Device, HwOrSwPipe Pipe,
                           const AvFifo_IpPars* P)
{
    const St2110_RxConfigVideo Video = {St2110_RxFrameFormat_Uyvy422_8b};
    DtapiResult Result = AvFifo_RxFifo_Attach2(Rx, Device, 1, Pipe);
    if (Result == DTAPI_OK || Result == DTAPI_E_ATTACHED)
        Result = AvFifo_RxFifo_ConfigureVideo(Rx, &Video);
    if (Result == DTAPI_OK)
        Result = AvFifo_RxFifo_SetIpPars(Rx, P);
    if (Result == DTAPI_OK)
        Result = AvFifo_RxFifo_Start(Rx);
    return Result;
}

DT_TEST(StartFailures)
{
    static const uint8_t Unknown[4] = {192, 168, 1, 77};
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;
    AvFifo_RxFifo* Rx = AvFifo_RxFifo_Alloc();
    AvFifo_TxFifo* Tx = AvFifo_TxFifo_Alloc();
    DT_ASSERT(Rx != NULL && Tx != NULL);
    IpSrcFlt Sources[3];
    AvFifo_IpPars P = Pars(5004, false, Sources);
    SimNwPipeState State;

    SimDtPcie_SetNwLink(false);
    DT_ASSERT_EQ(StartRx(Rx, Fix.Device, HwOrSwPipe_Auto, &P), DTAPI_E_NO_LINK);
    SimDtPcie_SetNwLink(true);
    SimDtPcie_SetNetInterfaceUp(SIM_NET_DTA2110_INDEX, false, false);
    DT_ASSERT_EQ(StartRx(Rx, Fix.Device, HwOrSwPipe_Auto, &P), DTAPI_E_DISABLED);
    SimDtPcie_SetNetInterfaceUp(SIM_NET_DTA2110_INDEX, true, true);
    SimDtPcie_ClearNetAddresses(SIM_NET_DTA2110_INDEX, false);
    DT_ASSERT_EQ(StartRx(Rx, Fix.Device, HwOrSwPipe_Auto, &P),
                 DTAPI_E_NO_ADAPTER_IP_ADDR);
    SimDtPcie_SetDta2110Index(INDEX);
    SimDtPcie_FailNetBind(true);
    DT_ASSERT_EQ(StartRx(Rx, Fix.Device, HwOrSwPipe_Auto, &P), DTAPI_E_BIND);
    SimDtPcie_FailNetBind(false);
    P.Vlan.Id = 100;
    DT_ASSERT_EQ(StartRx(Rx, Fix.Device, HwOrSwPipe_Auto, &P), DTAPI_E_VLAN_NOT_FOUND);
    P.Vlan.Id = 0;

    // A failed join leaves nothing behind.
    SimDtPcie_FailNetJoin(true);
    DT_ASSERT_EQ(StartRx(Rx, Fix.Device, HwOrSwPipe_Auto, &P), DTAPI_E_MULTICASTJOIN);
    SimDtPcie_FailNetJoin(false);
    DT_ASSERT(strstr(GetLastException(), "multicast") != NULL);
    DT_ASSERT_EQ(PipeInUse(SIM_NW_FIRST_RX_HWP, SIM_NW_FIRST_SWP + 8, &State), 0);
    DT_ASSERT_EQ(SimDtPcie_OpenNetSocketCount(), 0);
    int UsesHw = -1;
    DT_ASSERT_EQ(AvFifo_RxFifo_UsesHwPipe(Rx, &UsesHw), DTAPI_E_NOT_STARTED);

    // An unknown unicast destination has no MAC address.
    const St2110_TxConfigVideo Video = VideoConfig(St2110_TxFrameFormat_Uyvy422_8b);
    AvFifo_IpPars Unicast = P;
    memcpy(Unicast.IpAddr, Unknown, 4);
    DT_ASSERT_OK(AvFifo_TxFifo_Attach(Tx, Fix.Device, 1));
    DT_ASSERT_OK(AvFifo_TxFifo_ConfigureVideo(Tx, &Video));
    DT_ASSERT_OK(AvFifo_TxFifo_SetIpPars(Tx, &Unicast));
    DT_ASSERT_EQ(AvFifo_TxFifo_Start(Tx), DTAPI_E_DST_MAC_ADDR);
    DT_ASSERT_EQ(PipeInUse(SIM_NW_FIRST_TX_HWP, SIM_NW_FIRST_TX_HWP + 2, &State), 0);

    // Three receive FIFOs take the hardware pipes; a forced fourth gets none, a
    // preferring one a software pipe.
    AvFifo_RxFifo* Hw[3];
    for (int i = 0; i < 3; i++)
    {
        Hw[i] = AvFifo_RxFifo_Alloc();
        DT_ASSERT(Hw[i] != NULL);
        DT_ASSERT_OK(StartRx(Hw[i], Fix.Device, HwOrSwPipe_ForceHwPipe, &P));
        DT_ASSERT_OK(AvFifo_RxFifo_UsesHwPipe(Hw[i], &UsesHw));
        DT_ASSERT_EQ(UsesHw, 1);
    }
    DT_ASSERT_OK(AvFifo_RxFifo_Detach(Rx));
    DT_ASSERT_EQ(StartRx(Rx, Fix.Device, HwOrSwPipe_ForceHwPipe, &P),
                 DTAPI_E_OUT_OF_RESOURCES);
    DT_ASSERT_OK(AvFifo_RxFifo_Detach(Rx));
    DT_ASSERT_OK(StartRx(Rx, Fix.Device, HwOrSwPipe_PreferHwPipe, &P));
    DT_ASSERT_OK(AvFifo_RxFifo_UsesHwPipe(Rx, &UsesHw));
    DT_ASSERT_EQ(UsesHw, 0);
    DT_ASSERT_EQ(AvFifo_RxFifo_Start(Rx), DTAPI_E_STARTED);
    DT_ASSERT_EQ(AvFifo_RxFifo_Clear(Rx), DTAPI_E_STARTED);
    for (int i = 0; i < 3; i++)
        AvFifo_RxFifo_Free(Hw[i]);

    AvFifo_RxFifo_Free(Rx);
    AvFifo_TxFifo_Free(Tx);
    FINISH(Fix);
}

// A started receive FIFO runs a pipe with its filter and a membership; stopped, neither.
DT_TEST(StartedAndStopped)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;
    AvFifo_RxFifo* Rx = AvFifo_RxFifo_Alloc();
    DT_ASSERT(Rx != NULL);
    IpSrcFlt Sources[3];
    AvFifo_IpPars P = Pars(5004, true, Sources);
    Sources[0].Port = 6000;
    DT_ASSERT_OK(StartRx(Rx, Fix.Device, HwOrSwPipe_Auto, &P));

    SimNwPipeState State;
    int Id = PipeInUse(SIM_NW_FIRST_RX_HWP, SIM_NW_FIRST_RX_HWP + 2, &State);
    DT_ASSERT(Id != 0);
    DT_ASSERT_EQ(State.OpMode, DT_PIPE_OPMODE_RUN);
    DT_ASSERT(State.BufferRegistered && State.BufferSize >= 64 * 1024 * 1024);
    DT_ASSERT(State.FilterSet);
    DT_ASSERT_EQ(State.FilterFlags,
                 DT_PIPE_IPFLT_FLAG_EN_FILT | DT_PIPE_IPFLT_FLAG_EN_DSTIP_IPV4 |
                     DT_PIPE_IPFLT_FLAG_EN_DSTPORT0 | DT_PIPE_IPFLT_FLAG_EN_SRCIP_IPV4 |
                     DT_PIPE_IPFLT_FLAG_EN_SRCPORT0);
    DT_ASSERT_EQ(SimDtPcie_NetMembershipCount(), 1);
    SimNetMembership Joined;
    DT_ASSERT(SimDtPcie_GetNetMembership(0, &Joined));
    DT_ASSERT(Joined.HasSource);
    DT_ASSERT_MEM(Joined.Group, Group, 4);
    DT_ASSERT_EQ(Joined.Port, 5004);
    DT_ASSERT_EQ(Joined.IfIndex, SIM_NET_DTA2110_INDEX);

    DT_ASSERT_OK(AvFifo_RxFifo_Stop(Rx));
    SimDtPcie_GetNwPipeState(Id, &State);
    DT_ASSERT(!State.InUse && !State.BufferRegistered);
    DT_ASSERT_EQ(SimDtPcie_NetMembershipCount(), 0);
    DT_ASSERT_EQ(SimDtPcie_OpenNetSocketCount(), 0);
    DT_ASSERT_OK(AvFifo_RxFifo_Stop(Rx));

    // It starts again.
    DT_ASSERT_OK(AvFifo_RxFifo_Start(Rx));
    DT_ASSERT_EQ(SimDtPcie_NetMembershipCount(), 1);
    AvFifo_RxFifo_Free(Rx);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static bool HasMarker(void* Context)
{
    (void)Context;
    SimNwPacket Packet;
    int Count = SimDtPcie_NwSentCount();
    return Count > 0 && SimDtPcie_GetNwSent(Count - 1, &Packet) &&
           (Packet.Frame[43] & 0x80) != 0;
}

// The packets of a frame on the wire: addresses, ports, RTP header, and times.
DT_TEST(PacketsOnTheWire)
{
    static const uint8_t SrcMac[6] = SIM_DTA2110_MAC_ADDRESS;
    static const uint8_t GroupMac[6] = {0x01, 0x00, 0x5E, 0x01, 0x02, 0x03};
    static const uint8_t SrcIp[4] = SIM_NET_DTA2110_IPV4;
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;
    AvFifo_TxFifo* Tx = AvFifo_TxFifo_Alloc();
    DT_ASSERT(Tx != NULL);
    IpSrcFlt Sources[3];
    const AvFifo_IpPars P = Pars(5004, false, Sources);
    const St2110_TxConfigVideo Video = VideoConfig(St2110_TxFrameFormat_Uyvy422_8b);
    DT_ASSERT_OK(AvFifo_TxFifo_Attach(Tx, Fix.Device, 1));
    DT_ASSERT_OK(AvFifo_TxFifo_ConfigureVideo(Tx, &Video));
    DT_ASSERT_OK(AvFifo_TxFifo_SetIpPars(Tx, &P));
    DT_ASSERT_OK(AvFifo_TxFifo_Start(Tx));
    int UsesHw = -1;
    DT_ASSERT_OK(AvFifo_TxFifo_UsesHwPipe(Tx, &UsesHw));
    DT_ASSERT_EQ(UsesHw, 1);

    AvFifo_Frame* Frame = AvFifo_TxFifo_GetFromMemPool(Tx, WIDTH * 2 * HEIGHT);
    DT_ASSERT(Frame != NULL);
    for (int i = 0; i < WIDTH * 2 * HEIGHT; i++)
        Frame->Data[i] = (uint8_t)i;
    Frame->NumValidBytes = WIDTH * 2 * HEIGHT - 2;
    DT_ASSERT_EQ(AvFifo_TxFifo_Write(Tx, Frame), DTAPI_E_INVALID_FORMAT);
    Frame->NumValidBytes = WIDTH * 2 * HEIGHT;
    Frame->RtpTime = 0x01020304;
    Frame->ToD = DtAvTime_FromNs(T0 + 100 * MS);
    DT_ASSERT_OK(AvFifo_TxFifo_Write(Tx, Frame));

    // The packets up to the one with the marker carry the 240 rows of 640 bytes each.
    DT_ASSERT(RunUntil(HasMarker, NULL));
    DT_ASSERT_EQ(AvFifo_TxFifo_GetStatistics(Tx).FramesOk, 1);
    const int Packets = SimDtPcie_NwSentCount();
    int64_t RowBytes = 0;
    for (int i = 0; i < Packets; i++)
    {
        SimNwPacket Packet;
        DT_ASSERT(SimDtPcie_GetNwSent(i, &Packet));
        DT_ASSERT_EQ((Packet.Frame[43] & 0x80) != 0, i == Packets - 1);
        const uint8_t* Srd = Packet.Frame + 42 + 12 + 2;
        for (bool More = true; More; Srd += 6)
        {
            RowBytes += Srd[0] << 8 | Srd[1];
            More = (Srd[4] & 0x80) != 0;
        }
    }
    DT_ASSERT_EQ(RowBytes, (int64_t)WIDTH * 2 * HEIGHT);

    SimNwPacket Sent;
    DT_ASSERT(SimDtPcie_GetNwSent(0, &Sent));
    const uint8_t* F = Sent.Frame;
    DT_ASSERT_MEM(F, GroupMac, 6);
    DT_ASSERT_MEM(F + 6, SrcMac, 6);
    DT_ASSERT_EQ(F[14], 0x45);
    DT_ASSERT_EQ(F[15], 0x88);
    DT_ASSERT_EQ(F[22], 32);
    DT_ASSERT_MEM(F + 26, SrcIp, 4);
    DT_ASSERT_MEM(F + 30, Group, 4);
    DT_ASSERT((F[34] << 8 | F[35]) >= 49152);
    DT_ASSERT_EQ(F[36] << 8 | F[37], 5004);
    DT_ASSERT_EQ(F[42], 0x80);
    DT_ASSERT_EQ(F[43], 98);
    DT_ASSERT_EQ(F[46] << 24 | F[47] << 16 | F[48] << 8 | F[49], 0x01020304);
    SimNwPipeState State;
    int Id = PipeInUse(SIM_NW_FIRST_TX_HWP, SIM_NW_FIRST_TX_HWP + 2, &State);
    DT_ASSERT(Id != 0);
    DT_ASSERT_EQ(Sent.PipeId, Id);
    // The SSRC: the pipe's UUID in little-endian bytes. The DTA-2110's network function
    // has UUID DF flag | 1, so the pipe's is that with its number in bits 20 and up.
    uint32_t Ssrc = (uint32_t)F[50] | (uint32_t)F[51] << 8 | (uint32_t)F[52] << 16 |
                    (uint32_t)F[53] << 24;
    DT_ASSERT_EQ(Ssrc >> 20, (uint32_t)Id);
    // 28/750 of 40 ms after the frame, less the output delay.
    DT_ASSERT_EQ(Sent.TodNs, T0 + 100 * MS + 1493333 - 14800);

    AvFifo_TxFifo_Free(Tx);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Loopback +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// What a receive format makes of a transmitted frame of Size bytes, into Expected.
typedef void (*Expectation)(const uint8_t* Frame, size_t Size, uint8_t* Expected);

static void Pgroups(const uint8_t* Frame, size_t Size, uint8_t* Expected)
{
    DtAvPixConv_C()->Uyvy10ToPg10(Frame, Expected, Size / 5);
}

static void Same(const uint8_t* Frame, size_t Size, uint8_t* Expected)
{
    memcpy(Expected, Frame, Size);
}

static void To8Bit(const uint8_t* Frame, size_t Size, uint8_t* Expected)
{
    uint8_t* Pg = (uint8_t*)malloc(Size);
    if (Pg == NULL)
        return;
    DtAvPixConv_C()->Uyvy10ToPg10(Frame, Pg, Size / 5);
    DtAvPixConv_C()->Pg10ToUyvy8(Pg, Expected, Size / 5);
    free(Pg);
}

static void Planar(const uint8_t* Frame, size_t Size, uint8_t* Expected)
{
    size_t N = Size / 4;
    DtAvPixConv_C()->Uyvy8ToYuv422p(Frame, N, Expected, Expected + 2 * N,
                                    Expected + 3 * N);
}

// Sends three frames of video and receives the two after the first, which teaches the
// receiver the size, comparing them with what Expect makes of the frame.
static void CheckLoopback(St2110_TxFrameFormat TxFormat, St2110_RxFrameFormat RxFormat,
                          HwOrSwPipe Pipe, Expectation Expect, int* DtFailures)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;
    AvFifo_RxFifo* Rx = AvFifo_RxFifo_Alloc();
    AvFifo_TxFifo* Tx = AvFifo_TxFifo_Alloc();
    DT_ASSERT(Rx != NULL && Tx != NULL);
    IpSrcFlt Sources[3];
    const AvFifo_IpPars P = Pars(5004, false, Sources);
    const St2110_TxConfigVideo TxVideo = VideoConfig(TxFormat);
    const St2110_RxConfigVideo RxVideo = {RxFormat};
    DT_ASSERT_OK(AvFifo_RxFifo_Attach2(Rx, Fix.Device, 1, Pipe));
    DT_ASSERT_OK(AvFifo_RxFifo_ConfigureVideo(Rx, &RxVideo));
    DT_ASSERT_OK(AvFifo_RxFifo_SetIpPars(Rx, &P));
    DT_ASSERT_OK(AvFifo_RxFifo_Start(Rx));
    DT_ASSERT_OK(AvFifo_TxFifo_Attach2(Tx, Fix.Device, 1, Pipe));
    DT_ASSERT_OK(AvFifo_TxFifo_ConfigureVideo(Tx, &TxVideo));
    DT_ASSERT_OK(AvFifo_TxFifo_SetIpPars(Tx, &P));
    DT_ASSERT_OK(AvFifo_TxFifo_Start(Tx));

    int Size = TxFormat == St2110_TxFrameFormat_Uyvy422_10b ? WIDTH * 5 / 2 * HEIGHT
                                                            : WIDTH * 2 * HEIGHT;
    uint8_t* Image = (uint8_t*)malloc((size_t)Size);
    uint8_t* Expected = (uint8_t*)calloc((size_t)Size, 1);
    DT_ASSERT(Image != NULL && Expected != NULL);
    for (int i = 0; i < Size; i++)
        Image[i] = (uint8_t)(i * 13 + i / 640);
    Expect(Image, (size_t)Size, Expected);

    for (int f = 0; f < 3; f++)
    {
        AvFifo_Frame* Frame = AvFifo_TxFifo_GetFromMemPool(Tx, Size);
        DT_ASSERT(Frame != NULL);
        memcpy(Frame->Data, Image, (size_t)Size);
        Frame->NumValidBytes = Size;
        Frame->RtpTime = 3600u * (uint32_t)f;
        Frame->ToD = DtAvTime_FromNs(T0 + 100 * MS + (uint64_t)f * 40 * MS);
        DT_ASSERT_OK(AvFifo_TxFifo_Write(Tx, Frame));
    }
    LoadWanted Wanted = {Rx, 2};
    DT_ASSERT(RunUntil(HasLoad, &Wanted));
    DT_ASSERT_EQ(AvFifo_TxFifo_GetStatistics(Tx).FramesOk, 3);

    for (int f = 1; f < 3; f++)
    {
        AvFifo_Frame* Frame = AvFifo_RxFifo_Read(Rx);
        DT_ASSERT(Frame != NULL);
        int Valid = RxFormat == St2110_RxFrameFormat_Uyvy422_10b_to_8b ||
                            RxFormat == St2110_RxFrameFormat_Yuv422p_8b
                        ? WIDTH * 2 * HEIGHT
                        : Size;
        DT_ASSERT_EQ(Frame->NumValidBytes, Valid);
        DT_ASSERT_EQ(Frame->NumRows, HEIGHT);
        DT_ASSERT_EQ(Frame->RtpTime, 3600u * (uint32_t)f);
        uint64_t Sent = T0 + 100 * MS + (uint64_t)f * 40 * MS + 1493333 - 14800;
        DT_ASSERT_EQ(DtAvTime_ToNs(&Frame->ToD), Sent);
        size_t Compare =
            RxFormat == St2110_RxFrameFormat_Yuv422p_8b ? Frame->Size : (size_t)Valid;
        if (RxFormat == St2110_RxFrameFormat_Yuv422p_8b)
        {
            // The planes lie at quarters of the frame's size.
            size_t N = Frame->Size / 4;
            size_t Pixels = (size_t)Valid / 4;
            DT_ASSERT_MEM(Frame->Data, Expected, Pixels * 2);
            DT_ASSERT_MEM(Frame->Data + 2 * N, Expected + 2 * Pixels, Pixels);
            DT_ASSERT_MEM(Frame->Data + 3 * N, Expected + 3 * Pixels, Pixels);
        }
        else
            DT_ASSERT_MEM(Frame->Data, Expected, Compare);
        DT_ASSERT_OK(AvFifo_RxFifo_ReturnToMemPool(Rx, Frame));
        DT_ASSERT_EQ(AvFifo_RxFifo_ReturnToMemPool(Rx, Frame), DTAPI_E_INVALID_ARG);
    }
    RxStatistics Stats = AvFifo_RxFifo_GetStatistics(Rx);
    DT_ASSERT_EQ(Stats.FramesOk, 2);
    DT_ASSERT_EQ(Stats.FramesIncomplete + Stats.IpPacketErrors + Stats.SyncErrors, 0);

    free(Image);
    free(Expected);
    AvFifo_TxFifo_Free(Tx);
    AvFifo_RxFifo_Free(Rx);
    FINISH(Fix);
}

DT_TEST(Loopback10BitRawHardware)
{
    CheckLoopback(St2110_TxFrameFormat_Uyvy422_10b, St2110_RxFrameFormat_Raw,
                  HwOrSwPipe_Auto, Pgroups, DtFailures);
}

DT_TEST(Loopback10BitSoftware)
{
    CheckLoopback(St2110_TxFrameFormat_Uyvy422_10b, St2110_RxFrameFormat_Uyvy422_10b,
                  HwOrSwPipe_UseSwPipe, Same, DtFailures);
}

DT_TEST(Loopback10BitTo8Bit)
{
    CheckLoopback(St2110_TxFrameFormat_Uyvy422_10b,
                  St2110_RxFrameFormat_Uyvy422_10b_to_8b, HwOrSwPipe_Auto, To8Bit,
                  DtFailures);
}

DT_TEST(Loopback8Bit)
{
    CheckLoopback(St2110_TxFrameFormat_Uyvy422_8b, St2110_RxFrameFormat_Uyvy422_8b,
                  HwOrSwPipe_PreferHwPipe, Same, DtFailures);
}

DT_TEST(Loopback8BitPlanar)
{
    CheckLoopback(St2110_TxFrameFormat_Uyvy422_8b, St2110_RxFrameFormat_Yuv422p_8b,
                  HwOrSwPipe_Auto, Planar, DtFailures);
}

// Stereo audio in Format, SampleBytes a sample, in frames of 480 samples arrives as
// packets of 48 samples, in order.
static void CheckAudio(St2110_AudioFormat Format, int SampleBytes, int* DtFailures)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;
    AvFifo_RxFifo* Rx = AvFifo_RxFifo_Alloc();
    AvFifo_TxFifo* Tx = AvFifo_TxFifo_Alloc();
    DT_ASSERT(Rx != NULL && Tx != NULL);
    IpSrcFlt Sources[3];
    const AvFifo_IpPars P = Pars(5006, false, Sources);
    const St2110_RxConfigAudio RxAudio = {Format, 48000};
    const St2110_TxConfigAudio TxAudio = {Format, 2, 48, 48000};
    DT_ASSERT_OK(AvFifo_RxFifo_Attach(Rx, Fix.Device, 1));
    DT_ASSERT_OK(AvFifo_RxFifo_ConfigureAudio(Rx, &RxAudio));
    DT_ASSERT_EQ(AvFifo_RxFifo_GetMaxSize(Rx), 400);
    DT_ASSERT_OK(AvFifo_RxFifo_SetIpPars(Rx, &P));
    DT_ASSERT_OK(AvFifo_RxFifo_Start(Rx));
    DT_ASSERT_OK(AvFifo_TxFifo_Attach(Tx, Fix.Device, 1));
    DT_ASSERT_OK(AvFifo_TxFifo_ConfigureAudio(Tx, &TxAudio));
    DT_ASSERT_OK(AvFifo_TxFifo_SetIpPars(Tx, &P));
    DT_ASSERT_OK(AvFifo_TxFifo_Start(Tx));
    int UsesHw = -1;
    DT_ASSERT_OK(AvFifo_RxFifo_UsesHwPipe(Rx, &UsesHw));
    DT_ASSERT_EQ(UsesHw, 0);

    int FrameNumBytes = 480 * 2 * SampleBytes;
    uint8_t Sent[5 * 480 * 2 * 3];
    for (int f = 0; f < 5; f++)
    {
        AvFifo_Frame* Frame = AvFifo_TxFifo_GetFromMemPool(Tx, FrameNumBytes);
        DT_ASSERT(Frame != NULL);
        for (int i = 0; i < FrameNumBytes; i++)
            Frame->Data[i] = (uint8_t)(f * 31 + i);
        memcpy(Sent + f * FrameNumBytes, Frame->Data, (size_t)FrameNumBytes);
        Frame->NumValidBytes = FrameNumBytes;
        Frame->RtpTime = 48000u + 480u * (uint32_t)f;
        Frame->ToD = DtAvTime_FromNs(T0 + 100 * MS + (uint64_t)f * 10 * MS);
        DT_ASSERT_OK(AvFifo_TxFifo_Write(Tx, Frame));
    }
    LoadWanted Wanted = {Rx, 50};
    DT_ASSERT(RunUntil(HasLoad, &Wanted));
    DT_ASSERT_EQ(AvFifo_RxFifo_GetFifoLoad(Rx), 50);
    for (int k = 0; k < 50; k++)
    {
        AvFifo_Frame* Frame = AvFifo_RxFifo_Read(Rx);
        DT_ASSERT(Frame != NULL);
        DT_ASSERT_EQ(Frame->NumValidBytes, 48 * 2 * SampleBytes);
        DT_ASSERT_EQ(Frame->RtpTime, 48000u + 48u * (uint32_t)k);
        DT_ASSERT_MEM(Frame->Data, Sent + k * 48 * 2 * SampleBytes,
                      (size_t)Frame->NumValidBytes);
        DT_ASSERT_OK(AvFifo_RxFifo_ReturnToMemPool(Rx, Frame));
    }
    DT_ASSERT(AvFifo_RxFifo_Read(Rx) == NULL);
    DT_ASSERT_EQ(AvFifo_RxFifo_GetStatistics(Rx).FramesOk, 50);

    AvFifo_TxFifo_Free(Tx);
    AvFifo_RxFifo_Free(Rx);
    FINISH(Fix);
}

DT_TEST(LoopbackAudioL24)
{
    CheckAudio(St2110_AudioFormat_L24BE, 3, DtFailures);
}

DT_TEST(LoopbackAudioL16)
{
    CheckAudio(St2110_AudioFormat_L16BE, 2, DtFailures);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Faults +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// A receive FIFO of 5 frames that nobody reads drops the rest; a transmit FIFO whose
// software pipe holds its frames for later fills up.
DT_TEST(FullFifos)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;
    AvFifo_RxFifo* Rx = AvFifo_RxFifo_Alloc();
    AvFifo_TxFifo* Tx = AvFifo_TxFifo_Alloc();
    DT_ASSERT(Rx != NULL && Tx != NULL);
    IpSrcFlt Sources[3];
    const AvFifo_IpPars P = Pars(5006, false, Sources);
    const St2110_RxConfigAudio RxAudio = {St2110_AudioFormat_L24BE, 48000};
    const St2110_TxConfigAudio TxAudio = {St2110_AudioFormat_L24BE, 2, 240, 48000};
    DT_ASSERT_OK(AvFifo_RxFifo_Attach(Rx, Fix.Device, 1));
    DT_ASSERT_OK(AvFifo_RxFifo_ConfigureAudio(Rx, &RxAudio));
    AvFifo_RxFifo_SetMaxSize(Rx, 5);
    DT_ASSERT_EQ(AvFifo_RxFifo_GetMaxSize(Rx), 5);
    DT_ASSERT_OK(AvFifo_RxFifo_SetIpPars(Rx, &P));
    DT_ASSERT_OK(AvFifo_RxFifo_Start(Rx));
    AvFifo_RxFifo_SetMaxSize(Rx, 50);
    DT_ASSERT_EQ(AvFifo_RxFifo_GetMaxSize(Rx), 5);
    DT_ASSERT(strstr(GetLastException(), "already started") != NULL);

    DT_ASSERT_OK(AvFifo_TxFifo_Attach(Tx, Fix.Device, 1));
    DT_ASSERT_OK(AvFifo_TxFifo_ConfigureAudio(Tx, &TxAudio));
    AvFifo_TxFifo_SetMaxSize(Tx, 2);
    DT_ASSERT_OK(AvFifo_TxFifo_SetIpPars(Tx, &P));
    DT_ASSERT_OK(AvFifo_TxFifo_Start(Tx));

    // Ten packets now, then frames five seconds ahead until the FIFO is full.
    for (int f = 0; f < 10; f++)
    {
        AvFifo_Frame* Frame = AvFifo_TxFifo_GetFromMemPool(Tx, 1440);
        DT_ASSERT(Frame != NULL);
        Frame->NumValidBytes = 1440;
        Frame->ToD = DtAvTime_FromNs(T0 + 50 * MS + (uint64_t)f * 5 * MS);
        DtapiResult Result = DTAPI_E_FIFO_FULL;
        for (int Try = 0; Try < 1000 && Result == DTAPI_E_FIFO_FULL; Try++)
        {
            Result = AvFifo_TxFifo_Write(Tx, Frame);
            if (Result == DTAPI_E_FIFO_FULL)
                OsTime_SleepMs(1);
        }
        DT_ASSERT_OK(Result);
    }
    LoadWanted Wanted = {Rx, 5};
    DT_ASSERT(RunUntil(HasLoad, &Wanted));
    for (int Step = 0; Step < 20; Step++)
    {
        SimDtPcie_AdvanceNwTime(10 * MS);
        OsTime_SleepMs(3);
    }
    RxStatistics Stats = AvFifo_RxFifo_GetStatistics(Rx);
    DT_ASSERT_EQ(Stats.FramesOk, 10);
    DT_ASSERT_EQ(Stats.DroppedFrames, 5);

    DtapiResult Result = DTAPI_OK;
    uint64_t Later = SimDtPcie_Now() + 5000 * MS;
    for (int f = 0; f < 200 && Result == DTAPI_OK; f++)
    {
        AvFifo_Frame* Frame = AvFifo_TxFifo_GetFromMemPool(Tx, 1440);
        DT_ASSERT(Frame != NULL);
        Frame->NumValidBytes = 1440;
        Frame->ToD = DtAvTime_FromNs(Later + (uint64_t)f * 5 * MS);
        Result = AvFifo_TxFifo_Write(Tx, Frame);
        // The refused frame stays the application's; the receive FIFO does not take it.
        if (Result == DTAPI_E_FIFO_FULL)
            DT_ASSERT_EQ(AvFifo_RxFifo_ReturnToMemPool(Rx, Frame), DTAPI_E_INVALID_ARG);
        OsTime_SleepMs(1);
    }
    DT_ASSERT_EQ(Result, DTAPI_E_FIFO_FULL);

    AvFifo_TxFifo_Free(Tx);
    AvFifo_RxFifo_Free(Rx);
    FINISH(Fix);
}

// A packet with four row headers counts an IP packet error in the statistics.
DT_TEST(InjectedFaultIsCounted)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;
    AvFifo_RxFifo* Rx = AvFifo_RxFifo_Alloc();
    DT_ASSERT(Rx != NULL);
    IpSrcFlt Sources[3];
    const AvFifo_IpPars P = Pars(5004, false, Sources);
    DT_ASSERT_OK(StartRx(Rx, Fix.Device, HwOrSwPipe_Auto, &P));

    uint8_t Frame[14 + 20 + 8 + 12 + 2 + 4 * 6 + 8];
    memset(Frame, 0, sizeof(Frame));
    static const uint8_t Mac[6] = {0x01, 0x00, 0x5E, 0x01, 0x02, 0x03};
    memcpy(Frame, Mac, 6);
    Frame[12] = 0x08;
    Frame[14] = 0x45;
    Frame[16] = 0;
    Frame[17] = (uint8_t)(sizeof(Frame) - 14);
    Frame[23] = 17;
    Frame[26] = 192;
    Frame[27] = 168;
    Frame[28] = 1;
    Frame[29] = 50;
    memcpy(Frame + 30, Group, 4);
    Frame[34] = 0x13;
    Frame[35] = 0x88;
    Frame[36] = 0x13;
    Frame[37] = 0x8C;
    Frame[39] = (uint8_t)(sizeof(Frame) - 34);
    Frame[42] = 0x80;
    Frame[43] = 96;
    for (int h = 0; h < 4; h++)
    {
        uint8_t* Srd = Frame + 56 + 6 * h;
        Srd[1] = 2;
        Srd[4] = 0x80;
    }
    DT_ASSERT(SimDtPcie_InjectNwFrame(Frame, sizeof(Frame), T0 + 10 * MS));
    SimDtPcie_AdvanceNwTime(20 * MS);
    for (int Try = 0; Try < 500 && AvFifo_RxFifo_GetStatistics(Rx).IpPacketErrors == 0;
         Try++)
    {
        OsTime_SleepMs(2);
    }
    DT_ASSERT_EQ(AvFifo_RxFifo_GetStatistics(Rx).IpPacketErrors, 1);

    AvFifo_RxFifo_Free(Rx);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Failure text +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static char g_OtherText[DT_AV_ERROR_SIZE];

static void FailElsewhere(void* Context)
{
    (void)Context;
    AvFifo_TxFifo_Start(NULL);
    snprintf(g_OtherText, sizeof(g_OtherText), "%s", GetLastException());
}

// Each thread has its own text.
DT_TEST(FailureTextPerThread)
{
    AvFifo_RxFifo_Start(NULL);
    OsThread* Thread = OsThread_Start(FailElsewhere, NULL);
    DT_ASSERT(Thread != NULL);
    OsThread_Join(Thread);
    DT_ASSERT(strstr(GetLastException(), "AvFifo_RxFifo_Start") != NULL);
    DT_ASSERT(strstr(g_OtherText, "AvFifo_TxFifo_Start") != NULL);
    DT_ASSERT(strstr(GetLastException(), "No FIFO") != NULL);
}

// The capabilities the emulated DTA-2110 gives its port are read.
DT_TEST(PortCapabilities)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;

    const uint64_t Caps = Fix.Device->PortCaps[0];
    const uint64_t Want = DT_CAP_AVFIFO | DT_CAP_IP | DT_CAP_PTP | DT_CAP_SFP10G |
                          DT_CAP_ST2110 | DT_CAP_TS;
    DT_ASSERT_EQ(Caps & Want, Want);
    DT_ASSERT_EQ(Caps & DT_CAP_SFP25G, 0);
    FINISH(Fix);
}

DT_TEST_MAIN("SimAvFifo", DT_RUN(PortCapabilities), DT_RUN(ResultsOfTheLifecycle),
             DT_RUN(SdiPortIsRefused), DT_RUN(StartFailures), DT_RUN(StartedAndStopped),
             DT_RUN(PacketsOnTheWire), DT_RUN(Loopback10BitRawHardware),
             DT_RUN(Loopback10BitSoftware), DT_RUN(Loopback10BitTo8Bit),
             DT_RUN(Loopback8Bit), DT_RUN(Loopback8BitPlanar), DT_RUN(LoopbackAudioL24),
             DT_RUN(LoopbackAudioL16), DT_RUN(FullFifos), DT_RUN(InjectedFaultIsCounted),
             DT_RUN(FailureTextPerThread))
