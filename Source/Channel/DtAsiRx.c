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
#define DT_ASIRX_PAGE 4096

// How often a read looks for more.
#define DT_ASIRX_POLL_MS 5

// Bytes a scan passed over, which a take passes over too: a packet dropped for want of
// room, or bytes searched for the stream.
typedef struct DtAsiRxSkip
{
    uint64_t At; // Counted as DtAsiRx.ReadAt
    size_t Bytes;
} DtAsiRxSkip;

typedef struct DtAsiRx
{
    DtRx Base;
    OsDrv* Drv;
    int PortIndex;
    DtFuncInstance AfRx, AfDma;
    bool Held; // Exclusive access to both
    DtPartRef AsiRx, Cdmac, Burst;
    OsDmaBuffer Buf;
    bool Registered;
    DtRing Ring;
    bool Receiving;

    // What is scanned: bytes past the read offset, and whether the stream is found.
    DtTsTrp Scan, Take;
    bool OutOfSync;
    size_t Scanned;
    size_t Load;     // Bytes to deliver, Pending's included
    uint64_t ReadAt; // Bytes read from the buffer since receiving started
    DtVec Skips;     // DtAsiRxSkip, in order
    size_t SkipHead;
    uint8_t* Search; // Where the stream is searched for

    // Output of a packet that did not fit in the caller's buffer.
    uint8_t Pending[DT_TRP_MAX_OUTPUT];
    int PendingPos, PendingLen;

    // DTAPI_RX_FIFO_OVF for packets dropped, and for the burst FIFO's count moving.
    bool FifoOvf, FifoOvfLatched;
    bool BurstOvf, BurstOvfLatched;
    uint32_t OvfCount;
} DtAsiRx;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Buffer +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Empty -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Forgets what was scanned and kept, with the buffer starting again at its beginning.
//
static void Empty(DtAsiRx* Rx)
{
    if (Rx->Ring.Base != NULL)
        DtRing_Restart(&Rx->Ring, 0);
    Rx->Scanned = Rx->Load = 0;
    Rx->ReadAt = 0;
    DtVec_Clear(&Rx->Skips);
    Rx->SkipHead = 0;
    Rx->PendingPos = Rx->PendingLen = 0;
    Rx->OutOfSync = false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PacketAt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The transparent packet Offset bytes past the read offset, in place or copied into Copy
// when it runs across the end of the buffer.
//
static const uint8_t* PacketAt(const DtAsiRx* Rx, size_t Offset, uint8_t* Copy)
{
    const uint8_t* P = DtRing_Span(&Rx->Ring, Offset, DT_TRP_SIZE);
    if (P == NULL)
    {
        DtRing_PeekAt(&Rx->Ring, Offset, Copy, DT_TRP_SIZE);
        P = Copy;
    }
    return P;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Advance -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Moves the read offset on by Bytes, which were scanned, and tells the driver.
//
static DtapiResult Advance(DtAsiRx* Rx, size_t Bytes)
{
    if (Bytes == 0)
        return DTAPI_OK;
    if (Bytes > Rx->Scanned || DtRing_Skip(&Rx->Ring, Bytes) != 0)
        return DTAPI_E_INTERNAL;
    Rx->Scanned -= Bytes;
    Rx->ReadAt += Bytes;
    if (Rx->SkipHead == DtVec_Count(&Rx->Skips))
    {
        DtVec_Clear(&Rx->Skips);
        Rx->SkipHead = 0;
    }
    return DtPcieCmd_CdmacSetRxReadOffset(Rx->Drv, Rx->Cdmac,
                                          (uint32_t)DtRing_ReadOffset(&Rx->Ring));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Skip -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Passes over Bytes after what was scanned, noting them for the take.
//
static DtapiResult Skip(DtAsiRx* Rx, size_t Bytes)
{
    const uint64_t At = Rx->ReadAt + Rx->Scanned;
    const size_t Count = DtVec_Count(&Rx->Skips);
    DtAsiRxSkip* Last =
        Count > Rx->SkipHead ? (DtAsiRxSkip*)DtVec_At(&Rx->Skips, Count - 1) : NULL;

    if (Last != NULL && Last->At + Last->Bytes == At)
        Last->Bytes += Bytes;
    else
    {
        DtAsiRxSkip New = {At, Bytes};
        if (DtVec_Push(&Rx->Skips, &New) != 0)
            return DTAPI_E_OUT_OF_MEM;
    }
    Rx->Scanned += Bytes;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- UpdateBurst -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// An overflow while the burst FIFO's count moved since the last look.
//
static DtapiResult UpdateBurst(DtAsiRx* Rx)
{
    uint32_t Count = 0;
    DtapiResult Result = DtPcieCmd_BurstFifoGetOvfUflCount(Rx->Drv, Rx->Burst, &Count);
    if (Result != DTAPI_OK)
        return Result;
    Rx->BurstOvf = Count != Rx->OvfCount;
    Rx->BurstOvfLatched |= Rx->BurstOvf;
    Rx->OvfCount = Count;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ScanBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Walks what the card wrote since the last scan, converting it. In sync, each packet is
// converted to count its output; out of sync, the stream is searched for in what three
// packets or more fill, and what was searched without finding it is passed over but for
// the last three packets' worth. When nothing is left to deliver, what was scanned is
// released at once, so that a stream the application does not want does not fill the
// buffer.
//
static DtapiResult ScanBuffer(DtAsiRx* Rx)
{
    if (!Rx->Receiving)
        return DTAPI_OK;

    uint32_t WriteOffset = 0;
    DtapiResult Result =
        DtPcieCmd_CdmacGetRxWriteOffset(Rx->Drv, Rx->Cdmac, &WriteOffset);
    if (Result != DTAPI_OK)
        return Result;
    if (WriteOffset >= Rx->Ring.Size ||
        DtRing_SetWriteOffset(&Rx->Ring, WriteOffset) != 0)
        return DTAPI_E_DEV_DRIVER;

    while (Result == DTAPI_OK)
    {
        const size_t Available = DtRing_Load(&Rx->Ring) - Rx->Scanned;
        if (!Rx->OutOfSync)
        {
            if (Available < DT_TRP_SIZE)
                break;
            uint8_t Copy[DT_TRP_SIZE];
            const int n =
                DtTsTrp_Convert(&Rx->Scan, PacketAt(Rx, Rx->Scanned, Copy), NULL);
            if (n < 0)
                Rx->OutOfSync = true;
            else if (Rx->Load + (size_t)n > DT_ASIRX_FIFO_SIZE)
            {
                Rx->FifoOvf = Rx->FifoOvfLatched = true;
                Result = Skip(Rx, DT_TRP_SIZE);
            }
            else
            {
                Rx->Load += (size_t)n;
                Rx->Scanned += DT_TRP_SIZE;
            }
        }
        else
        {
            if (Available < (size_t)DT_TRP_SIZE * DT_TRP_NUM_SYNC)
                break;
            size_t Size =
                Available < DT_ASIRX_SEARCH_SIZE ? Available : DT_ASIRX_SEARCH_SIZE;
            DtRing_PeekAt(&Rx->Ring, Rx->Scanned, Rx->Search, Size);
            size_t Offset = 0;
            if (DtTsTrp_FindSync(&Rx->Scan, Rx->Search, Size, &Offset))
                Rx->OutOfSync = false;
            else
                Offset = Size - (size_t)DT_TRP_SIZE * DT_TRP_NUM_SYNC + 1;
            if (Offset > 0)
                Result = Skip(Rx, Offset);
        }
    }

    if (Result == DTAPI_OK && Rx->Load == 0)
    {
        Rx->SkipHead = DtVec_Count(&Rx->Skips);
        Result = Advance(Rx, Rx->Scanned);
    }
    if (Result == DTAPI_OK)
        Result = UpdateBurst(Rx);
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Control +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DTAPI_RX_FIFO_OVF takes the burst FIFO's count as it is now.
//
static DtapiResult ClearFlags(DtRx* Base, int Flags)
{
    DtAsiRx* Rx = (DtAsiRx*)Base;
    if ((Flags & DTAPI_RX_FIFO_OVF) != 0)
    {
        DtapiResult Result =
            DtPcieCmd_BurstFifoGetOvfUflCount(Rx->Drv, Rx->Burst, &Rx->OvfCount);
        if (Result != DTAPI_OK)
            return Result;
        Rx->BurstOvf = Rx->BurstOvfLatched = false;
        Rx->FifoOvf = Rx->FifoOvfLatched = false;
    }
    DtTsTrp_ClearFlags(&Rx->Scan, Flags);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Start -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// To RCV: the buffer empty, DTAPI_RX_FIFO_OVF cleared, and CDMAC, the burst FIFO and
// ASIRX running. A start that fails leaves everything idle.
//
static DtapiResult Start(DtAsiRx* Rx)
{
    OsDrv* Drv = Rx->Drv;

    Empty(Rx);
    DtapiResult Result = ClearFlags(&Rx->Base, DTAPI_RX_FIFO_OVF);
    if (Result != DTAPI_OK)
        return Result;
    DtTsTrp_Start(&Rx->Scan, Rx->Base.RxMode);
    DtTsTrp_Start(&Rx->Take, Rx->Base.RxMode);

    Result = DtPcieCmd_CdmacSetRxReadOffset(Drv, Rx->Cdmac, 0);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacSetOpMode(Drv, Rx->Cdmac, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacClearReorderBufMinMax(Drv, Rx->Cdmac);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoSetOpMode(Drv, Rx->Burst, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoClearMax(Drv, Rx->Burst, true, true);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_AsiRxSetOpMode(Drv, Rx->AsiRx, DT_FUNC_OPMODE_RUN);
    if (Result != DTAPI_OK)
    {
        DtPcieCmd_AsiRxSetOpMode(Drv, Rx->AsiRx, DT_FUNC_OPMODE_IDLE);
        DtPcieCmd_BurstFifoSetOpMode(Drv, Rx->Burst, DT_BLOCK_OPMODE_IDLE);
        DtPcieCmd_CdmacSetOpMode(Drv, Rx->Cdmac, DT_BLOCK_OPMODE_IDLE);
        return Result;
    }
    Rx->Receiving = true;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Stop -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// To IDLE: ASIRX, the burst FIFO and CDMAC stopped in that order, CDMAC flushed, the
// buffer empty and DTAPI_RX_FIFO_OVF cleared. Every step is taken whatever the one before
// gave; the first failure is returned.
//
static DtapiResult Stop(DtAsiRx* Rx)
{
    OsDrv* Drv = Rx->Drv;
    DtapiResult Results[5];

    Results[0] = DtPcieCmd_AsiRxSetOpMode(Drv, Rx->AsiRx, DT_FUNC_OPMODE_IDLE);
    Results[1] = DtPcieCmd_BurstFifoSetOpMode(Drv, Rx->Burst, DT_BLOCK_OPMODE_IDLE);
    Results[2] = DtPcieCmd_CdmacSetOpMode(Drv, Rx->Cdmac, DT_BLOCK_OPMODE_IDLE);
    Results[3] = DtPcieCmd_CdmacIssueChannelFlush(Drv, Rx->Cdmac);
    Rx->Receiving = false;
    Empty(Rx);
    Results[4] = ClearFlags(&Rx->Base, DTAPI_RX_FIFO_OVF);
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
static DtapiResult SetRxControl(DtRx* Base, int RxControl)
{
    DtAsiRx* Rx = (DtAsiRx*)Base;

    if (Base->RxControl == RxControl)
        return DTAPI_OK;
    if (RxControl == DTAPI_RXCTRL_IDLE)
    {
        Base->RxControl = RxControl;
        return Stop(Rx);
    }
    if (RxControl != DTAPI_RXCTRL_RCV)
        return DTAPI_E_INVALID_ARG;
    DtapiResult Result = Start(Rx);
    if (Result == DTAPI_OK)
        Base->RxControl = RxControl;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetRxMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// One of the ASI modes while idle, and ASIRX's packet mode for it, raw for
// DTAPI_RXMODE_STRAW.
//
static DtapiResult SetRxMode(DtRx* Base, int RxMode)
{
    DtAsiRx* Rx = (DtAsiRx*)Base;

    DtapiResult Result = DtTsTrp_CheckMode(RxMode);
    if (Result != DTAPI_OK)
        return Result;
    if (Base->RxControl != DTAPI_RXCTRL_IDLE)
        return DTAPI_E_NOT_IDLE;

    const int PacketMode = (RxMode & DTAPI_RXMODE_TS_MASK) == DTAPI_RXMODE_STRAW
                               ? DT_ASIRX_PCKMODE_RAW
                               : DT_ASIRX_PCKMODE_AUTO;
    Result = DtPcieCmd_AsiRxSetPacketMode(Rx->Drv, Rx->AsiRx, PacketMode);
    if (Result == DTAPI_OK)
        Base->RxMode = RxMode;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Stops, and clears DTAPI_RX_FIFO_OVF.
//
static DtapiResult ClearFifo(DtRx* Base)
{
    DtapiResult Result = SetRxControl(Base, DTAPI_RXCTRL_IDLE);
    if (Result != DTAPI_OK)
        return Result;
    return ClearFlags(Base, DTAPI_RX_FIFO_OVF);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The scan, the burst FIFO's flag, and the converter's.
//
static DtapiResult GetFlags(DtRx* Base, int* Flags, int* Latched)
{
    DtAsiRx* Rx = (DtAsiRx*)Base;

    *Flags = *Latched = 0;
    DtapiResult Result = ScanBuffer(Rx);
    if (Result == DTAPI_OK)
        Result = UpdateBurst(Rx);
    if (Result != DTAPI_OK)
        return Result;

    DtTsTrp_GetFlags(&Rx->Scan, Flags, Latched);
    if (Rx->BurstOvf || Rx->FifoOvf)
        *Flags |= DTAPI_RX_FIFO_OVF;
    if (Rx->BurstOvfLatched || Rx->FifoOvfLatched)
        *Latched |= DTAPI_RX_FIFO_OVF;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtapiResult GetLoad(DtRx* Base, size_t* Load)
{
    DtAsiRx* Rx = (DtAsiRx*)Base;

    *Load = 0;
    if (!Rx->Receiving)
        return DTAPI_OK;
    DtapiResult Result = ScanBuffer(Rx);
    if (Result == DTAPI_OK)
        *Load = Rx->Load;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetFifoLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// What a read would deliver, while receiving.
//
static DtapiResult GetFifoLoad(DtRx* Base, int* FifoLoad)
{
    size_t Load = 0;
    DtapiResult Result = DTAPI_OK;

    if (Base->RxControl == DTAPI_RXCTRL_RCV)
        Result = GetLoad(Base, &Load);
    *FifoLoad = (int)Load;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetMaxFifoSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult GetMaxFifoSize(DtRx* Base, int* MaxFifoSize)
{
    (void)Base;
    *MaxFifoSize = DT_ASIRX_FIFO_SIZE;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ApplyIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Applies the receive mode again, which sets ASIRX's packet mode.
//
static DtapiResult ApplyIoConfig(DtRx* Base, const DtIoConfig* Config)
{
    (void)Config;
    return SetRxMode(Base, Base->RxMode);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Take -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Converts the packets the scan counted, passing over what it passed over, into Out, or
// into Pending for a packet whose output Out has no room for.
//
static DtapiResult Take(DtRx* Base, uint8_t* Out, size_t Size)
{
    DtAsiRx* Rx = (DtAsiRx*)Base;

    if (Size > Rx->Load)
        return DTAPI_E_INTERNAL;

    size_t Done = 0, Passed = 0;
    while (Done < Size)
    {
        if (Rx->PendingPos < Rx->PendingLen)
        {
            size_t n = (size_t)(Rx->PendingLen - Rx->PendingPos);
            if (n > Size - Done)
                n = Size - Done;
            memcpy(Out + Done, Rx->Pending + Rx->PendingPos, n);
            Rx->PendingPos += (int)n;
            Done += n;
            continue;
        }

        if (Rx->SkipHead < DtVec_Count(&Rx->Skips))
        {
            const DtAsiRxSkip* Next =
                (const DtAsiRxSkip*)DtVec_At(&Rx->Skips, Rx->SkipHead);
            if (Next->At == Rx->ReadAt + Passed)
            {
                Passed += Next->Bytes;
                Rx->SkipHead++;
                continue;
            }
        }
        if (Passed + DT_TRP_SIZE > Rx->Scanned)
            return DTAPI_E_INTERNAL;

        uint8_t Copy[DT_TRP_SIZE];
        const bool Direct = Size - Done >= DT_TRP_MAX_OUTPUT;
        const int n = DtTsTrp_Convert(&Rx->Take, PacketAt(Rx, Passed, Copy),
                                      Direct ? Out + Done : Rx->Pending);
        Passed += DT_TRP_SIZE;
        if (n > 0 && Direct)
            Done += (size_t)n;
        else if (n > 0)
        {
            Rx->PendingPos = 0;
            Rx->PendingLen = n;
        }
    }
    Rx->Load -= Size;
    return Advance(Rx, Passed);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrepareWait -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// With no event to wait on, a read looks again every 5 ms.
//
static void PrepareWait(DtRx* Base, DtRxWait* Wait)
{
    memset(Wait, 0, sizeof(*Wait));
    Wait->Ops = Base->Ops;
    Wait->MaxMs = DT_ASIRX_POLL_MS;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Wait -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult Wait(DtRxWait* Wait, int Ms)
{
    (void)Wait;
    OsTime_SleepMs(Ms);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AfterWait -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtapiResult AfterWait(DtRx* Base, const DtRxWait* Wait)
{
    (void)Base;
    (void)Wait;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Status +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetTsRateBps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The rate on the wire, as the rate of 188-byte packets when the receiver found 204-byte
// ones, but in DTAPI_RXMODE_STRAW.
//
static DtapiResult GetTsRateBps(DtRx* Base, int* TsRate)
{
    DtAsiRx* Rx = (DtAsiRx*)Base;

    *TsRate = 0;
    int Rate = 0;
    DtapiResult Result = DtPcieCmd_AsiRxGetTsBitrate(Rx->Drv, Rx->AsiRx, &Rate);
    if (Result != DTAPI_OK)
        return Result;
    if ((Base->RxMode & DTAPI_RXMODE_TS_MASK) != DTAPI_RXMODE_STRAW)
    {
        DtAsiRxStatus Status;
        Result = DtPcieCmd_AsiRxGetStatus(Rx->Drv, Rx->AsiRx, &Status);
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
static DtapiResult GetStatus(DtRx* Base, int* PacketSize, int* NumInv, int* ClkDet,
                             int* AsiLock, int* RateOk, int* AsiInv)
{
    DtAsiRx* Rx = (DtAsiRx*)Base;
    DtAsiRxStatus Status;

    *NumInv = DTAPI_NOT_SUPPORTED;
    *PacketSize = DTAPI_PCKSIZE_INV;
    *ClkDet = DTAPI_CLKDET_FAIL;
    *AsiLock = 0;
    *RateOk = DTAPI_INPRATE_LOW;
    *AsiInv = DTAPI_NOT_SUPPORTED;
    DtapiResult Result = DtPcieCmd_AsiRxGetStatus(Rx->Drv, Rx->AsiRx, &Status);
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
        Result = GetTsRateBps(Base, &TsRate);
    *RateOk = TsRate > 900 ? DTAPI_INPRATE_OK : DTAPI_INPRATE_LOW;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetViolCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult GetViolCount(DtRx* Base, int* ViolCount)
{
    DtAsiRx* Rx = (DtAsiRx*)Base;
    return DtPcieCmd_AsiRxGetViolCount(Rx->Drv, Rx->AsiRx, ViolCount);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PolarityControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The public polarity values are the driver's.
//
static DtapiResult PolarityControl(DtRx* Base, int Polarity)
{
    DtAsiRx* Rx = (DtAsiRx*)Base;
    return DtPcieCmd_AsiRxSetPolarityCtrl(Rx->Drv, Rx->AsiRx, Polarity);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Lifetime +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Release -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Release(DtRx* Base)
{
    DtAsiRx* Rx = (DtAsiRx*)Base;

    if (Rx->Receiving)
        Stop(Rx);
    if (Rx->Registered)
    {
        DtPcieCmd_CdmacSetOpMode(Rx->Drv, Rx->Cdmac, DT_BLOCK_OPMODE_IDLE);
        DtPcieCmd_CdmacFreeBuffer(Rx->Drv, Rx->Cdmac);
    }
    OsDmaBuffer_Free(&Rx->Buf);
    if (Rx->Held)
    {
        DtFunc_ExclAccess(Rx->Drv, &Rx->AfRx, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
        DtFunc_ExclAccess(Rx->Drv, &Rx->AfDma, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
    }
    DtFunc_Release(&Rx->AfRx);
    DtFunc_Release(&Rx->AfDma);
    DtVec_Free(&Rx->Skips);
    DtAlloc_Free(Rx->Search);
    DtAlloc_Free(Rx);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindParts -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// ASIRX of AF_ASISDIRX, CDMAC and BURSTFIFO of AF_DMA, and whether the driver is new
// enough for each.
//
static DtapiResult FindParts(DtAsiRx* Rx, const DtDriverVersion* Version)
{
    typedef struct
    {
        DtFuncInstance* Instance;
        bool IsDf;
        int Type;
        DtPartRef* Ref;
    } Wanted;
    const Wanted Parts[] = {
        {&Rx->AfRx, true, DT_FUNC_TYPE_ASIRX, &Rx->AsiRx},
        {&Rx->AfDma, false, DT_BLOCK_TYPE_CDMAC, &Rx->Cdmac},
        {&Rx->AfDma, false, DT_BLOCK_TYPE_BURSTFIFO, &Rx->Burst},
    };

    DtapiResult Result =
        DtFunc_Find(Rx->Drv, Rx->PortIndex, "AF_ASISDIRX", "", &Rx->AfRx);
    if (Result == DTAPI_OK)
        Result = DtFunc_Find(Rx->Drv, Rx->PortIndex, "AF_DMA", "", &Rx->AfDma);
    for (size_t i = 0; i < sizeof(Parts) / sizeof(Parts[0]) && Result == DTAPI_OK; i++)
    {
        const DtFuncPart* Part =
            DtFunc_Get(Parts[i].Instance, Parts[i].IsDf, Parts[i].Type, "");
        if (Part == NULL)
            Result = DTAPI_E_NOT_FOUND;
        else
        {
            *Parts[i].Ref = Part->Ref;
            Result = DtFunc_CheckDriverVersion(Version, Parts[i].IsDf, Parts[i].Type);
        }
    }
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RegisterBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// CDMAC idle, a receive buffer of whole pages times the prefetch size, registered, and
// the test mode off. One data word stays free.
//
static DtapiResult RegisterBuffer(DtAsiRx* Rx)
{
    OsDrv* Drv = Rx->Drv;
    DtCdmacProps Props;

    memset(&Props, 0, sizeof(Props));
    DtapiResult Result = DtPcieCmd_CdmacSetOpMode(Drv, Rx->Cdmac, DT_BLOCK_OPMODE_IDLE);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacGetProps(Drv, Rx->Cdmac, &Props);
    if (Result == DTAPI_OK && (Props.Caps & DT_CDMAC_CAP_RX) == 0)
        Result = DTAPI_E_NOT_SUPPORTED;
    if (Result == DTAPI_OK && (Props.PrefetchSize <= 0 || Props.PcieDataWidth <= 0 ||
                               Props.PcieDataWidth % 32 != 0))
        Result = DTAPI_E_DEV_DRIVER;
    if (Result != DTAPI_OK)
        return Result;

    const size_t Unit = (size_t)DT_ASIRX_PAGE * (size_t)Props.PrefetchSize;
    const size_t Size = (DT_ASIRX_RING_SIZE + Unit - 1) / Unit * Unit;
    if (OsDmaBuffer_Alloc(Size, &Rx->Buf) != 0)
        return DTAPI_E_OUT_OF_MEM;
    Result = DtPcieCmd_CdmacAllocateBuffer(Drv, Rx->Cdmac, DT_CDMAC_DIR_RX, &Rx->Buf);
    Rx->Registered = Result == DTAPI_OK;
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacSetTestMode(Drv, Rx->Cdmac, DT_CDMAC_TESTMODE_NORMAL);
    if (Result == DTAPI_OK &&
        DtRing_Init(&Rx->Ring, Rx->Buf.Data, Size, (size_t)Props.PcieDataWidth / 8) != 0)
        Result = DTAPI_E_INTERNAL;
    return Result;
}

static const DtRxBackend g_Ops = {
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
    .GetLoad = GetLoad,
    .Take = Take,
    .GetStatus = GetStatus,
    .GetTsRateBps = GetTsRateBps,
    .GetViolCount = GetViolCount,
    .PolarityControl = PolarityControl,
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAsiRx_Attach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtAsiRx_Attach(const DtRxPort* Port, DtRx** Out)
{
    *Out = NULL;
    DtAsiRx* Rx = (DtAsiRx*)DtAlloc_Malloc(sizeof(DtAsiRx));
    if (Rx == NULL)
        return DTAPI_E_OUT_OF_MEM;
    memset(Rx, 0, sizeof(*Rx));
    Rx->Base.Ops = &g_Ops;
    Rx->Base.Port = *Port;
    Rx->Base.RxMode = DTAPI_RXMODE_ST188;
    Rx->Base.RxControl = DTAPI_RXCTRL_IDLE;
    OsDrv* Drv = Rx->Drv = Port->Device->Drv;
    Rx->PortIndex = Port->PortIndex;
    DtVec_Init(&Rx->AfRx.Parts, sizeof(DtFuncPart));
    DtVec_Init(&Rx->AfDma.Parts, sizeof(DtFuncPart));
    DtVec_Init(&Rx->Skips, sizeof(DtAsiRxSkip));

    DtapiResult Result = FindParts(Rx, &Port->Device->DriverVersion);
    if (Result == DTAPI_OK)
        Result = DtFunc_ExclAccess(Drv, &Rx->AfRx, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE);
    if (Result == DTAPI_OK)
    {
        Result = DtFunc_ExclAccess(Drv, &Rx->AfDma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE);
        if (Result != DTAPI_OK)
            DtFunc_ExclAccess(Drv, &Rx->AfRx, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
    }
    Rx->Held = Result == DTAPI_OK;

    // Everything idle and CDMAC flushed, before the buffer is registered.
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_AsiRxSetOpMode(Drv, Rx->AsiRx, DT_FUNC_OPMODE_IDLE);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoSetOpMode(Drv, Rx->Burst, DT_BLOCK_OPMODE_IDLE);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacIssueChannelFlush(Drv, Rx->Cdmac);
    if (Result == DTAPI_OK)
        Result = RegisterBuffer(Rx);
    if (Result == DTAPI_OK)
    {
        Rx->Search = (uint8_t*)DtAlloc_Malloc(DT_ASIRX_SEARCH_SIZE);
        if (Rx->Search == NULL)
            Result = DTAPI_E_OUT_OF_MEM;
    }

    // The receiver's defaults and cleared flags.
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_AsiRxSetPolarityCtrl(Drv, Rx->AsiRx, DT_ASIRX_POLARITY_AUTO);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_AsiRxSetSyncMode(Drv, Rx->AsiRx, DT_ASIRX_SYNCMODE_AUTO);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_AsiRxSetPacketMode(Drv, Rx->AsiRx, DT_ASIRX_PCKMODE_AUTO);
    if (Result == DTAPI_OK)
        Result = ClearFlags(&Rx->Base, -1);

    if (Result != DTAPI_OK)
    {
        Release(&Rx->Base);
        return Result;
    }
    *Out = &Rx->Base;
    return DTAPI_OK;
}
