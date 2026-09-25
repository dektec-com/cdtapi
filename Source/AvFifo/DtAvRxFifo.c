// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAvRxFifo.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The receive FIFO of SMPTE 2110 audio and video
//
// SPDX-License-Identifier: BSD-3-Clause
//
// A pipe of the port whose filter takes the stream, a thread that hands the packets in
// the pipe's buffer to the parser of the substandard, and a FIFO of the frames the
// parser completes. The thread polls the pipe instead of waiting for the driver's event
// (plan 0009, question 2).

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"  // Allocation seam.
#include "Core/DtAtomic.h" // The thread's stop flag.
#include "DtAvError.h"     // Failure texts.
#include "DtAvPort.h"      // The port, its network and pipes.
#include "DtPcieAbi.h"     // Pipe modes and filter flags.
#include "DtSt2110Audio.h" // Audio packets.
#include "DtSt2110Video.h" // Video packets.
#include "Net/DtNet.h"     // Own address and multicast groups.
#include "OAL/OsThread.h"  // Lock, thread, sleeping.
#include "cdtapi_avfifo.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The shared buffer sizes: 64 MB for video, 4 MB for audio.
#define RX_SHARED_BUFFER_VIDEO_BYTES (64 * 1024 * 1024)
#define RX_SHARED_BUFFER_AUDIO_BYTES (4 * 1024 * 1024)

// The FIFO size for audio when the application sets none: 400 frames, which hold at
// least 50 ms even when each frame is a single packet of 125 us, the shortest there is.
// The default of 4 would then hold 500 us.
#define RX_AUDIO_FIFO_FRAMES 400

struct AvFifo_RxFifoC
{
    OsMutex* Lock; // Guards the calls that set up, start and stop the FIFO
    bool Attached;
    bool Started;
    DtAvPort Port;

    DtAvKind Kind;
    St2110_RxConfigAudio AudioConfig;
    St2110_RxConfigVideo VideoConfig;
    bool HasIpPars;
    DtAvIpPars IpPars;
    bool HasExplicitMaxSize;

    DtAvFramePool Pool;
    DtAvFrameFifo Fifo;

    // While started.
    DtAvPipe Pipe;
    DtAvReader Reader;
    OsNetSocket* Socket;
    uint32_t ItfIndex;
    bool HasJoinedGroup;
    OsThread* Thread;
    DtAtomicInt StopRequested;
    OsMutex* ParserLock; // Guards the parsers, which the thread runs
    DtSt2110AudioRx AudioRx;
    DtSt2110VideoRx VideoRx;
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The thread +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DeliverToFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool DeliverToFifo(void* Context, DtAvFrame* Frame)
{
    AvFifo_RxFifo* Fifo = (AvFifo_RxFifo*)Context;
    return DtAvFrameFifo_Push(&Fifo->Fifo, Frame);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ParseByKind -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void ParseByKind(void* Context, const uint8_t* Packet, int Size)
{
    AvFifo_RxFifo* Fifo = (AvFifo_RxFifo*)Context;
    if (Fifo->Kind == DT_AV_KIND_AUDIO)
        DtSt2110AudioRx_Parse(&Fifo->AudioRx, Packet, Size);
    else
        DtSt2110VideoRx_Parse(&Fifo->VideoRx, Packet, Size);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvRxThread -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Pass after pass over the buffer, with a short sleep when a pass found nothing or
// failed. Lost packet boundaries count a synchronisation error.
//
static void AvRxThread(void* Context)
{
    AvFifo_RxFifo* Fifo = (AvFifo_RxFifo*)Context;

    OsThread_SetName("DtAvRx");

    OsThread_RaisePriority();
    while (DtAtomic_Load(&Fifo->StopRequested) == 0)
    {
        int Packets = 0;
        bool LostSync = false;
        OsMutex_Lock(Fifo->ParserLock);
        DtapiResult Result =
            DtAvReader_Pass(&Fifo->Reader, ParseByKind, Fifo, &Packets, &LostSync);
        if (LostSync && Fifo->Kind == DT_AV_KIND_AUDIO)
            Fifo->AudioRx.Stats.SyncErrors++;
        else if (LostSync)
            Fifo->VideoRx.Stats.SyncErrors++;
        OsMutex_Unlock(Fifo->ParserLock);
        if (Result != DTAPI_OK || Packets == 0)
            OsTime_SleepMs(DT_AV_RX_IDLE_SLEEP_MS);
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Starting +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetIpFilter -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The filter takes the destination address and port, the sources' address, each source
// port that is not -1, and the VLAN ID, which the filter's flags do not enable.
//
static DtapiResult SetIpFilter(AvFifo_RxFifo* Fifo, bool Enable)
{
    DtIpFilter Filter;
    memset(&Filter, 0, sizeof(Filter));
    if (Enable)
    {
        const AvFifo_IpPars* Pars = &Fifo->IpPars.Pars;
        bool IpV6 = DtAvIpPars_IsIpV6(&Fifo->IpPars);
        Filter.Flags =
            DT_PIPE_IPFLT_FLAG_EN_FILT | DT_PIPE_IPFLT_FLAG_EN_DSTPORT0 |
            (IpV6 ? DT_PIPE_IPFLT_FLAG_EN_DSTIP_IPV6 : DT_PIPE_IPFLT_FLAG_EN_DSTIP_IPV4);
        memcpy(Filter.DstIp, Pars->IpAddr, IpV6 ? 16 : 4);
        Filter.DstPort[0] = (uint16_t)Pars->Port;
        if (Pars->NSrcFlt > 0)
        {
            Filter.Flags |= IpV6 ? DT_PIPE_IPFLT_FLAG_EN_SRCIP_IPV6
                                 : DT_PIPE_IPFLT_FLAG_EN_SRCIP_IPV4;
            memcpy(Filter.SrcIp, Pars->SrcFlt[0].IpAddr, IpV6 ? 16 : 4);
        }
        for (int i = 0; i < Pars->NSrcFlt; i++)
        {
            if (Pars->SrcFlt[i].Port == -1)
                continue;
            Filter.SrcPort[i] = (uint16_t)Pars->SrcFlt[i].Port;
            Filter.Flags |= (uint32_t)DT_PIPE_IPFLT_FLAG_EN_SRCPORT0 << i;
        }
        Filter.VlanId[0] = Pars->Vlan.Id;
    }
    return DtPcieCmd_PipeSetIpFilter(Fifo->Port.Device.Drv, Fifo->Pipe.Ref, &Filter);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StopReceiving -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Stopping: the thread, the group, the filter, the pipe and the socket, as far as they
// are there. The frame the parser was filling goes back to the pool.
//
static void StopReceiving(AvFifo_RxFifo* Fifo)
{
    DtAtomic_Store(&Fifo->StopRequested, 1);
    OsThread_Join(Fifo->Thread);
    Fifo->Thread = NULL;

    bool IpV6 = DtAvIpPars_IsIpV6(&Fifo->IpPars);
    if (Fifo->HasJoinedGroup)
    {
        uint8_t Sources[3 * 16];
        int NumSources = DtAvIpPars_CopySources(&Fifo->IpPars, Sources);
        DtNet_Leave(Fifo->Socket, Fifo->ItfIndex, IpV6, Fifo->IpPars.Pars.IpAddr, Sources,
                    NumSources);
        Fifo->HasJoinedGroup = false;
    }
    if (Fifo->Pipe.Ref.Uuid != 0)
        SetIpFilter(Fifo, false);
    DtAvPipe_Close(&Fifo->Pipe);
    OsNetSocket_Close(Fifo->Socket);
    Fifo->Socket = NULL;
    if (Fifo->Kind == DT_AV_KIND_VIDEO)
        DtSt2110VideoRx_Reset(&Fifo->VideoRx);
    Fifo->Started = false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StartReceiving -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Starting, in its order: the network, the pipe and its buffer, the own address and a
// socket bound to it, an empty FIFO and statistics, the pipe running with its filter,
// the thread, and the multicast group last.
//
static DtapiResult StartReceiving(AvFifo_RxFifo* Fifo)
{
    static const char* const Where = "AvFifo_RxFifo_Start";
    const AvFifo_IpPars* Pars = &Fifo->IpPars.Pars;
    bool IpV6 = DtAvIpPars_IsIpV6(&Fifo->IpPars);
    bool IsVideo = Fifo->Kind == DT_AV_KIND_VIDEO;

    DtapiResult Result = DtAvPort_CheckNetwork(&Fifo->Port, Pars, Where);
    if (Result != DTAPI_OK)
        return Result;
    Result = DtAvPort_OpenPipe(&Fifo->Port, &Fifo->Pipe, true, IsVideo, Where);
    if (Result != DTAPI_OK)
        return Result;
    Result = DtAvPipe_SetBuffer(&Fifo->Pipe, IsVideo ? RX_SHARED_BUFFER_VIDEO_BYTES
                                                     : RX_SHARED_BUFFER_AUDIO_BYTES);
    if (Result != DTAPI_OK)
        return DtAvError_Set(Result, Where, "Allocating the shared buffer failed");

    DtNetOwnAddress Own;
    Result =
        DtNet_ChooseInputAddress(Fifo->Port.Mac, Pars->Vlan.Id, IpV6, Pars->IpAddr, &Own);
    if (Result == DTAPI_OK && OsNetSocket_Bind(IpV6, Own.Ip, (uint16_t)Pars->Port,
                                               Own.Itf.Index, &Fifo->Socket) != OS_NET_OK)
    {
        Result = DTAPI_E_BIND;
    }
    if (Result != DTAPI_OK)
        return DtAvError_Set(Result, Where, "Failed to prepare the input pipe");
    Fifo->ItfIndex = Own.Itf.Index;

    DtAvFrameFifo_Clear(&Fifo->Fifo, &Fifo->Pool);
    const DtAvRxSink Target = {&Fifo->Pool, DeliverToFifo, Fifo};
    if (IsVideo)
        DtSt2110VideoRx_Init(&Fifo->VideoRx, Fifo->VideoConfig.Format, DtAvPixConv_Best(),
                             &Target);
    else
        DtSt2110AudioRx_Init(&Fifo->AudioRx, &Fifo->AudioConfig, &Target);

    OsDrv* Drv = Fifo->Port.Device.Drv;
    Result = DtPcieCmd_PipeFlush(Drv, Fifo->Pipe.Ref);
    DtAvReader_Init(&Fifo->Reader, &Fifo->Pipe);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_PipeSetOpMode(Drv, Fifo->Pipe.Ref, DT_PIPE_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result = SetIpFilter(Fifo, true);
    if (Result != DTAPI_OK)
        return DtAvError_Set(Result, Where, "Error setting the IP filter in the driver");

    DtAtomic_Store(&Fifo->StopRequested, 0);
    Fifo->Thread = OsThread_Start(AvRxThread, Fifo);
    if (Fifo->Thread == NULL)
        return DtAvError_Set(DTAPI_E_OUT_OF_RESOURCES, Where,
                             "Starting the thread failed");
    Fifo->Started = true;

    if (DtNet_IsMulticast(IpV6, Pars->IpAddr))
    {
        uint8_t Sources[3 * 16];
        int NumSources = DtAvIpPars_CopySources(&Fifo->IpPars, Sources);
        Result = DtNet_Join(Fifo->Socket, Fifo->ItfIndex, IpV6, Pars->IpAddr, Sources,
                            NumSources);
        if (Result != DTAPI_OK)
        {
            DtNet_Leave(Fifo->Socket, Fifo->ItfIndex, IpV6, Pars->IpAddr, Sources,
                        NumSources);
            return DtAvError_Set(Result, Where, "Failed to register multicast address");
        }
        Fifo->HasJoinedGroup = true;
    }
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Checks +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckStopped -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DTAPI_OK for an attached FIFO that is not started.
//
static DtapiResult CheckStopped(const AvFifo_RxFifo* Fifo, const char* Where)
{
    if (!Fifo->Attached)
        return DtAvError_Set(DTAPI_E_NOT_ATTACHED, Where, "RxFifo not attached");
    if (Fifo->Started)
        return DtAvError_Set(DTAPI_E_STARTED, Where,
                             "Not allowed: RxFifo already started");
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Lifetime +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
AvFifo_RxFifo* AvFifo_RxFifo_Alloc(void)
{
    AvFifo_RxFifo* Fifo = (AvFifo_RxFifo*)DtAlloc_Malloc(sizeof(AvFifo_RxFifo));
    if (Fifo == NULL)
        return NULL;
    memset(Fifo, 0, sizeof(*Fifo));
    Fifo->Lock = OsMutex_Create();
    Fifo->ParserLock = OsMutex_Create();
    bool PoolMade = DtAvFramePool_Init(&Fifo->Pool) == DTAPI_OK;
    bool FifoMade = DtAvFrameFifo_Init(&Fifo->Fifo) == DTAPI_OK;
    if (Fifo->Lock == NULL || Fifo->ParserLock == NULL || !PoolMade || !FifoMade)
    {
        if (FifoMade)
            DtAvFrameFifo_Destroy(&Fifo->Fifo);
        if (PoolMade)
            DtAvFramePool_Destroy(&Fifo->Pool);
        OsMutex_Destroy(Fifo->ParserLock);
        OsMutex_Destroy(Fifo->Lock);
        DtAlloc_Free(Fifo);
        return NULL;
    }
    return Fifo;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void AvFifo_RxFifo_Free(AvFifo_RxFifo* Fifo)
{
    if (Fifo == NULL)
        return;
    AvFifo_RxFifo_Detach(Fifo);
    DtAvFrameFifo_Destroy(&Fifo->Fifo);
    DtAvFramePool_Destroy(&Fifo->Pool);
    OsMutex_Destroy(Fifo->ParserLock);
    OsMutex_Destroy(Fifo->Lock);
    DtAlloc_Free(Fifo);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void AvFifo_RxFifo_Freep(AvFifo_RxFifo** Fifo)
{
    if (Fifo == NULL)
        return;
    AvFifo_RxFifo_Free(*Fifo);
    *Fifo = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_Attach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult AvFifo_RxFifo_Attach(AvFifo_RxFifo* Fifo, const DtDevice* Device, int Port)
{
    return AvFifo_RxFifo_Attach2(Fifo, Device, Port, HwOrSwPipe_Auto);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_Attach2 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult AvFifo_RxFifo_Attach2(AvFifo_RxFifo* Fifo, const DtDevice* Device, int Port,
                                  HwOrSwPipe Pipe)
{
    static const char* const Where = "AvFifo_RxFifo_Attach";
    if (Fifo == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO");
    OsMutex_Lock(Fifo->Lock);
    DtapiResult Result = DTAPI_OK;
    if (Fifo->Attached)
        Result = DtAvError_Set(DTAPI_E_ATTACHED, Where, "RxFifo already attached");
    else // A Port counts from 1, a PortIndex from 0
        Result =
            DtAvPort_Attach(&Fifo->Port, Device, Port >= 1 ? Port - 1 : -1, Pipe, Where);
    Fifo->Attached = Fifo->Attached || Result == DTAPI_OK;
    OsMutex_Unlock(Fifo->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult AvFifo_RxFifo_Detach(AvFifo_RxFifo* Fifo)
{
    static const char* const Where = "AvFifo_RxFifo_Detach";
    if (Fifo == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO");
    OsMutex_Lock(Fifo->Lock);
    DtapiResult Result = DTAPI_OK;
    if (!Fifo->Attached)
        Result = DtAvError_Set(DTAPI_E_NOT_ATTACHED, Where, "RxFifo not attached");
    else
    {
        StopReceiving(Fifo);
        DtAvFrameFifo_Clear(&Fifo->Fifo, &Fifo->Pool);
        DtAvPort_Detach(&Fifo->Port);
        Fifo->Attached = false;
    }
    OsMutex_Unlock(Fifo->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_Clear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult AvFifo_RxFifo_Clear(AvFifo_RxFifo* Fifo)
{
    static const char* const Where = "AvFifo_RxFifo_Clear";
    if (Fifo == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO");
    OsMutex_Lock(Fifo->Lock);
    DtapiResult Result =
        Fifo->Started
            ? DtAvError_Set(DTAPI_E_STARTED, Where, "Not allowed: RxFifo already started")
            : DTAPI_OK;
    if (Result == DTAPI_OK)
        DtAvFrameFifo_Clear(&Fifo->Fifo, &Fifo->Pool);
    OsMutex_Unlock(Fifo->Lock);
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Configuration +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_ConfigureAudio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult AvFifo_RxFifo_ConfigureAudio(AvFifo_RxFifo* Fifo,
                                         const St2110_RxConfigAudio* Config)
{
    static const char* const Where = "AvFifo_RxFifo_ConfigureAudio";
    if (Fifo == NULL || Config == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO or configuration");
    if ((int)Config->Format < 0 || (int)Config->Format > (int)St2110_AudioFormat_Raw)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "Invalid audio format");
    OsMutex_Lock(Fifo->Lock);
    DtapiResult Result = CheckStopped(Fifo, Where);
    if (Result == DTAPI_OK)
    {
        Fifo->AudioConfig = *Config;
        Fifo->Kind = DT_AV_KIND_AUDIO;
        if (!Fifo->HasExplicitMaxSize)
            DtAvFrameFifo_SetMaxSize(&Fifo->Fifo, RX_AUDIO_FIFO_FRAMES);
    }
    OsMutex_Unlock(Fifo->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_ConfigureVideo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult AvFifo_RxFifo_ConfigureVideo(AvFifo_RxFifo* Fifo,
                                         const St2110_RxConfigVideo* Config)
{
    static const char* const Where = "AvFifo_RxFifo_ConfigureVideo";
    if (Fifo == NULL || Config == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO or configuration");
    if ((int)Config->Format < 0 ||
        (int)Config->Format > (int)St2110_RxFrameFormat_Yuv422p_8b)
    {
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "Invalid frame format");
    }
    OsMutex_Lock(Fifo->Lock);
    DtapiResult Result = CheckStopped(Fifo, Where);
    if (Result == DTAPI_OK)
    {
        Fifo->VideoConfig = *Config;
        Fifo->Kind = DT_AV_KIND_VIDEO;
    }
    OsMutex_Unlock(Fifo->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_SetIpPars -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult AvFifo_RxFifo_SetIpPars(AvFifo_RxFifo* Fifo, const AvFifo_IpPars* IpPars)
{
    static const char* const Where = "AvFifo_RxFifo_SetIpPars";
    if (Fifo == NULL || IpPars == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO or IP parameters");
    OsMutex_Lock(Fifo->Lock);
    DtapiResult Result = CheckStopped(Fifo, Where);
    DtAvIpPars Copy = {0};
    if (Result == DTAPI_OK)
        Result = DtAvIpPars_Copy(&Copy, IpPars, Where);
    if (Result == DTAPI_OK)
    {
        Fifo->IpPars = Copy;
        Fifo->IpPars.Pars.SrcFlt = IpPars->NSrcFlt > 0 ? Fifo->IpPars.Sources : NULL;
        Fifo->HasIpPars = true;
    }
    OsMutex_Unlock(Fifo->Lock);
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Running +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_Start -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult AvFifo_RxFifo_Start(AvFifo_RxFifo* Fifo)
{
    static const char* const Where = "AvFifo_RxFifo_Start";
    if (Fifo == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO");
    OsMutex_Lock(Fifo->Lock);
    DtapiResult Result = CheckStopped(Fifo, Where);
    if (Result == DTAPI_OK && Fifo->Kind == DT_AV_KIND_NONE)
        Result =
            DtAvError_Set(DTAPI_E_CONFIG, Where, "Configure the RxFifo before starting");
    if (Result == DTAPI_OK && !Fifo->HasIpPars)
        Result = DtAvError_Set(DTAPI_E_NO_IPPARS, Where,
                               "Set IP parameters before starting the RxFifo");
    if (Result == DTAPI_OK)
    {
        Result = StartReceiving(Fifo);
        if (Result != DTAPI_OK)
            StopReceiving(Fifo);
    }
    OsMutex_Unlock(Fifo->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_Stop -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult AvFifo_RxFifo_Stop(AvFifo_RxFifo* Fifo)
{
    static const char* const Where = "AvFifo_RxFifo_Stop";
    if (Fifo == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO");
    OsMutex_Lock(Fifo->Lock);
    DtapiResult Result = DTAPI_OK;
    if (!Fifo->Attached)
        Result = DtAvError_Set(DTAPI_E_NOT_ATTACHED, Where, "RxFifo not attached");
    else if (Fifo->Started)
        StopReceiving(Fifo);
    OsMutex_Unlock(Fifo->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_GetFifoLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int AvFifo_RxFifo_GetFifoLoad(const AvFifo_RxFifo* Fifo)
{
    return Fifo != NULL ? DtAvFrameFifo_Load(&Fifo->Fifo) : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_Read -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
AvFifo_Frame* AvFifo_RxFifo_Read(AvFifo_RxFifo* Fifo)
{
    if (Fifo == NULL)
        return NULL;
    DtAvFrame* Frame = DtAvFrameFifo_Pop(&Fifo->Fifo);
    return Frame != NULL ? &Frame->Frame : NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_ReturnToMemPool -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult AvFifo_RxFifo_ReturnToMemPool(AvFifo_RxFifo* Fifo, AvFifo_Frame* Frame)
{
    static const char* const Where = "AvFifo_RxFifo_ReturnToMemPool";
    if (Fifo == NULL || Frame == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO or frame");
    if (Fifo->Kind == DT_AV_KIND_NONE)
        return DtAvError_Set(DTAPI_E_CONFIG, Where, "Configure the RxFifo first");
    if (!DtAvFramePool_Return(&Fifo->Pool, Frame))
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where,
                             "The frame is not a frame this RxFifo gave");
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_GetMaxSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int AvFifo_RxFifo_GetMaxSize(const AvFifo_RxFifo* Fifo)
{
    return Fifo != NULL ? DtAvFrameFifo_GetMaxSize(&Fifo->Fifo) : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_SetMaxSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A FIFO that is started, or a size below 1, keeps its size, and the failure's text is
// set.
//
void AvFifo_RxFifo_SetMaxSize(AvFifo_RxFifo* Fifo, int Size)
{
    static const char* const Where = "AvFifo_RxFifo_SetMaxSize";
    if (Fifo == NULL)
        return;
    OsMutex_Lock(Fifo->Lock);
    if (Fifo->Started)
        DtAvError_Set(DTAPI_E_STARTED, Where, "Not allowed: RxFifo already started");
    else if (Size < 1)
        DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "The size must be at least 1");
    else if (DtAvFrameFifo_SetMaxSize(&Fifo->Fifo, Size) != DTAPI_OK)
        DtAvError_Set(DTAPI_E_OUT_OF_MEM, Where, "No memory for the FIFO");
    else
        Fifo->HasExplicitMaxSize = true;
    OsMutex_Unlock(Fifo->Lock);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_GetStatistics -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
RxStatistics AvFifo_RxFifo_GetStatistics(const AvFifo_RxFifo* Fifo)
{
    RxStatistics Stats;
    memset(&Stats, 0, sizeof(Stats));
    if (Fifo == NULL)
        return Stats;
    OsMutex_Lock(Fifo->ParserLock);
    if (Fifo->Kind == DT_AV_KIND_AUDIO)
        Stats = Fifo->AudioRx.Stats;
    else if (Fifo->Kind == DT_AV_KIND_VIDEO)
        Stats = Fifo->VideoRx.Stats;
    OsMutex_Unlock(Fifo->ParserLock);
    return Stats;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_UsesHwPipe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult AvFifo_RxFifo_UsesHwPipe(const AvFifo_RxFifo* Fifo, int* UsesHwPipe)
{
    static const char* const Where = "AvFifo_RxFifo_UsesHwPipe";
    if (Fifo == NULL || UsesHwPipe == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO or result");
    OsMutex_Lock(Fifo->Lock);
    DtapiResult Result =
        Fifo->Attached
            ? DtAvPort_UsesHwPipe(&Fifo->Port, Fifo->Started, &Fifo->Pipe, UsesHwPipe,
                                  Where)
            : DtAvError_Set(DTAPI_E_NOT_ATTACHED, Where, "RxFifo not attached");
    OsMutex_Unlock(Fifo->Lock);
    return Result;
}
