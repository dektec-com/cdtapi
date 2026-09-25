// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestSimAvPipe.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Pipes and their shared buffers against the emulated DTA-2110
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPI_SIM=1. Video goes from the packetizer through a writer
// into a hardware transmit pipe, over the emulator's loopback into a hardware receive
// pipe, and through a reader into the parser, with buffers small enough that the stream
// runs around their ends more than three times.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "AvFifo/DtAvPipe.h"        // Functions under test.
#include "AvFifo/DtAvTime.h"        // Times of day.
#include "AvFifo/DtSt2110Video.h"   // The packets.
#include "Core/DtAlloc.h"           // Live allocations.
#include "DtFunc.h"                 // Finding the network function.
#include "DtPcieAbi.h"              // Pipe types and filter flags.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // Device handles.
#include "OAL/Sim/SimDtPcie.h"      // The emulated devices and their controls.
#include "OAL/Sim/SimDta2110.h"     // The DTA-2110's MAC address.
#include "OAL/Sim/SimNw.h"          // The network function's controls.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define INDEX 1
#define PORT 0
#define T0 (UINT64_C(1800000000) * DT_AV_NS_PER_SEC)
#define MS UINT64_C(1000000)
#define WIDTH 320
#define HEIGHT 240
#define ROW (WIDTH * 2)

// Opens the DTA-2110 with its clock stopped and the loopback on; gives the network
// function, with UUID 0 when there is none.
static OsDrv* OpenDevice(DtDrvObject* Nw)
{
    SimDtPcie_Reset();
    SimDtPcie_SetDta2110Index(INDEX);
    SimDtPcie_SetNwTime(T0);
    SimDtPcie_SetNwLoopback(true);
    OsDrv* Drv = OsDrv_Open(INDEX);
    memset(Nw, 0, sizeof(*Nw));
    DtFuncInstance Af;
    DtVec_Init(&Af.Objects, sizeof(DtFuncObject));
    if (Drv != NULL && DtFunc_Find(Drv, PORT, "AF_NW", "", &Af) == DTAPI_OK)
    {
        const DtFuncObject* Object = DtFunc_FindObject(&Af, true, DT_FUNC_TYPE_NW, "");
        if (Object != NULL)
            *Nw = Object->Object;
    }
    DtFunc_Release(&Af);
    return Drv;
}

typedef struct Received
{
    DtAvFramePool* Pool;
    int Count;
    bool AllEqual;
    const uint8_t* Expected;
    int Bytes;
} Received;

static bool Deliver(void* Context, DtAvFrame* Frame)
{
    Received* R = (Received*)Context;
    R->Count++;
    R->AllEqual = R->AllEqual && Frame->Frame.NumValidBytes == HEIGHT * ROW &&
                  memcmp(Frame->Frame.Data, R->Expected, (size_t)(HEIGHT * ROW)) == 0;
    DtAvFramePool_Return(R->Pool, &Frame->Frame);
    return true;
}

typedef struct ParseContext
{
    DtSt2110VideoRx* Rx;
    int Bytes;
} ParseContext;

static void ParsePacket(void* Context, const uint8_t* Packet, int Size)
{
    ParseContext* P = (ParseContext*)Context;
    P->Bytes += Size;
    DtSt2110VideoRx_Parse(P->Rx, Packet, Size);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(OpenBufferClose)
{
    int Live = DtAlloc_NumLive();
    DtDrvObject Nw;
    OsDrv* Drv = OpenDevice(&Nw);
    DT_ASSERT(Drv != NULL && Nw.Uuid != 0);
    DtAvPipe Pipe;

    DT_ASSERT_OK(DtAvPipe_Open(&Pipe, Drv, Nw, DT_PIPE_RX_RT_HWP, -1));
    DT_ASSERT(DtAvPipe_IsHardware(&Pipe));
    DT_ASSERT_EQ(DtAvPipe_Alignment(&Pipe), SIM_DTA2110_PACKET_ALIGNMENT);
    DT_ASSERT_OK(DtAvPipe_SetBuffer(&Pipe, 100000));
    DT_ASSERT_EQ(Pipe.BufferSize % (4096 * SIM_NW_HWP_PREFETCH_PAGES), 0);
    DT_ASSERT(Pipe.BufferSize >= 100000);
    DT_ASSERT_EQ(DtAvPipe_UsableBytes(&Pipe), Pipe.BufferSize - 8);
    SimNwPipeState State;
    SimDtPcie_GetNwPipeState(Pipe.Object.Uuid >> 20, &State);
    DT_ASSERT(State.InUse && State.BufferRegistered &&
              State.BufferSize == Pipe.BufferSize);
    DT_ASSERT_EQ(DtAvPipe_SetBuffer(&Pipe, 100000), DTAPI_E_INVALID_ARG);
    int Id = Pipe.Object.Uuid >> 20;
    DtAvPipe_Close(&Pipe);
    SimDtPcie_GetNwPipeState(Id, &State);
    DT_ASSERT(!State.InUse && !State.BufferRegistered);
    DtAvPipe_Close(&Pipe);

    // Three hardware receive pipes; a fourth is refused unless a software one stands in.
    DtAvPipe Hw[3];
    for (int i = 0; i < 3; i++)
        DT_ASSERT_OK(DtAvPipe_Open(&Hw[i], Drv, Nw, DT_PIPE_RX_RT_HWP, -1));
    DT_ASSERT_EQ(DtAvPipe_Open(&Pipe, Drv, Nw, DT_PIPE_RX_RT_HWP, -1), DTAPI_E_IN_USE);
    DT_ASSERT_EQ(Pipe.Object.Uuid, 0);
    DT_ASSERT_OK(DtAvPipe_Open(&Pipe, Drv, Nw, DT_PIPE_RX_RT_HWP, DT_PIPE_RX_RT_SWP));
    DT_ASSERT(!DtAvPipe_IsHardware(&Pipe));
    DtAvPipe_Close(&Pipe);
    for (int i = 0; i < 3; i++)
        DtAvPipe_Close(&Hw[i]);

    OsDrv_Close(Drv);
    DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);
    SimDtPcie_Reset();
    DT_ASSERT_EQ(DtAlloc_NumLive(), Live);
}

// Eight frames of 320x240 8-bit video through buffers a frame and a half long; the
// parser learns the size from the first, so seven arrive.
DT_TEST(FramesAroundTheBuffers)
{
    int Live = DtAlloc_NumLive();
    DtDrvObject Nw;
    OsDrv* Drv = OpenDevice(&Nw);
    DT_ASSERT(Drv != NULL && Nw.Uuid != 0);
    static DtAvWriter Writer;
    static DtAvReader Reader;
    DtAvPipe Tx;
    DtAvPipe Rx;
    DT_ASSERT_OK(DtAvPipe_Open(&Tx, Drv, Nw, DT_PIPE_TX_RT_HWP, -1));
    DT_ASSERT_OK(DtAvPipe_Open(&Rx, Drv, Nw, DT_PIPE_RX_RT_HWP, -1));

    // The stream.
    DtAvTxStream S;
    memset(&S, 0, sizeof(S));
    S.Net.Alignment = DtAvPipe_Alignment(&Tx);
    S.Net.IsVersion2 = DtAvPipe_IsJumbo(&Tx);
    static const uint8_t SrcMac[6] = SIM_DTA2110_MAC_ADDRESS;
    static const uint8_t DstMac[6] = {0x01, 0x00, 0x5E, 0x01, 0x02, 0x03};
    static const uint8_t SrcIp[4] = {192, 168, 1, 10};
    static const uint8_t DstIp[4] = {239, 1, 2, 3};
    memcpy(S.Net.SrcMac, SrcMac, 6);
    memcpy(S.Net.DstMac, DstMac, 6);
    memcpy(S.Net.SrcIp, SrcIp, 4);
    memcpy(S.Net.DstIp, DstIp, 4);
    S.Net.TimeToLive = 32;
    S.Net.SrcPort = 5000;
    S.Net.DstPort = 5004;
    S.PayloadType = 96;
    const St2110_TxConfigVideo Config = {
        St2110_TxFrameFormat_Uyvy422_8b,
        {0, St2110_PackingMode_General, -1},
        {WIDTH, HEIGHT},
        {{25, 1}, St2110_Scheduling_Linear, St2110_VideoScanning_Progressive}};
    DtSt2110VideoTx Video;
    DT_ASSERT_OK(DtSt2110VideoTx_Configure(&Video, &Config, DtAvPixConv_C()));
    DT_ASSERT_OK(DtSt2110VideoTx_Start(&Video, &S));
    int FrameNumBytes = DtSt2110VideoTx_FrameBytes(&Video, &S);

    // Buffers of a frame and a half.
    DT_ASSERT_OK(DtAvPipe_SetBuffer(&Tx, (size_t)FrameNumBytes * 3 / 2));
    DT_ASSERT_OK(DtAvPipe_SetBuffer(&Rx, (size_t)FrameNumBytes * 3 / 2));
    DT_ASSERT((uint32_t)FrameNumBytes < DtAvPipe_UsableBytes(&Tx));
    DtIpFilter Filter;
    memset(&Filter, 0, sizeof(Filter));
    memcpy(Filter.DstIp, DstIp, 4);
    Filter.DstPort[0] = 5004;
    Filter.Flags = DT_PIPE_IPFLT_FLAG_EN_FILT | DT_PIPE_IPFLT_FLAG_EN_DSTIP_IPV4 |
                   DT_PIPE_IPFLT_FLAG_EN_DSTPORT0;
    DT_ASSERT_OK(DtPcieCmd_PipeSetIpFilter(Drv, Rx.Object, &Filter));
    DT_ASSERT_OK(DtPcieCmd_PipeFlush(Drv, Tx.Object));
    DT_ASSERT_OK(DtPcieCmd_PipeFlush(Drv, Rx.Object));
    DT_ASSERT_OK(DtPcieCmd_PipeSetOpMode(Drv, Rx.Object, DT_PIPE_OPMODE_RUN));
    DT_ASSERT_OK(DtPcieCmd_PipeSetOpMode(Drv, Tx.Object, DT_PIPE_OPMODE_RUN));
    DtAvWriter_Init(&Writer, &Tx);
    DtAvReader_Init(&Reader, &Rx);

    DtAvFramePool Pool;
    DT_ASSERT_OK(DtAvFramePool_Init(&Pool));
    uint8_t* Image = (uint8_t*)malloc(HEIGHT * ROW);
    DT_ASSERT(Image != NULL);
    for (int i = 0; i < HEIGHT * ROW; i++)
        Image[i] = (uint8_t)(i * 7 + i / ROW);
    Received Got = {&Pool, 0, true, Image, 0};
    const DtAvRxSink Target = {&Pool, Deliver, &Got};
    DtSt2110VideoRx Parser;
    DtSt2110VideoRx_Init(&Parser, St2110_RxFrameFormat_Uyvy422_8b, DtAvPixConv_C(),
                         &Target);
    ParseContext Context = {&Parser, 0};

    uint64_t Written = 0;
    for (int f = 0; f < 8; f++)
    {
        DtAvFrame* Frame = DtAvFramePool_Get(&Pool, HEIGHT * ROW);
        DT_ASSERT(Frame != NULL);
        memcpy(Frame->Frame.Data, Image, HEIGHT * ROW);
        Frame->Frame.NumValidBytes = HEIGHT * ROW;
        Frame->Frame.RtpTime = (uint32_t)f * 3600;
        Frame->Frame.ToD = DtAvTime_FromNs(T0 + 100 * MS + (uint64_t)f * 40 * MS);

        uint32_t Free = 0;
        DT_ASSERT_OK(DtAvWriter_FreeBytes(&Writer, &Free));
        DT_ASSERT(Free >= (uint32_t)FrameNumBytes);
        uint32_t Before = Tx.Offset;
        DT_ASSERT_OK(DtSt2110VideoTx_Packetize(&Video, &S, &Frame->Frame, &Writer.Sink));
        DT_ASSERT_OK(DtAvWriter_Flush(&Writer));
        Written += (Tx.Offset + Tx.BufferSize - Before) % Tx.BufferSize;
        DtAvFramePool_Return(&Pool, &Frame->Frame);

        // The frame goes out and comes back, a pass of the reader at a time.
        for (int Step = 0; Step < 4; Step++)
        {
            SimDtPcie_AdvanceNwTime(10 * MS);
            int Packets = 0;
            bool Lost = false;
            DT_ASSERT_OK(
                DtAvReader_Pass(&Reader, ParsePacket, &Context, &Packets, &Lost));
            DT_ASSERT(!Lost);
        }
    }
    for (int Step = 0; Step < 40; Step++)
    {
        SimDtPcie_AdvanceNwTime(10 * MS);
        int Packets = 0;
        bool Lost = false;
        DT_ASSERT_OK(DtAvReader_Pass(&Reader, ParsePacket, &Context, &Packets, &Lost));
        DT_ASSERT(!Lost);
    }
    DT_ASSERT(Written > 3 * (uint64_t)Tx.BufferSize);
    DT_ASSERT(Context.Bytes > 3 * (int)Rx.BufferSize);
    DT_ASSERT_EQ(Got.Count, 7);
    DT_ASSERT(Got.AllEqual);
    DT_ASSERT_EQ(Parser.Stats.IpPacketErrors + Parser.Stats.FramesIncomplete, 0);

    // Off by one alignment word, the reader loses the boundaries and skips to the pipe's
    // offset.
    DtAvFrame* Frame = DtAvFramePool_Get(&Pool, HEIGHT * ROW);
    DT_ASSERT(Frame != NULL);
    memcpy(Frame->Frame.Data, Image, HEIGHT * ROW);
    Frame->Frame.NumValidBytes = HEIGHT * ROW;
    Frame->Frame.ToD = DtAvTime_FromNs(SimDtPcie_Now() + 50 * MS);
    DT_ASSERT_OK(DtSt2110VideoTx_Packetize(&Video, &S, &Frame->Frame, &Writer.Sink));
    DT_ASSERT_OK(DtAvWriter_Flush(&Writer));
    DtAvFramePool_Return(&Pool, &Frame->Frame);
    SimDtPcie_AdvanceNwTime(100 * MS);
    Rx.Offset = (Rx.Offset + 8) % Rx.BufferSize;
    int Packets = 0;
    bool Lost = false;
    DT_ASSERT_OK(DtAvReader_Pass(&Reader, ParsePacket, &Context, &Packets, &Lost));
    DT_ASSERT(Lost);
    DT_ASSERT_EQ(Packets, 0);
    SimNwPipeState State;
    SimDtPcie_GetNwPipeState(Rx.Object.Uuid >> 20, &State);
    DT_ASSERT_EQ(State.ReadOffset, State.WriteOffset);
    DT_ASSERT_EQ(Rx.Offset, State.WriteOffset);

    DtSt2110VideoRx_Reset(&Parser);
    DtAvPipe_Close(&Tx);
    DtAvPipe_Close(&Rx);
    free(Image);
    DtAvFramePool_Destroy(&Pool);
    OsDrv_Close(Drv);
    DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);
    SimDtPcie_Reset();
    DT_ASSERT_EQ(DtAlloc_NumLive(), Live);
}

DT_TEST_MAIN("SimAvPipe", DT_RUN(OpenBufferClose), DT_RUN(FramesAroundTheBuffers))
