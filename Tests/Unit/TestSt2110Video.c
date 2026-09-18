// #*#*#*#*#*#*#*#*#*#*#*#*#* TestSt2110Video.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Unit tests for ST 2110-20 video packetizing and parsing
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Transmitted packets are checked with a reader of RFC 4175 written here: it puts every
// row segment at its row and pixel offset in an image, and checks the markers, sequence
// numbers and times. Reception is fed a stream made here as well, one row segment per
// packet, with the faults the parser must count.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "AvFifo/DtAvTime.h"      // Nanoseconds.
#include "AvFifo/DtSt2110Video.h" // Functions under test.
#include "Core/DtAlloc.h"         // Live allocations and failures.
#include "DtPcie/DtEthIp.h"       // Packet headers of the made stream.
#include "DtTest.h"               // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define DELAY_NS 14800
#define TOD UINT64_C(1800000000000000000)

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Memory sink -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

typedef struct MemSink
{
    uint8_t* Buf;
    size_t Capacity;
    size_t Used;
    int LastMax;
    int Count;
    int MaxPackets;
    size_t* Offsets;
    int* Sizes;
    bool Overrun;
} MemSink;

static bool Sink_Init(MemSink* Sink, size_t Capacity, int MaxPackets)
{
    memset(Sink, 0, sizeof(*Sink));
    Sink->Buf = (uint8_t*)malloc(Capacity);
    Sink->Offsets = (size_t*)malloc((size_t)MaxPackets * sizeof(size_t));
    Sink->Sizes = (int*)malloc((size_t)MaxPackets * sizeof(int));
    Sink->Capacity = Capacity;
    Sink->MaxPackets = MaxPackets;
    return Sink->Buf != NULL && Sink->Offsets != NULL && Sink->Sizes != NULL;
}

static void Sink_Free(MemSink* Sink)
{
    free(Sink->Buf);
    free(Sink->Offsets);
    free(Sink->Sizes);
}

static uint8_t* SinkBegin(void* Context, int MaxSize)
{
    MemSink* Sink = (MemSink*)Context;
    if (Sink->Used + (size_t)MaxSize > Sink->Capacity || Sink->Count == Sink->MaxPackets)
    {
        Sink->Overrun = true;
        Sink->Used = 0;
        Sink->Count = 0;
    }
    Sink->LastMax = MaxSize;
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

// A stream in 64-bit words, standard frames.
static DtAvTxStream Stream(void)
{
    DtAvTxStream S;
    memset(&S, 0, sizeof(S));
    S.Net.Alignment = 8;
    S.Net.TimeToLive = 32;
    S.Net.SrcPort = 5000;
    S.Net.DstPort = 5004;
    S.PayloadType = 96;
    S.Ssrc = 0xCAFEBABE;
    S.OutputDelayNs = DELAY_NS;
    S.SequenceNumber = 0x0001FFF0;
    return S;
}

// Bytes counting in a pattern that differs per row.
static void FillFrame(uint8_t* Data, int Rows, int RowSize, int Seed)
{
    for (int r = 0; r < Rows; r++)
    {
        for (int i = 0; i < RowSize; i++)
            Data[r * RowSize + i] = (uint8_t)(Seed + r * 7 + i);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- 10-bit samples -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

// Sample K of pixel group I, of 10 bits, most or least significant bit first.
static unsigned GetSample(const uint8_t* Buf, size_t I, int K, bool MsbFirst)
{
    unsigned Value = 0;
    for (int b = 0; b < 10; b++)
    {
        size_t Index = I * 40 + (size_t)(K * 10 + b);
        unsigned Shift = MsbFirst ? 7 - (unsigned)(Index % 8) : (unsigned)(Index % 8);
        unsigned Bit = (unsigned)(Buf[Index / 8] >> Shift) & 1;
        Value |= MsbFirst ? Bit << (9 - b) : Bit << b;
    }
    return Value;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Packet reader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// What the packets of one frame showed.
typedef struct Reading
{
    int Packets;
    int Markers;
    bool MarkerLast;
    bool SequenceOk;
    bool SegmentsOk;
    bool FieldsOk;
    uint32_t RtpTime;
    uint64_t FirstTodNs;
    uint64_t LastTodNs;
    int MaxRowsInPacket;
    int MinRowsInPacket;
} Reading;

// Reads packets [First, First + Count) of Sink into Image, rows of RowSize bytes of
// pixel groups at RowStep row numbers apart, expecting field bit Field.
static void ReadFrame(const MemSink* Sink, int First, int Count, uint8_t* Image,
                      int RowSize, int PgroupBytes, int PgroupPixels, int RowStep,
                      bool Field, uint32_t FirstSeq, Reading* R)
{
    memset(R, 0, sizeof(*R));
    R->SequenceOk = true;
    R->SegmentsOk = true;
    R->FieldsOk = true;
    R->MinRowsInPacket = 99;
    for (int p = First; p < First + Count; p++)
    {
        const uint8_t* Bytes = Sink->Buf + Sink->Offsets[p];
        uint64_t Tod = 0;
        for (int i = 0; i < 8; i++)
            Tod |= (uint64_t)Bytes[8 + i] << 8 * i;
        uint64_t TodNs = (Tod >> 32) * DT_AV_NS_PER_SEC + (Tod & 0x3FFFFFFF);
        const uint8_t* Udp = Bytes + 18 + 14 + 20;
        int UdpLength = Udp[4] << 8 | Udp[5];
        const uint8_t* Rtp = Udp + 8;
        bool Marker = (Rtp[1] & 0x80) != 0;
        uint32_t Seq = (uint32_t)(Rtp[12] << 24 | Rtp[13] << 16 | Rtp[2] << 8 | Rtp[3]);
        uint32_t RtpTime = (uint32_t)Rtp[4] << 24 | (uint32_t)Rtp[5] << 16 |
                           (uint32_t)Rtp[6] << 8 | Rtp[7];

        if (p == First)
        {
            R->RtpTime = RtpTime;
            R->FirstTodNs = TodNs;
        }
        R->SequenceOk = R->SequenceOk && Seq == FirstSeq + (uint32_t)(p - First) &&
                        RtpTime == R->RtpTime && (Rtp[1] & 0x7F) == 96;
        R->LastTodNs = TodNs;
        R->Packets++;
        if (Marker)
        {
            R->Markers++;
            R->MarkerLast = p == First + Count - 1;
        }

        int Rows = 0;
        const uint8_t* Srd = Rtp + 14;
        int DataBytes = 0;
        for (bool More = true; More; Rows++, Srd += 6)
        {
            DataBytes += Srd[0] << 8 | Srd[1];
            More = (Srd[4] & 0x80) != 0;
        }
        const uint8_t* Data = Srd;
        R->SegmentsOk = R->SegmentsOk && UdpLength == 8 + 12 + 2 + 6 * Rows + DataBytes;
        Srd = Rtp + 14;
        for (int h = 0; h < Rows; h++, Srd += 6)
        {
            int Length = Srd[0] << 8 | Srd[1];
            bool F = (Srd[2] & 0x80) != 0;
            int Row = (Srd[2] & 0x7F) << 8 | Srd[3];
            int Offset = (Srd[4] & 0x7F) << 8 | Srd[5];
            int Byte = Offset / PgroupPixels * PgroupBytes;
            R->FieldsOk = R->FieldsOk && F == Field && Row % RowStep == 0;
            R->SegmentsOk = R->SegmentsOk && Length % PgroupBytes == 0 &&
                            Offset % PgroupPixels == 0 && Byte + Length <= RowSize;
            if (R->SegmentsOk && Row % RowStep == 0)
                memcpy(Image + Row / RowStep * RowSize + Byte, Data, (size_t)Length);
            Data += Length;
        }
        R->MaxRowsInPacket = Rows > R->MaxRowsInPacket ? Rows : R->MaxRowsInPacket;
        R->MinRowsInPacket = Rows < R->MinRowsInPacket ? Rows : R->MinRowsInPacket;
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(ConfigurationChecks)
{
    DtSt2110VideoTx Tx;
    DtAvTxStream S = Stream();
    St2110_TxConfigVideo Config = {
        St2110_TxFrameFormat_Uyvy422_10b,
        {0, St2110_PackingMode_General, -1},
        {1920, 1080},
        {{50, 1}, St2110_Scheduling_Linear, St2110_VideoScanning_Progressive}};

    DT_ASSERT_OK(DtSt2110VideoTx_Configure(&Tx, &Config, DtAvPixConv_C()));
    DT_ASSERT_EQ(Tx.RowSize, 4800);
    DT_ASSERT_EQ(Tx.TrOffsetNs, 764444); // 43/1125 of 20 ms
    DT_ASSERT_OK(DtSt2110VideoTx_Start(&Tx, &S));
    DT_ASSERT_EQ(Tx.PayloadSize, 1420);
    DT_ASSERT_EQ(Tx.PacketsPerFrame, (1080 * 4800 + 1419) / 1420);
    DT_ASSERT_EQ(DtSt2110VideoTx_FrameSize(&Tx, 0), 4800 * 1080);

    // 720p59.94, 525i and 625i offsets and active parts.
    Config.Resolution = (VideoSize){1280, 720};
    Config.Timing.Rate = (FrameRate){60000, 1001};
    DT_ASSERT_OK(DtSt2110VideoTx_Configure(&Tx, &Config, DtAvPixConv_C()));
    DT_ASSERT_EQ(Tx.TrOffsetNs, 622844); // 28/750 of 16.683 ms
    Config.Resolution = (VideoSize){720, 486};
    Config.Timing.VideoScanning = St2110_VideoScanning_Interlaced;
    DT_ASSERT_OK(DtSt2110VideoTx_Configure(&Tx, &Config, DtAvPixConv_C()));
    DT_ASSERT_EQ(Tx.ActiveVideo.Numerator, 487);
    DT_ASSERT_EQ(Tx.Rate.Denominator, 2002);
    Config.Resolution = (VideoSize){720, 576};
    Config.Timing.Rate = (FrameRate){50, 1};
    DT_ASSERT_OK(DtSt2110VideoTx_Configure(&Tx, &Config, DtAvPixConv_C()));
    DT_ASSERT_EQ(Tx.ActiveVideo.Numerator, 576);
    DT_ASSERT_EQ(Tx.TrOffsetNs, 1664000); // 26/625 of 40 ms
    DT_ASSERT_EQ(DtSt2110VideoTx_FrameSize(&Tx, 0), 288 * 1800);

    // Refused.
    Config.Resolution = (VideoSize){1919, 1080};
    DT_ASSERT_EQ(DtSt2110VideoTx_Configure(&Tx, &Config, DtAvPixConv_C()),
                 DTAPI_E_INVALID_ARG);
    Config.Resolution = (VideoSize){1920, 1080};
    Config.Timing.Rate = (FrameRate){0, 1};
    DT_ASSERT_EQ(DtSt2110VideoTx_Configure(&Tx, &Config, DtAvPixConv_C()),
                 DTAPI_E_INVALID_ARG);
    Config.Timing.Rate = (FrameRate){25, 1};
    Config.Format = (St2110_TxFrameFormat)3;
    DT_ASSERT_EQ(DtSt2110VideoTx_Configure(&Tx, &Config, DtAvPixConv_C()),
                 DTAPI_E_INVALID_ARG);
    Config.Format = St2110_TxFrameFormat_Uyvy422_8b;
    DT_ASSERT_OK(DtSt2110VideoTx_Configure(&Tx, &Config, DtAvPixConv_C()));

    // Payload sizes.
    static const int Refused[] = {0, -2, 1002, 1424};
    for (int i = 0; i < 4; i++)
    {
        Tx.Packing.PayloadSize = Refused[i];
        DT_ASSERT_EQ(DtSt2110VideoTx_Start(&Tx, &S), DTAPI_E_INVALID_ARG);
    }
    Tx.Packing.PayloadSize = 1420;
    DT_ASSERT_OK(DtSt2110VideoTx_Start(&Tx, &S));
    Tx.Packing.PackingMode = St2110_PackingMode_Block;
    DT_ASSERT_EQ(DtSt2110VideoTx_Start(&Tx, &S), DTAPI_E_INVALID_ARG);
    Tx.Packing.PayloadSize = 1260;
    DT_ASSERT_OK(DtSt2110VideoTx_Start(&Tx, &S));
    Tx.Packing.PayloadSize = -1;
    DT_ASSERT_OK(DtSt2110VideoTx_Start(&Tx, &S));
    DT_ASSERT_EQ(Tx.PayloadSize, 1260);
    Tx.Packing.PayloadSize = 8000;
    DT_ASSERT_EQ(DtSt2110VideoTx_Start(&Tx, &S), DTAPI_E_INVALID_ARG);
    S.Net.Jumbo = true;
    DT_ASSERT_EQ(DtSt2110VideoTx_Start(&Tx, &S), DTAPI_E_INVALID_ARG);
    Tx.Packing.PayloadSize = 7920;
    DT_ASSERT_OK(DtSt2110VideoTx_Start(&Tx, &S));

    St2110_TxConfigRawVideo Raw;
    memset(&Raw, 0, sizeof(Raw));
    Raw.PGroup.NumBytes = 6;
    Raw.PGroup.NumPixels = 4;
    Raw.RowSize = 20;
    Raw.NumRows = 10;
    Raw.Timing.Rate = (FrameRate){25, 1};
    Raw.ActiveVideo = (Ratio){1, 1};
    DT_ASSERT_EQ(DtSt2110VideoTx_ConfigureRaw(&Tx, &Raw), DTAPI_E_INVALID_ARG);
    Raw.RowSize = 24;
    DT_ASSERT_OK(DtSt2110VideoTx_ConfigureRaw(&Tx, &Raw));
    Raw.ActiveVideo.Denominator = 0;
    DT_ASSERT_EQ(DtSt2110VideoTx_ConfigureRaw(&Tx, &Raw), DTAPI_E_INVALID_ARG);
}

// Transmits one frame and reads it back: every byte in place, one marker on the last
// packet, contiguous sequence numbers, the first packet at the transmit offset and the
// packets spaced over the period.
static void CheckTransmit(const St2110_TxConfigVideo* Config, bool Gapped,
                          int* DtFailures)
{
    int Live = DtAlloc_Live();
    DtSt2110VideoTx Tx;
    DtAvTxStream S = Stream();
    DtAvFramePool Pool;
    MemSink Sink;
    DT_ASSERT_OK(DtAvFramePool_Init(&Pool));
    DT_ASSERT_OK(DtSt2110VideoTx_Configure(&Tx, Config, DtAvPixConv_Best()));
    DT_ASSERT_OK(DtSt2110VideoTx_Start(&Tx, &S));
    int FrameSize = DtSt2110VideoTx_FrameSize(&Tx, 0);
    int Rows = FrameSize / Tx.RowSizeFrame;
    DT_ASSERT(Sink_Init(&Sink, (size_t)DtSt2110VideoTx_FrameBytes(&Tx, &S) + 4096,
                        Tx.PacketsPerFrame + 1));

    DtAvFrame* Frame = DtAvFramePool_Get(&Pool, (size_t)FrameSize);
    DT_ASSERT(Frame != NULL);
    FillFrame(Frame->Frame.Data, Rows, Tx.RowSizeFrame, 3);
    Frame->Frame.NumValidBytes = FrameSize;
    Frame->Frame.RtpTime = 123456;
    Frame->Frame.ToD = DtAvTime_FromNs(TOD);
    const DtAvSink Out = {SinkBegin, SinkCommit, &Sink};
    uint32_t FirstSeq = S.SequenceNumber;
    DT_ASSERT_OK(DtSt2110VideoTx_Packetize(&Tx, &S, &Frame->Frame, &Out));
    DT_ASSERT(!Sink.Overrun);
    DT_ASSERT(Sink.Count <= Tx.PacketsPerFrame);
    if (!Config->Timing.VideoScanning)
        DT_ASSERT_EQ(Sink.Count, Tx.PacketsPerFrame);

    uint8_t* Image = (uint8_t*)calloc((size_t)(Rows * Tx.RowSize), 1);
    DT_ASSERT(Image != NULL);
    Reading R;
    ReadFrame(&Sink, 0, Sink.Count, Image, Tx.RowSize, Tx.PgroupBytes, 2, 1, false,
              FirstSeq, &R);
    DT_ASSERT(R.SequenceOk && R.SegmentsOk && R.FieldsOk);
    DT_ASSERT(R.Markers == 1 && R.MarkerLast);
    DT_ASSERT_EQ(R.RtpTime, 123456);
    DT_ASSERT_EQ(R.FirstTodNs, TOD + (uint64_t)Tx.TrOffsetNs - DELAY_NS);
    uint64_t Spacing = Tx.Spacing;
    uint64_t Expected = (uint64_t)(Sink.Count - 1) * Spacing / 1000;
    DT_ASSERT_EQ(R.LastTodNs - R.FirstTodNs, Expected);
    uint64_t Period =
        DT_AV_NS_PER_SEC * (uint64_t)Tx.Rate.Denominator / (uint64_t)Tx.Rate.Numerator;
    uint64_t Span = Gapped ? Period * 1080 / 1125 : Period;
    DT_ASSERT(Spacing * (uint64_t)Tx.PacketsPerFrame / 1000 <= Span);
    DT_ASSERT(Spacing * (uint64_t)Tx.PacketsPerFrame / 1000 + 1 >= Span - Span / 1000);
    DT_ASSERT(!Tx.Packing.OneLinePerPacket || R.MaxRowsInPacket == 1);

    // The image in the frame's format.
    if (Tx.PgroupBytes == 4)
        DT_ASSERT_MEM(Image, Frame->Frame.Data, (size_t)FrameSize);
    else
    {
        for (int i = 0; i < FrameSize / 5; i++)
        {
            for (int k = 0; k < 4; k++)
            {
                if (GetSample(Image, (size_t)i, k, true) !=
                    GetSample(Frame->Frame.Data, (size_t)i, k, false))
                    DT_FAIL("pixel group %d, sample %d", i, k);
            }
        }
    }
    free(Image);
    Sink_Free(&Sink);
    DtAvFramePool_Destroy(&Pool);
    DT_ASSERT_EQ(DtAlloc_Live(), Live);
}

DT_TEST(Transmit1080p10Bit)
{
    const St2110_TxConfigVideo Config = {
        St2110_TxFrameFormat_Uyvy422_10b,
        {0, St2110_PackingMode_General, -1},
        {1920, 1080},
        {{50, 1}, St2110_Scheduling_Linear, St2110_VideoScanning_Progressive}};
    CheckTransmit(&Config, false, DtFailures);
}

DT_TEST(Transmit720p8BitGappedBlocks)
{
    const St2110_TxConfigVideo Config = {
        St2110_TxFrameFormat_Uyvy422_8b,
        {0, St2110_PackingMode_Block, -1},
        {1280, 720},
        {{60000, 1001}, St2110_Scheduling_Gapped, St2110_VideoScanning_Progressive}};
    CheckTransmit(&Config, true, DtFailures);
}

DT_TEST(TransmitSmallRowsThreePerPacket)
{
    const St2110_TxConfigVideo Config = {
        St2110_TxFrameFormat_Uyvy422_8b,
        {0, St2110_PackingMode_General, -1},
        {64, 17},
        {{25, 1}, St2110_Scheduling_Linear, St2110_VideoScanning_Progressive}};
    CheckTransmit(&Config, false, DtFailures);
}

DT_TEST(TransmitOneLinePerPacket)
{
    const St2110_TxConfigVideo Config = {
        St2110_TxFrameFormat_Uyvy422_10b,
        {1, St2110_PackingMode_General, 1000},
        {1280, 12},
        {{30, 1}, St2110_Scheduling_Linear, St2110_VideoScanning_Progressive}};
    CheckTransmit(&Config, false, DtFailures);
}

// Interlaced fields of 1080i: 540 rows with the field bit on the second; PsF repeats the
// first field's timestamp; a frame of the wrong size sends nothing.
DT_TEST(FieldsAndPsf)
{
    int Live = DtAlloc_Live();
    St2110_TxConfigVideo Config = {
        St2110_TxFrameFormat_Uyvy422_8b,
        {0, St2110_PackingMode_General, -1},
        {1920, 1080},
        {{50, 1}, St2110_Scheduling_Linear, St2110_VideoScanning_Interlaced}};
    DtSt2110VideoTx Tx;
    DtAvTxStream S = Stream();
    DtAvFramePool Pool;
    MemSink Sink;
    DT_ASSERT_OK(DtAvFramePool_Init(&Pool));
    DT_ASSERT_OK(DtSt2110VideoTx_Configure(&Tx, &Config, DtAvPixConv_C()));
    DT_ASSERT_EQ(Tx.TrOffsetNs, 782222); // 22/1125 of 40 ms
    DT_ASSERT_OK(DtSt2110VideoTx_Start(&Tx, &S));
    DT_ASSERT(Sink_Init(&Sink, (size_t)DtSt2110VideoTx_FrameBytes(&Tx, &S), 8000));
    const DtAvSink Out = {SinkBegin, SinkCommit, &Sink};

    DtAvFrame* Frame = DtAvFramePool_Get(&Pool, 3840 * 540);
    DT_ASSERT(Frame != NULL);
    FillFrame(Frame->Frame.Data, 540, 3840, 1);
    Frame->Frame.NumValidBytes = 3840 * 540;
    Frame->Frame.Field = 1;
    Frame->Frame.RtpTime = 42;
    uint32_t FirstSeq = S.SequenceNumber;
    DT_ASSERT_OK(DtSt2110VideoTx_Packetize(&Tx, &S, &Frame->Frame, &Out));
    uint8_t* Image = (uint8_t*)calloc(3840 * 540, 1);
    DT_ASSERT(Image != NULL);
    Reading R;
    ReadFrame(&Sink, 0, Sink.Count, Image, 3840, 4, 2, 1, true, FirstSeq, &R);
    DT_ASSERT(R.SequenceOk && R.SegmentsOk && R.FieldsOk && R.Markers == 1 &&
              R.MarkerLast);
    DT_ASSERT_MEM(Image, Frame->Frame.Data, 3840 * 540);
    DT_ASSERT_EQ(R.RtpTime, 42);

    // PsF: the second field keeps the first's timestamp.
    Config.Timing.VideoScanning = St2110_VideoScanning_PsF;
    DT_ASSERT_OK(DtSt2110VideoTx_Configure(&Tx, &Config, DtAvPixConv_C()));
    DT_ASSERT_OK(DtSt2110VideoTx_Start(&Tx, &S));
    Sink.Used = 0;
    Sink.Count = 0;
    Frame->Frame.Field = 0;
    Frame->Frame.RtpTime = 1000;
    DT_ASSERT_OK(DtSt2110VideoTx_Packetize(&Tx, &S, &Frame->Frame, &Out));
    int FirstField = Sink.Count;
    Sink.Used = 0;
    Sink.Count = 0;
    Frame->Frame.Field = 1;
    Frame->Frame.RtpTime = 2800;
    FirstSeq = S.SequenceNumber;
    DT_ASSERT_OK(DtSt2110VideoTx_Packetize(&Tx, &S, &Frame->Frame, &Out));
    DT_ASSERT_EQ(Sink.Count, FirstField);
    ReadFrame(&Sink, 0, Sink.Count, Image, 3840, 4, 2, 1, true, FirstSeq, &R);
    DT_ASSERT_EQ(R.RtpTime, 1000);

    // The wrong size.
    Sink.Count = 0;
    Frame->Frame.NumValidBytes -= 1;
    DT_ASSERT_EQ(DtSt2110VideoTx_Packetize(&Tx, &S, &Frame->Frame, &Out),
                 DTAPI_E_INVALID_FORMAT);
    DT_ASSERT_EQ(Sink.Count, 0);

    free(Image);
    Sink_Free(&Sink);
    DtAvFramePool_Destroy(&Pool);
    DT_ASSERT_EQ(DtAlloc_Live(), Live);
}

// Raw 4:2:0 rows of pixel groups: row numbers step by two.
DT_TEST(Raw420Rows)
{
    int Live = DtAlloc_Live();
    St2110_TxConfigRawVideo Raw;
    memset(&Raw, 0, sizeof(Raw));
    Raw.Is420 = 1;
    Raw.NumRows = 20;
    Raw.PGroup.NumBytes = 6;
    Raw.PGroup.NumPixels = 4;
    Raw.RowSize = 600;
    Raw.Timing.Rate = (FrameRate){25, 1};
    Raw.ActiveVideo = (Ratio){1, 1};
    Raw.TrOffset = 5000;
    Raw.Packing.PayloadSize = -1;
    DtSt2110VideoTx Tx;
    DtAvTxStream S = Stream();
    DtAvFramePool Pool;
    MemSink Sink;
    DT_ASSERT_OK(DtAvFramePool_Init(&Pool));
    DT_ASSERT_OK(DtSt2110VideoTx_ConfigureRaw(&Tx, &Raw));
    DT_ASSERT_OK(DtSt2110VideoTx_Start(&Tx, &S));
    DT_ASSERT_EQ(Tx.PayloadSize, 1416);
    DT_ASSERT(Sink_Init(&Sink, (size_t)DtSt2110VideoTx_FrameBytes(&Tx, &S), 100));
    const DtAvSink Out = {SinkBegin, SinkCommit, &Sink};

    DtAvFrame* Frame = DtAvFramePool_Get(&Pool, 20 * 600);
    DT_ASSERT(Frame != NULL);
    FillFrame(Frame->Frame.Data, 20, 600, 9);
    Frame->Frame.NumValidBytes = 20 * 600;
    Frame->Frame.ToD = DtAvTime_FromNs(TOD);
    uint32_t FirstSeq = S.SequenceNumber;
    DT_ASSERT_OK(DtSt2110VideoTx_Packetize(&Tx, &S, &Frame->Frame, &Out));
    DT_ASSERT_EQ(Sink.Count, Tx.PacketsPerFrame);
    uint8_t Image[20 * 600];
    memset(Image, 0, sizeof(Image));
    Reading R;
    ReadFrame(&Sink, 0, Sink.Count, Image, 600, 6, 4, 2, false, FirstSeq, &R);
    DT_ASSERT(R.SequenceOk && R.SegmentsOk && R.FieldsOk && R.MarkerLast);
    DT_ASSERT_MEM(Image, Frame->Frame.Data, 20 * 600);
    DT_ASSERT_EQ(R.FirstTodNs, TOD + 5000 - DELAY_NS);

    Sink_Free(&Sink);
    DtAvFramePool_Destroy(&Pool);
    DT_ASSERT_EQ(DtAlloc_Live(), Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reception +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define MAX_FRAMES 16

typedef struct Collected
{
    DtAvFramePool* Pool;
    AvFifo_Frame Frames[MAX_FRAMES]; // Copies, without data
    uint8_t* Data[MAX_FRAMES];
    int Count;
    bool Refuse;
} Collected;

// Keeps a copy of each frame and returns it to the pool.
static bool Collect(void* Context, DtAvFrame* Frame)
{
    Collected* C = (Collected*)Context;
    if (C->Refuse)
        return false;
    if (C->Count < MAX_FRAMES)
    {
        C->Frames[C->Count] = Frame->Frame;
        C->Data[C->Count] = (uint8_t*)malloc(Frame->Frame.Size);
        if (C->Data[C->Count] != NULL)
            memcpy(C->Data[C->Count], Frame->Frame.Data, Frame->Frame.Size);
        C->Count++;
    }
    DtAvFramePool_Return(C->Pool, &Frame->Frame);
    return true;
}

static void Collected_Free(Collected* C)
{
    for (int i = 0; i < C->Count; i++)
        free(C->Data[i]);
    C->Count = 0;
}

// The made stream: 8-bit rows of 32 pixels, in packets of one segment of at most Chunk
// bytes each.
typedef struct Maker
{
    uint8_t Packet[512];
    int Size;
    uint32_t Seq;
    int Rows;
    int RowSize;
    int Chunk;
} Maker;

// Makes a packet of one row segment, or with Extra row headers declared.
static void Make(Maker* M, int Row, int Offset, int Length, bool Marker, uint32_t Rtp,
                 uint64_t TodNs, bool Field, const uint8_t* Data, int Extra)
{
    uint8_t* P = M->Packet;
    int Headers = 1 + Extra;
    int Udp = 8 + 12 + 2 + 6 * Headers + Length;
    int FrameSize = 14 + 20 + Udp;
    memset(P, 0, sizeof(M->Packet));

    DtEthIpFields H;
    memset(&H, 0, sizeof(H));
    H.NumWords = DtEthIp_NumWords(FrameSize, 8);
    H.FrameSize = FrameSize;
    H.IpAddressOffset = 18 + 14 + 12;
    H.PortOffset = 18 + 14 + 20;
    H.Protocol = DT_ETHIP_PROTO_UDP;
    H.PacketType = DT_ETHIP_TYPE_IPV4;
    H.TimestampValid = true;
    H.Seconds = (uint32_t)(TodNs / DT_AV_NS_PER_SEC);
    H.Nanoseconds = (uint32_t)(TodNs % DT_AV_NS_PER_SEC);
    DtEthIp_Write(&H, P);
    P[18 + 12] = 0x08;
    P[18 + 14] = 0x45;
    P[18 + 14 + 9] = 17;
    uint8_t* U = P + 18 + 14 + 20;
    U[4] = (uint8_t)(Udp >> 8);
    U[5] = (uint8_t)Udp;
    uint8_t* R = U + 8;
    R[0] = 0x80;
    R[1] = (uint8_t)((Marker ? 0x80 : 0) | 96);
    R[2] = (uint8_t)(M->Seq >> 8);
    R[3] = (uint8_t)M->Seq;
    R[4] = (uint8_t)(Rtp >> 24);
    R[5] = (uint8_t)(Rtp >> 16);
    R[6] = (uint8_t)(Rtp >> 8);
    R[7] = (uint8_t)Rtp;
    R[12] = (uint8_t)(M->Seq >> 24);
    R[13] = (uint8_t)(M->Seq >> 16);
    uint8_t* S = R + 14;
    for (int h = 0; h < Headers; h++, S += 6)
    {
        int L = h == 0 ? Length : 4;
        S[0] = (uint8_t)(L >> 8);
        S[1] = (uint8_t)L;
        S[2] = (uint8_t)((Field ? 0x80 : 0) | (Row >> 8 & 0x7F));
        S[3] = (uint8_t)Row;
        S[4] = (uint8_t)((h + 1 < Headers ? 0x80 : 0) | (Offset / 2 >> 8 & 0x7F));
        S[5] = (uint8_t)(Offset / 2);
    }
    memcpy(S, Data, (size_t)Length);
    M->Size = H.NumWords * 8;
    M->Seq++;
}

typedef enum Fault
{
    FAULT_NONE,
    FAULT_LOSE_PACKET,   // One packet inside the frame is not sent
    FAULT_SKIP_SEQUENCE, // The sequence numbers jump before the frame
    FAULT_NO_MARKER,     // The frame's last packet has no marker
    FAULT_FOUR_HEADERS,  // A packet declares four row headers
    FAULT_CORRUPT,       // A packet's header does not check
    FAULT_FIELD_BIT,     // The field bit appears on a progressive stream
} Fault;

// Sends a frame of the made stream to Rx.
static void SendFrame(Maker* M, DtSt2110VideoRx* Rx, const uint8_t* Frame, uint32_t Rtp,
                      uint64_t TodNs, Fault F)
{
    if (F == FAULT_SKIP_SEQUENCE)
        M->Seq += 10;
    int Packet = 0;
    for (int Row = 0; Row < M->Rows; Row++)
    {
        for (int Offset = 0; Offset < M->RowSize; Offset += M->Chunk, Packet++)
        {
            int Length = M->RowSize - Offset < M->Chunk ? M->RowSize - Offset : M->Chunk;
            bool Last = Row == M->Rows - 1 && Offset + Length == M->RowSize;
            Make(M, Row, Offset, Length, Last && F != FAULT_NO_MARKER, Rtp, TodNs,
                 F == FAULT_FIELD_BIT && Packet == 3, Frame + Row * M->RowSize + Offset,
                 F == FAULT_FOUR_HEADERS && Packet == 2 ? 3 : 0);
            if (F == FAULT_LOSE_PACKET && Packet == 5)
                continue;
            if (F == FAULT_CORRUPT && Packet == 4)
                M->Packet[1] ^= 0x55;
            DtSt2110VideoRx_Parse(Rx, M->Packet, M->Size);
        }
    }
}

// Frames of 8-bit video, 32 pixels by 10 rows, in packets of 24 bytes.
DT_TEST(ReceiveLearnsAndCountsFaults)
{
    int Live = DtAlloc_Live();
    DtAvFramePool Pool;
    DT_ASSERT_OK(DtAvFramePool_Init(&Pool));
    static Collected Got;
    memset(&Got, 0, sizeof(Got));
    Got.Pool = &Pool;
    const DtAvRxTarget Target = {&Pool, Collect, &Got};
    DtSt2110VideoRx Rx;
    DtSt2110VideoRx_Init(&Rx, St2110_RxFrameFormat_Uyvy422_8b, DtAvPixConv_C(), &Target);
    Maker M = {{0}, 0, 0x0000FFF0, 10, 64, 24};
    uint8_t Frame[640];
    FillFrame(Frame, 10, 64, 5);

    // The first frame teaches the size, and the second is received.
    SendFrame(&M, &Rx, Frame, 1, TOD, FAULT_NONE);
    DT_ASSERT_EQ(Got.Count, 0);
    DT_ASSERT(!Rx.WaitForEndFrame);
    DT_ASSERT_EQ(Rx.NumRowsFrame, 10);
    DT_ASSERT_EQ(Rx.LineSizeFrame, 64);
    DT_ASSERT_EQ(Rx.CountedFrameSize, 640);
    SendFrame(&M, &Rx, Frame, 2, TOD + 1000, FAULT_NONE);
    DT_ASSERT_EQ(Got.Count, 1);
    DT_ASSERT_EQ(Got.Frames[0].NumValidBytes, 640);
    DT_ASSERT_EQ(Got.Frames[0].NumRows, 10);
    DT_ASSERT_EQ(Got.Frames[0].RtpTime, 2);
    DT_ASSERT_EQ(DtAvTime_ToNs(&Got.Frames[0].ToD), TOD + 1000);
    DT_ASSERT_EQ(Got.Frames[0].Is420, 0);
    DT_ASSERT_EQ(Got.Frames[0].Field, 0);
    DT_ASSERT_MEM(Got.Data[0], Frame, 640);

    // A lost packet: incomplete, the rest of the frame skipped, the next received.
    SendFrame(&M, &Rx, Frame, 3, TOD, FAULT_LOSE_PACKET);
    DT_ASSERT_EQ(Rx.Stats.FramesIncomplete, 1);
    SendFrame(&M, &Rx, Frame, 4, TOD, FAULT_NONE);
    DT_ASSERT_EQ(Got.Count, 2);

    // A jump between frames: a gap, and the frame received.
    SendFrame(&M, &Rx, Frame, 5, TOD, FAULT_SKIP_SEQUENCE);
    DT_ASSERT_EQ(Rx.Stats.Gaps, 1);
    DT_ASSERT_EQ(Got.Count, 3);

    // No marker: two frames' bytes do not fit, a size error; the next frame teaches the
    // size again, and the one after is received.
    SendFrame(&M, &Rx, Frame, 6, TOD, FAULT_NO_MARKER);
    SendFrame(&M, &Rx, Frame, 7, TOD, FAULT_NONE);
    DT_ASSERT_EQ(Rx.Stats.FramesSizeError, 1);
    DT_ASSERT(Rx.WaitForEndFrame || Rx.CalculatedFrameSize == -1);
    SendFrame(&M, &Rx, Frame, 8, TOD, FAULT_NONE);
    DT_ASSERT_EQ(Got.Count, 4);
    SendFrame(&M, &Rx, Frame, 9, TOD, FAULT_NONE);
    DT_ASSERT_EQ(Got.Count, 5);
    DT_ASSERT_EQ(Got.Frames[4].RtpTime, 9);

    // Four row headers: an IP packet error, the frame skipped.
    SendFrame(&M, &Rx, Frame, 10, TOD, FAULT_FOUR_HEADERS);
    DT_ASSERT_EQ(Rx.Stats.IpPacketErrors, 1);
    SendFrame(&M, &Rx, Frame, 11, TOD, FAULT_NONE);
    DT_ASSERT_EQ(Got.Count, 6);
    DT_ASSERT_EQ(Got.Frames[5].RtpTime, 11);

    // A corrupt packet header.
    SendFrame(&M, &Rx, Frame, 12, TOD, FAULT_CORRUPT);
    DT_ASSERT_EQ(Rx.Stats.IpPacketErrors, 2);
    DT_ASSERT_EQ(Rx.Stats.FramesIncomplete, 2);
    SendFrame(&M, &Rx, Frame, 13, TOD, FAULT_NONE);
    DT_ASSERT_EQ(Got.Count, 7);

    // The field bit once the size is known: a size error.
    int SizeErrors = Rx.Stats.FramesSizeError;
    SendFrame(&M, &Rx, Frame, 14, TOD, FAULT_FIELD_BIT);
    DT_ASSERT_EQ(Rx.Stats.FramesSizeError, SizeErrors + 1);

    // A refused frame is dropped.
    Collected_Free(&Got);
    DtSt2110VideoRx_Reset(&Rx);
    SendFrame(&M, &Rx, Frame, 15, TOD, FAULT_NONE);
    Got.Refuse = true;
    SendFrame(&M, &Rx, Frame, 16, TOD, FAULT_NONE);
    DT_ASSERT_EQ(Rx.Stats.DroppedFrames, 1);
    DT_ASSERT_EQ(Got.Count, 0);
    DT_ASSERT_EQ(DtAvFramePool_NumFree(&Pool), DtAvFramePool_NumFrames(&Pool));
    DT_ASSERT_EQ(Rx.Stats.FramesOk, 8);

    DtSt2110VideoRx_Reset(&Rx);
    DtAvFramePool_Destroy(&Pool);
    DT_ASSERT_EQ(DtAlloc_Live(), Live);
}

// Frames transmitted by the packetizer and received by the parser, in each receive
// format, progressive and interlaced.
static void CheckLoopback(St2110_TxFrameFormat TxFormat, St2110_RxFrameFormat RxFormat,
                          St2110_VideoScanning Scanning, int* DtFailures)
{
    int Live = DtAlloc_Live();
    const St2110_TxConfigVideo Config = {TxFormat,
                                         {0, St2110_PackingMode_General, -1},
                                         {1920, 1080},
                                         {{50, 1}, St2110_Scheduling_Linear, Scanning}};
    DtSt2110VideoTx Tx;
    DtAvTxStream S = Stream();
    DtAvFramePool Pool;
    MemSink Sink;
    static Collected Got;
    memset(&Got, 0, sizeof(Got));
    Got.Pool = &Pool;
    DT_ASSERT_OK(DtAvFramePool_Init(&Pool));
    DT_ASSERT_OK(DtSt2110VideoTx_Configure(&Tx, &Config, DtAvPixConv_Best()));
    DT_ASSERT_OK(DtSt2110VideoTx_Start(&Tx, &S));
    DT_ASSERT(Sink_Init(&Sink, (size_t)DtSt2110VideoTx_FrameBytes(&Tx, &S), 8000));
    const DtAvSink Out = {SinkBegin, SinkCommit, &Sink};
    const DtAvRxTarget Target = {&Pool, Collect, &Got};
    DtSt2110VideoRx Rx;
    DtSt2110VideoRx_Init(&Rx, RxFormat, DtAvPixConv_Best(), &Target);

    bool Fields = Scanning != St2110_VideoScanning_Progressive;
    int Rows = Fields ? 540 : 1080;
    int Size = Rows * Tx.RowSizeFrame;
    DtAvFrame* Frame = DtAvFramePool_Get(&Pool, (size_t)Size);
    DT_ASSERT(Frame != NULL);
    FillFrame(Frame->Frame.Data, Rows, Tx.RowSizeFrame, 77);
    Frame->Frame.NumValidBytes = Size;
    for (int f = 0; f < 3; f++)
    {
        Sink.Used = 0;
        Sink.Count = 0;
        Frame->Frame.Field = Fields ? f % 2 : 0;
        Frame->Frame.RtpTime = (uint32_t)f;
        Frame->Frame.ToD = DtAvTime_FromNs(TOD + (uint64_t)f * 20000000);
        DT_ASSERT_OK(DtSt2110VideoTx_Packetize(&Tx, &S, &Frame->Frame, &Out));
        DT_ASSERT(!Sink.Overrun);
        for (int p = 0; p < Sink.Count; p++)
            DtSt2110VideoRx_Parse(&Rx, Sink.Buf + Sink.Offsets[p], Sink.Sizes[p]);
    }
    DT_ASSERT_EQ(Got.Count, 2);
    DT_ASSERT_EQ(Rx.Stats.IpPacketErrors + Rx.Stats.FramesSizeError + Rx.Stats.Gaps +
                     Rx.Stats.FramesIncomplete,
                 0);

    for (int i = 0; i < Got.Count; i++)
    {
        const AvFifo_Frame* F = &Got.Frames[i];
        const uint8_t* Data = Got.Data[i];
        DT_ASSERT_EQ(F->RtpTime, (uint32_t)(i + 1));
        DT_ASSERT_EQ(F->NumRows, Rows);
        DT_ASSERT_EQ(F->Field, Fields ? (i + 1) % 2 : 0);
        DT_ASSERT(Data != NULL);
        int Pixels = Rows * 1920;
        bool TenBitIn = TxFormat == St2110_TxFrameFormat_Uyvy422_10b;
        switch (RxFormat)
        {
        case St2110_RxFrameFormat_Uyvy422_10b_to_8b:
            DT_ASSERT_EQ(F->NumValidBytes, Pixels * 2);
            for (int g = 0; g < Pixels / 2; g++)
            {
                for (int k = 0; k < 4; k++)
                {
                    if (Data[g * 4 + k] !=
                        (uint8_t)(GetSample(Frame->Frame.Data, (size_t)g, k, false) >> 2))
                        DT_FAIL("frame %d pixel group %d sample %d", i, g, k);
                }
            }
            break;
        case St2110_RxFrameFormat_Yuv422p_8b:
        {
            DT_ASSERT_EQ(F->NumValidBytes, Pixels * 2);
            size_t Plane = F->Size / 4;
            for (int g = 0; g < Pixels / 2; g++)
            {
                const uint8_t* In = Frame->Frame.Data + g * 4;
                if (Data[2 * g] != In[1] || Data[2 * g + 1] != In[3] ||
                    Data[Plane * 2 + (size_t)g] != In[0] ||
                    Data[Plane * 3 + (size_t)g] != In[2])
                    DT_FAIL("frame %d pixel group %d", i, g);
            }
            break;
        }
        default:
            DT_ASSERT_EQ(F->NumValidBytes, Size);
            DT_ASSERT_MEM(Data, Frame->Frame.Data, (size_t)Size);
            break;
        }
        (void)TenBitIn;
    }

    Collected_Free(&Got);
    DtSt2110VideoRx_Reset(&Rx);
    Sink_Free(&Sink);
    DtAvFramePool_Destroy(&Pool);
    DT_ASSERT_EQ(DtAlloc_Live(), Live);
}

DT_TEST(Loopback10Bit)
{
    CheckLoopback(St2110_TxFrameFormat_Uyvy422_10b, St2110_RxFrameFormat_Uyvy422_10b,
                  St2110_VideoScanning_Progressive, DtFailures);
}

DT_TEST(Loopback10BitTo8BitInterlaced)
{
    CheckLoopback(St2110_TxFrameFormat_Uyvy422_10b,
                  St2110_RxFrameFormat_Uyvy422_10b_to_8b, St2110_VideoScanning_Interlaced,
                  DtFailures);
}

DT_TEST(Loopback8Bit)
{
    CheckLoopback(St2110_TxFrameFormat_Uyvy422_8b, St2110_RxFrameFormat_Uyvy422_8b,
                  St2110_VideoScanning_Progressive, DtFailures);
}

DT_TEST(Loopback8BitPlanar)
{
    CheckLoopback(St2110_TxFrameFormat_Uyvy422_8b, St2110_RxFrameFormat_Yuv422p_8b,
                  St2110_VideoScanning_Progressive, DtFailures);
}

DT_TEST(LoopbackRawInterlaced)
{
    CheckLoopback(St2110_TxFrameFormat_Uyvy422_8b, St2110_RxFrameFormat_Raw,
                  St2110_VideoScanning_Interlaced, DtFailures);
}

DT_TEST_MAIN("St2110Video", DT_RUN(ConfigurationChecks), DT_RUN(Transmit1080p10Bit),
             DT_RUN(Transmit720p8BitGappedBlocks),
             DT_RUN(TransmitSmallRowsThreePerPacket), DT_RUN(TransmitOneLinePerPacket),
             DT_RUN(FieldsAndPsf), DT_RUN(Raw420Rows),
             DT_RUN(ReceiveLearnsAndCountsFaults), DT_RUN(Loopback10Bit),
             DT_RUN(Loopback10BitTo8BitInterlaced), DT_RUN(Loopback8Bit),
             DT_RUN(Loopback8BitPlanar), DT_RUN(LoopbackRawInterlaced))
