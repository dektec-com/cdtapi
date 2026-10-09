// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestSt2110Raw.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Unit tests for raw RTP: a packet for every frame, and a frame for every packet
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The packets are read back here from their bytes: the RTP header per RFC 3550 and the
// payload after it, and the time of day from the packet header.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "AvFifo/DtAvTime.h"    // Nanoseconds.
#include "AvFifo/DtSt2110Raw.h" // Functions under test.
#include "Core/DtAlloc.h"       // Live allocations and failures.
#include "DtTest.h"             // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define MAX_PACKETS 64
#define SINK_BYTES (MAX_PACKETS * 1600)
#define DELAY_NS 14800
#define TOD UINT64_C(1800000000000000000)

// The headers in front of the RTP packet in the sink: the DtEthIp header, Ethernet, IPv4
// and UDP.
#define HEADERS (18 + 14 + 20 + 8)

// Packets kept in memory.
typedef struct MemSink
{
    uint8_t Buf[SINK_BYTES];
    size_t Used;
    int LastMax;
    int Count;
    size_t Offsets[MAX_PACKETS];
    int Sizes[MAX_PACKETS];
    bool Overrun;
} MemSink;

static uint8_t* SinkBegin(void* Context, int MaxSize)
{
    MemSink* Sink = (MemSink*)Context;
    if (Sink->Used + (size_t)MaxSize > SINK_BYTES || Sink->Count == MAX_PACKETS)
    {
        Sink->Overrun = true;
        Sink->Used = 0;
        Sink->Count = 0;
    }
    Sink->LastMax = MaxSize;
    memset(Sink->Buf + Sink->Used, 0xCC, (size_t)MaxSize);
    return Sink->Buf + Sink->Used;
}

static void SinkCommit(void* Context, int Size)
{
    MemSink* Sink = (MemSink*)Context;
    if (Size > Sink->LastMax)
        Sink->Overrun = true;
    Sink->Offsets[Sink->Count] = Sink->Used;
    Sink->Sizes[Sink->Count] = Size;
    Sink->Count++;
    Sink->Used += (size_t)Size;
}

// A stream in 64-bit words; the addresses are left zero, as packetizing does not read
// them.
static DtAvTxStream Stream(void)
{
    DtAvTxStream S;
    memset(&S, 0, sizeof(S));
    S.Net.Alignment = 8;
    S.Net.TimeToLive = 32;
    S.Net.SrcPort = 5000;
    S.Net.DstPort = 5004;
    S.PayloadType = 97;
    S.Ssrc = 0x11223344;
    S.OutputDelayNs = DELAY_NS;
    S.NextSequenceNumber = 0xFFFE;
    return S;
}

// The RTP packet of packet Index in the sink, its size and its time of day.
static const uint8_t* RtpOf(const MemSink* Sink, int Index, int* Size, uint64_t* TodNs)
{
    const uint8_t* Bytes = Sink->Buf + Sink->Offsets[Index];
    uint64_t Tod = 0;
    for (int i = 0; i < 8; i++)
        Tod |= (uint64_t)Bytes[8 + i] << 8 * i;
    *TodNs = (Tod >> 32) * DT_AV_NS_PER_SEC + (Tod & 0x3FFFFFFF);
    const uint8_t* Udp = Bytes + HEADERS - 8;
    *Size = (Udp[4] << 8 | Udp[5]) - 8;
    return Udp + 8;
}

// A frame of the pool of NumBytes counting bytes from Seed.
static DtAvFrame* MakeFrame(DtAvFramePool* Pool, int NumBytes, uint8_t Seed)
{
    DtAvFrame* Frame = DtAvFramePool_Get(Pool, (size_t)NumBytes);
    if (Frame == NULL)
        return NULL;
    for (int i = 0; i < NumBytes; i++)
        Frame->Frame.Data[i] = (uint8_t)(Seed + i);
    Frame->Frame.NumValidBytes = NumBytes;
    Frame->Frame.ToD = DtAvTime_FromNs(TOD);
    return Frame;
}

// Writes into Bytes an RTP packet of the program's own: version 2, payload type 33,
// sequence number Seq, timestamp 0x01020304, marker set, NumCsrc CSRCs, an extension of
// ExtWords words when Ext, PayloadBytes counting bytes from 0x40, and Padding bytes of
// padding. Returns its size.
static int OwnPacket(uint8_t* Bytes, uint16_t Seq, int NumCsrc, bool Ext, int ExtWords,
                     int PayloadBytes, int Padding)
{
    int n = 0;
    Bytes[n++] = (uint8_t)(0x80 | (Padding > 0 ? 0x20 : 0) | (Ext ? 0x10 : 0) | NumCsrc);
    Bytes[n++] = 0x80 | 33;
    Bytes[n++] = (uint8_t)(Seq >> 8);
    Bytes[n++] = (uint8_t)Seq;
    for (int i = 1; i <= 4; i++)
        Bytes[n++] = (uint8_t)i;
    for (int i = 0; i < 4; i++)
        Bytes[n++] = 0xA0;
    for (int i = 0; i < 4 * NumCsrc; i++)
        Bytes[n++] = 0xC0;
    if (Ext)
    {
        Bytes[n++] = 0xBE;
        Bytes[n++] = 0xDE;
        Bytes[n++] = (uint8_t)(ExtWords >> 8);
        Bytes[n++] = (uint8_t)ExtWords;
        for (int i = 0; i < 4 * ExtWords; i++)
            Bytes[n++] = 0xE0;
    }
    for (int i = 0; i < PayloadBytes; i++)
        Bytes[n++] = (uint8_t)(0x40 + i);
    for (int i = 0; i < Padding; i++)
        Bytes[n++] = i == Padding - 1 ? (uint8_t)Padding : 0;
    return n;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(ConfigurationChecks)
{
    DtSt2110RawTx Tx;
    St2110_TxConfigRaw Config = {false, 0};
    DT_ASSERT_EQ(DtSt2110RawTx_Configure(&Tx, &Config), DTAPI_E_INVALID_ARG);
    Config.MaxRate = -1;
    DT_ASSERT_EQ(DtSt2110RawTx_Configure(&Tx, &Config), DTAPI_E_INVALID_ARG);
    Config.MaxRate = 1;
    DT_ASSERT_OK(DtSt2110RawTx_Configure(&Tx, &Config));
}

// Without the program's header, a frame gets one: the stream's payload type, sequence
// number and SSRC, the frame's timestamp and marker bit.
DT_TEST(FramesGetAHeader)
{
    static MemSink Sink;
    int Live = DtAlloc_NumLive();
    DtAvFramePool Pool;
    DtSt2110RawTx Tx;
    const St2110_TxConfigRaw Config = {false, 1000000};
    DtAvTxStream S = Stream();
    const DtAvTxSink Out = {SinkBegin, SinkCommit, &Sink};
    memset(&Sink, 0, sizeof(Sink));
    DT_ASSERT_OK(DtAvFramePool_Init(&Pool));
    DT_ASSERT_OK(DtSt2110RawTx_Configure(&Tx, &Config));

    for (int f = 0; f < 3; f++)
    {
        DtAvFrame* Frame = MakeFrame(&Pool, 100 + f, (uint8_t)f);
        DT_ASSERT(Frame != NULL);
        Frame->Frame.RtpTime = 5000u + (uint32_t)f;
        Frame->Frame.Marker = f == 2;
        DT_ASSERT_EQ(DtSt2110RawTx_PacketBytes(&Tx, &S, &Frame->Frame),
                     (HEADERS + 12 + 100 + f + 7) / 8 * 8);
        DT_ASSERT_OK(DtSt2110RawTx_Packetize(&Tx, &S, &Frame->Frame, &Out));
        int Size = 0;
        uint64_t TodNs = 0;
        const uint8_t* Rtp = RtpOf(&Sink, f, &Size, &TodNs);
        DT_ASSERT_EQ(Size, 12 + 100 + f);
        DT_ASSERT_EQ(TodNs, TOD - DELAY_NS);
        DT_ASSERT_EQ(Rtp[0], 0x80);
        DT_ASSERT_EQ(Rtp[1], (f == 2 ? 0x80 : 0) | 97);
        DT_ASSERT_EQ(Rtp[2] << 8 | Rtp[3], (0xFFFE + f) & 0xFFFF);
        DT_ASSERT_EQ(DtAvPacket_Get16(Rtp + 6), 5000 + f);
        DT_ASSERT_EQ(DtAvPacket_Get16(Rtp + 8), 0x1122);
        DT_ASSERT_MEM(Rtp + 12, Frame->Frame.Data, (size_t)(100 + f));
        DT_ASSERT(DtAvFramePool_Return(&Pool, &Frame->Frame));
    }
    DT_ASSERT(!Sink.Overrun);

    // A UDP datagram holds at most 1440 bytes after the RTP header, or 8940 in a jumbo
    // frame.
    DtAvFrame* Frame = MakeFrame(&Pool, 1441, 0);
    DT_ASSERT(Frame != NULL);
    DT_ASSERT(!DtSt2110RawTx_FrameFits(&Tx, &S, &Frame->Frame));
    DT_ASSERT_EQ(DtSt2110RawTx_PacketBytes(&Tx, &S, &Frame->Frame), -1);
    DT_ASSERT_EQ(DtSt2110RawTx_Packetize(&Tx, &S, &Frame->Frame, &Out),
                 DTAPI_E_INVALID_FORMAT);
    DT_ASSERT_EQ(Sink.Count, 3);
    Frame->Frame.NumValidBytes = 1440;
    DT_ASSERT(DtSt2110RawTx_FrameFits(&Tx, &S, &Frame->Frame));
    DT_ASSERT(DtAvFramePool_Return(&Pool, &Frame->Frame));
    Frame = MakeFrame(&Pool, 8941, 0);
    DT_ASSERT(Frame != NULL);
    S.Net.IsVersion2 = true;
    DT_ASSERT(!DtSt2110RawTx_FrameFits(&Tx, &S, &Frame->Frame));
    Frame->Frame.NumValidBytes = 8940;
    DT_ASSERT(DtSt2110RawTx_FrameFits(&Tx, &S, &Frame->Frame));
    Frame->Frame.NumValidBytes = -1;
    DT_ASSERT(!DtSt2110RawTx_FrameFits(&Tx, &S, &Frame->Frame));
    DT_ASSERT(DtAvFramePool_Return(&Pool, &Frame->Frame));

    DtAvFramePool_Destroy(&Pool);
    DT_ASSERT_EQ(DtAlloc_NumLive(), Live);
}

// With the program's header, a frame goes out as it is, and the stream's sequence number
// stays where it was.
DT_TEST(FramesWithTheirOwnHeader)
{
    static MemSink Sink;
    int Live = DtAlloc_NumLive();
    DtAvFramePool Pool;
    DtSt2110RawTx Tx;
    const St2110_TxConfigRaw Config = {true, 1000000};
    DtAvTxStream S = Stream();
    const DtAvTxSink Out = {SinkBegin, SinkCommit, &Sink};
    memset(&Sink, 0, sizeof(Sink));
    DT_ASSERT_OK(DtAvFramePool_Init(&Pool));
    DT_ASSERT_OK(DtSt2110RawTx_Configure(&Tx, &Config));

    DtAvFrame* Frame = DtAvFramePool_Get(&Pool, 200);
    DT_ASSERT(Frame != NULL);
    Frame->Frame.NumValidBytes = OwnPacket(Frame->Frame.Data, 1234, 2, true, 1, 50, 4);
    Frame->Frame.ToD = DtAvTime_FromNs(TOD);
    DT_ASSERT_OK(DtSt2110RawTx_Packetize(&Tx, &S, &Frame->Frame, &Out));
    int Size = 0;
    uint64_t TodNs = 0;
    const uint8_t* Rtp = RtpOf(&Sink, 0, &Size, &TodNs);
    DT_ASSERT_EQ(Size, Frame->Frame.NumValidBytes);
    DT_ASSERT_MEM(Rtp, Frame->Frame.Data, (size_t)Size);
    DT_ASSERT_EQ(TodNs, TOD - DELAY_NS);
    DT_ASSERT_EQ(S.NextSequenceNumber, 0xFFFE);

    // At least a whole RTP header, and at most a UDP datagram.
    Frame->Frame.NumValidBytes = 11;
    DT_ASSERT_EQ(DtSt2110RawTx_PacketBytes(&Tx, &S, &Frame->Frame), -1);
    DT_ASSERT_EQ(DtSt2110RawTx_Packetize(&Tx, &S, &Frame->Frame, &Out),
                 DTAPI_E_INVALID_FORMAT);
    Frame->Frame.NumValidBytes = 12;
    DT_ASSERT(DtSt2110RawTx_FrameFits(&Tx, &S, &Frame->Frame));
    DT_ASSERT(DtAvFramePool_Return(&Pool, &Frame->Frame));
    Frame = MakeFrame(&Pool, 1453, 0);
    DT_ASSERT(Frame != NULL);
    DT_ASSERT(!DtSt2110RawTx_FrameFits(&Tx, &S, &Frame->Frame));
    Frame->Frame.NumValidBytes = 1452;
    DT_ASSERT(DtSt2110RawTx_FrameFits(&Tx, &S, &Frame->Frame));
    DT_ASSERT(DtAvFramePool_Return(&Pool, &Frame->Frame));
    DT_ASSERT_EQ(Sink.Count, 1);

    DtAvFramePool_Destroy(&Pool);
    DT_ASSERT_EQ(DtAlloc_NumLive(), Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reception +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct Collected
{
    DtAvFrame* Frames[MAX_PACKETS];
    int Count;
    bool Refuse;
} Collected;

static bool Collect(void* Context, DtAvFrame* Frame)
{
    Collected* C = (Collected*)Context;
    if (C->Refuse || C->Count == MAX_PACKETS)
        return false;
    C->Frames[C->Count++] = Frame;
    return true;
}

// Sends a packet of the program's own through Tx into Sink, and parses it with Rx.
static void SendAndParse(DtSt2110RawTx* Tx, DtAvTxStream* S, DtAvFramePool* Pool,
                         MemSink* Sink, DtSt2110RawRx* Rx, const uint8_t* Bytes, int Size)
{
    DtAvFrame* Frame = DtAvFramePool_Get(Pool, (size_t)Size);
    if (Frame == NULL)
        return;
    memcpy(Frame->Frame.Data, Bytes, (size_t)Size);
    Frame->Frame.NumValidBytes = Size;
    Frame->Frame.ToD = DtAvTime_FromNs(TOD);
    const DtAvTxSink Out = {SinkBegin, SinkCommit, Sink};
    if (DtSt2110RawTx_Packetize(Tx, S, &Frame->Frame, &Out) == DTAPI_OK)
    {
        const int Last = Sink->Count - 1;
        DtSt2110RawRx_Parse(Rx, Sink->Buf + Sink->Offsets[Last], Sink->Sizes[Last]);
    }
    DtAvFramePool_Return(Pool, &Frame->Frame);
}

// Every packet is a frame: whole, or its payload alone without the CSRCs, the extension
// and the padding; with the timestamp, the marker bit and the time of arrival.
DT_TEST(ReceivedPacketsAreFrames)
{
    static MemSink Sink;
    static Collected Got;
    int Live = DtAlloc_NumLive();
    DtAvFramePool TxPool;
    DtAvFramePool RxPool;
    DtSt2110RawTx Tx;
    DtSt2110RawRx Rx;
    const St2110_TxConfigRaw TxConfig = {true, 1000000};
    St2110_RxConfigRaw RxConfig = {true, 1000000};
    DtAvTxStream S = Stream();
    memset(&Sink, 0, sizeof(Sink));
    memset(&Got, 0, sizeof(Got));
    DT_ASSERT_OK(DtAvFramePool_Init(&TxPool));
    DT_ASSERT_OK(DtAvFramePool_Init(&RxPool));
    DT_ASSERT_OK(DtSt2110RawTx_Configure(&Tx, &TxConfig));
    const DtAvRxSink Target = {&RxPool, Collect, &Got};

    static uint8_t Bytes[256];
    const int Size = OwnPacket(Bytes, 7, 2, true, 1, 50, 4);
    DtSt2110RawRx_Init(&Rx, &RxConfig, &Target);
    SendAndParse(&Tx, &S, &TxPool, &Sink, &Rx, Bytes, Size);
    DT_ASSERT_EQ(Got.Count, 1);
    AvFifo_Frame* F = &Got.Frames[0]->Frame;
    DT_ASSERT_EQ(F->NumValidBytes, Size);
    DT_ASSERT_MEM(F->Data, Bytes, (size_t)Size);
    DT_ASSERT_EQ(F->RtpTime, 0x01020304u);
    DT_ASSERT(F->Marker);
    DT_ASSERT_EQ(DtAvTime_ToNs(&F->ToD), TOD - DELAY_NS);

    // The payload alone: after 12 bytes of header, 8 of CSRCs and 8 of extension, and
    // before 4 of padding.
    RxConfig.IncludeRtpHeader = false;
    DtSt2110RawRx_Init(&Rx, &RxConfig, &Target);
    SendAndParse(&Tx, &S, &TxPool, &Sink, &Rx, Bytes, Size);
    DT_ASSERT_EQ(Got.Count, 2);
    F = &Got.Frames[1]->Frame;
    DT_ASSERT_EQ(F->NumValidBytes, 50);
    DT_ASSERT_MEM(F->Data, Bytes + 28, 50);
    DT_ASSERT_EQ(F->RtpTime, 0x01020304u);
    DT_ASSERT(F->Marker);

    // A plain header, and a payload of none.
    const int Plain = OwnPacket(Bytes, 8, 0, false, 0, 0, 0);
    SendAndParse(&Tx, &S, &TxPool, &Sink, &Rx, Bytes, Plain);
    DT_ASSERT_EQ(Got.Count, 3);
    DT_ASSERT_EQ(Got.Frames[2]->Frame.NumValidBytes, 0);
    DT_ASSERT_EQ(Rx.Stats.FramesOk, 2);
    DT_ASSERT_EQ(Rx.Stats.IpPacketErrors, 0);

    // CSRCs, an extension or padding that do not fit are errors.
    const int TooManyCsrcs = OwnPacket(Bytes, 9, 15, false, 0, 4, 0);
    SendAndParse(&Tx, &S, &TxPool, &Sink, &Rx, Bytes, TooManyCsrcs - 40);
    const int LongExtension = OwnPacket(Bytes, 9, 0, true, 1, 0, 0);
    Bytes[15] = 2;
    SendAndParse(&Tx, &S, &TxPool, &Sink, &Rx, Bytes, LongExtension);
    const int MuchPadding = OwnPacket(Bytes, 9, 0, false, 0, 2, 3);
    Bytes[MuchPadding - 1] = 6;
    SendAndParse(&Tx, &S, &TxPool, &Sink, &Rx, Bytes, MuchPadding);
    DT_ASSERT_EQ(Rx.Stats.IpPacketErrors, 3);
    DT_ASSERT_EQ(Got.Count, 3);

    // Whole packets need no more than an RTP header.
    RxConfig.IncludeRtpHeader = true;
    DtSt2110RawRx_Init(&Rx, &RxConfig, &Target);
    SendAndParse(&Tx, &S, &TxPool, &Sink, &Rx, Bytes, MuchPadding);
    DT_ASSERT_EQ(Rx.Stats.IpPacketErrors, 0);
    DT_ASSERT_EQ(Got.Count, 4);

    for (int i = 0; i < Got.Count; i++)
        DT_ASSERT(DtAvFramePool_Return(&RxPool, &Got.Frames[i]->Frame));
    DtAvFramePool_Destroy(&TxPool);
    DtAvFramePool_Destroy(&RxPool);
    DT_ASSERT_EQ(DtAlloc_NumLive(), Live);
}

// A sequence number that does not follow the one before is a gap; 65535 to 0 is none. A
// refused frame is dropped, as is one the pool has no memory for.
DT_TEST(GapsAndDrops)
{
    static MemSink Sink;
    static Collected Got;
    int Live = DtAlloc_NumLive();
    DtAvFramePool TxPool;
    DtAvFramePool RxPool;
    DtSt2110RawTx Tx;
    DtSt2110RawRx Rx;
    const St2110_TxConfigRaw TxConfig = {true, 1000000};
    const St2110_RxConfigRaw RxConfig = {true, 1000000};
    DtAvTxStream S = Stream();
    memset(&Sink, 0, sizeof(Sink));
    memset(&Got, 0, sizeof(Got));
    DT_ASSERT_OK(DtAvFramePool_Init(&TxPool));
    DT_ASSERT_OK(DtAvFramePool_Init(&RxPool));
    DT_ASSERT_OK(DtSt2110RawTx_Configure(&Tx, &TxConfig));
    const DtAvRxSink Target = {&RxPool, Collect, &Got};
    DtSt2110RawRx_Init(&Rx, &RxConfig, &Target);

    static const uint16_t Seqs[] = {65534, 65535, 0, 1, 3, 4, 4};
    uint8_t Bytes[64];
    for (size_t i = 0; i < sizeof(Seqs) / sizeof(Seqs[0]); i++)
    {
        const int Size = OwnPacket(Bytes, Seqs[i], 0, false, 0, 8, 0);
        SendAndParse(&Tx, &S, &TxPool, &Sink, &Rx, Bytes, Size);
    }
    DT_ASSERT_EQ(Rx.Stats.Gaps, 2);
    DT_ASSERT_EQ(Rx.Stats.FramesOk, 7);
    DT_ASSERT_EQ(Got.Count, 7);
    for (int i = 0; i < Got.Count; i++)
        DT_ASSERT(DtAvFramePool_Return(&RxPool, &Got.Frames[i]->Frame));

    Got.Refuse = true;
    const int Size = OwnPacket(Bytes, 5, 0, false, 0, 8, 0);
    SendAndParse(&Tx, &S, &TxPool, &Sink, &Rx, Bytes, Size);
    DT_ASSERT_EQ(Rx.Stats.DroppedFrames, 1);
    DT_ASSERT_EQ(DtAvFramePool_NumFree(&RxPool), DtAvFramePool_NumFrames(&RxPool));
    Got.Refuse = false;

    DtAvFramePool Empty;
    DT_ASSERT_OK(DtAvFramePool_Init(&Empty));
    const DtAvRxSink NoMemory = {&Empty, Collect, &Got};
    DtSt2110RawRx_Init(&Rx, &RxConfig, &NoMemory);
    DtAlloc_FailAfter(0);
    DtSt2110RawRx_Parse(&Rx, Sink.Buf + Sink.Offsets[0], Sink.Sizes[0]);
    DtAlloc_FailAfter(-1);
    DT_ASSERT_EQ(Rx.Stats.DroppedFrames, 1);
    DT_ASSERT_EQ(Rx.Stats.FramesOk, 0);
    DtAvFramePool_Destroy(&Empty);

    DtAvFramePool_Destroy(&TxPool);
    DtAvFramePool_Destroy(&RxPool);
    DT_ASSERT_EQ(DtAlloc_NumLive(), Live);
}

DT_TEST_MAIN("St2110Raw", DT_RUN(ConfigurationChecks), DT_RUN(FramesGetAHeader),
             DT_RUN(FramesWithTheirOwnHeader), DT_RUN(ReceivedPacketsAreFrames),
             DT_RUN(GapsAndDrops))
