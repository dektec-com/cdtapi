// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAsiRx.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The ASI side of an input channel - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"    // Allocation seam.
#include "Core/DtRing.h"     // The receive buffer.
#include "Core/DtVec.h"      // The bytes to skip.
#include "Device/DtFunc.h"   // The API functions held.
#include "DtAsiRx.h"         // Interface being implemented.
#include "DtPcieAbi.h"       // Operational modes and ASIRX values.
#include "OAL/OsDmaBuffer.h" // The receive buffer.
#include "OAL/OsThread.h"    // Sleeping.
#include "Ts/DtTsTrp.h"      // The packets the card writes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The bytes the stream is searched in at a time.
#define DT_ASIRX_SEARCH_SIZE (64 * 1024)

// The page a receive buffer's size is rounded to, times the prefetch size.
#define DT_ASIRX_PAGE_SIZE 4096

// How often a read looks for more.
#define DT_ASIRX_READ_POLL_MS 5

// Bytes a scan passed over, which a take passes over too: a packet dropped for want of
// room, or bytes searched for the stream.
typedef struct DtAsiRxSkip
{
    uint64_t Position; // Counted as DtAsiRx.ReadPosition
    size_t Bytes;
} DtAsiRxSkip;

typedef struct DtAsiRx
{
    DtRx Rx;
    OsDrv* Drv;
    int PortIndex;
    DtFuncInstance RxFunction, DmaFunction;
    bool HasExclusiveAccess; // Exclusive access to both
    DtDrvObject AsiRx, Cdmac, BurstFifo;
    OsDmaBuffer DmaBuffer;
    bool BufferRegistered;
    DtRing Ring;
    bool Receiving;

    // What is scanned: bytes past the read offset, and whether the stream is found.
    DtTsTrp Scan, DeliverConverter;
    bool OutOfSync;
    size_t ScannedBytes;
    size_t DeliverableBytes; // Bytes to deliver, Pending's included
    uint64_t ReadPosition;   // Bytes the read offset moved on since receiving started
    DtVec Skips;             // DtAsiRxSkip, in order
    size_t NextSkipIndex;
    uint8_t* SearchBuffer; // Where the stream is searched for

    // Output of a packet converted when the caller's buffer had less room than the
    // largest packet's output, kept for what the buffer could not take.
    uint8_t Pending[DT_TRP_MAX_OUTPUT];
    int PendingPos, PendingLen;

    // DTAPI_RX_FIFO_OVF for packets dropped, and for the burst FIFO's count moving.
    bool FifoOvf, FifoOvfLatched;
    bool BurstFifoOvf, BurstFifoOvfLatched;
    uint32_t LastBurstFifoOvfCount;
} DtAsiRx;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Buffer +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ResetScan -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Forgets what was scanned and kept, with the buffer starting again at its beginning.
//
static void ResetScan(DtAsiRx* Asi)
{
    if (Asi->Ring.Base != NULL)
        DtRing_Restart(&Asi->Ring, 0);
    Asi->ScannedBytes = Asi->DeliverableBytes = 0;
    Asi->ReadPosition = 0;
    DtVec_Clear(&Asi->Skips);
    Asi->NextSkipIndex = 0;
    Asi->PendingPos = Asi->PendingLen = 0;
    Asi->OutOfSync = false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PacketAt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The transparent packet Offset bytes past the read offset, in place or copied into Copy
// when it runs across the end of the buffer.
//
static const uint8_t* PacketAt(const DtAsiRx* Asi, size_t Offset, uint8_t* Copy)
{
    const uint8_t* P = DtRing_Span(&Asi->Ring, Offset, DT_TRP_SIZE);
    if (P == NULL)
    {
        DtRing_PeekAt(&Asi->Ring, Offset, Copy, DT_TRP_SIZE);
        P = Copy;
    }
    return P;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AdvanceReadOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Moves the read offset on by Bytes, which were scanned, and tells the driver.
//
static DtapiResult AdvanceReadOffset(DtAsiRx* Asi, size_t Bytes)
{
    if (Bytes == 0)
        return DTAPI_OK;
    if (Bytes > Asi->ScannedBytes || DtRing_Skip(&Asi->Ring, Bytes) != 0)
        return DTAPI_E_INTERNAL;
    Asi->ScannedBytes -= Bytes;
    Asi->ReadPosition += Bytes;
    if (Asi->NextSkipIndex == DtVec_Count(&Asi->Skips))
    {
        DtVec_Clear(&Asi->Skips);
        Asi->NextSkipIndex = 0;
    }
    return DtPcieCmd_CdmacSetRxReadOffset(Asi->Drv, Asi->Cdmac,
                                          (uint32_t)DtRing_ReadOffset(&Asi->Ring));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SkipBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Passes over Bytes after what was scanned, noting them for the take.
//
static DtapiResult SkipBytes(DtAsiRx* Asi, size_t Bytes)
{
    const uint64_t Position = Asi->ReadPosition + Asi->ScannedBytes;
    const size_t Count = DtVec_Count(&Asi->Skips);
    DtAsiRxSkip* Last = Count > Asi->NextSkipIndex
                            ? (DtAsiRxSkip*)DtVec_At(&Asi->Skips, Count - 1)
                            : NULL;

    if (Last != NULL && Last->Position + Last->Bytes == Position)
        Last->Bytes += Bytes;
    else
    {
        DtAsiRxSkip New = {Position, Bytes};
        if (DtVec_Push(&Asi->Skips, &New) != 0)
            return DTAPI_E_OUT_OF_MEM;
    }
    Asi->ScannedBytes += Bytes;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- UpdateOvf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sets the overflow flag when the burst FIFO's count has moved since the previous
// GetFlags, or since the flag was cleared, and latches it. Only GetFlags looks, so that a
// scan between two of them, which every read and every load makes, does not take the
// change away before it is reported.
//
static DtapiResult UpdateOvf(DtAsiRx* Asi)
{
    uint32_t Count = 0;
    DtapiResult Result =
        DtPcieCmd_BurstFifoGetOvfUflCount(Asi->Drv, Asi->BurstFifo, &Count);
    if (Result != DTAPI_OK)
        return Result;
    Asi->BurstFifoOvf = Count != Asi->LastBurstFifoOvfCount;
    Asi->BurstFifoOvfLatched |= Asi->BurstFifoOvf;
    Asi->LastBurstFifoOvfCount = Count;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ScanBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Walks what the card wrote since the last scan, converting it. In sync, each packet is
// converted to count its output; out of sync, the stream is searched for in what three
// packets or more fill, and what was searched without finding it is passed over except
// for its last three packets' worth less a byte, where a stream could still start. When
// nothing is left to deliver, what was scanned is released at once, so that a stream the
// application does not want does not fill the buffer.
//
static DtapiResult ScanBuffer(DtAsiRx* Asi)
{
    if (!Asi->Receiving)
        return DTAPI_OK;

    uint32_t WriteOffset = 0;
    DtapiResult Result =
        DtPcieCmd_CdmacGetRxWriteOffset(Asi->Drv, Asi->Cdmac, &WriteOffset);
    if (Result != DTAPI_OK)
        return Result;
    if (WriteOffset >= Asi->Ring.Size ||
        DtRing_SetWriteOffset(&Asi->Ring, WriteOffset) != 0)
        return DTAPI_E_DEV_DRIVER;

    while (Result == DTAPI_OK)
    {
        const size_t Available = DtRing_Load(&Asi->Ring) - Asi->ScannedBytes;
        if (!Asi->OutOfSync)
        {
            if (Available < DT_TRP_SIZE)
                break;
            uint8_t Copy[DT_TRP_SIZE];
            const int OutputBytes =
                DtTsTrp_Decode(&Asi->Scan, PacketAt(Asi, Asi->ScannedBytes, Copy), NULL);
            if (OutputBytes < 0)
            {
                // The search accepts a first packet the conversion refuses, so it starts
                // a byte further on, or it would find this packet again.
                Asi->OutOfSync = true;
                Result = SkipBytes(Asi, 1);
            }
            else if (Asi->DeliverableBytes + (size_t)OutputBytes > DT_ASIRX_FIFO_SIZE)
            {
                Asi->FifoOvf = Asi->FifoOvfLatched = true;
                Result = SkipBytes(Asi, DT_TRP_SIZE);
            }
            else
            {
                Asi->DeliverableBytes += (size_t)OutputBytes;
                Asi->ScannedBytes += DT_TRP_SIZE;
            }
        }
        else
        {
            if (Available < (size_t)DT_TRP_SIZE * DT_TRP_NUM_SYNC)
                break;
            size_t Size =
                Available < DT_ASIRX_SEARCH_SIZE ? Available : DT_ASIRX_SEARCH_SIZE;
            DtRing_PeekAt(&Asi->Ring, Asi->ScannedBytes, Asi->SearchBuffer, Size);
            size_t Offset = 0;
            if (DtTsTrp_FindSync(&Asi->Scan, Asi->SearchBuffer, Size, &Offset))
                Asi->OutOfSync = false;
            else
                Offset = Size - (size_t)DT_TRP_SIZE * DT_TRP_NUM_SYNC + 1;
            if (Offset > 0)
                Result = SkipBytes(Asi, Offset);
        }
    }

    if (Result == DTAPI_OK && Asi->DeliverableBytes == 0)
    {
        Asi->NextSkipIndex = DtVec_Count(&Asi->Skips);
        Result = AdvanceReadOffset(Asi, Asi->ScannedBytes);
    }
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Control +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DTAPI_RX_FIFO_OVF takes the burst FIFO's count as it is now.
//
static DtapiResult ClearFlags(DtRx* Rx, int Flags)
{
    DtAsiRx* Asi = (DtAsiRx*)Rx;
    if ((Flags & DTAPI_RX_FIFO_OVF) != 0)
    {
        DtapiResult Result = DtPcieCmd_BurstFifoGetOvfUflCount(
            Asi->Drv, Asi->BurstFifo, &Asi->LastBurstFifoOvfCount);
        if (Result != DTAPI_OK)
            return Result;
        Asi->BurstFifoOvf = Asi->BurstFifoOvfLatched = false;
        Asi->FifoOvf = Asi->FifoOvfLatched = false;
    }
    DtTsTrp_ClearFlags(&Asi->Scan, Flags);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Start -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// To RCV: the buffer empty, DTAPI_RX_FIFO_OVF cleared, and CDMAC, the burst FIFO and
// ASIRX running. A start that fails leaves everything idle.
//
static DtapiResult Start(DtAsiRx* Asi)
{
    OsDrv* Drv = Asi->Drv;

    ResetScan(Asi);
    DtapiResult Result = ClearFlags(&Asi->Rx, DTAPI_RX_FIFO_OVF);
    if (Result != DTAPI_OK)
        return Result;
    DtTsTrp_Start(&Asi->Scan, Asi->Rx.RxMode);
    DtTsTrp_Start(&Asi->DeliverConverter, Asi->Rx.RxMode);

    Result = DtPcieCmd_CdmacSetRxReadOffset(Drv, Asi->Cdmac, 0);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacSetOpMode(Drv, Asi->Cdmac, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacClearReorderBufMinMax(Drv, Asi->Cdmac);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoSetOpMode(Drv, Asi->BurstFifo, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoClearMax(Drv, Asi->BurstFifo, true, true);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_AsiRxSetOpMode(Drv, Asi->AsiRx, DT_FUNC_OPMODE_RUN);
    if (Result != DTAPI_OK)
    {
        DtPcieCmd_AsiRxSetOpMode(Drv, Asi->AsiRx, DT_FUNC_OPMODE_IDLE);
        DtPcieCmd_BurstFifoSetOpMode(Drv, Asi->BurstFifo, DT_BLOCK_OPMODE_IDLE);
        DtPcieCmd_CdmacSetOpMode(Drv, Asi->Cdmac, DT_BLOCK_OPMODE_IDLE);
        return Result;
    }
    Asi->Receiving = true;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Stop -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// To IDLE: ASIRX, the burst FIFO and CDMAC stopped in that order, CDMAC flushed, the
// buffer empty and DTAPI_RX_FIFO_OVF cleared. Every step is taken whatever the one before
// gave; the first failure is returned.
//
static DtapiResult Stop(DtAsiRx* Asi)
{
    OsDrv* Drv = Asi->Drv;
    DtapiResult Results[5];

    Results[0] = DtPcieCmd_AsiRxSetOpMode(Drv, Asi->AsiRx, DT_FUNC_OPMODE_IDLE);
    Results[1] = DtPcieCmd_BurstFifoSetOpMode(Drv, Asi->BurstFifo, DT_BLOCK_OPMODE_IDLE);
    Results[2] = DtPcieCmd_CdmacSetOpMode(Drv, Asi->Cdmac, DT_BLOCK_OPMODE_IDLE);
    Results[3] = DtPcieCmd_CdmacIssueChannelFlush(Drv, Asi->Cdmac);
    Asi->Receiving = false;
    ResetScan(Asi);
    Results[4] = ClearFlags(&Asi->Rx, DTAPI_RX_FIFO_OVF);
    for (size_t i = 0; i < sizeof(Results) / sizeof(Results[0]); i++)
    {
        if (Results[i] != DTAPI_OK)
            return Results[i];
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetRxControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Takes IDLE and RCV, and is idle after a stop that failed.
//
static DtapiResult SetRxControl(DtRx* Rx, int RxControl)
{
    DtAsiRx* Asi = (DtAsiRx*)Rx;

    if (Rx->RxControl == RxControl)
        return DTAPI_OK;
    if (RxControl == DTAPI_RXCTRL_IDLE)
    {
        Rx->RxControl = RxControl;
        return Stop(Asi);
    }
    if (RxControl != DTAPI_RXCTRL_RCV)
        return DTAPI_E_INVALID_ARG;
    DtapiResult Result = Start(Asi);
    if (Result == DTAPI_OK)
        Rx->RxControl = RxControl;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetRxMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// One of the ASI modes while idle, and ASIRX's packet mode for it, raw for
// DTAPI_RXMODE_STRAW.
//
static DtapiResult SetRxMode(DtRx* Rx, int RxMode)
{
    DtAsiRx* Asi = (DtAsiRx*)Rx;

    DtapiResult Result = DtTsTrp_CheckMode(RxMode);
    if (Result != DTAPI_OK)
        return Result;
    if (Rx->RxControl != DTAPI_RXCTRL_IDLE)
        return DTAPI_E_NOT_IDLE;

    const int PacketMode = (RxMode & DTAPI_RXMODE_TS_MASK) == DTAPI_RXMODE_STRAW
                               ? DT_ASIRX_PCKMODE_RAW
                               : DT_ASIRX_PCKMODE_AUTO;
    Result = DtPcieCmd_AsiRxSetPacketMode(Asi->Drv, Asi->AsiRx, PacketMode);
    if (Result == DTAPI_OK)
        Rx->RxMode = RxMode;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Stops, and clears DTAPI_RX_FIFO_OVF.
//
static DtapiResult ClearFifo(DtRx* Rx)
{
    DtapiResult Result = SetRxControl(Rx, DTAPI_RXCTRL_IDLE);
    if (Result != DTAPI_OK)
        return Result;
    return ClearFlags(Rx, DTAPI_RX_FIFO_OVF);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The scan, the burst FIFO's flag, and the converter's.
//
static DtapiResult GetFlags(DtRx* Rx, int* Flags, int* Latched)
{
    DtAsiRx* Asi = (DtAsiRx*)Rx;

    *Flags = *Latched = 0;
    DtapiResult Result = ScanBuffer(Asi);
    if (Result == DTAPI_OK)
        Result = UpdateOvf(Asi);
    if (Result != DTAPI_OK)
        return Result;

    DtTsTrp_GetFlags(&Asi->Scan, Flags, Latched);
    if (Asi->BurstFifoOvf || Asi->FifoOvf)
        *Flags |= DTAPI_RX_FIFO_OVF;
    if (Asi->BurstFifoOvfLatched || Asi->FifoOvfLatched)
        *Latched |= DTAPI_RX_FIFO_OVF;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetDeliverableBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtapiResult GetDeliverableBytes(DtRx* Rx, size_t* Load)
{
    DtAsiRx* Asi = (DtAsiRx*)Rx;

    *Load = 0;
    if (!Asi->Receiving)
        return DTAPI_OK;
    DtapiResult Result = ScanBuffer(Asi);
    if (Result == DTAPI_OK)
        *Load = Asi->DeliverableBytes;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetFifoLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// What a read would deliver, while receiving.
//
static DtapiResult GetFifoLoad(DtRx* Rx, int* FifoLoad)
{
    size_t Load = 0;
    DtapiResult Result = DTAPI_OK;

    if (Rx->RxControl == DTAPI_RXCTRL_RCV)
        Result = GetDeliverableBytes(Rx, &Load);
    *FifoLoad = (int)Load;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetMaxFifoSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult GetMaxFifoSize(DtRx* Rx, int* MaxFifoSize)
{
    (void)Rx;
    *MaxFifoSize = DT_ASIRX_FIFO_SIZE;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ApplyIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Applies the receive mode again, which sets ASIRX's packet mode.
//
static DtapiResult ApplyIoConfig(DtRx* Rx, const DtIoConfig* Config)
{
    (void)Config;
    return SetRxMode(Rx, Rx->RxMode);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DeliverBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Decodes the packets the scan counted, passing over what it passed over, into Out, or
// into Pending when Out has less room than the largest packet's output.
//
static DtapiResult DeliverBytes(DtRx* Rx, uint8_t* Out, size_t Size)
{
    DtAsiRx* Asi = (DtAsiRx*)Rx;

    if (Size > Asi->DeliverableBytes)
        return DTAPI_E_INTERNAL;

    size_t Delivered = 0, Consumed = 0;
    while (Delivered < Size)
    {
        if (Asi->PendingPos < Asi->PendingLen)
        {
            size_t OutputBytes = (size_t)(Asi->PendingLen - Asi->PendingPos);
            if (OutputBytes > Size - Delivered)
                OutputBytes = Size - Delivered;
            memcpy(Out + Delivered, Asi->Pending + Asi->PendingPos, OutputBytes);
            Asi->PendingPos += (int)OutputBytes;
            Delivered += OutputBytes;
            continue;
        }

        if (Asi->NextSkipIndex < DtVec_Count(&Asi->Skips))
        {
            const DtAsiRxSkip* Next =
                (const DtAsiRxSkip*)DtVec_At(&Asi->Skips, Asi->NextSkipIndex);
            if (Next->Position == Asi->ReadPosition + Consumed)
            {
                Consumed += Next->Bytes;
                Asi->NextSkipIndex++;
                continue;
            }
        }
        if (Consumed + DT_TRP_SIZE > Asi->ScannedBytes)
            return DTAPI_E_INTERNAL;

        uint8_t Copy[DT_TRP_SIZE];
        const bool ToCaller = Size - Delivered >= DT_TRP_MAX_OUTPUT;
        const int OutputBytes =
            DtTsTrp_Decode(&Asi->DeliverConverter, PacketAt(Asi, Consumed, Copy),
                           ToCaller ? Out + Delivered : Asi->Pending);
        Consumed += DT_TRP_SIZE;
        if (OutputBytes > 0 && ToCaller)
            Delivered += (size_t)OutputBytes;
        else if (OutputBytes > 0)
        {
            Asi->PendingPos = 0;
            Asi->PendingLen = OutputBytes;
        }
    }
    Asi->DeliverableBytes -= Size;
    return AdvanceReadOffset(Asi, Consumed);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrepareWait -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// With no event to wait on, a read looks again every 5 ms.
//
static void PrepareWait(DtRx* Rx, DtRxWaitState* State)
{
    memset(State, 0, sizeof(*State));
    State->Backend = Rx->Backend;
    State->MaxMs = DT_ASIRX_READ_POLL_MS;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Wait -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult Wait(DtRxWaitState* State, int Ms)
{
    (void)State;
    OsTime_SleepMs(Ms);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AfterWait -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtapiResult AfterWait(DtRx* Rx, const DtRxWaitState* State)
{
    (void)Rx;
    (void)State;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Status +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetTsRateBps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The rate on the wire, given as the rate of 188-byte packets when the receiver found
// 204-byte ones, except in DTAPI_RXMODE_STRAW.
//
static DtapiResult GetTsRateBps(DtRx* Rx, int* TsRate)
{
    DtAsiRx* Asi = (DtAsiRx*)Rx;

    *TsRate = 0;
    int Rate = 0;
    DtapiResult Result = DtPcieCmd_AsiRxGetTsBitrate(Asi->Drv, Asi->AsiRx, &Rate);
    if (Result != DTAPI_OK)
        return Result;
    if ((Rx->RxMode & DTAPI_RXMODE_TS_MASK) != DTAPI_RXMODE_STRAW)
    {
        DtAsiRxStatus Status;
        Result = DtPcieCmd_AsiRxGetStatus(Asi->Drv, Asi->AsiRx, &Status);
        if (Result != DTAPI_OK)
            return Result;
        if (Status.PacketSize == DT_ASIRX_PCKSIZE_204)
            Rate = (int)((int64_t)Rate * 188 / 204);
    }
    *TsRate = Rate;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The receiver's values as the public ones, and the rate as good above 900 bit/s.
//
static DtapiResult GetStatus(DtRx* Rx, int* PacketSize, int* NumInv, int* ClkDet,
                             int* AsiLock, int* RateOk, int* AsiInv)
{
    DtAsiRx* Asi = (DtAsiRx*)Rx;
    DtAsiRxStatus Status;

    *NumInv = DTAPI_NOT_SUPPORTED;
    *PacketSize = DTAPI_PCKSIZE_INV;
    *ClkDet = DTAPI_CLKDET_FAIL;
    *AsiLock = 0;
    *RateOk = DTAPI_INPRATE_LOW;
    *AsiInv = DTAPI_NOT_SUPPORTED;
    DtapiResult Result = DtPcieCmd_AsiRxGetStatus(Asi->Drv, Asi->AsiRx, &Status);
    if (Result != DTAPI_OK)
        return Result;

    *PacketSize = Status.PacketSize == DT_ASIRX_PCKSIZE_188   ? DTAPI_PCKSIZE_188
                  : Status.PacketSize == DT_ASIRX_PCKSIZE_204 ? DTAPI_PCKSIZE_204
                                                              : DTAPI_PCKSIZE_INV;
    *ClkDet = Status.CarrierDetect ? DTAPI_CLKDET_OK : DTAPI_CLKDET_FAIL;
    *AsiLock = Status.AsiLock ? DTAPI_ASI_INLOCK : 0;
    *AsiInv = Status.Polarity == DT_ASIRX_POLARITY_NORMAL   ? DTAPI_ASIINV_NORMAL
              : Status.Polarity == DT_ASIRX_POLARITY_INVERT ? DTAPI_ASIINV_INVERT
                                                            : DTAPI_NOT_SUPPORTED;

    int TsRate = 0;
    if (Status.AsiLock)
        Result = GetTsRateBps(Rx, &TsRate);
    *RateOk = TsRate > 900 ? DTAPI_INPRATE_OK : DTAPI_INPRATE_LOW;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetViolCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult GetViolCount(DtRx* Rx, int* ViolCount)
{
    DtAsiRx* Asi = (DtAsiRx*)Rx;
    return DtPcieCmd_AsiRxGetViolCount(Asi->Drv, Asi->AsiRx, ViolCount);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PolarityControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The public polarity values are the driver's.
//
static DtapiResult PolarityControl(DtRx* Rx, int Polarity)
{
    DtAsiRx* Asi = (DtAsiRx*)Rx;
    return DtPcieCmd_AsiRxSetPolarityCtrl(Asi->Drv, Asi->AsiRx, Polarity);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Lifetime +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Release -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Release(DtRx* Rx)
{
    DtAsiRx* Asi = (DtAsiRx*)Rx;

    if (Asi->Receiving)
        Stop(Asi);
    if (Asi->BufferRegistered)
    {
        DtPcieCmd_CdmacSetOpMode(Asi->Drv, Asi->Cdmac, DT_BLOCK_OPMODE_IDLE);
        DtPcieCmd_CdmacFreeBuffer(Asi->Drv, Asi->Cdmac);
    }
    OsDmaBuffer_Free(&Asi->DmaBuffer);
    if (Asi->HasExclusiveAccess)
    {
        DtFunc_ExclAccess(Asi->Drv, &Asi->RxFunction, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
        DtFunc_ExclAccess(Asi->Drv, &Asi->DmaFunction, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
    }
    DtFunc_Release(&Asi->RxFunction);
    DtFunc_Release(&Asi->DmaFunction);
    DtVec_Free(&Asi->Skips);
    DtAlloc_Free(Asi->SearchBuffer);
    DtAlloc_Free(Asi);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindDriverBlocks -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// ASIRX of AF_ASISDIRX, CDMAC and BURSTFIFO of AF_DMA, and whether the driver is new
// enough for each.
//
static DtapiResult FindDriverBlocks(DtAsiRx* Asi, const DtDriverVersion* Version)
{
    typedef struct
    {
        DtFuncInstance* Instance;
        bool IsDriverFunction;
        int Type;
        DtDrvObject* Ref;
    } DriverBlockSpec;
    const DriverBlockSpec Objects[] = {
        {&Asi->RxFunction, true, DT_FUNC_TYPE_ASIRX, &Asi->AsiRx},
        {&Asi->DmaFunction, false, DT_BLOCK_TYPE_CDMAC, &Asi->Cdmac},
        {&Asi->DmaFunction, false, DT_BLOCK_TYPE_BURSTFIFO, &Asi->BurstFifo},
    };

    DtapiResult Result =
        DtFunc_Find(Asi->Drv, Asi->PortIndex, "AF_ASISDIRX", "", &Asi->RxFunction);
    if (Result == DTAPI_OK)
        Result = DtFunc_Find(Asi->Drv, Asi->PortIndex, "AF_DMA", "", &Asi->DmaFunction);
    for (size_t i = 0; i < sizeof(Objects) / sizeof(Objects[0]) && Result == DTAPI_OK;
         i++)
    {
        const DtFuncObject* Object = DtFunc_Get(
            Objects[i].Instance, Objects[i].IsDriverFunction, Objects[i].Type, "");
        if (Object == NULL)
            Result = DTAPI_E_NOT_FOUND;
        else
        {
            *Objects[i].Ref = Object->Ref;
            Result = DtFunc_CheckDriverVersion(Version, Objects[i].IsDriverFunction,
                                               Objects[i].Type);
        }
    }
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RegisterBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// CDMAC idle, a receive buffer of whole pages times the prefetch size, registered, and
// the test mode off. One data word stays free.
//
static DtapiResult RegisterBuffer(DtAsiRx* Asi)
{
    OsDrv* Drv = Asi->Drv;
    DtCdmacProps Props;

    memset(&Props, 0, sizeof(Props));
    DtapiResult Result = DtPcieCmd_CdmacSetOpMode(Drv, Asi->Cdmac, DT_BLOCK_OPMODE_IDLE);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacGetProps(Drv, Asi->Cdmac, &Props);
    if (Result == DTAPI_OK && (Props.Caps & DT_CDMAC_CAP_RX) == 0)
        Result = DTAPI_E_NOT_SUPPORTED;
    if (Result == DTAPI_OK && (Props.PrefetchSize <= 0 || Props.PcieDataWidth <= 0 ||
                               Props.PcieDataWidth % 32 != 0))
        Result = DTAPI_E_DEV_DRIVER;
    if (Result != DTAPI_OK)
        return Result;

    const size_t Unit = (size_t)DT_ASIRX_PAGE_SIZE * (size_t)Props.PrefetchSize;
    const size_t Size = (DT_ASIRX_RING_SIZE + Unit - 1) / Unit * Unit;
    if (OsDmaBuffer_Alloc(Size, &Asi->DmaBuffer) != 0)
        return DTAPI_E_OUT_OF_MEM;
    Result =
        DtPcieCmd_CdmacAllocateBuffer(Drv, Asi->Cdmac, DT_CDMAC_DIR_RX, &Asi->DmaBuffer);
    Asi->BufferRegistered = Result == DTAPI_OK;
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacSetTestMode(Drv, Asi->Cdmac, DT_CDMAC_TESTMODE_NORMAL);
    if (Result == DTAPI_OK && DtRing_Init(&Asi->Ring, Asi->DmaBuffer.Data, Size,
                                          (size_t)Props.PcieDataWidth / 8) != 0)
        Result = DTAPI_E_INTERNAL;
    return Result;
}

static const DtRxBackend g_AsiRxBackend = {
    .Release = Release,
    .SetRxMode = SetRxMode,
    .SetRxControl = SetRxControl,
    .ClearFifo = ClearFifo,
    .ClearFlags = ClearFlags,
    .GetFlags = GetFlags,
    .GetFifoLoad = GetFifoLoad,
    .GetMaxFifoSize = GetMaxFifoSize,
    .ApplyIoConfig = ApplyIoConfig,
    .PrepareWait = PrepareWait,
    .Wait = Wait,
    .AfterWait = AfterWait,
    .GetDeliverableBytes = GetDeliverableBytes,
    .DeliverBytes = DeliverBytes,
    .GetStatus = GetStatus,
    .GetTsRateBps = GetTsRateBps,
    .GetViolCount = GetViolCount,
    .PolarityControl = PolarityControl,
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAsiRx_Attach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtAsiRx_Attach(const DtRxAttachedPort* Port, DtRx** Rx)
{
    *Rx = NULL;
    DtAsiRx* Asi = (DtAsiRx*)DtAlloc_Malloc(sizeof(DtAsiRx));
    if (Asi == NULL)
        return DTAPI_E_OUT_OF_MEM;
    memset(Asi, 0, sizeof(*Asi));
    Asi->Rx.Backend = &g_AsiRxBackend;
    Asi->Rx.IsAsi = true;
    Asi->Rx.Port = *Port;
    Asi->Rx.RxMode = DTAPI_RXMODE_ST188;
    Asi->Rx.RxControl = DTAPI_RXCTRL_IDLE;
    OsDrv* Drv = Asi->Drv = Port->Device->Drv;
    Asi->PortIndex = Port->Port - 1;
    DtVec_Init(&Asi->RxFunction.Objects, sizeof(DtFuncObject));
    DtVec_Init(&Asi->DmaFunction.Objects, sizeof(DtFuncObject));
    DtVec_Init(&Asi->Skips, sizeof(DtAsiRxSkip));

    DtapiResult Result = FindDriverBlocks(Asi, &Port->Device->DriverVersion);
    if (Result == DTAPI_OK)
        Result =
            DtFunc_ExclAccess(Drv, &Asi->RxFunction, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE);
    if (Result == DTAPI_OK)
    {
        Result =
            DtFunc_ExclAccess(Drv, &Asi->DmaFunction, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE);
        if (Result != DTAPI_OK)
            DtFunc_ExclAccess(Drv, &Asi->RxFunction, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
    }
    Asi->HasExclusiveAccess = Result == DTAPI_OK;

    // Everything idle and CDMAC flushed, before the buffer is registered.
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_AsiRxSetOpMode(Drv, Asi->AsiRx, DT_FUNC_OPMODE_IDLE);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoSetOpMode(Drv, Asi->BurstFifo, DT_BLOCK_OPMODE_IDLE);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacIssueChannelFlush(Drv, Asi->Cdmac);
    if (Result == DTAPI_OK)
        Result = RegisterBuffer(Asi);
    if (Result == DTAPI_OK)
    {
        Asi->SearchBuffer = (uint8_t*)DtAlloc_Malloc(DT_ASIRX_SEARCH_SIZE);
        if (Asi->SearchBuffer == NULL)
            Result = DTAPI_E_OUT_OF_MEM;
    }

    // The receiver's defaults and cleared flags.
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_AsiRxSetPolarityCtrl(Drv, Asi->AsiRx, DT_ASIRX_POLARITY_AUTO);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_AsiRxSetSyncMode(Drv, Asi->AsiRx, DT_ASIRX_SYNCMODE_AUTO);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_AsiRxSetPacketMode(Drv, Asi->AsiRx, DT_ASIRX_PCKMODE_AUTO);
    if (Result == DTAPI_OK)
        Result = ClearFlags(&Asi->Rx, -1);

    if (Result != DTAPI_OK)
    {
        Release(&Asi->Rx);
        return Result;
    }
    *Rx = &Asi->Rx;
    return DTAPI_OK;
}
