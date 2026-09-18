// #*#*#*#*#*#*#*#*#*#*#*#*#* TestSt2110Audio.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Unit tests for ST 2110-30 audio packetizing and parsing
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The packets are read back here from their bytes: the RTP header per RFC 3550 and the
// samples after it, and the time of day from the packet header.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "AvFifo/DtAvTime.h"      // Nanoseconds.
#include "AvFifo/DtSt2110Audio.h" // Functions under test.
#include "Core/DtAlloc.h"         // Live allocations and failures.
#include "DtTest.h"               // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define MAX_PACKETS 256
#define SINK_BYTES (MAX_PACKETS * 1600)
#define DELAY_NS 14800
#define TOD UINT64_C(1800000000000000000)

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

// A stream to 239.1.2.3 in 64-bit words.
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
    S.SequenceNumber = 0xFFFE;
    return S;
}

// A packet read from its bytes.
typedef struct Packet
{
    uint64_t TodNs;
    int PayloadType;
    uint16_t Seq;
    uint32_t RtpTime;
    uint32_t Ssrc;
    bool Marker;
    const uint8_t* Samples;
    int NumBytes;
} Packet;

static bool ReadPacket(const MemSink* Sink, int Index, Packet* P)
{
    const uint8_t* Bytes = Sink->Buf + Sink->Offsets[Index];
    uint64_t Tod = 0;
    for (int i = 0; i < 8; i++)
        Tod |= (uint64_t)Bytes[8 + i] << 8 * i;
    P->TodNs = (Tod >> 32) * DT_AV_NS_PER_SEC + (Tod & 0x3FFFFFFF);
    const uint8_t* Udp = Bytes + 18 + 14 + 20;
    int UdpLength = Udp[4] << 8 | Udp[5];
    const uint8_t* Rtp = Udp + 8;
    if (Rtp[0] != 0x80)
        return false;
    P->Marker = (Rtp[1] & 0x80) != 0;
    P->PayloadType = Rtp[1] & 0x7F;
    P->Seq = (uint16_t)(Rtp[2] << 8 | Rtp[3]);
    P->RtpTime =
        (uint32_t)Rtp[4] << 24 | (uint32_t)Rtp[5] << 16 | (uint32_t)Rtp[6] << 8 | Rtp[7];
    P->Ssrc = (uint32_t)Rtp[8] << 24 | (uint32_t)Rtp[9] << 16 | (uint32_t)Rtp[10] << 8 |
              Rtp[11];
    P->Samples = Rtp + 12;
    P->NumBytes = UdpLength - 20;
    return true;
}

// Fills a frame of the pool with NumBytes counting bytes from Seed.
static DtAvFrame* MakeFrame(DtAvFramePool* Pool, int NumBytes, uint8_t Seed, uint32_t Rtp,
                            uint64_t TodNs)
{
    DtAvFrame* Frame = DtAvFramePool_Get(Pool, (size_t)NumBytes);
    if (Frame == NULL)
        return NULL;
    for (int i = 0; i < NumBytes; i++)
        Frame->Frame.Data[i] = (uint8_t)(Seed + i);
    Frame->Frame.NumValidBytes = NumBytes;
    Frame->Frame.RtpTime = Rtp;
    Frame->Frame.ToD = DtAvTime_FromNs(TodNs);
    return Frame;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(ConfigurationChecks)
{
    DtSt2110AudioTx Tx;
    St2110_TxConfigAudio Config = {St2110_AudioFormat_L24BE, 2, 125, 48000};

    DT_ASSERT_OK(DtSt2110AudioTx_Configure(&Tx, &Config));
    DT_ASSERT_EQ(Tx.BytesPerSample, 6);
    DT_ASSERT_EQ(Tx.PayloadSize, 750);

    Config.NumSamplesPerIpPacket = 240; // 1,440 bytes: the most
    DT_ASSERT_OK(DtSt2110AudioTx_Configure(&Tx, &Config));
    Config.NumSamplesPerIpPacket = 241;
    DT_ASSERT_EQ(DtSt2110AudioTx_Configure(&Tx, &Config), DTAPI_E_INVALID_ARG);
    Config.NumSamplesPerIpPacket = 0;
    DT_ASSERT_EQ(DtSt2110AudioTx_Configure(&Tx, &Config), DTAPI_E_INVALID_ARG);
    Config.NumSamplesPerIpPacket = 48;
    Config.NumChannels = 0;
    DT_ASSERT_EQ(DtSt2110AudioTx_Configure(&Tx, &Config), DTAPI_E_INVALID_ARG);
    Config.NumChannels = 8;
    Config.SampleRate = 0;
    DT_ASSERT_EQ(DtSt2110AudioTx_Configure(&Tx, &Config), DTAPI_E_INVALID_ARG);
    Config.SampleRate = 48000;
    Config.Format = (St2110_AudioFormat)7;
    DT_ASSERT_EQ(DtSt2110AudioTx_Configure(&Tx, &Config), DTAPI_E_INVALID_ARG);
}

// Frames of 1,000 and 700 stereo L24 samples in packets of 125: whole packets go out,
// the rest waits, and every packet has its first sample's timestamp and time.
DT_TEST(SamplesAcrossFrames)
{
    static MemSink Sink;
    int Live = DtAlloc_Live();
    DtAvFramePool Pool;
    DtSt2110AudioTx Tx;
    const St2110_TxConfigAudio Config = {St2110_AudioFormat_L24BE, 2, 125, 48000};
    DtAvTxStream S = Stream();
    const DtAvSink Out = {SinkBegin, SinkCommit, &Sink};
    memset(&Sink, 0, sizeof(Sink));
    DT_ASSERT_OK(DtAvFramePool_Init(&Pool));
    DT_ASSERT_OK(DtSt2110AudioTx_Configure(&Tx, &Config));

    // The samples of all frames in order, to compare with the packets.
    uint8_t Sent[6 * 4000];
    int NumSent = 0;
    static const int Samples[] = {1000, 700, 700, 50, 1550};
    uint64_t FirstTod = TOD;
    uint32_t FirstRtp = 4000000000u;
    uint64_t Sample = 0;
    int ExpectedPackets = 0;
    for (int f = 0; f < 5; f++)
    {
        uint64_t TodNs =
            FirstTod + DtAvTime_MulAddDiv(Sample, DT_AV_NS_PER_SEC, 0, 48000);
        DtAvFrame* Frame = MakeFrame(&Pool, Samples[f] * 6, (uint8_t)(f * 50),
                                     FirstRtp + (uint32_t)Sample, TodNs);
        DT_ASSERT(Frame != NULL);
        memcpy(Sent + NumSent, Frame->Frame.Data, (size_t)Samples[f] * 6);
        NumSent += Samples[f] * 6;
        int Bytes = DtSt2110AudioTx_PacketBytes(&Tx, &S, &Frame->Frame);
        int Before = Sink.Count;
        DT_ASSERT_OK(DtSt2110AudioTx_Packetize(&Tx, &S, &Frame->Frame, &Out));
        int Made = Sink.Count - Before;
        DT_ASSERT_EQ(Bytes, Made * (18 + 42 + 12 + 750 + 2));
        ExpectedPackets = NumSent / 750;
        DT_ASSERT_EQ(Sink.Count, ExpectedPackets);
        DT_ASSERT(DtAvFramePool_Return(&Pool, &Frame->Frame));
        Sample += (uint64_t)Samples[f];
    }
    DT_ASSERT(!Sink.Overrun);
    DT_ASSERT_EQ(Tx.LeftOver, NumSent % 750);

    for (int i = 0; i < Sink.Count; i++)
    {
        Packet P;
        DT_ASSERT(ReadPacket(&Sink, i, &P));
        DT_ASSERT_EQ(P.NumBytes, 750);
        DT_ASSERT_MEM(P.Samples, Sent + i * 750, 750);
        DT_ASSERT_EQ(P.Seq, (uint16_t)(0xFFFE + i));
        DT_ASSERT_EQ(P.RtpTime, FirstRtp + 125u * (uint32_t)i);
        DT_ASSERT_EQ(P.PayloadType, 97);
        DT_ASSERT_EQ(P.Ssrc, 0x11223344u);
        DT_ASSERT(!P.Marker);

        // The time of the packet's first sample, less the delay, within the nanosecond
        // the frames' times were rounded to.
        uint64_t Expected =
            FirstTod +
            DtAvTime_MulAddDiv(125u * (uint64_t)i, DT_AV_NS_PER_SEC, 0, 48000) - DELAY_NS;
        DT_ASSERT(P.TodNs + 3 >= Expected && P.TodNs <= Expected + 1);
    }
    DT_ASSERT_EQ(S.SequenceNumber, 0xFFFEu + (uint32_t)Sink.Count);

    // A reset drops the waiting samples.
    DtSt2110AudioTx_Reset(&Tx);
    DT_ASSERT_EQ(Tx.LeftOver, 0);
    DtAvFramePool_Destroy(&Pool);
    DT_ASSERT_EQ(DtAlloc_Live(), Live);
}

DT_TEST(InvalidAndRawFrames)
{
    static MemSink Sink;
    int Live = DtAlloc_Live();
    DtAvFramePool Pool;
    DtSt2110AudioTx Tx;
    St2110_TxConfigAudio Config = {St2110_AudioFormat_L16BE, 2, 48, 48000};
    DtAvTxStream S = Stream();
    const DtAvSink Out = {SinkBegin, SinkCommit, &Sink};
    memset(&Sink, 0, sizeof(Sink));
    DT_ASSERT_OK(DtAvFramePool_Init(&Pool));
    DT_ASSERT_OK(DtSt2110AudioTx_Configure(&Tx, &Config));

    // Not a whole number of samples.
    DtAvFrame* Frame = MakeFrame(&Pool, 1001, 0, 0, TOD);
    DT_ASSERT(Frame != NULL);
    DT_ASSERT_EQ(DtSt2110AudioTx_PacketBytes(&Tx, &S, &Frame->Frame), -1);
    DT_ASSERT_EQ(DtSt2110AudioTx_Packetize(&Tx, &S, &Frame->Frame, &Out),
                 DTAPI_E_INVALID_FORMAT);
    DT_ASSERT_EQ(Sink.Count, 0);

    // Raw: one packet of the frame's bytes at its own time and timestamp.
    Config.Format = St2110_AudioFormat_Raw;
    DT_ASSERT_OK(DtSt2110AudioTx_Configure(&Tx, &Config));
    Frame->Frame.RtpTime = 777;
    DT_ASSERT_EQ(DtSt2110AudioTx_PacketBytes(&Tx, &S, &Frame->Frame),
                 (18 + 42 + 12 + 1001 + 7) / 8 * 8);
    DT_ASSERT_OK(DtSt2110AudioTx_Packetize(&Tx, &S, &Frame->Frame, &Out));
    DT_ASSERT_EQ(Sink.Count, 1);
    Packet P;
    DT_ASSERT(ReadPacket(&Sink, 0, &P));
    DT_ASSERT_EQ(P.NumBytes, 1001);
    DT_ASSERT_EQ(P.RtpTime, 777);
    DT_ASSERT_EQ(P.TodNs, TOD - DELAY_NS);
    DT_ASSERT_MEM(P.Samples, Frame->Frame.Data, 1001);
    DT_ASSERT(DtAvFramePool_Return(&Pool, &Frame->Frame));

    Frame = MakeFrame(&Pool, 1441, 0, 0, TOD);
    DT_ASSERT(Frame != NULL);
    DT_ASSERT_EQ(DtSt2110AudioTx_Packetize(&Tx, &S, &Frame->Frame, &Out),
                 DTAPI_E_INVALID_FORMAT);
    DT_ASSERT_EQ(Sink.Count, 1);

    DtAvFramePool_Destroy(&Pool);
    DT_ASSERT_EQ(DtAlloc_Live(), Live);
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

// The transmitted packets parse back into frames of their samples.
DT_TEST(ReceivedPacketsAreFrames)
{
    static MemSink Sink;
    static Collected Got;
    int Live = DtAlloc_Live();
    DtAvFramePool TxPool;
    DtAvFramePool RxPool;
    DtSt2110AudioTx Tx;
    DtSt2110AudioRx Rx;
    const St2110_TxConfigAudio Config = {St2110_AudioFormat_L16BE, 8, 48, 48000};
    const St2110_RxConfigAudio RxConfig = {St2110_AudioFormat_L16BE, 48000};
    DtAvTxStream S = Stream();
    const DtAvSink Out = {SinkBegin, SinkCommit, &Sink};
    memset(&Sink, 0, sizeof(Sink));
    memset(&Got, 0, sizeof(Got));
    DT_ASSERT_OK(DtAvFramePool_Init(&TxPool));
    DT_ASSERT_OK(DtAvFramePool_Init(&RxPool));
    DT_ASSERT_OK(DtSt2110AudioTx_Configure(&Tx, &Config));
    const DtAvRxTarget Target = {&RxPool, Collect, &Got};
    DtSt2110AudioRx_Init(&Rx, &RxConfig, &Target);

    DtAvFrame* Frame = MakeFrame(&TxPool, 48 * 16 * 3, 9, 1000, TOD);
    DT_ASSERT(Frame != NULL);
    DT_ASSERT_OK(DtSt2110AudioTx_Packetize(&Tx, &S, &Frame->Frame, &Out));
    DT_ASSERT_EQ(Sink.Count, 3);
    for (int i = 0; i < Sink.Count; i++)
        DtSt2110AudioRx_Parse(&Rx, Sink.Buf + Sink.Offsets[i], Sink.Sizes[i]);
    DT_ASSERT_EQ(Got.Count, 3);
    DT_ASSERT_EQ(Rx.Stats.FramesOk, 3);
    for (int i = 0; i < 3; i++)
    {
        AvFifo_Frame* F = &Got.Frames[i]->Frame;
        DT_ASSERT_EQ(F->NumValidBytes, 768);
        DT_ASSERT_EQ(F->RtpTime, 1000u + 48u * (uint32_t)i);
        DT_ASSERT_MEM(F->Data, Frame->Frame.Data + 768 * i, 768);
        DT_ASSERT_EQ(DtAvTime_ToNs(&F->ToD), TOD - DELAY_NS + (uint64_t)i * 1000000);
        DT_ASSERT(DtAvFramePool_Return(&RxPool, F));
    }

    // A refused frame is dropped and back in the pool; a corrupt packet is an error.
    Got.Refuse = true;
    DtSt2110AudioRx_Parse(&Rx, Sink.Buf, Sink.Sizes[0]);
    DT_ASSERT_EQ(Rx.Stats.FramesOk, 4);
    DT_ASSERT_EQ(Rx.Stats.DroppedFrames, 1);
    DT_ASSERT_EQ(DtAvFramePool_NumFree(&RxPool), DtAvFramePool_NumFrames(&RxPool));
    Sink.Buf[0] ^= 0xFF;
    DtSt2110AudioRx_Parse(&Rx, Sink.Buf, Sink.Sizes[0]);
    DT_ASSERT_EQ(Rx.Stats.IpPacketErrors, 1);
    Sink.Buf[0] ^= 0xFF;
    Got.Refuse = false;

    // No memory for a frame.
    DtAvFramePool Empty;
    DT_ASSERT_OK(DtAvFramePool_Init(&Empty));
    const DtAvRxTarget NoMemory = {&Empty, Collect, &Got};
    DtSt2110AudioRx_Init(&Rx, &RxConfig, &NoMemory);
    DtAlloc_FailAfter(0);
    DtSt2110AudioRx_Parse(&Rx, Sink.Buf + Sink.Offsets[1], Sink.Sizes[1]);
    DtAlloc_FailAfter(-1);
    DT_ASSERT_EQ(Rx.Stats.DroppedFrames, 1);
    DT_ASSERT_EQ(Rx.Stats.FramesOk, 0);
    DT_ASSERT_EQ(DtAvFramePool_NumFrames(&Empty), 0);
    DtAvFramePool_Destroy(&Empty);

    DtAvFramePool_Destroy(&TxPool);
    DtAvFramePool_Destroy(&RxPool);
    DT_ASSERT_EQ(DtAlloc_Live(), Live);
}

DT_TEST_MAIN("St2110Audio", DT_RUN(ConfigurationChecks), DT_RUN(SamplesAcrossFrames),
             DT_RUN(InvalidAndRawFrames), DT_RUN(ReceivedPacketsAreFrames))
