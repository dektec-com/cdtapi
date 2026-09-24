// #*#*#*#*#*#*#*#*#*#*#*#*#* TestConformance.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Every function of the public headers, called once against the emulator
//
// SPDX-License-Identifier: BSD-3-Clause
//
// An application sees the library through its public headers and nothing else, and this
// file is written that way: it includes the two public headers and the test framework,
// and no header of the library's own. Two things are checked.
//
// Completeness: PublicFunctions.inc, which CMake writes from the headers at configure
// time, lists every function they declare. The table below takes the address of each, so
// a function that is declared and not defined fails the link. One that is dropped from a
// header leaves its call further on without a declaration, which fails the build of this
// file where warnings are errors.
//
// Behaviour: every function is called at least once, against the emulated device, and
// its answer is checked to be a result the documentation names. Not the depth of the
// other suites, which is where a function's behaviour is held to its detail; this is the
// promise that each function of the interface exists, links and answers.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The public interface, and the test framework.
#include "DtTest.h"
#include "cdtapi.h"
#include "cdtapi_avfifo.h"

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// True when Result is one of the two the call is allowed to give.
static bool IsOneOf(DtapiResult Result, DtapiResult First, DtapiResult Second)
{
    if (Result != First && Result != Second)
        printf("    got %s\n", DtapiResult2Str(Result));
    return Result == First || Result == Second;
}

typedef bool (*Suits)(const DtHwFuncDesc* Port);

static bool IsSdiInput(const DtHwFuncDesc* Port)
{
    return Port->IsSdi && Port->IsInput;
}

static bool IsSdiOutput(const DtHwFuncDesc* Port)
{
    return Port->IsSdi && Port->IsOutput;
}

static bool IsAvFifo(const DtHwFuncDesc* Port)
{
    return Port->IsAvFifo;
}

// The first port that suits, scanned as an application scans. False when there is none.
static bool FindPort(Suits Wanted, DtHwFuncDesc* Found)
{
    int Count = 0;
    if (DtapiHwFuncScan(0, &Count, NULL) != DTAPI_E_BUF_TOO_SMALL || Count <= 0)
        return false;

    DtHwFuncDesc* Ports = (DtHwFuncDesc*)calloc((size_t)Count, sizeof(DtHwFuncDesc));
    if (Ports == NULL)
        return false;
    int Scanned = 0;
    bool Ok = false;
    if (DtapiHwFuncScan(Count, &Scanned, Ports) == DTAPI_OK)
    {
        for (int i = 0; i < Scanned && !Ok; i++)
        {
            if (Wanted(&Ports[i]))
            {
                *Found = Ports[i];
                Ok = true;
            }
        }
    }
    free(Ports);
    return Ok;
}

// The device of a port, attached. NULL when it cannot be attached.
static DtDevice* AttachTo(const DtHwFuncDesc* Port)
{
    DtDevice* Device = DtDevice_Alloc();

    if (Device == NULL)
        return NULL;
    if (DtDevice_AttachToSerial(Device, Port->SerialNumber) != DTAPI_OK)
    {
        DtDevice_Free(Device);
        return NULL;
    }
    return Device;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Completeness +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

typedef void (*AnyFunction)(void);

typedef struct PublicFunction
{
    const char* Name;
    AnyFunction Address;
} PublicFunction;

#define F(Name) {#Name, (AnyFunction) & Name},
static const PublicFunction g_Functions[] = {
#include "PublicFunctions.inc"
};
#undef F

DT_TEST(EveryPublicFunctionIsThere)
{
    const size_t Count = sizeof(g_Functions) / sizeof(g_Functions[0]);

    DT_ASSERT(Count >= 90);
    for (size_t i = 0; i < Count; i++)
    {
        if (g_Functions[i].Address == NULL)
            DT_FAIL("%s has no address", g_Functions[i].Name);
    }
    printf("    %zu functions\n", Count);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Library +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(LibraryCalls)
{
    DT_ASSERT(DtapiGetVersion() != NULL && DtapiGetVersion()[0] != '\0');
    DT_ASSERT_STR(DtapiResult2Str(DTAPI_OK), "DTAPI_OK");
    DT_ASSERT_STR(DtapiResult2Str(DTAPI_E_NOT_ATTACHED), "DTAPI_E_NOT_ATTACHED");

    int Value = 0;
    int SubValue = 0;
    DT_ASSERT_OK(DtapiVidStd2IoStd(DTAPI_VIDSTD_1080I50, -1, &Value, &SubValue));
    DT_ASSERT(Value != 0);

    int Count = 0;
    DT_ASSERT_EQ(DtapiHwFuncScan(0, &Count, NULL), DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT(Count > 0);

    int Devices = 0;
    DT_ASSERT_EQ(DtapiDeviceScan(0, &Devices, NULL), DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT(Devices > 0);
    DtDeviceDesc* Descs = (DtDeviceDesc*)calloc((size_t)Devices, sizeof(DtDeviceDesc));
    DT_ASSERT(Descs != NULL);
    int Scanned = 0;
    DT_ASSERT_OK(DtapiDeviceScan(Devices, &Scanned, Descs));
    DT_ASSERT_EQ(Scanned, Devices);
    DT_ASSERT(Descs[0].Serial > 0);
    free(Descs);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Device +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(DeviceCalls)
{
    DtHwFuncDesc Port;
    DT_ASSERT(FindPort(IsSdiInput, &Port));

    DtDevice* Device = DtDevice_Alloc();
    DT_ASSERT(Device != NULL);
    DT_ASSERT_OK(DtDevice_AttachToSerial(Device, Port.SerialNumber));

    DtTimeOfDay ToD = {0, 0};
    DT_ASSERT_OK(DtDevice_GetTimeOfDay(Device, &ToD));
    DT_ASSERT(ToD.Seconds > 0);

    DT_ASSERT_OK(DtDevice_SetToInput(Device, Port.Port));
    DtIoConfig Config = {Port.Port,
                         DTAPI_IOCONFIG_IODIR,
                         DTAPI_IOCONFIG_INPUT,
                         DTAPI_IOCONFIG_INPUT,
                         {-1, -1}};
    DT_ASSERT_OK(DtDevice_SetIoConfig(Device, &Config, 1));
    Config.Value = Config.SubValue = 0;
    DT_ASSERT_OK(DtDevice_GetIoConfig(Device, &Config, 1));
    DT_ASSERT_EQ(Config.Value, DTAPI_IOCONFIG_INPUT);

    int VidStd = DTAPI_VIDSTD_UNKNOWN;
    DT_ASSERT(IsOneOf(DtDevice_DetectVidStd(Device, Port.Port, &VidStd), DTAPI_OK,
                      DTAPI_E_NO_LOCK));

    DtDetVidStd Waited;
    memset(&Waited, 0, sizeof(Waited));
    DT_ASSERT(IsOneOf(DtDevice_WaitForSignalTimeout(Device, Port.Port, 20, &Waited),
                      DTAPI_OK, DTAPI_E_TIMEOUT));

    // The wait without a time limit retries until a standard is detected, so a test can
    // only call it where it returns at once: without a device.
    DtDetVidStd Detected = DtDevice_WaitForSignal(NULL, Port.Port);
    DT_ASSERT_EQ(Detected.VidStd, DTAPI_VIDSTD_UNKNOWN);

    DtHwFuncDesc Output;
    if (FindPort(IsSdiOutput, &Output))
        DT_ASSERT_OK(DtDevice_SetToOutput(Device, Output.Port));

    DT_ASSERT_OK(DtDevice_Detach(Device));
    DT_ASSERT_EQ(DtDevice_Detach(Device), DTAPI_E_NOT_ATTACHED);
    DtDevice_Freep(&Device);
    DT_ASSERT(Device == NULL);
    DtDevice_Free(NULL);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Input channel +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(InputChannelCalls)
{
    DtHwFuncDesc Port;
    DT_ASSERT(FindPort(IsSdiInput, &Port));
    DtDevice* Device = AttachTo(&Port);
    DT_ASSERT(Device != NULL);
    DT_ASSERT_OK(DtDevice_SetToInput(Device, Port.Port));

    DtInpChannel* Channel = DtInpChannel_Alloc();
    DT_ASSERT(Channel != NULL);
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Channel, Device, Port.Port));
    int Value = 0;
    int SubValue = 0;
    DT_ASSERT_OK(DtapiVidStd2IoStd(DTAPI_VIDSTD_1080I50, -1, &Value, &SubValue));
    DT_ASSERT_OK(
        DtInpChannel_SetIoConfig(Channel, DTAPI_IOCONFIG_IOSTD, Value, SubValue, -1, -1));
    DT_ASSERT_OK(
        DtInpChannel_SetRxMode(Channel, DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Channel, DTAPI_RXCTRL_RCV));
    DT_ASSERT_OK(DtInpChannel_ClearFifo(Channel));

    int FifoLoad = -1;
    DT_ASSERT_OK(DtInpChannel_GetFifoLoad(Channel, &FifoLoad));
    DT_ASSERT(FifoLoad >= 0);

    int MaxFifoSize = 0;
    DT_ASSERT_OK(DtInpChannel_GetMaxFifoSize(Channel, &MaxFifoSize));
    DT_ASSERT(MaxFifoSize > 0);

    int Flags = 0;
    int Latched = 0;
    DT_ASSERT_OK(DtInpChannel_GetFlags(Channel, &Flags, &Latched));
    DT_ASSERT_OK(DtInpChannel_ClearFlags(Channel, Latched));

    DT_ASSERT(IsOneOf(DtInpChannel_DetectIoStd(Channel, &Value, &SubValue), DTAPI_OK,
                      DTAPI_E_INVALID_VIDSTD));
    int64_t ParXtra0 = 0;
    int64_t ParXtra1 = 0;
    DT_ASSERT_OK(DtInpChannel_GetIoConfig(Channel, DTAPI_IOCONFIG_IODIR, &Value,
                                          &SubValue, &ParXtra0, &ParXtra1));
    DT_ASSERT_EQ(Value, DTAPI_IOCONFIG_INPUT);

    // The work over a pool of two threads of the library's own, freed by the program at
    // once and held by the channel, then back to the reading thread alone. A pool set to
    // no dispatch function is one with neither, which threads may follow.
    DtWorkPool* Pool = DtWorkPool_Alloc();
    DT_ASSERT(Pool != NULL);
    DT_ASSERT_OK(DtWorkPool_SetDispatch(Pool, NULL, NULL, 0));
    DT_ASSERT_OK(DtWorkPool_StartThreads(Pool, 2));
    DT_ASSERT_OK(DtInpChannel_SetWorkPool(Channel, Pool, 0));
    DtWorkPool_Freep(&Pool);
    DT_ASSERT_OK(DtInpChannel_SetWorkPool(Channel, NULL, 0));

    // A pool the program's threads join: a member sent back before it joins returns from
    // its Join at once, and a channel takes such a pool as it takes any other.
    Pool = DtWorkPool_Alloc();
    DtWorkPoolMember* Member = DtWorkPoolMember_Alloc();
    DT_ASSERT(Pool != NULL && Member != NULL);
    DT_ASSERT_OK(DtWorkPool_ExpectThreads(Pool, 1));
    DtWorkPool_Dismiss(Pool, Member);
    DT_ASSERT_OK(DtWorkPool_Join(Pool, Member));
    DtWorkPool_DismissAll(Pool);
    DT_ASSERT_OK(DtInpChannel_SetWorkPool(Channel, Pool, 0));
    DT_ASSERT_OK(DtInpChannel_SetWorkPool(Channel, NULL, 0));
    DtWorkPoolMember_Freep(&Member);
    DtWorkPoolMember_Free(Member);
    DtWorkPool_Freep(&Pool);

    // The functions of ASI, which an SDI channel does not have.
    int NumInv = 0, ClkDet = 0, AsiLock = 0, RateOk = 0, AsiInv = 0, Count = 0;
    static uint32_t Packets[47];
    DT_ASSERT_EQ(DtInpChannel_Read(Channel, Packets, (int)sizeof(Packets), 20),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtInpChannel_GetStatus(Channel, &Value, &NumInv, &ClkDet, &AsiLock,
                                        &RateOk, &AsiInv),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtInpChannel_GetTsRateBps(Channel, &Count), DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtInpChannel_GetViolCount(Channel, &Count), DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtInpChannel_PolarityControl(Channel, DTAPI_POLARITY_AUTO),
                 DTAPI_E_NOT_SUPPORTED);

    // Room for a whole 1080-line frame of 10-bit symbols, which is what the channel is
    // configured for; a smaller buffer is refused rather than filled.
    const int BufferSize = 16 * 1024 * 1024;
    uint8_t* Buffer = (uint8_t*)malloc((size_t)BufferSize);
    DT_ASSERT(Buffer != NULL);
    int FrameSize = BufferSize;
    DT_ASSERT(IsOneOf(DtInpChannel_ReadFrame(Channel, Buffer, &FrameSize, 20), DTAPI_OK,
                      DTAPI_E_TIMEOUT));
    FrameSize = BufferSize;
    DtTimeOfDay Arrival = {0, 0};
    DT_ASSERT(IsOneOf(DtInpChannel_ReadFrame2(Channel, Buffer, &FrameSize, 20, &Arrival),
                      DTAPI_OK, DTAPI_E_TIMEOUT));
    free(Buffer);

    DT_ASSERT_OK(DtInpChannel_Detach(Channel, 1));
    DtInpChannel_Freep(&Channel);
    DT_ASSERT(Channel == NULL);
    DtInpChannel_Free(NULL);
    DtDevice_Free(Device);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Output channel +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(OutputChannelCalls)
{
    DtHwFuncDesc Port;
    DT_ASSERT(FindPort(IsSdiOutput, &Port));
    DtDevice* Device = AttachTo(&Port);
    DT_ASSERT(Device != NULL);
    DT_ASSERT_OK(DtDevice_SetToOutput(Device, Port.Port));
    DtIoConfig Config = {Port.Port,
                         DTAPI_IOCONFIG_IOSTD,
                         DTAPI_IOCONFIG_HDSDI,
                         DTAPI_IOCONFIG_1080I50,
                         {-1, -1}};
    DT_ASSERT_OK(DtDevice_SetIoConfig(Device, &Config, 1));

    DtOutpChannel* Channel = DtOutpChannel_Alloc();
    DT_ASSERT(Channel != NULL);
    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Channel, Device, Port.Port));
    DT_ASSERT_OK(DtOutpChannel_SetIoConfig(Channel, DTAPI_IOCONFIG_IODIR,
                                           DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_OUTPUT,
                                           -1, -1));
    DT_ASSERT_OK(DtOutpChannel_SetTxMode(
        Channel, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B, 0));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Channel, DTAPI_TXCTRL_IDLE));
    DT_ASSERT_OK(DtOutpChannel_ClearFifo(Channel));

    int FifoLoad = -1;
    DT_ASSERT_OK(DtOutpChannel_GetFifoLoad(Channel, &FifoLoad));
    DT_ASSERT(FifoLoad >= 0);

    int FifoSize = 0;
    DT_ASSERT_OK(DtOutpChannel_GetFifoSize(Channel, &FifoSize));
    DT_ASSERT(FifoSize > 0);

    int MaxFifoSize = 0;
    DT_ASSERT_OK(DtOutpChannel_GetMaxFifoSize(Channel, &MaxFifoSize));
    DT_ASSERT(MaxFifoSize >= FifoSize);

    int Status = 0;
    int Latched = 0;
    DT_ASSERT_OK(DtOutpChannel_GetFlags(Channel, &Status, &Latched));
    DT_ASSERT_OK(DtOutpChannel_ClearFlags(Channel, Latched));

    int Value = 0;
    int SubValue = 0;
    int64_t ParXtra0 = 0;
    int64_t ParXtra1 = 0;
    DT_ASSERT_OK(DtOutpChannel_GetIoConfig(Channel, DTAPI_IOCONFIG_IODIR, &Value,
                                           &SubValue, &ParXtra0, &ParXtra1));
    DT_ASSERT_EQ(Value, DTAPI_IOCONFIG_OUTPUT);

    // The work over a pool of two threads of the library's own, freed by the program at
    // once and held by the channel, then back to the writing thread alone.
    DtWorkPool* Pool = DtWorkPool_Alloc();
    DT_ASSERT(Pool != NULL);
    DT_ASSERT_OK(DtWorkPool_StartThreads(Pool, 2));
    DT_ASSERT_OK(DtOutpChannel_SetWorkPool(Channel, Pool, 0));
    DtWorkPool_Free(Pool);
    DT_ASSERT_OK(DtOutpChannel_SetWorkPool(Channel, NULL, 0));

    // The functions of ASI: SDI takes the normal polarity and has no rate.
    DT_ASSERT_OK(DtOutpChannel_SetTxPolarity(Channel, DTAPI_TXPOL_NORMAL));
    int Rate = 0;
    DT_ASSERT_EQ(DtOutpChannel_GetTsRateBps(Channel, &Rate), DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtOutpChannel_SetTsRateBps(Channel, 10000000), DTAPI_E_NOT_SUPPORTED);

    // A channel that is idle refuses what is written to it, and a frame of the wrong
    // size is refused whatever the channel does: answer enough that both writes are
    // there, where the transmit suites hold what a whole frame does.
    static uint8_t Frame[4096];
    memset(Frame, 0, sizeof(Frame));
    DT_ASSERT(IsOneOf(DtOutpChannel_Write(Channel, Frame, (int)sizeof(Frame)), DTAPI_OK,
                      DTAPI_E_IDLE));
    DT_ASSERT(IsOneOf(DtOutpChannel_WriteFrame(Channel, Frame, (int)sizeof(Frame), 20),
                      DTAPI_E_IDLE, DTAPI_E_INVALID_SIZE));

    DT_ASSERT_OK(DtOutpChannel_Detach(Channel, 1));
    DtOutpChannel_Freep(&Channel);
    DT_ASSERT(Channel == NULL);
    DtOutpChannel_Free(NULL);
    DtDevice_Free(Device);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Receive FIFO +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The stream both FIFOs use.
static AvFifo_IpPars StreamPars(void)
{
    static const uint8_t Group[4] = {239, 1, 2, 3};
    AvFifo_IpPars Pars;

    memset(&Pars, 0, sizeof(Pars));
    memcpy(Pars.IpAddr, Group, sizeof(Group));
    Pars.IpVersion = IpProtocolVersion_IPv4;
    Pars.Port = 5004;
    Pars.RtpPayloadType = 96;
    Pars.TimeToLive = 32;
    Pars.TransportProtocol = IpTransportProtocol_Rtp;
    return Pars;
}

DT_TEST(ReceiveFifoCalls)
{
    DtHwFuncDesc Port;
    if (!FindPort(IsAvFifo, &Port))
    {
        printf("    no IP port; is CDTAPI_SIM_DTA2110 set?\n");
        (*DtFailures)++;
        return;
    }
    DtDevice* Device = AttachTo(&Port);
    DT_ASSERT(Device != NULL);

    AvFifo_RxFifo* Fifo = AvFifo_RxFifo_Alloc();
    DT_ASSERT(Fifo != NULL);
    DT_ASSERT_EQ(AvFifo_RxFifo_Start(Fifo), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT(strstr(GetLastException(), "AvFifo_RxFifo_Start") != NULL);

    DT_ASSERT_OK(AvFifo_RxFifo_Attach(Fifo, Device, Port.Port));
    DT_ASSERT_OK(AvFifo_RxFifo_Detach(Fifo));
    DT_ASSERT_OK(AvFifo_RxFifo_Attach2(Fifo, Device, Port.Port, HwOrSwPipe_PreferHwPipe));

    const St2110_RxConfigVideo Video = {St2110_RxFrameFormat_Uyvy422_10b};
    DT_ASSERT_OK(AvFifo_RxFifo_ConfigureVideo(Fifo, &Video));
    const St2110_RxConfigAudio Audio = {St2110_AudioFormat_L24BE, 48000};
    DT_ASSERT_OK(AvFifo_RxFifo_ConfigureAudio(Fifo, &Audio));
    DT_ASSERT_OK(AvFifo_RxFifo_ConfigureVideo(Fifo, &Video));

    AvFifo_RxFifo_SetMaxSize(Fifo, 6);
    DT_ASSERT_EQ(AvFifo_RxFifo_GetMaxSize(Fifo), 6);

    const AvFifo_IpPars Pars = StreamPars();
    DT_ASSERT_OK(AvFifo_RxFifo_SetIpPars(Fifo, &Pars));
    DT_ASSERT_OK(AvFifo_RxFifo_Start(Fifo));

    int UsesHwPipe = -1;
    DT_ASSERT_OK(AvFifo_RxFifo_UsesHwPipe(Fifo, &UsesHwPipe));
    DT_ASSERT(UsesHwPipe == 0 || UsesHwPipe == 1);

    DT_ASSERT_EQ(AvFifo_RxFifo_GetFifoLoad(Fifo), 0);
    DT_ASSERT(AvFifo_RxFifo_Read(Fifo) == NULL);
    DT_ASSERT_EQ(AvFifo_RxFifo_ReturnToMemPool(Fifo, NULL), DTAPI_E_INVALID_ARG);

    RxStatistics Stats = AvFifo_RxFifo_GetStatistics(Fifo);
    DT_ASSERT_EQ(Stats.FramesOk, 0);

    DT_ASSERT_OK(AvFifo_RxFifo_Stop(Fifo));
    DT_ASSERT_OK(AvFifo_RxFifo_Clear(Fifo));
    DT_ASSERT_OK(AvFifo_RxFifo_Detach(Fifo));
    AvFifo_RxFifo_Freep(&Fifo);
    DT_ASSERT(Fifo == NULL);
    AvFifo_RxFifo_Free(NULL);
    DtDevice_Free(Device);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmit FIFO +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(TransmitFifoCalls)
{
    DtHwFuncDesc Port;
    if (!FindPort(IsAvFifo, &Port))
    {
        printf("    no IP port; is CDTAPI_SIM_DTA2110 set?\n");
        (*DtFailures)++;
        return;
    }
    DtDevice* Device = AttachTo(&Port);
    DT_ASSERT(Device != NULL);

    AvFifo_TxFifo* Fifo = AvFifo_TxFifo_Alloc();
    DT_ASSERT(Fifo != NULL);
    DT_ASSERT_EQ(AvFifo_TxFifo_Start(Fifo), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_OK(AvFifo_TxFifo_Attach(Fifo, Device, Port.Port));
    DT_ASSERT_OK(AvFifo_TxFifo_Detach(Fifo));
    DT_ASSERT_OK(AvFifo_TxFifo_Attach2(Fifo, Device, Port.Port, HwOrSwPipe_UseSwPipe));

    const St2110_TxConfigAudio Audio = {St2110_AudioFormat_L24BE, 2, 48, 48000};
    DT_ASSERT_OK(AvFifo_TxFifo_ConfigureAudio(Fifo, &Audio));

    St2110_TxConfigVideo Video;
    memset(&Video, 0, sizeof(Video));
    Video.Format = St2110_TxFrameFormat_Uyvy422_10b;
    Video.Packing.PayloadSize = -1;
    Video.Resolution.Width = 320;
    Video.Resolution.Height = 240;
    Video.Timing.Rate.Numerator = 25;
    Video.Timing.Rate.Denominator = 1;
    DT_ASSERT_OK(AvFifo_TxFifo_ConfigureVideo(Fifo, &Video));

    AvFifo_TxFifo_SetMaxSize(Fifo, 4);
    DT_ASSERT_EQ(AvFifo_TxFifo_GetMaxSize(Fifo), 4);

    const AvFifo_IpPars Pars = StreamPars();
    DT_ASSERT_OK(AvFifo_TxFifo_SetIpPars(Fifo, &Pars));
    DT_ASSERT_OK(AvFifo_TxFifo_Start(Fifo));

    int UsesHwPipe = -1;
    DT_ASSERT_OK(AvFifo_TxFifo_UsesHwPipe(Fifo, &UsesHwPipe));
    DT_ASSERT_EQ(UsesHwPipe, 0);
    DT_ASSERT_EQ(AvFifo_TxFifo_GetFifoLoad(Fifo), 0);

    const int FrameNumBytes = 320 / 2 * 5 * 240;
    AvFifo_Frame* Frame = AvFifo_TxFifo_GetFromMemPool(Fifo, FrameNumBytes);
    DT_ASSERT(Frame != NULL);
    memset(Frame->Data, 0x10, (size_t)FrameNumBytes);
    Frame->NumValidBytes = FrameNumBytes;
    DtTimeOfDay Now = {0, 0};
    DT_ASSERT_OK(DtDevice_GetTimeOfDay(Device, &Now));
    Frame->ToD = Tod2Grid_Video(&Now, &Video.Timing.Rate);
    Frame->RtpTime = Tod2Rtp_Video(&Frame->ToD);
    DT_ASSERT_OK(AvFifo_TxFifo_Write(Fifo, Frame));

    TxStatistics Stats = AvFifo_TxFifo_GetStatistics(Fifo);
    DT_ASSERT(Stats.FramesOk >= 0);

    DT_ASSERT_OK(AvFifo_TxFifo_Stop(Fifo));
    DT_ASSERT_OK(AvFifo_TxFifo_Clear(Fifo));
    DT_ASSERT_OK(AvFifo_TxFifo_Detach(Fifo));
    AvFifo_TxFifo_Freep(&Fifo);
    DT_ASSERT(Fifo == NULL);
    AvFifo_TxFifo_Free(NULL);
    DtDevice_Free(Device);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+= Frames, times and failures +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(FramePropertiesAndTimingCalls)
{
    AvFifo_Frame Frame;
    memset(&Frame, 0, sizeof(Frame));
    Frame.NumValidBytes = 1920 / 2 * 5 * 1080;
    Frame.NumRows = 1080;
    FrameProperties Properties;
    memset(&Properties, 0, sizeof(Properties));
    DT_ASSERT_OK(GetFrameProperties(&Frame, &Properties));
    DT_ASSERT(Properties.BytesPerFrame > 0);

    const DtTimeOfDay ToD = {1800000000u, 123456789u};
    const FrameRate Rate = {25, 1};

    DtTimeOfDay Grid = Tod2Grid_Video(&ToD, &Rate);
    DT_ASSERT(Grid.Seconds == ToD.Seconds || Grid.Seconds == ToD.Seconds + 1);
    DT_ASSERT_EQ(Grid.Nanoseconds % 40000000u, 0);

    DtTimeOfDay Audio = Tod2Grid_Audio(&ToD, 48000);
    DT_ASSERT_EQ(Audio.Seconds, ToD.Seconds);

    uint32_t RtpVideo = Tod2Rtp_Video(&Grid);
    DtTimeOfDay Back = Rtp2Tod_Video(RtpVideo, &ToD);
    DT_ASSERT_EQ(Back.Seconds, Grid.Seconds);

    uint32_t RtpAudio = Tod2Rtp_Audio(&Audio, 48000);
    DtTimeOfDay BackAudio = Rtp2Tod_Audio(RtpAudio, &ToD, 48000);
    DT_ASSERT_EQ(BackAudio.Seconds, Audio.Seconds);

    // Always a string: the text of this thread's last failure, or an empty one.
    DT_ASSERT(GetLastException() != NULL);
}

DT_TEST_MAIN("Conformance", DT_RUN(EveryPublicFunctionIsThere), DT_RUN(LibraryCalls),
             DT_RUN(DeviceCalls), DT_RUN(InputChannelCalls), DT_RUN(OutputChannelCalls),
             DT_RUN(ReceiveFifoCalls), DT_RUN(TransmitFifoCalls),
             DT_RUN(FramePropertiesAndTimingCalls))
