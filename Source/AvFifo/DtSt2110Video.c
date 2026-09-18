// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtSt2110Video.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - ST 2110-20 video: frames into packets, and packets into frames
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <limits.h>
#include <string.h>

// CDTAPI includes
#include "DtAvTime.h"      // Nanoseconds and 128-bit arithmetic.
#include "DtSt2110Video.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The packet spacing is kept in thousandths of a nanosecond, as DTAPI keeps it.
#define SPACING_FRACTION 1000

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PartOfPeriod -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Part / Whole of a frame period at Rate, in nanoseconds; -1 when it does not fit an int.
//
static int PartOfPeriod(const FrameRate* Rate, int Part, int Whole)
{
    uint64_t Ns =
        DtAvTime_MulAddDiv(DT_AV_NS_PER_SEC * (uint64_t)Part, (uint64_t)Rate->Denominator,
                           0, (uint64_t)Rate->Numerator * (uint64_t)Whole);
    return Ns <= INT_MAX ? (int)Ns : -1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110VideoTx_Configure -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// VideoTx::VideoTx for TxConfigVideo. The transmit offset is a part of the frame period:
// 43/1125 for progressive video of 1080 lines and more, 28/750 below; 26/625 for
// 625-line interlaced video, 20/525 for 525 lines, told apart by a width of 720 and the
// field rate, and 22/1125 for other interlaced video.
//
DtapiResult DtSt2110VideoTx_Configure(DtSt2110VideoTx* Tx,
                                      const St2110_TxConfigVideo* Config,
                                      const DtAvPixConv* Conv)
{
    memset(Tx, 0, sizeof(*Tx));
    int Width = Config->Resolution.Width;
    int Height = Config->Resolution.Height;
    if (Config->Timing.Rate.Numerator <= 0 || Config->Timing.Rate.Denominator <= 0 ||
        Width <= 0 || Height <= 0 || Width % 2 != 0)
    {
        return DTAPI_E_INVALID_ARG;
    }

    switch (Config->Format)
    {
    case St2110_TxFrameFormat_Uyvy422_10b:
        Tx->PgroupBytes = 5;
        Tx->Convert = Conv->Uyvy10ToPg10;
        break;
    case St2110_TxFrameFormat_Uyvy422_8b:
        Tx->PgroupBytes = 4;
        break;
    default:
        return DTAPI_E_INVALID_ARG;
    }
    Tx->PgroupPixels = 2;
    Tx->RowSize = Width / 2 * Tx->PgroupBytes;
    Tx->RowSizeFrame = Tx->RowSize;
    Tx->NumRows = Height;
    Tx->Interlaced = Config->Timing.VideoScanning == St2110_VideoScanning_Interlaced;
    Tx->Psf = Config->Timing.VideoScanning == St2110_VideoScanning_PsF;
    Tx->Packing = Config->Packing;
    Tx->Scheduling = Config->Timing.Scheduling;
    Tx->Rate = Config->Timing.Rate;

    int Part = 0;
    int Whole = 0;
    if (!Tx->Interlaced && !Tx->Psf)
    {
        Tx->ActiveVideo = (Ratio){1080, 1125};
        Part = Height >= 1080 ? 43 : 28;
        Whole = Height >= 1080 ? 1125 : 750;
    }
    else
    {
        Tx->Rate.Denominator *= 2;
        if (Width == 720 && Tx->Rate.Numerator == 50)
        {
            Tx->ActiveVideo = (Ratio){576, 625};
            Part = 26;
            Whole = 625;
        }
        else if (Width == 720)
        {
            Tx->ActiveVideo = (Ratio){487, 525};
            Part = 20;
            Whole = 525;
        }
        else
        {
            Tx->ActiveVideo = (Ratio){1080, 1125};
            Part = 22;
            Whole = 1125;
        }
    }
    Tx->TrOffsetNs = PartOfPeriod(&Tx->Rate, Part, Whole);
    return Tx->TrOffsetNs >= 0 ? DTAPI_OK : DTAPI_E_INVALID_ARG;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110VideoTx_ConfigureRaw -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSt2110VideoTx_ConfigureRaw(DtSt2110VideoTx* Tx,
                                         const St2110_TxConfigRawVideo* Config)
{
    memset(Tx, 0, sizeof(*Tx));
    if (Config->PGroup.NumBytes <= 0 || Config->PGroup.NumPixels <= 0 ||
        Config->RowSize <= 0 || Config->RowSize % Config->PGroup.NumBytes != 0 ||
        Config->NumRows <= 0 || Config->Timing.Rate.Numerator <= 0 ||
        Config->Timing.Rate.Denominator <= 0 || Config->ActiveVideo.Numerator < 0 ||
        Config->ActiveVideo.Denominator <= 0)
    {
        return DTAPI_E_INVALID_ARG;
    }
    Tx->Is420 = Config->Is420 != 0;
    Tx->Interlaced = Config->Timing.VideoScanning == St2110_VideoScanning_Interlaced;
    Tx->Psf = Config->Timing.VideoScanning == St2110_VideoScanning_PsF;
    Tx->NumRows = Config->NumRows;
    Tx->RowSize = Config->RowSize;
    Tx->RowSizeFrame = Config->RowSize;
    Tx->PgroupBytes = Config->PGroup.NumBytes;
    Tx->PgroupPixels = Config->PGroup.NumPixels;
    Tx->Packing = Config->Packing;
    Tx->Scheduling = Config->Timing.Scheduling;
    Tx->Rate = Config->Timing.Rate;
    if (Tx->Interlaced || Tx->Psf)
        Tx->Rate.Denominator *= 2;
    Tx->ActiveVideo = Config->ActiveVideo;
    Tx->TrOffsetNs = Config->TrOffset;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PacketsPerFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// VideoTx::CalcNumberOfIpPacketsPerFrame.
//
static int PacketsPerFrame(const DtSt2110VideoTx* Tx)
{
    int64_t Rows = Tx->NumRows;
    int64_t RowSize = Tx->RowSize;
    int64_t Payload = Tx->PayloadSize;

    if (Tx->Packing.OneLinePerPacket)
        return (int)((RowSize + Payload - 1) / Payload * Rows);
    if (Payload / RowSize >= 3)
        return (int)((Rows + 2) / 3);
    return (int)((Rows * RowSize + Payload - 1) / Payload);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110VideoTx_Start -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// VideoTx::Init.
//
DtapiResult DtSt2110VideoTx_Start(DtSt2110VideoTx* Tx, const DtAvTxStream* Stream)
{
    int Udp = Stream->Net.Jumbo ? DT_AV_UDP_JUMBO : DT_AV_UDP_STANDARD;
    int Max = Udp - DT_ST2110_VIDEO_HEADERS;
    int Payload = Tx->Packing.PayloadSize;
    bool Block = Tx->Packing.PackingMode == St2110_PackingMode_Block;

    if (Payload != -1)
    {
        if (Payload <= 0 || Payload % Tx->PgroupBytes != 0 || Payload > Max ||
            (Block && Payload % 180 != 0))
        {
            return DTAPI_E_INVALID_ARG;
        }
    }
    else
    {
        Payload = (DT_AV_UDP_STANDARD - DT_ST2110_VIDEO_HEADERS) / Tx->PgroupBytes *
                  Tx->PgroupBytes;
        if (Block)
            Payload = Payload / 180 * 180;
        if (Payload <= 0)
            return DTAPI_E_INVALID_ARG;
    }
    Tx->PayloadSize = Payload;
    Tx->PacketsPerFrame = PacketsPerFrame(Tx);

    uint64_t Numerator =
        DT_AV_NS_PER_SEC * SPACING_FRACTION * (uint64_t)Tx->Rate.Denominator;
    uint64_t Denominator = (uint64_t)Tx->PacketsPerFrame * (uint64_t)Tx->Rate.Numerator;
    if (Tx->Scheduling == St2110_Scheduling_Gapped)
        Tx->Spacing =
            DtAvTime_MulAddDiv(Numerator, (uint64_t)Tx->ActiveVideo.Numerator, 0,
                               Denominator * (uint64_t)Tx->ActiveVideo.Denominator);
    else
        Tx->Spacing = Numerator / Denominator;
    Tx->PrevRtpTime = 0;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110VideoTx_FrameBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtSt2110VideoTx_FrameBytes(const DtSt2110VideoTx* Tx, const DtAvTxStream* Stream)
{
    int Payload = DT_ST2110_VIDEO_HEADERS - DT_AV_UDP_HEADER_SIZE + Tx->PayloadSize;
    return Tx->PacketsPerFrame * DtAvNet_PacketSize(&Stream->Net, Payload);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RowsToSend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A field has half the rows; the first field of an odd number of rows one more.
//
static int RowsToSend(const DtSt2110VideoTx* Tx, int Field)
{
    if (!Tx->Interlaced && !Tx->Psf)
        return Tx->NumRows;
    return Tx->NumRows / 2 + (Field == 0 && Tx->NumRows % 2 != 0 ? 1 : 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110VideoTx_FrameSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtSt2110VideoTx_FrameSize(const DtSt2110VideoTx* Tx, int Field)
{
    return RowsToSend(Tx, Field) * Tx->RowSizeFrame;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110VideoTx_Packetize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// VideoTx::TransferFrame. A packet takes a second row header when the rest of its row
// leaves room in the payload and more rows follow, and a third when the room after the
// rest of the row exceeds a whole row and more than two rows follow.
//
DtapiResult DtSt2110VideoTx_Packetize(DtSt2110VideoTx* Tx, DtAvTxStream* Stream,
                                      const AvFifo_Frame* Frame, const DtAvSink* Sink)
{
    if (Frame->NumValidBytes != DtSt2110VideoTx_FrameSize(Tx, Frame->Field) ||
        (size_t)Frame->NumValidBytes > Frame->Size)
    {
        return DTAPI_E_INVALID_FORMAT;
    }
    int RowsToDo = RowsToSend(Tx, Frame->Field);
    uint32_t RtpTime = Frame->RtpTime;
    if (Tx->Psf && Frame->Field == 1)
        RtpTime = Tx->PrevRtpTime;
    else
        Tx->PrevRtpTime = RtpTime;

    int Header = DtAvNet_HeaderSize(&Stream->Net);
    int MaxPacket = DtAvNet_PacketSize(
        &Stream->Net, DT_ST2110_VIDEO_HEADERS - DT_AV_UDP_HEADER_SIZE + Tx->PayloadSize);
    int64_t FirstTodNs =
        (int64_t)DtAvTime_ToNs(&Frame->ToD) + Tx->TrOffsetNs - Stream->OutputDelayNs;
    const uint8_t* Src = Frame->Data;
    int RowOffset = 0;
    int RowNum = 0;
    uint64_t Offset = 0;
    while (RowsToDo != 0)
    {
        int RowBytesToDo = Tx->RowSize - RowOffset;
        int NumHeaders = 1;
        if (!Tx->Packing.OneLinePerPacket && RowBytesToDo < Tx->PayloadSize &&
            RowsToDo > 1)
        {
            NumHeaders =
                Tx->PayloadSize - RowBytesToDo <= Tx->RowSize || RowsToDo == 2 ? 2 : 3;
        }

        uint8_t* Packet = Sink->Begin(Sink->Context, MaxPacket);
        uint8_t* Rtp = Packet + Header;
        uint8_t* Srd = Rtp + DT_AV_RTP_HEADER_SIZE + DT_AV_ESN_SIZE;
        uint8_t* Dst = Srd + NumHeaders * DT_AV_SRD_SIZE;
        DtAvPacket_Put16((uint16_t)(Stream->SequenceNumber >> 16),
                         Rtp + DT_AV_RTP_HEADER_SIZE);

        bool Marker = false;
        int Used = 0;
        for (int h = 0; h < NumHeaders; h++, Srd += DT_AV_SRD_SIZE)
        {
            RowBytesToDo = Tx->RowSize - RowOffset;
            DtAvSrd Row;
            Row.Length = RowBytesToDo >= Tx->PayloadSize - Used ? Tx->PayloadSize - Used
                                                                : RowBytesToDo;
            Row.Field = Frame->Field != 0;
            Row.Row = RowNum;
            Row.Continuation = h + 1 != NumHeaders;
            Row.Offset = RowOffset / Tx->PgroupBytes * Tx->PgroupPixels;
            DtAvSrd_Write(&Row, Srd);

            if (Tx->Convert != NULL)
                Tx->Convert(Src, Dst, (size_t)(Row.Length / Tx->PgroupBytes));
            else
                memcpy(Dst, Src, (size_t)Row.Length);
            Src += Row.Length;
            Dst += Row.Length;
            RowOffset += Row.Length;
            Used += Row.Length;

            if (RowOffset == Tx->RowSize)
            {
                RowNum += Tx->Is420 ? 2 : 1;
                RowsToDo--;
                RowOffset = 0;
                Marker = RowsToDo == 0;
            }
        }

        DtAvRtp RtpHeader;
        RtpHeader.Marker = Marker;
        RtpHeader.PayloadType = Stream->PayloadType;
        RtpHeader.SequenceNumber = (uint16_t)Stream->SequenceNumber++;
        RtpHeader.Timestamp = RtpTime;
        RtpHeader.Ssrc = Stream->Ssrc;
        DtAvRtp_Write(&RtpHeader, Rtp);

        int UdpPayload =
            DT_AV_RTP_HEADER_SIZE + DT_AV_ESN_SIZE + NumHeaders * DT_AV_SRD_SIZE + Used;
        uint64_t TodNs = (uint64_t)FirstTodNs + Offset / SPACING_FRACTION;
        Sink->Commit(Sink->Context,
                     DtAvNet_Finish(&Stream->Net, Packet, UdpPayload, 0, TodNs));
        Offset += Tx->Spacing;
    }
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reception +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ResetSizes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// VideoRx::ResetFrameSizeCalculations, and the frame being received back to the pool.
//
static void ResetSizes(DtSt2110VideoRx* Rx)
{
    if (Rx->PartialFrame != NULL)
        DtAvFramePool_Return(Rx->Target.Pool, &Rx->PartialFrame->Frame);
    Rx->PartialFrame = NULL;
    Rx->CalculatedFrameSize = -1;
    Rx->NumRowsFrame = -1;
    Rx->CountedFrameSize = -1;
    Rx->CountedNumLines = 0;
    Rx->LineSizeFrame = -1;
    Rx->WaitForEndFrame = true;
    Rx->Interlaced = false;
    Rx->Is420 = false;
    Rx->PrevRowNum = -1;
    Rx->InputBytes = 0;
    Rx->OutputBytes = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110VideoRx_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSt2110VideoRx_Init(DtSt2110VideoRx* Rx, St2110_RxFrameFormat Format,
                          const DtAvPixConv* Conv, const DtAvRxTarget* Target)
{
    memset(Rx, 0, sizeof(*Rx));
    Rx->Format = Format;
    Rx->Conv = Conv;
    Rx->Target = *Target;
    ResetSizes(Rx);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110VideoRx_Reset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSt2110VideoRx_Reset(DtSt2110VideoRx* Rx)
{
    ResetSizes(Rx);
    Rx->LastSeqNum = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadRows -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// VideoRx::ParsePayloadHeader: the row headers up to one without continuation or of
// length 0, at most three, with bounds checks DTAPI leaves out. Sets the offset of the
// data. False when the packet is to be skipped.
//
static bool ReadRows(DtSt2110VideoRx* Rx, const uint8_t* Payload, int PayloadSize,
                     DtAvSrd* Srd, int* NumRows, bool* Field1, int* DataOffset)
{
    int Offset = DT_AV_ESN_SIZE;
    int Rows = 0;
    int64_t DataBytes = 0;

    *NumRows = 0;
    *Field1 = false;
    for (;;)
    {
        if (Offset + DT_AV_SRD_SIZE > PayloadSize)
        {
            Rx->Stats.IpPacketErrors++;
            Rx->WaitForEndFrame = true;
            return false;
        }
        DtAvSrd Row;
        DtAvSrd_Read(Payload + Offset, &Row);
        if (Row.Length == 0)
            break;
        if (Row.Field)
        {
            *Field1 = true;
            if (!Rx->Interlaced)
            {
                if (Rx->CalculatedFrameSize != -1)
                {
                    Rx->Stats.FramesSizeError++;
                    ResetSizes(Rx);
                    return false;
                }
                Rx->Interlaced = true;
            }
        }
        Srd[Rows++] = Row;
        DataBytes += Row.Length;
        Offset += DT_AV_SRD_SIZE;
        if (!Row.Continuation)
            break;
        if (Rows == 3)
        {
            Rx->Stats.IpPacketErrors++;
            Rx->WaitForEndFrame = true;
            return false;
        }
    }
    *NumRows = Rows;
    *DataOffset = DT_AV_ESN_SIZE + Rows * DT_AV_SRD_SIZE;
    if (*DataOffset + DataBytes > PayloadSize)
    {
        Rx->Stats.IpPacketErrors++;
        Rx->WaitForEndFrame = true;
        return false;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CountFrameSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// VideoRx::CountFrameSize: counts the bytes and rows of a frame from its row 0 while its
// packets follow each other, and takes the last row number, whether rows step by two,
// and a row's length from every packet.
//
static void CountFrameSize(DtSt2110VideoRx* Rx, int NumRows, const DtAvSrd* Srd,
                           uint32_t SeqNum)
{
    if (NumRows != 0 && Rx->CountedFrameSize == -1 && Srd[0].Row == 0 &&
        Srd[0].Offset == 0)
    {
        Rx->CountedFrameSize = 0;
        Rx->CountedNumLines = 0;
        Rx->LastSeqNum = SeqNum - 1;
    }
    if (Rx->CountedFrameSize != -1)
    {
        if (SeqNum != Rx->LastSeqNum + 1)
            Rx->CountedFrameSize = -1;
        else if (NumRows > 0)
        {
            for (int i = 0; i < NumRows; i++)
                Rx->CountedFrameSize += Srd[i].Length;
            Rx->CountedNumLines = Srd[NumRows - 1].Row + 1;
        }
    }
    if (NumRows == 0)
        return;

    Rx->NumRowsFrame = Srd[NumRows - 1].Row + 1;
    if (Rx->PrevRowNum == -1)
        Rx->PrevRowNum = Rx->NumRowsFrame;
    else if (Rx->PrevRowNum != Rx->NumRowsFrame)
    {
        Rx->Is420 = Rx->NumRowsFrame - Rx->PrevRowNum == 2;
        Rx->PrevRowNum = Rx->NumRowsFrame;
    }
    if (Srd[0].Offset == 0)
        Rx->LineSizeFrame = 0;
    if (Rx->LineSizeFrame != -1)
    {
        for (int i = 0; i < NumRows; i++)
        {
            if (Srd[i].Offset == 0)
                Rx->LineSizeFrame = 0;
            Rx->LineSizeFrame += Srd[i].Length;
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CalculateFrameSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// VideoRx::CalculateFrameSize: rows times their length, a row more for a field, or the
// counted size when that is larger.
//
static void CalculateFrameSize(DtSt2110VideoRx* Rx)
{
    int64_t Size = -1;
    if (Rx->NumRowsFrame >= 0 && Rx->LineSizeFrame >= 0)
        Size = (int64_t)(Rx->NumRowsFrame + (Rx->Interlaced ? 1 : 0)) * Rx->LineSizeFrame;
    if (Rx->CountedFrameSize != -1 && Rx->Interlaced && Rx->CountedNumLines > 0)
        Rx->CountedFrameSize += Rx->CountedFrameSize / Rx->CountedNumLines;
    if (Size < Rx->CountedFrameSize)
        Size = Rx->CountedFrameSize;
    Rx->CalculatedFrameSize = Size > 0 && Size <= INT_MAX ? (int)Size : -1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Take -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Converts a row segment into the frame being received.
//
static void Take(DtSt2110VideoRx* Rx, const uint8_t* Src, int Length)
{
    uint8_t* Data = Rx->PartialFrame->Frame.Data;
    size_t Out = (size_t)Rx->OutputBytes;

    switch (Rx->Format)
    {
    case St2110_RxFrameFormat_Uyvy422_10b:
        Rx->Conv->Pg10ToUyvy10(Src, Data + Out, (size_t)Length / 5);
        Rx->OutputBytes += Length / 5 * 5;
        break;
    case St2110_RxFrameFormat_Uyvy422_10b_to_8b:
        Rx->Conv->Pg10ToUyvy8(Src, Data + Out, (size_t)Length / 5);
        Rx->OutputBytes += Length / 5 * 4;
        break;
    case St2110_RxFrameFormat_Yuv422p_8b:
    {
        size_t Plane = (size_t)Rx->CalculatedFrameSize / 4;
        size_t Pgroup = Out / 4;
        Rx->Conv->Uyvy8ToYuv422p(Src, (size_t)Length / 4, Data + Pgroup * 2,
                                 Data + Plane * 2 + Pgroup, Data + Plane * 3 + Pgroup);
        Rx->OutputBytes += Length / 4 * 4;
        break;
    }
    default:
        memcpy(Data + Out, Src, (size_t)Length);
        Rx->OutputBytes += Length;
        break;
    }
    Rx->InputBytes += Length;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Finish -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Delivers the frame being received. A field has as many rows as its bytes fill.
//
static void Finish(DtSt2110VideoRx* Rx, bool Field1)
{
    DtAvFrame* Frame = Rx->PartialFrame;
    int LineSize = Rx->Format == St2110_RxFrameFormat_Uyvy422_10b_to_8b
                       ? Rx->LineSizeFrame / 5 * 4
                       : Rx->LineSizeFrame;

    Frame->Frame.NumValidBytes = Rx->OutputBytes;
    Frame->Frame.Field = Field1 ? 1 : 0;
    Frame->Frame.NumRows =
        Rx->Interlaced && LineSize > 0 ? Rx->OutputBytes / LineSize : Rx->NumRowsFrame;
    Frame->Frame.Is420 = Rx->Is420 ? 1 : 0;
    Rx->PartialFrame = NULL;
    Rx->InputBytes = 0;
    Rx->OutputBytes = 0;
    DtAvRxTarget_Deliver(&Rx->Target, &Rx->Stats, Frame);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSt2110VideoRx_Parse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// VideoRx::ParseData.
//
void DtSt2110VideoRx_Parse(DtSt2110VideoRx* Rx, const uint8_t* Packet, int Size)
{
    DtAvRxPacket Udp;
    if (!DtAvRxPacket_Parse(Packet, Size, &Udp) ||
        Udp.UdpSize < DT_AV_RTP_HEADER_SIZE + DT_AV_ESN_SIZE)
    {
        Rx->Stats.IpPacketErrors++;
        return;
    }
    DtAvRtp Rtp;
    DtAvRtp_Read(Udp.Udp, &Rtp);
    const uint8_t* Payload = Udp.Udp + DT_AV_RTP_HEADER_SIZE;
    int PayloadSize = Udp.UdpSize - DT_AV_RTP_HEADER_SIZE;

    DtAvSrd Srd[3];
    int NumRows = 0;
    bool Field1 = false;
    int DataOffset = 0;
    if (!ReadRows(Rx, Payload, PayloadSize, Srd, &NumRows, &Field1, &DataOffset))
        return;
    bool EndOfFrame = Rtp.Marker;
    uint32_t SeqNum = (uint32_t)DtAvPacket_Get16(Payload) << 16 | Rtp.SequenceNumber;

    // Until a marker: learn the size.
    if (Rx->WaitForEndFrame)
    {
        if (Rx->CalculatedFrameSize == -1)
            CountFrameSize(Rx, NumRows, Srd, SeqNum);
        Rx->LastSeqNum = SeqNum;
        if (!EndOfFrame)
            return;
        if (Rx->CalculatedFrameSize == -1 &&
            (Rx->NumRowsFrame == -1 || Rx->LineSizeFrame == -1) &&
            Rx->CountedFrameSize == -1)
        {
            return;
        }
        Rx->WaitForEndFrame = false;
        Rx->InputBytes = 0;
        Rx->OutputBytes = 0;
        return;
    }

    // A lost packet inside a frame skips it; lost packets between frames are a gap.
    uint32_t PrevSeqNum = Rx->LastSeqNum;
    Rx->LastSeqNum = SeqNum;
    if (SeqNum != PrevSeqNum + 1)
    {
        if (Rx->InputBytes != 0 || NumRows == 0 || Srd[0].Row != 0 || Srd[0].Offset != 0)
        {
            Rx->Stats.FramesIncomplete++;
            Rx->WaitForEndFrame = !EndOfFrame;
            Rx->InputBytes = 0;
            Rx->OutputBytes = 0;
            return;
        }
        Rx->Stats.Gaps++;
    }

    if (Rx->PartialFrame == NULL)
    {
        if (Rx->CalculatedFrameSize == -1)
        {
            Rx->Interlaced = Rx->Interlaced || (NumRows > 0 && Srd[NumRows - 1].Field);
            CalculateFrameSize(Rx);
            if (Rx->CalculatedFrameSize == -1)
            {
                ResetSizes(Rx);
                return;
            }
        }
        Rx->PartialFrame =
            DtAvFramePool_Get(Rx->Target.Pool, (size_t)Rx->CalculatedFrameSize);
        if (Rx->PartialFrame == NULL)
        {
            Rx->Stats.DroppedFrames++;
            Rx->WaitForEndFrame = !EndOfFrame;
            return;
        }
        Rx->InputBytes = 0;
        Rx->OutputBytes = 0;
    }
    if (Rx->InputBytes == 0)
    {
        Rx->PartialFrame->Frame.RtpTime = Rtp.Timestamp;
        Rx->PartialFrame->Frame.ToD = DtAvTime_FromNs(Udp.TodNs);
    }

    const uint8_t* Src = Payload + DataOffset;
    for (int i = 0; i < NumRows; i++)
    {
        if (Rx->InputBytes + Srd[i].Length > Rx->CalculatedFrameSize)
        {
            Rx->Stats.FramesSizeError++;
            ResetSizes(Rx);
            return;
        }
        Take(Rx, Src, Srd[i].Length);
        Src += Srd[i].Length;
    }
    if (EndOfFrame)
        Finish(Rx, Field1);
}
