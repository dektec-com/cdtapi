// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAsiTx.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The ASI side of an output channel - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"    // Allocation seam.
#include "Core/DtVec.h"      // The slaves.
#include "Device/DtFunc.h"   // The API functions held.
#include "DtAsiTx.h"         // Interface being implemented.
#include "DtPcieAbi.h"       // Operational modes and types.
#include "OAL/OsDmaBuffer.h" // The DMA buffer.
#include "OAL/OsThread.h"    // The converter's thread and events.
#include "Ts/DtAsiEnc.h"     // The symbols.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Constants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The page a transmit buffer's size is rounded to, times the prefetch size.
#define DT_ASITX_PAGE 4096

// The widest data word a card may read the buffer in, 1024 bits, which the padding of the
// last word has room for.
#define DT_ASITX_MAX_WORD 128

// Convert codes into the buffer only when it has at least this much room, and then as
// much as fits.
#define DT_ASITX_MIN_OUTPUT_FREE (1024 * 1024)

// A write wakes the converter when it leaves more than 100 packets or 5 ms of data in
// the FIFO; the converter wakes itself every 10 ms.
#define DT_ASITX_WAKE_BYTES (188 * 100)
#define DT_ASITX_WAKE_DATA_MS 5
#define DT_ASITX_WAKE_MS 10

// With stuffing, null packets top the buffer up to 50 ms of symbols on every pass: 50 ms
// of 27 M symbols a second, of 16 bits.
#define DT_ASITX_STUFF_LOAD 2700000

// A write that has to wait writes 1 MB at a time and looks again every 5 ms.
#define DT_ASITX_WRITE_BLOCK (1024 * 1024)
#define DT_ASITX_WRITE_POLL_MS 5

// WaitForBurstFifo waits up to five milliseconds, reading the burst FIFO's load every
// millisecond.
#define DT_ASITX_BURST_POLLS 5

// A detach waiting until everything is sent looks every 10 ms, and gives up after a
// second in which the load did not go down.
#define DT_ASITX_SENT_POLL_MS 10
#define DT_ASITX_SENT_STALL_MS 1000

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A slave port and those of its objects the master drives.
typedef struct DtAsiTxSlave
{
    int Port;     // From 1
    int SubValue; // Of its I/O direction
    DtFuncInstance Af;
    bool Held;
    DtDrvObject Phy;
    DtDrvObject Txp; // Its UUID 0 when the function has none
} DtAsiTxSlave;

typedef struct DtAsiTx
{
    DtTx Base;
    OsDrv* Drv;
    int PortIndex;
    DtFuncInstance AfTx, AfDma;
    bool Held;
    DtDrvObject AsiTxG, Phy, Ser, Cdmac, Burst; // The UUID of Phy or Ser 0 when absent
    int BurstFifoSize;
    DtVec Slaves; // DtAsiTxSlave

    // The DMA buffer.
    OsDmaBuffer Buf;
    bool Registered;
    size_t MaxLoad; // The buffer less the data word kept free
    // The data word the card reads the buffer in; a load below it waits for more.
    size_t PcieDataWidthInBytes;
    size_t WriteOffset; // Where the next symbol goes, and the driver's offset
    uint64_t Committed; // Bytes committed since CDMAC was set running
    DtAsiEnc Enc;

    // The FIFO of transport-stream bytes.
    uint8_t* Fifo;
    size_t FifoRead, FifoLoad;
    size_t LoadInHold; // The load reported while holding

    // The settings.
    int StuffMode;
    int64_t Rate;

    // Flags: the burst FIFO's count moving, and stuffing.
    uint32_t UflCount;
    bool Ufl, UflLatched;
    bool Stuffing, StuffingLatched;

    // The converter's thread.
    OsThread* Thread;
    bool StopThread;
    OsEvent* Wake; // Wakes the converter
    OsEvent* Room; // Set after every pass of the thread, and to wake a write for a detach
} DtAsiTx;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Buffer +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DmaBufferLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The bytes of symbols the card has not yet taken, 0 while idle. Right after CDMAC is set
// running a DTA-2178 reports a read offset of an earlier run for a while; the load is
// therefore never more than what was committed since.
//
static DtapiResult DmaBufferLoad(DtAsiTx* Tx, size_t* Load)
{
    *Load = 0;
    if (Tx->Base.TxControl == DTAPI_TXCTRL_IDLE)
        return DTAPI_OK;

    uint32_t ReadOffset = 0;
    DtapiResult Result = DtPcieCmd_CdmacGetTxReadOffset(Tx->Drv, Tx->Cdmac, &ReadOffset);
    if (Result != DTAPI_OK)
        return Result;
    if (ReadOffset >= Tx->Buf.Size)
        return DTAPI_E_DEV_DRIVER;

    const size_t Free =
        (ReadOffset + Tx->MaxLoad + Tx->Buf.Size - Tx->WriteOffset) % Tx->Buf.Size;
    *Load = Free <= Tx->MaxLoad ? Tx->MaxLoad - Free : 0;
    if ((uint64_t)*Load > Tx->Committed)
        *Load = (size_t)Tx->Committed;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Commit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Bytes more of the buffer hold symbols for the card.
//
static DtapiResult Commit(DtAsiTx* Tx, size_t Bytes)
{
    if (Bytes == 0)
        return DTAPI_OK;
    const size_t Offset = (Tx->WriteOffset + Bytes) % Tx->Buf.Size;
    DtapiResult Result =
        DtPcieCmd_CdmacSetTxWriteOffset(Tx->Drv, Tx->Cdmac, (uint32_t)Offset);
    if (Result == DTAPI_OK)
    {
        Tx->WriteOffset = Offset;
        Tx->Committed += Bytes;
    }
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OutAt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Where symbols go, and how many fit there in one piece within Free bytes.
//
static uint16_t* OutAt(const DtAsiTx* Tx, size_t Free, size_t* Syms)
{
    size_t Flat = Tx->Buf.Size - Tx->WriteOffset;
    *Syms = (Free < Flat ? Free : Flat) / 2;
    return (uint16_t*)(void*)(Tx->Buf.Data + Tx->WriteOffset);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- InsertNulls -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Count null packets coded into the buffer as far as Free bytes allow.
//
static DtapiResult InsertNulls(DtAsiTx* Tx, int64_t Count, size_t Free)
{
    static const uint8_t Null[204] = {0x47, 0x1F, 0xFF, 0x10, 0x00};
    const size_t Size = (size_t)Tx->Enc.InSize;

    for (int64_t i = 0; i < Count && Free >= 2; i++)
    {
        size_t Done = 0;
        while (Done < Size && Free >= 2)
        {
            size_t Syms, Taken, Written;
            uint16_t* Out = OutAt(Tx, Free, &Syms);
            DtAsiEnc_Convert(&Tx->Enc, Null + Done, Size - Done, Out, Syms, &Taken,
                             &Written);
            DtapiResult Result = Commit(Tx, 2 * Written);
            if (Result != DTAPI_OK)
                return Result;
            Done += Taken;
            Free -= 2 * Written;
            if (Taken == 0 && Written == 0)
                return DTAPI_OK;
        }
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Convert -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// When the buffer has 1 MB of room, as much of what the FIFO holds as fits is coded into
// it. With the FIFO empty and less than a data word left in the buffer, K28.5 fill that
// word, so that the last symbols go out.
//
static DtapiResult Convert(DtAsiTx* Tx)
{
    if (Tx->Base.TxControl == DTAPI_TXCTRL_IDLE)
        return DTAPI_OK;

    size_t Load;
    DtapiResult Result = DmaBufferLoad(Tx, &Load);
    if (Result != DTAPI_OK)
        return Result;
    size_t Free = Tx->MaxLoad - Load;

    if (Load > 1 && Load < Tx->PcieDataWidthInBytes && Tx->FifoLoad == 0)
    {
        uint16_t Pad[DT_ASITX_MAX_WORD / 2];
        const size_t Syms = (Tx->PcieDataWidthInBytes - Load) / 2;
        DtAsiEnc_Pad(&Tx->Enc, Pad, Syms);
        for (size_t i = 0; i < Syms; i++)
            memcpy(Tx->Buf.Data + (Tx->WriteOffset + 2 * i) % Tx->Buf.Size, &Pad[i], 2);
        return Commit(Tx, 2 * Syms);
    }
    if (Free < DT_ASITX_MIN_OUTPUT_FREE)
        return DTAPI_OK;

    while (Tx->FifoLoad > 0 && Free >= 2)
    {
        size_t Flat = DT_ASITX_FIFO_SIZE - Tx->FifoRead;
        size_t InSize = Tx->FifoLoad < Flat ? Tx->FifoLoad : Flat;
        size_t Syms, Taken, Written;
        uint16_t* Out = OutAt(Tx, Free, &Syms);

        DtAsiEnc_Convert(&Tx->Enc, Tx->Fifo + Tx->FifoRead, InSize, Out, Syms, &Taken,
                         &Written);
        Tx->FifoRead = (Tx->FifoRead + Taken) % DT_ASITX_FIFO_SIZE;
        Tx->FifoLoad -= Taken;
        Result = Commit(Tx, 2 * Written);
        if (Result != DTAPI_OK)
            return Result;
        Free -= 2 * Written;
        if (Taken == 0 && Written == 0)
            break;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Stuff -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// With stuffing, fewer than 50 ms of symbols in the buffer is an underflow of a kind:
// null packets make up the difference.
//
static DtapiResult Stuff(DtAsiTx* Tx)
{
    size_t Load;
    DtapiResult Result = DmaBufferLoad(Tx, &Load);
    if (Result != DTAPI_OK)
        return Result;
    if (Load >= DT_ASITX_STUFF_LOAD)
    {
        Tx->Stuffing = false;
        return DTAPI_OK;
    }

    Tx->Stuffing = Tx->StuffingLatched = true;
    const int64_t Bytes =
        DtAsiEnc_BytesOf(&Tx->Enc, (int64_t)(DT_ASITX_STUFF_LOAD - Load) / 2);
    const int64_t Packets = (Bytes + Tx->Enc.OutSize - 1) / Tx->Enc.OutSize;
    return InsertNulls(Tx, Packets, Tx->MaxLoad - Load);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Thread +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Converter -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Every 10 ms, or when woken, converts, and stuffs while sending with stuffing. Wakes a
// write that waits for room after each pass.
//
static void Converter(void* Context)
{
    DtAsiTx* Tx = (DtAsiTx*)Context;

    OsThread_SetName("DtAsiTx");

    OsThread_RaisePriority();
    OsMutex_Lock(Tx->Base.Port.Lock);
    while (!Tx->StopThread)
    {
        OsMutex_Unlock(Tx->Base.Port.Lock);
        OsEvent_Wait(Tx->Wake, DT_ASITX_WAKE_MS);
        OsMutex_Lock(Tx->Base.Port.Lock);
        if (Tx->StopThread)
            break;

        Convert(Tx);
        if (Tx->StuffMode != 0 && Tx->Base.TxControl == DTAPI_TXCTRL_SEND)
            Stuff(Tx);
        OsEvent_Set(Tx->Room);
    }
    OsMutex_Unlock(Tx->Base.Port.Lock);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StopThread -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Stops the converter and waits for it, releasing the lock while it does, since the
// thread takes the lock to see that it must stop.
//
static void StopThread(DtAsiTx* Tx)
{
    OsThread* Thread = Tx->Thread;

    if (Thread == NULL)
        return;
    Tx->StopThread = true;
    Tx->Thread = NULL;
    OsEvent_Set(Tx->Wake);
    OsMutex_Unlock(Tx->Base.Port.Lock);
    OsThread_Join(Thread);
    OsMutex_Lock(Tx->Base.Port.Lock);
    Tx->StopThread = false;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Slaves +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SlaveAt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtAsiTxSlave* SlaveAt(const DtAsiTx* Tx, size_t i)
{
    return (DtAsiTxSlave*)DtVec_At(&Tx->Slaves, i);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SlavesToMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Each slave's PHY to OpMode, a DT_FUNC_OPMODE_ value; the encoder, which has no standby,
// runs for STANDBY, but only on a slave whose direction's sub-value is an output, which a
// slave's never is.
//
static DtapiResult SlavesToMode(DtAsiTx* Tx, int OpMode)
{
    for (size_t i = 0; i < DtVec_Count(&Tx->Slaves); i++)
    {
        const DtAsiTxSlave* S = SlaveAt(Tx, i);
        const bool Encoder =
            S->Txp.Uuid != 0 && (S->SubValue == DTAPI_IOCONFIG_OUTPUT ||
                                 S->SubValue == DTAPI_IOCONFIG_INTOUTPUT);
        const int TxpMode =
            OpMode == DT_FUNC_OPMODE_IDLE ? DT_BLOCK_OPMODE_IDLE : DT_BLOCK_OPMODE_RUN;

        DtapiResult Result = DTAPI_OK;
        if (Encoder && OpMode == DT_FUNC_OPMODE_RUN)
            Result = DtPcieCmd_SdiTxPSetOpMode(Tx->Drv, S->Txp, TxpMode);
        if (Result == DTAPI_OK)
            Result = DtPcieCmd_SdiTxPhySetOpMode(Tx->Drv, S->Phy, OpMode);
        if (Result == DTAPI_OK && Encoder && OpMode != DT_FUNC_OPMODE_RUN)
            Result = DtPcieCmd_SdiTxPSetOpMode(Tx->Drv, S->Txp, TxpMode);
        if (Result != DTAPI_OK)
            return Result;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReleaseSlaves -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Every PHY and encoder idle, the functions released.
//
static void ReleaseSlaves(DtAsiTx* Tx)
{
    for (size_t i = 0; i < DtVec_Count(&Tx->Slaves); i++)
    {
        DtAsiTxSlave* S = SlaveAt(Tx, i);
        if (S->Phy.Uuid != 0)
            DtPcieCmd_SdiTxPhySetOpMode(Tx->Drv, S->Phy, DT_FUNC_OPMODE_IDLE);
        if (S->Txp.Uuid != 0)
            DtPcieCmd_SdiTxPSetOpMode(Tx->Drv, S->Txp, DT_BLOCK_OPMODE_IDLE);
        if (S->Held)
            DtFunc_ExclAccess(Tx->Drv, &S->Af, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
        DtFunc_Release(&S->Af);
    }
    DtVec_Free(&Tx->Slaves);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindSlaves -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Every port whose direction is a double-buffered output or a monitor naming this port in
// ParXtra[0], taken exclusively through the function its direction gives, with its PHY
// and, when it has one, its encoder. A slave another user holds fails the attach with the
// driver's result.
//
static DtapiResult FindSlaves(DtAsiTx* Tx)
{
    const DtDevice* Device = Tx->Base.Port.Device;
    const int Master = Tx->Base.Port.Port;

    for (int Index = 0; Index < Device->NumPorts; Index++)
    {
        DtIoConfig Dir = {Index + 1, DTAPI_IOCONFIG_IODIR, -1, -1, {-1, -1}};
        DtapiResult Result = DtPcieCmd_GetIoConfig(Tx->Drv, &Dir);
        if (Result != DTAPI_OK)
            return Result;
        const char* Name =
            Dir.Value == DTAPI_IOCONFIG_MONITOR && Dir.SubValue == DTAPI_IOCONFIG_MONITOR
                ? "AF_ASISDIMON"
            : Dir.Value == DTAPI_IOCONFIG_INTOUTPUT &&
                    Dir.SubValue == DTAPI_IOCONFIG_DBLBUF
                ? "AF_SDIPHYONLYTX"
            : Dir.Value == DTAPI_IOCONFIG_OUTPUT && Dir.SubValue == DTAPI_IOCONFIG_DBLBUF
                ? "AF_ASISDITX"
                : NULL;
        if (Name == NULL || Dir.ParXtra[0] != Master)
            continue;

        DtAsiTxSlave S;
        memset(&S, 0, sizeof(S));
        S.Port = Index + 1;
        S.SubValue = Dir.SubValue;
        DtVec_Init(&S.Af.Objects, sizeof(DtFuncObject));
        Result = DtFunc_Find(Tx->Drv, Index, Name, "", &S.Af);
        if (Result == DTAPI_OK)
            Result = DtFunc_ExclAccess(Tx->Drv, &S.Af, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE);
        S.Held = Result == DTAPI_OK;
        if (Result == DTAPI_OK)
        {
            const DtFuncObject* Phy = DtFunc_Get(&S.Af, true, DT_FUNC_TYPE_SDITXPHY, "");
            const DtFuncObject* Txp = DtFunc_Get(&S.Af, false, DT_BLOCK_TYPE_SDITXP, "");
            Result = Phy == NULL ? DTAPI_E_NOT_FOUND : DTAPI_OK;
            if (Phy != NULL)
                S.Phy = Phy->Ref;
            if (Txp != NULL)
                S.Txp = Txp->Ref;
        }
        if (DtVec_Push(&Tx->Slaves, &S) != 0)
        {
            if (S.Held)
                DtFunc_ExclAccess(Tx->Drv, &S.Af, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
            DtFunc_Release(&S.Af);
            return DTAPI_E_OUT_OF_MEM;
        }
        if (Result != DTAPI_OK)
            return Result;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SlavesToAsi -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The slaves set to the I/O standard ASI: one list for all of them.
//
static DtapiResult SlavesToAsi(DtAsiTx* Tx)
{
    const size_t Count = DtVec_Count(&Tx->Slaves);
    if (Count == 0)
        return DTAPI_OK;

    DtIoConfig* Configs = (DtIoConfig*)DtAlloc_Malloc(Count * sizeof(DtIoConfig));
    if (Configs == NULL)
        return DTAPI_E_OUT_OF_MEM;
    for (size_t i = 0; i < Count; i++)
    {
        const DtIoConfig Config = {
            SlaveAt(Tx, i)->Port, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_ASI, -1, {-1, -1}};
        Configs[i] = Config;
    }
    DtapiResult Result = DtPcieCmd_SetIoConfigList(Tx->Drv, Configs, (int)Count);
    DtAlloc_Free(Configs);
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Flags +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- UpdateUfl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sets the underflow flag when the burst FIFO's count has moved since the last look, and
// latches it.
//
static DtapiResult UpdateUfl(DtAsiTx* Tx)
{
    uint32_t Count = 0;
    DtapiResult Result = DtPcieCmd_BurstFifoGetOvfUflCount(Tx->Drv, Tx->Burst, &Count);
    if (Result != DTAPI_OK)
        return Result;
    Tx->Ufl = Count != Tx->UflCount;
    Tx->UflLatched |= Tx->Ufl;
    Tx->UflCount = Count;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DTAPI_TX_FIFO_UFL takes the burst FIFO's count as it is now and clears stuffing;
// DTAPI_TX_SYNC_ERR the encoder's.
//
static DtapiResult ClearFlags(DtTx* Base, int Flags)
{
    DtAsiTx* Tx = (DtAsiTx*)Base;

    if ((Flags & DTAPI_TX_FIFO_UFL) != 0)
    {
        DtapiResult Result =
            DtPcieCmd_BurstFifoGetOvfUflCount(Tx->Drv, Tx->Burst, &Tx->UflCount);
        if (Result != DTAPI_OK)
            return Result;
        Tx->Ufl = Tx->UflLatched = false;
        Tx->Stuffing = Tx->StuffingLatched = false;
    }
    DtAsiEnc_ClearFlags(&Tx->Enc, Flags);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult GetFlags(DtTx* Base, int* Status, int* Latched)
{
    DtAsiTx* Tx = (DtAsiTx*)Base;

    *Status = *Latched = 0;
    DtapiResult Result = UpdateUfl(Tx);
    if (Result != DTAPI_OK)
        return Result;
    DtAsiEnc_GetFlags(&Tx->Enc, Status, Latched);
    if (Tx->Ufl || Tx->Stuffing)
        *Status |= DTAPI_TX_FIFO_UFL;
    if (Tx->UflLatched || Tx->StuffingLatched)
        *Latched |= DTAPI_TX_FIFO_UFL;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Load +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FifoLoadOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// While holding, what was written; while sending, the FIFO and what the symbols in the
// buffer and the burst FIFO carry, at most the FIFO's size. With DTAPI_TXMODE_TXONTIME
// only the FIFO counts: without a fixed rate, the symbols in the buffer cannot be
// converted to the bytes they carry.
//
static DtapiResult FifoLoadOf(DtAsiTx* Tx, size_t* Load)
{
    *Load = 0;
    if (Tx->Base.TxControl == DTAPI_TXCTRL_IDLE)
        return DTAPI_OK;
    if (Tx->Base.TxControl == DTAPI_TXCTRL_HOLD)
    {
        *Load = Tx->LoadInHold;
        return DTAPI_OK;
    }

    size_t Dma = 0;
    DtapiResult Result = DmaBufferLoad(Tx, &Dma);
    if (Result != DTAPI_OK)
        return Result;
    *Load = Tx->FifoLoad;
    if (!Tx->Enc.TxOnTime && Dma >= Tx->PcieDataWidthInBytes)
    {
        int64_t Bytes =
            DtAsiEnc_BytesOf(&Tx->Enc, (int64_t)(Dma + (size_t)Tx->BurstFifoSize) / 2);
        const int TsMode = Tx->Base.TxMode & DTAPI_TXMODE_TS_MASK;
        if (TsMode == DTAPI_TXMODE_MIN16)
            Bytes = Bytes * 204 / 188;
        else if (TsMode == DTAPI_TXMODE_ADD16)
            Bytes = Bytes * 188 / 204;
        *Load += (size_t)(Bytes > 0 ? Bytes : 0);
    }
    if (*Load > DT_ASITX_FIFO_SIZE)
        *Load = DT_ASITX_FIFO_SIZE;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetFifoLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtapiResult GetFifoLoad(DtTx* Base, int* FifoLoad)
{
    size_t Load = 0;
    DtapiResult Result = FifoLoadOf((DtAsiTx*)Base, &Load);
    *FifoLoad = (int)Load;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetFifoSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtapiResult GetFifoSize(DtTx* Base, int* FifoSize)
{
    (void)Base;
    *FifoSize = DT_ASITX_FIFO_SIZE;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= States +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IdleToHold -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// CDMAC flushed and running from the start of the buffer, the gate's input cleared, the
// burst FIFO in standby, and the encoder started, which refuses a rate that does not fit
// the packet size, except with DTAPI_TXMODE_TXONTIME. A failure leaves CDMAC and the
// burst FIFO idle.
//
static DtapiResult IdleToHold(DtAsiTx* Tx)
{
    OsDrv* Drv = Tx->Drv;

    DtapiResult Result = DtPcieCmd_CdmacIssueChannelFlush(Drv, Tx->Cdmac);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_AsiTxGClearInputState(Drv, Tx->AsiTxG);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacSetTxWriteOffset(Drv, Tx->Cdmac, 0);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacSetOpMode(Drv, Tx->Cdmac, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoSetOpMode(Drv, Tx->Burst, DT_BLOCK_OPMODE_STANDBY);
    if (Result == DTAPI_OK)
        Result = DtAsiEnc_Start(&Tx->Enc);
    if (Result != DTAPI_OK)
    {
        DtPcieCmd_BurstFifoSetOpMode(Drv, Tx->Burst, DT_BLOCK_OPMODE_IDLE);
        DtPcieCmd_CdmacSetOpMode(Drv, Tx->Cdmac, DT_BLOCK_OPMODE_IDLE);
        return Result;
    }
    Tx->WriteOffset = 0;
    Tx->Committed = 0;
    Tx->LoadInHold = 0;
    Tx->Base.TxControl = DTAPI_TXCTRL_HOLD;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WaitForBurstFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Up to five milliseconds, reading every millisecond, for the burst FIFO to hold three
// quarters of itself, or of what the buffer holds when that is less; DTAPI_E_TIMEOUT
// otherwise.
//
static DtapiResult WaitForBurstFifo(DtAsiTx* Tx)
{
    size_t Load;
    DtapiResult Result = DmaBufferLoad(Tx, &Load);
    if (Result != DTAPI_OK)
        return Result;
    const size_t Size = (size_t)Tx->BurstFifoSize;
    const size_t Target = (Size < Load ? Size : Load) / 32 * 24;

    DtBurstFifoStatus Status;
    memset(&Status, 0, sizeof(Status));
    Result = DtPcieCmd_BurstFifoGetStatus(Tx->Drv, Tx->Burst, &Status);
    for (int Poll = 0; Result == DTAPI_OK && (size_t)Status.CurLoad < Target &&
                       Poll < DT_ASITX_BURST_POLLS;
         Poll++)
    {
        OsTime_SleepMs(1);
        Result = DtPcieCmd_BurstFifoGetStatus(Tx->Drv, Tx->Burst, &Status);
    }
    if (Result == DTAPI_OK && (size_t)Status.CurLoad < Target)
        Result = DTAPI_E_TIMEOUT;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HoldToSend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The converter's thread started, the burst FIFO filled, its maximum, the underflow
// flags and the reorder buffer's statistics cleared, and the burst FIFO and the gate
// running.
//
static DtapiResult HoldToSend(DtAsiTx* Tx)
{
    OsDrv* Drv = Tx->Drv;

    Tx->StopThread = false;
    Tx->Thread = OsThread_Start(Converter, Tx);
    if (Tx->Thread == NULL)
        return DTAPI_E_OUT_OF_MEM;

    DtapiResult Result = WaitForBurstFifo(Tx);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoClearMax(Drv, Tx->Burst, true, true);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoGetOvfUflCount(Drv, Tx->Burst, &Tx->UflCount);
    if (Result == DTAPI_OK)
        Tx->Ufl = Tx->UflLatched = false;
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacClearReorderBufMinMax(Drv, Tx->Cdmac);
    if (Result == DTAPI_OK && Tx->Phy.Uuid != 0)
        Result = DtPcieCmd_SdiTxPhyClearUnderflowFlag(Drv, Tx->Phy);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoSetOpMode(Drv, Tx->Burst, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_AsiTxGSetOpMode(Drv, Tx->AsiTxG, DT_BLOCK_OPMODE_RUN);
    if (Result != DTAPI_OK)
    {
        StopThread(Tx);
        return Result;
    }
    Tx->Base.TxControl = DTAPI_TXCTRL_SEND;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SendToHold -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The gate and the burst FIFO in standby, the load kept as it was, and the thread
// stopped.
//
static DtapiResult SendToHold(DtAsiTx* Tx)
{
    DtapiResult Result =
        DtPcieCmd_AsiTxGSetOpMode(Tx->Drv, Tx->AsiTxG, DT_BLOCK_OPMODE_STANDBY);
    if (Result == DTAPI_OK)
        Result =
            DtPcieCmd_BurstFifoSetOpMode(Tx->Drv, Tx->Burst, DT_BLOCK_OPMODE_STANDBY);
    if (Result == DTAPI_OK)
        Result = FifoLoadOf(Tx, &Tx->LoadInHold);
    if (Result != DTAPI_OK)
        return Result;
    Tx->Base.TxControl = DTAPI_TXCTRL_HOLD;
    StopThread(Tx);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HoldToIdle -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The burst FIFO and CDMAC idle, CDMAC flushed and the gate's input cleared, the FIFO
// empty and DTAPI_TX_FIFO_UFL cleared.
//
static DtapiResult HoldToIdle(DtAsiTx* Tx)
{
    OsDrv* Drv = Tx->Drv;

    DtapiResult Result =
        DtPcieCmd_BurstFifoSetOpMode(Drv, Tx->Burst, DT_BLOCK_OPMODE_IDLE);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacSetOpMode(Drv, Tx->Cdmac, DT_BLOCK_OPMODE_IDLE);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacIssueChannelFlush(Drv, Tx->Cdmac);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_AsiTxGClearInputState(Drv, Tx->AsiTxG);
    Tx->FifoRead = Tx->FifoLoad = 0;
    if (Result == DTAPI_OK)
        Result = ClearFlags(&Tx->Base, DTAPI_TX_FIFO_UFL);
    if (Result != DTAPI_OK)
        return Result;
    Tx->LoadInHold = 0;
    Tx->Base.TxControl = DTAPI_TXCTRL_IDLE;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetTxControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// IDLE to SEND goes through HOLD, and SEND to IDLE too.
//
static DtapiResult SetTxControl(DtTx* Base, int TxControl)
{
    DtAsiTx* Tx = (DtAsiTx*)Base;
    DtapiResult Result = DTAPI_OK;

    if (Base->TxControl == TxControl)
        return DTAPI_OK;
    if (TxControl != DTAPI_TXCTRL_IDLE && TxControl != DTAPI_TXCTRL_HOLD &&
        TxControl != DTAPI_TXCTRL_SEND)
    {
        return DTAPI_E_INVALID_ARG;
    }

    if (Base->TxControl == DTAPI_TXCTRL_IDLE)
        Result = IdleToHold(Tx);
    else if (Base->TxControl == DTAPI_TXCTRL_SEND)
        Result = SendToHold(Tx);
    if (Result != DTAPI_OK || TxControl == DTAPI_TXCTRL_HOLD)
        return Result;
    return TxControl == DTAPI_TXCTRL_SEND ? HoldToSend(Tx) : HoldToIdle(Tx);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Idle, and DTAPI_TX_FIFO_UFL cleared.
//
static DtapiResult ClearFifo(DtTx* Base)
{
    DtapiResult Result = SetTxControl(Base, DTAPI_TXCTRL_IDLE);
    if (Result != DTAPI_OK)
        return Result;
    return ClearFlags(Base, DTAPI_TX_FIFO_UFL);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Settings +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetTxMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// In any state: stuffing 0 or 1, and not with DTAPI_TXMODE_RAW, whatever flags it has.
//
static DtapiResult SetTxMode(DtTx* Base, int TxMode, int StuffMode)
{
    DtAsiTx* Tx = (DtAsiTx*)Base;

    if (StuffMode != 0 && StuffMode != 1)
        return DTAPI_E_INVALID_ARG;
    if (StuffMode == 1 && (TxMode & DTAPI_TXMODE_TS_MASK) == DTAPI_TXMODE_RAW)
        return DTAPI_E_INVALID_MODE;
    DtapiResult Result = DtAsiEnc_SetTxMode(&Tx->Enc, TxMode);
    if (Result != DTAPI_OK)
        return Result;
    Base->TxMode = TxMode;
    Tx->StuffMode = StuffMode;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetTsRateBps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult SetTsRateBps(DtTx* Base, int TsRate)
{
    DtAsiTx* Tx = (DtAsiTx*)Base;

    DtapiResult Result = DtAsiEnc_SetRate(&Tx->Enc, TsRate);
    if (Result == DTAPI_OK)
        Tx->Rate = TsRate;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetTsRateBps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult GetTsRateBps(DtTx* Base, int* TsRate)
{
    *TsRate = (int)((DtAsiTx*)Base)->Rate;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetTxPolarity -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// DTAPI_TXPOL_NORMAL and INVERTED are the gate's values.
//
static DtapiResult SetTxPolarity(DtTx* Base, int TxPolarity)
{
    DtAsiTx* Tx = (DtAsiTx*)Base;
    return DtPcieCmd_AsiTxGSetPolarity(Tx->Drv, Tx->AsiTxG, TxPolarity);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ForceBlocksToIdle -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Around an I/O configuration: the serialiser, the PHYs and the gate idle; afterwards
// back to sending K28.5, as the attach starts it.
//
static DtapiResult ForceBlocksToIdle(DtAsiTx* Tx, bool ToIdle)
{
    OsDrv* Drv = Tx->Drv;
    DtapiResult Result = DTAPI_OK;

    if (ToIdle)
    {
        if (Tx->Ser.Uuid != 0)
            Result = DtPcieCmd_AsiTxSerSetOpMode(Drv, Tx->Ser, DT_BLOCK_OPMODE_IDLE);
        if (Result == DTAPI_OK && Tx->Phy.Uuid != 0)
            Result = DtPcieCmd_SdiTxPhySetOpMode(Drv, Tx->Phy, DT_FUNC_OPMODE_IDLE);
        if (Result == DTAPI_OK)
            Result = SlavesToMode(Tx, DT_FUNC_OPMODE_IDLE);
        if (Result == DTAPI_OK)
            Result = DtPcieCmd_AsiTxGSetOpMode(Drv, Tx->AsiTxG, DT_BLOCK_OPMODE_IDLE);
        return Result;
    }

    Result = DtPcieCmd_AsiTxGSetOpMode(Drv, Tx->AsiTxG, DT_BLOCK_OPMODE_STANDBY);
    if (Result == DTAPI_OK)
        Result = SlavesToMode(Tx, DT_FUNC_OPMODE_STANDBY);
    if (Result == DTAPI_OK && Tx->Phy.Uuid != 0)
        Result = DtPcieCmd_SdiTxPhySetOpMode(Drv, Tx->Phy, DT_FUNC_OPMODE_STANDBY);
    if (Result == DTAPI_OK)
        Result = SlavesToMode(Tx, DT_FUNC_OPMODE_RUN);
    if (Result == DTAPI_OK && Tx->Phy.Uuid != 0)
        Result = DtPcieCmd_SdiTxPhySetOpMode(Drv, Tx->Phy, DT_FUNC_OPMODE_RUN);
    if (Result == DTAPI_OK && Tx->Ser.Uuid != 0)
        Result = DtPcieCmd_AsiTxSerSetOpMode(Drv, Tx->Ser, DT_BLOCK_OPMODE_RUN);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BeforeIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult BeforeIoConfig(DtTx* Base)
{
    return ForceBlocksToIdle((DtAsiTx*)Base, true);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ApplyIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The blocks back to sending K28.5 whatever the setting gave, and the transmit mode
// applied again.
//
static DtapiResult ApplyIoConfig(DtTx* Base, const DtIoConfig* Config,
                                 DtapiResult SetResult)
{
    DtAsiTx* Tx = (DtAsiTx*)Base;
    (void)Config;

    DtapiResult Result = ForceBlocksToIdle(Tx, false);
    if (SetResult != DTAPI_OK)
        return SetResult;
    if (Result != DTAPI_OK)
        return Result;
    return SetTxMode(Base, Base->TxMode, Tx->StuffMode);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Writing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FifoPut -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void FifoPut(DtAsiTx* Tx, const uint8_t* Data, size_t Size)
{
    const size_t At = (Tx->FifoRead + Tx->FifoLoad) % DT_ASITX_FIFO_SIZE;
    const size_t First = DT_ASITX_FIFO_SIZE - At < Size ? DT_ASITX_FIFO_SIZE - At : Size;

    memcpy(Tx->Fifo + At, Data, First);
    memcpy(Tx->Fifo, Data + First, Size - First);
    Tx->FifoLoad += Size;
    if (Tx->Base.TxControl == DTAPI_TXCTRL_HOLD)
        Tx->LoadInHold += Size;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasRoom -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether the FIFO takes Size more bytes: its own room and the load reported.
//
static DtapiResult HasRoom(DtAsiTx* Tx, size_t Size, bool* Room)
{
    size_t Load = 0;
    DtapiResult Result = FifoLoadOf(Tx, &Load);
    *Room = Result == DTAPI_OK && Size <= DT_ASITX_FIFO_SIZE - Tx->FifoLoad &&
            Size <= DT_ASITX_FIFO_SIZE - Load;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// What fits goes into the FIFO at once; otherwise 1 MB at a time, waiting for room
// without the lock, which a detach ends with DTAPI_E_CANCELLED and a return to idle with
// DTAPI_E_IDLE. While holding the bytes are converted at once; while sending the thread
// is woken when the FIFO holds more than 100 packets or 5 ms of data.
//
static DtapiResult Write(DtTx* Base, const uint8_t* Data, size_t Size)
{
    DtAsiTx* Tx = (DtAsiTx*)Base;
    bool Room = false;

    DtapiResult Result = HasRoom(Tx, Size, &Room);
    if (Result == DTAPI_OK && Room)
        FifoPut(Tx, Data, Size);
    while (Result == DTAPI_OK && !Room && Size > 0)
    {
        const size_t Block = Size < DT_ASITX_WRITE_BLOCK ? Size : DT_ASITX_WRITE_BLOCK;
        Result = HasRoom(Tx, Block, &Room);
        if (Result == DTAPI_OK && Room)
        {
            FifoPut(Tx, Data, Block);
            Data += Block;
            Size -= Block;
            Room = Size == 0;
            continue;
        }
        if (Result != DTAPI_OK)
            break;

        OsMutex_Unlock(Base->Port.Lock);
        OsEvent_Wait(Tx->Room, DT_ASITX_WRITE_POLL_MS);
        OsMutex_Lock(Base->Port.Lock);
        if (*Base->Port.WaitingDetaches > 0)
            Result = DTAPI_E_CANCELLED;
        else if (Base->TxControl == DTAPI_TXCTRL_IDLE)
            Result = DTAPI_E_IDLE;
        else if (Base->TxControl == DTAPI_TXCTRL_HOLD)
            Result = Convert(Tx);
    }
    if (Result != DTAPI_OK)
        return Result;

    if (Base->TxControl == DTAPI_TXCTRL_HOLD)
        return Convert(Tx);
    const double LoadMs =
        Tx->Rate > 0 ? (double)Tx->FifoLoad * 8000.0 / (double)Tx->Rate : 0;
    if (Tx->FifoLoad > DT_ASITX_WAKE_BYTES || LoadMs > DT_ASITX_WAKE_DATA_MS)
        OsEvent_Set(Tx->Wake);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Wake -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Wake(DtTx* Base)
{
    OsEvent_Set(((DtAsiTx*)Base)->Room);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WaitUntilSent -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Until the load is a data word or less, looking every 10 ms without the lock. The load
// leaves out the burst FIFO once the buffer is empty, where up to half a megabyte of
// symbols, some 10 ms of the line, is still to go, so the burst FIFO is waited for as
// well. The wait gives up when the load has not gone down for a second.
//
static void WaitUntilSent(DtTx* Base)
{
    DtAsiTx* Tx = (DtAsiTx*)Base;
    size_t Load = 0, Lowest = SIZE_MAX;
    uint64_t Since = OsTime_MonotonicMs();

    OsEvent_Set(Tx->Wake);
    for (bool Burst = false; Base->TxControl == DTAPI_TXCTRL_SEND;)
    {
        DtBurstFifoStatus Status;
        if (!Burst &&
            (FifoLoadOf(Tx, &Load) != DTAPI_OK || Load <= Tx->PcieDataWidthInBytes))
        {
            Burst = true;
            Lowest = SIZE_MAX;
        }
        if (Burst)
        {
            if (DtPcieCmd_BurstFifoGetStatus(Tx->Drv, Tx->Burst, &Status) != DTAPI_OK ||
                (size_t)Status.CurLoad <= Tx->PcieDataWidthInBytes)
                break;
            Load = (size_t)Status.CurLoad;
        }

        if (Load < Lowest)
        {
            Lowest = Load;
            Since = OsTime_MonotonicMs();
        }
        else if (OsTime_MonotonicMs() - Since >= DT_ASITX_SENT_STALL_MS)
            break;
        OsMutex_Unlock(Base->Port.Lock);
        OsTime_SleepMs(DT_ASITX_SENT_POLL_MS);
        OsMutex_Lock(Base->Port.Lock);
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Lifetime +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Release -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Idle, the thread stopped, the buffer let go of, every block idle, the functions and the
// slaves released. Failures are ignored.
//
static void Release(DtTx* Base)
{
    DtAsiTx* Tx = (DtAsiTx*)Base;
    OsDrv* Drv = Tx->Drv;

    if (Tx->Held)
        SetTxControl(Base, DTAPI_TXCTRL_IDLE);
    StopThread(Tx);
    if (Tx->Registered)
    {
        DtPcieCmd_CdmacSetOpMode(Drv, Tx->Cdmac, DT_BLOCK_OPMODE_IDLE);
        DtPcieCmd_CdmacFreeBuffer(Drv, Tx->Cdmac);
    }
    OsDmaBuffer_Free(&Tx->Buf);
    if (Tx->Held)
    {
        DtPcieCmd_BurstFifoSetOpMode(Drv, Tx->Burst, DT_BLOCK_OPMODE_IDLE);
        if (Tx->Ser.Uuid != 0)
            DtPcieCmd_AsiTxSerSetOpMode(Drv, Tx->Ser, DT_BLOCK_OPMODE_IDLE);
        if (Tx->Phy.Uuid != 0)
            DtPcieCmd_SdiTxPhySetOpMode(Drv, Tx->Phy, DT_FUNC_OPMODE_IDLE);
        DtPcieCmd_AsiTxGSetOpMode(Drv, Tx->AsiTxG, DT_BLOCK_OPMODE_IDLE);
        DtFunc_ExclAccess(Drv, &Tx->AfTx, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
        DtFunc_ExclAccess(Drv, &Tx->AfDma, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
    }
    ReleaseSlaves(Tx);
    DtFunc_Release(&Tx->AfTx);
    DtFunc_Release(&Tx->AfDma);
    DtAlloc_Free(Tx->Fifo);
    OsEvent_Destroy(Tx->Wake);
    OsEvent_Destroy(Tx->Room);
    DtAlloc_Free(Tx);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindObjects -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The objects the side drives: CDMAC and BURSTFIFO of AF_DMA, ASITXG of AF_ASISDITX, and
// the port's SDITXPHY or ASITXSER, one of which it must have; and whether the driver is
// new enough for each.
//
static DtapiResult FindObjects(DtAsiTx* Tx)
{
    const DtDriverVersion* Version = &Tx->Base.Port.Device->DriverVersion;
    DtapiResult Result =
        DtFunc_Find(Tx->Drv, Tx->PortIndex, "AF_ASISDITX", "", &Tx->AfTx);
    if (Result == DTAPI_OK)
        Result = DtFunc_Find(Tx->Drv, Tx->PortIndex, "AF_DMA", "", &Tx->AfDma);
    if (Result != DTAPI_OK)
        return Result;

    const DtFuncObject* Cdmac = DtFunc_Get(&Tx->AfDma, false, DT_BLOCK_TYPE_CDMAC, "");
    const DtFuncObject* Burst =
        DtFunc_Get(&Tx->AfDma, false, DT_BLOCK_TYPE_BURSTFIFO, "");
    const DtFuncObject* Gate = DtFunc_Get(&Tx->AfTx, false, DT_BLOCK_TYPE_ASITXG, "");
    const DtFuncObject* Phy = DtFunc_Get(&Tx->AfTx, true, DT_FUNC_TYPE_SDITXPHY, "");
    const DtFuncObject* Ser = DtFunc_Get(&Tx->AfTx, false, DT_BLOCK_TYPE_ASITXSER, "");
    if (Cdmac == NULL || Burst == NULL || Gate == NULL || (Phy == NULL && Ser == NULL))
        return DTAPI_E_NOT_FOUND;
    Tx->Cdmac = Cdmac->Ref;
    Tx->Burst = Burst->Ref;
    Tx->AsiTxG = Gate->Ref;
    if (Phy != NULL)
        Tx->Phy = Phy->Ref;
    if (Ser != NULL)
        Tx->Ser = Ser->Ref;

    Result = DtFunc_CheckDriverVersion(Version, false, DT_BLOCK_TYPE_CDMAC);
    if (Result == DTAPI_OK)
        Result = DtFunc_CheckDriverVersion(Version, false, DT_BLOCK_TYPE_BURSTFIFO);
    if (Result == DTAPI_OK)
        Result = DtFunc_CheckDriverVersion(Version, false, DT_BLOCK_TYPE_ASITXG);
    if (Result == DTAPI_OK && Phy != NULL)
        Result = DtFunc_CheckDriverVersion(Version, true, DT_FUNC_TYPE_SDITXPHY);
    if (Result == DTAPI_OK && Ser != NULL)
        Result = DtFunc_CheckDriverVersion(Version, false, DT_BLOCK_TYPE_ASITXSER);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RegisterBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// CDMAC idle, a transmit buffer of whole pages times the prefetch size, registered, and
// the test mode off. One data word stays free.
//
static DtapiResult RegisterBuffer(DtAsiTx* Tx)
{
    OsDrv* Drv = Tx->Drv;
    DtCdmacProps Props;

    memset(&Props, 0, sizeof(Props));
    DtapiResult Result = DtPcieCmd_CdmacSetOpMode(Drv, Tx->Cdmac, DT_BLOCK_OPMODE_IDLE);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacGetProps(Drv, Tx->Cdmac, &Props);
    if (Result == DTAPI_OK && (Props.Caps & DT_CDMAC_CAP_TX) == 0)
        Result = DTAPI_E_NOT_SUPPORTED;
    if (Result == DTAPI_OK &&
        (Props.PrefetchSize <= 0 || Props.PcieDataWidth <= 0 ||
         Props.PcieDataWidth % 32 != 0 || Props.PcieDataWidth / 8 > DT_ASITX_MAX_WORD))
        Result = DTAPI_E_DEV_DRIVER;
    if (Result != DTAPI_OK)
        return Result;

    const size_t Unit = (size_t)DT_ASITX_PAGE * (size_t)Props.PrefetchSize;
    const size_t Size = (DT_ASITX_BUF_SIZE + Unit - 1) / Unit * Unit;
    if (OsDmaBuffer_Alloc(Size, &Tx->Buf) != 0)
        return DTAPI_E_OUT_OF_MEM;
    Result = DtPcieCmd_CdmacAllocateBuffer(Drv, Tx->Cdmac, DT_CDMAC_DIR_TX, &Tx->Buf);
    Tx->Registered = Result == DTAPI_OK;
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacSetTestMode(Drv, Tx->Cdmac, DT_CDMAC_TESTMODE_NORMAL);
    Tx->PcieDataWidthInBytes = (size_t)Props.PcieDataWidth / 8;
    Tx->MaxLoad = Tx->Buf.Size - Tx->PcieDataWidthInBytes;
    return Result;
}

static const DtTxBackend g_AsiTxBackend = {
    .Release = Release,
    .SetTxControl = SetTxControl,
    .ClearFifo = ClearFifo,
    .GetFifoLoad = GetFifoLoad,
    .GetFifoSize = GetFifoSize,
    .GetMaxFifoSize = GetFifoSize,
    .GetFlags = GetFlags,
    .SetTxMode = SetTxMode,
    .ClearFlags = ClearFlags,
    .BeforeIoConfig = BeforeIoConfig,
    .ApplyIoConfig = ApplyIoConfig,
    .SetTxPolarity = SetTxPolarity,
    .GetTsRateBps = GetTsRateBps,
    .SetTsRateBps = SetTsRateBps,
    .Write = Write,
    .Wake = Wake,
    .WaitUntilSent = WaitUntilSent,
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAsiTx_Attach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtAsiTx_Attach(const DtTxPort* Port, DtTx** Out)
{
    *Out = NULL;
    DtAsiTx* Tx = (DtAsiTx*)DtAlloc_Malloc(sizeof(DtAsiTx));
    if (Tx == NULL)
        return DTAPI_E_OUT_OF_MEM;
    memset(Tx, 0, sizeof(*Tx));
    Tx->Base.Backend = &g_AsiTxBackend;
    Tx->Base.Port = *Port;
    Tx->Base.TxControl = DTAPI_TXCTRL_IDLE;
    OsDrv* Drv = Tx->Drv = Port->Device->Drv;
    Tx->PortIndex = Port->PortIndex;
    DtVec_Init(&Tx->AfTx.Objects, sizeof(DtFuncObject));
    DtVec_Init(&Tx->AfDma.Objects, sizeof(DtFuncObject));
    DtVec_Init(&Tx->Slaves, sizeof(DtAsiTxSlave));
    DtAsiEnc_Init(&Tx->Enc);
    Tx->Wake = OsEvent_Create();
    Tx->Room = OsEvent_Create();
    Tx->Fifo = (uint8_t*)DtAlloc_Malloc(DT_ASITX_FIFO_SIZE);

    DtapiResult Result = Tx->Wake == NULL || Tx->Room == NULL || Tx->Fifo == NULL
                             ? DTAPI_E_OUT_OF_MEM
                             : FindObjects(Tx);
    if (Result == DTAPI_OK)
        Result = DtFunc_ExclAccess(Drv, &Tx->AfTx, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE);
    if (Result == DTAPI_OK)
    {
        Result = DtFunc_ExclAccess(Drv, &Tx->AfDma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE);
        if (Result != DTAPI_OK)
            DtFunc_ExclAccess(Drv, &Tx->AfTx, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
    }
    Tx->Held = Result == DTAPI_OK;

    DtBurstFifoProps Burst;
    memset(&Burst, 0, sizeof(Burst));
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoGetProps(Drv, Tx->Burst, &Burst);
    Tx->BurstFifoSize = Burst.FifoSize;
    if (Result == DTAPI_OK)
        Result = RegisterBuffer(Tx);
    if (Result == DTAPI_OK)
        Result = FindSlaves(Tx);
    if (Result == DTAPI_OK)
        Result = SlavesToAsi(Tx);

    // K28.5 from here on: the pipeline idle and flushed, the gate in standby and the
    // PHYs running.
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacIssueChannelFlush(Drv, Tx->Cdmac);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_AsiTxGClearInputState(Drv, Tx->AsiTxG);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoSetOpMode(Drv, Tx->Burst, DT_BLOCK_OPMODE_IDLE);
    if (Result == DTAPI_OK)
        Result = ForceBlocksToIdle(Tx, false);

    // The defaults, and the flags cleared.
    if (Result == DTAPI_OK)
        Result = SetTxPolarity(&Tx->Base, DTAPI_TXPOL_NORMAL);
    if (Result == DTAPI_OK)
        Result = SetTxMode(&Tx->Base, DTAPI_TXMODE_188 | DTAPI_TXMODE_BURST, 0);
    if (Result == DTAPI_OK)
        Result = SetTsRateBps(&Tx->Base, 10000000);
    if (Result == DTAPI_OK)
        Result = ClearFlags(&Tx->Base, -1);

    if (Result != DTAPI_OK)
    {
        Release(&Tx->Base);
        return Result;
    }
    *Out = &Tx->Base;
    return DTAPI_OK;
}
