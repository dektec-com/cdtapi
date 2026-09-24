// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAvTxFifo.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The transmit FIFO of SMPTE 2110 audio and video
//
// SPDX-License-Identifier: BSD-3-Clause
//
// A FIFO of the application's frames, a thread that packetizes each into the shared
// buffer of a pipe of the port once it has room, and the card's scheduler, which sends
// every packet at its time of day. The thread polls the pipe for room instead of waiting
// for the driver's event (plan 0009, question 2).

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"  // Allocation seam.
#include "Core/DtAtomic.h" // The thread's stop flag and the statistics.
#include "DtAvError.h"     // Failure texts.
#include "DtAvPort.h"      // The port, its network and pipes.
#include "DtPcieAbi.h"     // Pipe modes.
#include "DtSt2110Audio.h" // Audio packets.
#include "DtSt2110Video.h" // Video packets.
#include "Net/DtNet.h"     // Own address and the destination's MAC address.
#include "OAL/OsThread.h"  // Lock, thread, event, sleeping.
#include "cdtapi_avfifo.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The FIFO size for audio, 50 ms of 125 us packets.
#define TX_AUDIO_MAX_SIZE 400

// How long the thread waits for a frame before it looks at its stop flag again.
#define TX_WAIT_MS 100

// The fewest bytes of a shared buffer.
#define TX_MIN_BUFFER (64 * 1024)

#define KIND_NONE 0
#define KIND_AUDIO 1
#define KIND_VIDEO 2

struct AvFifo_TxFifoC
{
    OsMutex* Lock; // Guards the calls of the application
    bool Attached;
    DtAtomicInt Started;
    DtAvPort Port;

    int Kind;
    St2110_TxConfigAudio AudioConfig;
    St2110_TxConfigVideo VideoConfig;
    DtSt2110AudioTx Audio;
    DtSt2110VideoTx Video;
    bool IpParsSet;
    DtAvIpPars Ip;
    bool MaxSizeWasSet;

    DtAvFramePool Pool;
    DtAvFrameFifo Fifo;
    OsEvent* Wake; // Set when a frame is written, and to stop the thread
    DtAtomicInt FramesOk;

    // While started.
    DtAvPipe Pipe;
    DtAvWriter Writer;
    DtAvTxStream Stream;
    OsNetSocket* Socket;
    OsThread* Thread;
    DtAtomicInt Stop;
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The thread +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BytesNeeded -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The room a frame's packets need: those of a video frame at most, those of an audio
// frame exactly.
//
static int BytesNeeded(AvFifo_TxFifo* Fifo, const AvFifo_Frame* Frame)
{
    if (Fifo->Kind == KIND_VIDEO)
        return DtSt2110VideoTx_FrameBytes(&Fifo->Video, &Fifo->Stream);
    return DtSt2110AudioTx_PacketBytes(&Fifo->Audio, &Fifo->Stream, Frame);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TransmitThread -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Takes the oldest frame, waits until the buffer has room for its packets, packetizes it
// and returns it to the pool. A frame that no buffer's worth of room can hold, or that
// fails to go out, returns to the pool unsent.
//
static void TransmitThread(void* Context)
{
    AvFifo_TxFifo* Fifo = (AvFifo_TxFifo*)Context;

    OsThread_SetName("DtAvTx");

    OsThread_RaisePriority();
    while (DtAtomic_Load(&Fifo->Stop) == 0)
    {
        DtAvFrame* Frame = DtAvFrameFifo_Pop(&Fifo->Fifo);
        if (Frame == NULL)
        {
            OsEvent_Wait(Fifo->Wake, TX_WAIT_MS);
            continue;
        }

        int Needed = BytesNeeded(Fifo, &Frame->Frame);
        bool Room = Needed >= 0 && (uint32_t)Needed <= DtAvPipe_MaxLoad(&Fifo->Pipe);
        while (Room && DtAtomic_Load(&Fifo->Stop) == 0)
        {
            uint32_t Free = 0;
            if (DtAvWriter_Free(&Fifo->Writer, &Free) == DTAPI_OK &&
                Free >= (uint32_t)Needed)
            {
                break;
            }
            OsTime_SleepMs(1);
        }

        if (Room && DtAtomic_Load(&Fifo->Stop) == 0)
        {
            DtapiResult Result =
                Fifo->Kind == KIND_VIDEO
                    ? DtSt2110VideoTx_Packetize(&Fifo->Video, &Fifo->Stream,
                                                &Frame->Frame, &Fifo->Writer.Sink)
                    : DtSt2110AudioTx_Packetize(&Fifo->Audio, &Fifo->Stream,
                                                &Frame->Frame, &Fifo->Writer.Sink);
            if (DtAvWriter_Flush(&Fifo->Writer) == DTAPI_OK && Result == DTAPI_OK)
                DtAtomic_Increment(&Fifo->FramesOk);
        }
        DtAvFramePool_Return(&Fifo->Pool, &Frame->Frame);
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Starting +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RoundToPages -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Rounds up to whole 4 KB pages, and adds one page more.
//
static size_t RoundToPages(uint64_t Size)
{
    return (size_t)((Size + 4095) / 4096 + 1) * 4096;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BufferSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The shared buffer size: 80 ms of video, at least two frames of it and the room a
// frame's packets need; or 80 ms of audio samples, at least two of the largest packets.
// Never less than TX_MIN_BUFFER.
//
static size_t BufferSize(AvFifo_TxFifo* Fifo)
{
    uint64_t Size = 0;
    uint64_t Needed = 0;
    if (Fifo->Kind == KIND_VIDEO)
    {
        const DtSt2110VideoTx* Video = &Fifo->Video;
        uint64_t Frame = (uint64_t)Video->NumRows * (uint64_t)Video->RowSizeFrame;
        Size = Frame * 8 * (uint64_t)Video->Rate.Numerator /
               (100 * (uint64_t)Video->Rate.Denominator);
        if (Size < 2 * Frame)
            Size = 2 * Frame;
        Needed = (uint64_t)DtSt2110VideoTx_FrameBytes(Video, &Fifo->Stream);
    }
    else
    {
        Size = (uint64_t)Fifo->Audio.BytesPerSample *
               (uint64_t)Fifo->AudioConfig.SampleRate * 8 / 100;
        Needed = 2 * (uint64_t)DT_AV_PIPE_MAX_PACKET;
    }
    if (Size < Needed)
        Size = Needed;
    if (Size < TX_MIN_BUFFER)
        Size = TX_MIN_BUFFER;
    return RoundToPages(Size);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Teardown -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Stopping: the thread, the pipe and the socket, as far as they are there.
//
static void Teardown(AvFifo_TxFifo* Fifo)
{
    DtAtomic_Store(&Fifo->Stop, 1);
    OsEvent_Set(Fifo->Wake);
    OsThread_Join(Fifo->Thread);
    Fifo->Thread = NULL;
    DtAvPipe_Close(&Fifo->Pipe);
    OsNetSocket_Close(Fifo->Socket);
    Fifo->Socket = NULL;
    DtAtomic_Store(&Fifo->Started, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ByteSwapped -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static uint32_t ByteSwapped(uint32_t Value)
{
    return Value >> 24 | (Value >> 8 & 0xFF00) | (Value << 8 & 0xFF0000) | Value << 24;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Start -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Starting, in its order: the network, the pipe, the own address and a socket bound to
// it, the destination's MAC address, the stream, the pipe's buffer, sized for the
// stream, an empty FIFO and statistics, the pipe running, and the thread. The
// synchronisation source is the pipe's UUID with its bytes reversed, so that the RTP
// header carries it least significant byte first.
//
static DtapiResult Start(AvFifo_TxFifo* Fifo)
{
    static const char* const Where = "AvFifo_TxFifo_Start";
    const AvFifo_IpPars* Pars = &Fifo->Ip.Pars;
    bool IpV6 = DtAvIpPars_IsIpV6(&Fifo->Ip);
    bool Video = Fifo->Kind == KIND_VIDEO;

    DtapiResult Result = DtAvPort_CheckNetwork(&Fifo->Port, Pars, Where);
    if (Result != DTAPI_OK)
        return Result;
    Result = DtAvPort_OpenPipe(&Fifo->Port, &Fifo->Pipe, false, Video, Where);
    if (Result != DTAPI_OK)
        return Result;

    DtNetOwn Own;
    Result = DtNet_ChooseOutputAddress(Fifo->Port.Mac, Pars->Vlan.Id, IpV6, Pars->IpAddr,
                                       &Own);
    if (Result == DTAPI_OK &&
        OsNetSocket_Bind(IpV6, Own.Ip, 0, Own.Itf.Index, &Fifo->Socket) != OS_NET_OK)
    {
        Result = DTAPI_E_BIND;
    }
    if (Result != DTAPI_OK)
        return DtAvError_Set(Result, Where, "Failed to prepare the output pipe");

    DtAvTxStream* Stream = &Fifo->Stream;
    memset(Stream, 0, sizeof(*Stream));
    Result = DtNet_ResolveDstMac(&Own, Pars->IpAddr, Pars->Gateway, Stream->Net.DstMac);
    if (Result != DTAPI_OK)
        return DtAvError_Set(Result, Where,
                             "Failed to resolve the destination MAC address");

    Stream->Net.HeaderV2 = DtAvPipe_IsJumbo(&Fifo->Pipe);
    Stream->Net.Alignment = DtAvPipe_Alignment(&Fifo->Pipe);
    memcpy(Stream->Net.SrcMac, Fifo->Port.Mac, 6);
    Stream->Net.VlanId = Pars->Vlan.Id;
    Stream->Net.VlanPriority = Pars->Vlan.Priority;
    Stream->Net.IpV6 = IpV6;
    memcpy(Stream->Net.SrcIp, Own.Ip, 16);
    memcpy(Stream->Net.DstIp, Pars->IpAddr, 16);
    Stream->Net.DiffServ = Pars->DiffServ;
    Stream->Net.TimeToLive = Pars->TimeToLive;
    Stream->Net.SrcPort = OsNetSocket_Port(Fifo->Socket);
    Stream->Net.DstPort = (uint16_t)Pars->Port;
    Stream->PayloadType = Pars->RtpPayloadType;
    Stream->Ssrc = ByteSwapped((uint32_t)Fifo->Pipe.Ref.Uuid);
    Stream->OutputDelayNs = DT_AV_OUTPUT_DELAY_NS;
    if (Video)
    {
        Result = DtSt2110VideoTx_Configure(&Fifo->Video, &Fifo->VideoConfig,
                                           DtAvPixConv_Best());
        if (Result == DTAPI_OK)
            Result = DtSt2110VideoTx_Start(&Fifo->Video, Stream);
        if (Result != DTAPI_OK)
            return DtAvError_Set(Result, Where, "Invalid video packing");
    }
    else
        DtSt2110AudioTx_Reset(&Fifo->Audio);

    Result = DtAvPipe_SetBuffer(&Fifo->Pipe, BufferSize(Fifo));
    if (Result != DTAPI_OK)
        return DtAvError_Set(Result, Where, "Allocating the shared buffer failed");

    DtAvFrameFifo_Clear(&Fifo->Fifo, &Fifo->Pool);
    DtAtomic_Store(&Fifo->FramesOk, 0);
    OsDrv* Drv = Fifo->Port.Device.Drv;
    Result = DtPcieCmd_PipeFlush(Drv, Fifo->Pipe.Ref);
    DtAvWriter_Init(&Fifo->Writer, &Fifo->Pipe);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_PipeSetOpMode(Drv, Fifo->Pipe.Ref, DT_PIPE_OPMODE_RUN);
    if (Result != DTAPI_OK)
        return DtAvError_Set(Result, Where, "Starting the pipe failed");

    DtAtomic_Store(&Fifo->Stop, 0);
    Fifo->Thread = OsThread_Start(TransmitThread, Fifo);
    if (Fifo->Thread == NULL)
        return DtAvError_Set(DTAPI_E_OUT_OF_RESOURCES, Where,
                             "Starting the thread failed");
    DtAtomic_Store(&Fifo->Started, 1);
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Checks +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckStopped -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult CheckStopped(const AvFifo_TxFifo* Fifo, const char* Where)
{
    if (!Fifo->Attached)
        return DtAvError_Set(DTAPI_E_NOT_ATTACHED, Where, "TxFifo not attached");
    if (DtAtomic_Load(&Fifo->Started) != 0)
        return DtAvError_Set(DTAPI_E_STARTED, Where,
                             "Not allowed: TxFifo already started");
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The size checks the video and audio packetizers make, applied here when the frame is
// written.
//
static bool CheckFrame(const AvFifo_TxFifo* Fifo, const AvFifo_Frame* Frame)
{
    if (Frame->NumValidBytes < 0 || (size_t)Frame->NumValidBytes > Frame->Size)
        return false;
    if (Fifo->Kind == KIND_VIDEO)
        return Frame->NumValidBytes ==
               DtSt2110VideoTx_FrameSize(&Fifo->Video, Frame->Field);
    if (Fifo->Audio.BytesPerSample == 0)
        return Frame->NumValidBytes <= DT_ST2110_AUDIO_MAX_PAYLOAD;
    return Frame->NumValidBytes % Fifo->Audio.BytesPerSample == 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Lifetime +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
AvFifo_TxFifo* AvFifo_TxFifo_Alloc(void)
{
    AvFifo_TxFifo* Fifo = (AvFifo_TxFifo*)DtAlloc_Malloc(sizeof(AvFifo_TxFifo));
    if (Fifo == NULL)
        return NULL;
    memset(Fifo, 0, sizeof(*Fifo));
    Fifo->Lock = OsMutex_Create();
    Fifo->Wake = OsEvent_Create();
    bool PoolMade = DtAvFramePool_Init(&Fifo->Pool) == DTAPI_OK;
    bool FifoMade = DtAvFrameFifo_Init(&Fifo->Fifo) == DTAPI_OK;
    if (Fifo->Lock == NULL || Fifo->Wake == NULL || !PoolMade || !FifoMade)
    {
        if (FifoMade)
            DtAvFrameFifo_Destroy(&Fifo->Fifo);
        if (PoolMade)
            DtAvFramePool_Destroy(&Fifo->Pool);
        OsEvent_Destroy(Fifo->Wake);
        OsMutex_Destroy(Fifo->Lock);
        DtAlloc_Free(Fifo);
        return NULL;
    }
    return Fifo;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void AvFifo_TxFifo_Free(AvFifo_TxFifo* Fifo)
{
    if (Fifo == NULL)
        return;
    AvFifo_TxFifo_Detach(Fifo);
    DtAvFrameFifo_Destroy(&Fifo->Fifo);
    DtAvFramePool_Destroy(&Fifo->Pool);
    OsEvent_Destroy(Fifo->Wake);
    OsMutex_Destroy(Fifo->Lock);
    DtAlloc_Free(Fifo);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void AvFifo_TxFifo_Freep(AvFifo_TxFifo** Fifo)
{
    if (Fifo == NULL)
        return;
    AvFifo_TxFifo_Free(*Fifo);
    *Fifo = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_Attach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult AvFifo_TxFifo_Attach(AvFifo_TxFifo* Fifo, const DtDevice* Device, int Port)
{
    return AvFifo_TxFifo_Attach2(Fifo, Device, Port, HwOrSwPipe_Auto);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_Attach2 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult AvFifo_TxFifo_Attach2(AvFifo_TxFifo* Fifo, const DtDevice* Device, int Port,
                                  HwOrSwPipe Pipe)
{
    static const char* const Where = "AvFifo_TxFifo_Attach";
    if (Fifo == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO");
    OsMutex_Lock(Fifo->Lock);
    DtapiResult Result = DTAPI_OK;
    if (Fifo->Attached)
        Result = DtAvError_Set(DTAPI_E_ATTACHED, Where, "TxFifo already attached");
    else // A Port counts from 1, a PortIndex from 0
        Result =
            DtAvPort_Attach(&Fifo->Port, Device, Port >= 1 ? Port - 1 : -1, Pipe, Where);
    Fifo->Attached = Fifo->Attached || Result == DTAPI_OK;
    OsMutex_Unlock(Fifo->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult AvFifo_TxFifo_Detach(AvFifo_TxFifo* Fifo)
{
    static const char* const Where = "AvFifo_TxFifo_Detach";
    if (Fifo == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO");
    OsMutex_Lock(Fifo->Lock);
    DtapiResult Result = DTAPI_OK;
    if (!Fifo->Attached)
        Result = DtAvError_Set(DTAPI_E_NOT_ATTACHED, Where, "TxFifo not attached");
    else
    {
        Teardown(Fifo);
        DtAvFrameFifo_Clear(&Fifo->Fifo, &Fifo->Pool);
        DtAvPort_Detach(&Fifo->Port);
        Fifo->Attached = false;
    }
    OsMutex_Unlock(Fifo->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_Clear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult AvFifo_TxFifo_Clear(AvFifo_TxFifo* Fifo)
{
    static const char* const Where = "AvFifo_TxFifo_Clear";
    if (Fifo == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO");
    OsMutex_Lock(Fifo->Lock);
    DtapiResult Result = DTAPI_OK;
    if (DtAtomic_Load(&Fifo->Started) != 0)
        Result =
            DtAvError_Set(DTAPI_E_STARTED, Where, "Not allowed: TxFifo already started");
    else
        DtAvFrameFifo_Clear(&Fifo->Fifo, &Fifo->Pool);
    OsMutex_Unlock(Fifo->Lock);
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Configuration +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_ConfigureAudio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult AvFifo_TxFifo_ConfigureAudio(AvFifo_TxFifo* Fifo,
                                         const St2110_TxConfigAudio* Config)
{
    static const char* const Where = "AvFifo_TxFifo_ConfigureAudio";
    if (Fifo == NULL || Config == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO or configuration");
    OsMutex_Lock(Fifo->Lock);
    DtapiResult Result = CheckStopped(Fifo, Where);
    DtSt2110AudioTx Audio = {0};
    if (Result == DTAPI_OK && DtSt2110AudioTx_Configure(&Audio, Config) != DTAPI_OK)
        Result = DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "Invalid audio configuration");
    if (Result == DTAPI_OK)
    {
        Fifo->AudioConfig = *Config;
        Fifo->Audio = Audio;
        Fifo->Kind = KIND_AUDIO;
        if (!Fifo->MaxSizeWasSet)
            DtAvFrameFifo_SetMaxSize(&Fifo->Fifo, TX_AUDIO_MAX_SIZE);
    }
    OsMutex_Unlock(Fifo->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_ConfigureVideo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult AvFifo_TxFifo_ConfigureVideo(AvFifo_TxFifo* Fifo,
                                         const St2110_TxConfigVideo* Config)
{
    static const char* const Where = "AvFifo_TxFifo_ConfigureVideo";
    if (Fifo == NULL || Config == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO or configuration");
    OsMutex_Lock(Fifo->Lock);
    DtapiResult Result = CheckStopped(Fifo, Where);
    DtSt2110VideoTx Video = {0};
    if (Result == DTAPI_OK &&
        DtSt2110VideoTx_Configure(&Video, Config, DtAvPixConv_Best()) != DTAPI_OK)
    {
        Result = DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "Invalid video configuration");
    }
    if (Result == DTAPI_OK)
    {
        Fifo->VideoConfig = *Config;
        Fifo->Video = Video;
        Fifo->Kind = KIND_VIDEO;
    }
    OsMutex_Unlock(Fifo->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_SetIpPars -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult AvFifo_TxFifo_SetIpPars(AvFifo_TxFifo* Fifo, const AvFifo_IpPars* IpPars)
{
    static const char* const Where = "AvFifo_TxFifo_SetIpPars";
    if (Fifo == NULL || IpPars == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO or IP parameters");
    if (IpPars->RtpPayloadType < 0 || IpPars->RtpPayloadType > 0x7F)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where,
                             "Invalid RtpPayloadType. Range: 0..127");
    OsMutex_Lock(Fifo->Lock);
    DtapiResult Result = CheckStopped(Fifo, Where);
    DtAvIpPars Copy = {0};
    if (Result == DTAPI_OK)
        Result = DtAvIpPars_Copy(&Copy, IpPars, Where);
    if (Result == DTAPI_OK)
    {
        Fifo->Ip = Copy;
        Fifo->Ip.Pars.SrcFlt = IpPars->NSrcFlt > 0 ? Fifo->Ip.Sources : NULL;
        Fifo->IpParsSet = true;
    }
    OsMutex_Unlock(Fifo->Lock);
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Running +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_Start -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult AvFifo_TxFifo_Start(AvFifo_TxFifo* Fifo)
{
    static const char* const Where = "AvFifo_TxFifo_Start";
    if (Fifo == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO");
    OsMutex_Lock(Fifo->Lock);
    DtapiResult Result = CheckStopped(Fifo, Where);
    if (Result == DTAPI_OK && Fifo->Kind == KIND_NONE)
        Result =
            DtAvError_Set(DTAPI_E_CONFIG, Where, "Configure the TxFifo before starting");
    if (Result == DTAPI_OK && !Fifo->IpParsSet)
        Result = DtAvError_Set(DTAPI_E_NO_IPPARS, Where,
                               "Set IP parameters before starting the TxFifo");
    if (Result == DTAPI_OK)
    {
        Result = Start(Fifo);
        if (Result != DTAPI_OK)
            Teardown(Fifo);
    }
    OsMutex_Unlock(Fifo->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_Stop -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult AvFifo_TxFifo_Stop(AvFifo_TxFifo* Fifo)
{
    static const char* const Where = "AvFifo_TxFifo_Stop";
    if (Fifo == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO");
    OsMutex_Lock(Fifo->Lock);
    DtapiResult Result = DTAPI_OK;
    if (!Fifo->Attached)
        Result = DtAvError_Set(DTAPI_E_NOT_ATTACHED, Where, "TxFifo not attached");
    else if (DtAtomic_Load(&Fifo->Started) != 0)
        Teardown(Fifo);
    OsMutex_Unlock(Fifo->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_GetFifoLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int AvFifo_TxFifo_GetFifoLoad(const AvFifo_TxFifo* Fifo)
{
    return Fifo != NULL ? DtAvFrameFifo_Load(&Fifo->Fifo) : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_Write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The frame is checked here as well as when it is packetized, so that a frame of the
// wrong size fails the write. A frame the FIFO has no room for stays the application's.
//
DtapiResult AvFifo_TxFifo_Write(AvFifo_TxFifo* Fifo, AvFifo_Frame* Frame)
{
    static const char* const Where = "AvFifo_TxFifo_Write";
    if (Fifo == NULL || Frame == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO or frame");
    if (DtAtomic_Load(&Fifo->Started) == 0)
        return DtAvError_Set(DTAPI_E_NOT_STARTED, Where, "TxFifo not started");
    if (!DtAvFramePool_Owns(&Fifo->Pool, Frame))
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where,
                             "The frame is not a frame this TxFifo gave");
    if (!CheckFrame(Fifo, Frame))
        return DtAvError_Set(DTAPI_E_INVALID_FORMAT, Where,
                             "Incorrect frame size for the configuration");
    if (!DtAvFrameFifo_Push(&Fifo->Fifo, DtAvFrame_Of(Frame)))
        return DtAvError_Set(DTAPI_E_FIFO_FULL, Where, "TxFifo overflow");
    OsEvent_Set(Fifo->Wake);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_GetFromMemPool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
AvFifo_Frame* AvFifo_TxFifo_GetFromMemPool(AvFifo_TxFifo* Fifo, int Size)
{
    static const char* const Where = "AvFifo_TxFifo_GetFromMemPool";
    if (Fifo == NULL || Size < 0)
    {
        DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO or a negative size");
        return NULL;
    }
    if (Fifo->Kind == KIND_NONE)
    {
        DtAvError_Set(DTAPI_E_CONFIG, Where,
                      "Configure the TxFifo before calling GetFromMemPool");
        return NULL;
    }
    DtAvFrame* Frame = DtAvFramePool_Get(&Fifo->Pool, (size_t)Size);
    if (Frame == NULL)
    {
        DtAvError_Set(DTAPI_E_OUT_OF_MEM, Where, "No memory for the frame");
        return NULL;
    }
    return &Frame->Frame;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_GetMaxSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int AvFifo_TxFifo_GetMaxSize(const AvFifo_TxFifo* Fifo)
{
    return Fifo != NULL ? DtAvFrameFifo_GetMaxSize(&Fifo->Fifo) : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_SetMaxSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A FIFO that is started, or a size below 1, keeps its size, and the failure's text is
// set.
//
void AvFifo_TxFifo_SetMaxSize(AvFifo_TxFifo* Fifo, int Size)
{
    static const char* const Where = "AvFifo_TxFifo_SetMaxSize";
    if (Fifo == NULL)
        return;
    OsMutex_Lock(Fifo->Lock);
    if (DtAtomic_Load(&Fifo->Started) != 0)
        DtAvError_Set(DTAPI_E_STARTED, Where, "Not allowed: TxFifo already started");
    else if (Size < 1)
        DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "The size must be at least 1");
    else if (DtAvFrameFifo_SetMaxSize(&Fifo->Fifo, Size) != DTAPI_OK)
        DtAvError_Set(DTAPI_E_OUT_OF_MEM, Where, "No memory for the FIFO");
    else
        Fifo->MaxSizeWasSet = true;
    OsMutex_Unlock(Fifo->Lock);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_GetStatistics -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
TxStatistics AvFifo_TxFifo_GetStatistics(const AvFifo_TxFifo* Fifo)
{
    TxStatistics Stats;
    Stats.FramesOk = Fifo != NULL ? DtAtomic_Load(&Fifo->FramesOk) : 0;
    return Stats;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_UsesHwPipe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult AvFifo_TxFifo_UsesHwPipe(const AvFifo_TxFifo* Fifo, int* UsesHwPipe)
{
    static const char* const Where = "AvFifo_TxFifo_UsesHwPipe";
    if (Fifo == NULL || UsesHwPipe == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "No FIFO or result");
    OsMutex_Lock(Fifo->Lock);
    DtapiResult Result =
        Fifo->Attached
            ? DtAvPort_UsesHwPipe(&Fifo->Port, DtAtomic_Load(&Fifo->Started) != 0,
                                  &Fifo->Pipe, UsesHwPipe, Where)
            : DtAvError_Set(DTAPI_E_NOT_ATTACHED, Where, "TxFifo not attached");
    OsMutex_Unlock(Fifo->Lock);
    return Result;
}
