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
#define DT_ASITX_PAGE_SIZE 4096

// The widest data word a card may read the buffer in, 1024 bits, which the padding of the
// last word has room for.
#define DT_ASITX_MAX_WORD_BYTES 128

// EncodeFifo codes into the buffer only when it has at least this much room, and then as
// much as fits.
#define DT_ASITX_MIN_OUTPUT_FREE (1024 * 1024)

// A write wakes the converter when it leaves more than 100 packets or 5 ms of data in
// the FIFO; the converter wakes itself every 10 ms.
#define DT_ASITX_WAKE_FIFO_BYTES (188 * 100)
#define DT_ASITX_WAKE_FIFO_MS 5
#define DT_ASITX_CONVERT_PERIOD_MS 10

// With stuffing, null packets top the buffer up to 50 ms of symbols on every pass: 50 ms
// of 27 M symbols a second, of 16 bits.
#define DT_ASITX_STUFF_TARGET_BYTES 2700000

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
    int Port; // From 1
    int IoDirSubValue;
    DtFuncInstance Function;
    bool HasExclusiveAccess;
    DtDrvObject Phy;
    DtDrvObject Txp; // Its UUID 0 when the function has none
} DtAsiTxSlave;

typedef struct DtAsiTx
{
    DtTx Tx;
    OsDrv* Drv;
    int PortIndex;
    DtFuncInstance TxFunction, DmaFunction;
    bool HasExclusiveAccess;
    DtDrvObject AsiTxG, Phy, Ser, Cdmac,
        BurstFifo; // The UUID of Phy or Ser 0 when absent
    int BurstFifoSize;
    DtVec Slaves; // DtAsiTxSlave

    // The DMA buffer.
    OsDmaBuffer DmaBuffer;
    bool BufferRegistered;
    size_t MaxLoad; // The buffer less the data word kept free
    // The data word the card reads the buffer in; a load below it waits for more.
    size_t PcieDataWidthInBytes;
    size_t WriteOffset;      // Where the next symbol goes, and the driver's offset
    uint64_t CommittedBytes; // Bytes committed since CDMAC was set running
    DtAsiEnc Encoder;

    // The FIFO of transport-stream bytes.
    uint8_t* Fifo;
    size_t FifoReadOffset, FifoLoad;
    size_t LoadWhileHolding; // The load reported while holding

    // The settings.
    int StuffMode;
    int64_t TsRateBps;

    // Flags: the burst FIFO's count moving, and stuffing.
    uint32_t LastBurstFifoUflCount;
    bool FifoUfl, FifoUflLatched;
    bool Stuffing, StuffingLatched;

    // The converter's thread.
    OsThread* ConverterThread;
    bool StopRequested;
    OsEvent* ConvertEvent; // Wakes the converter
    OsEvent*
        RoomEvent; // Set after every pass of the thread, and to wake a write for a detach
} DtAsiTx;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Buffer +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DmaBufferLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The bytes of symbols the card has not yet taken, 0 while idle. Right after CDMAC is set
// running a DTA-2178 reports a read offset of an earlier run for a while; the load is
// therefore never more than what was committed since.
//
static DtapiResult DmaBufferLoad(DtAsiTx* Asi, size_t* Load)
{
    *Load = 0;
    if (Asi->Tx.TxControl == DTAPI_TXCTRL_IDLE)
        return DTAPI_OK;

    uint32_t ReadOffset = 0;
    DtapiResult Result =
        DtPcieCmd_CdmacGetTxReadOffset(Asi->Drv, Asi->Cdmac, &ReadOffset);
    if (Result != DTAPI_OK)
        return Result;
    if (ReadOffset >= Asi->DmaBuffer.Size)
        return DTAPI_E_DEV_DRIVER;

    const size_t Free =
        (ReadOffset + Asi->MaxLoad + Asi->DmaBuffer.Size - Asi->WriteOffset) %
        Asi->DmaBuffer.Size;
    *Load = Free <= Asi->MaxLoad ? Asi->MaxLoad - Free : 0;
    if ((uint64_t)*Load > Asi->CommittedBytes)
        *Load = (size_t)Asi->CommittedBytes;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CommitBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Bytes more of the buffer hold symbols for the card.
//
static DtapiResult CommitBytes(DtAsiTx* Asi, size_t Bytes)
{
    if (Bytes == 0)
        return DTAPI_OK;
    const size_t Offset = (Asi->WriteOffset + Bytes) % Asi->DmaBuffer.Size;
    DtapiResult Result =
        DtPcieCmd_CdmacSetTxWriteOffset(Asi->Drv, Asi->Cdmac, (uint32_t)Offset);
    if (Result == DTAPI_OK)
    {
        Asi->WriteOffset = Offset;
        Asi->CommittedBytes += Bytes;
    }
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NextOutputSpan -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Where symbols go, and how many fit there in one piece within Free bytes.
//
static uint16_t* NextOutputSpan(const DtAsiTx* Asi, size_t Free, size_t* Syms)
{
    size_t BytesToEnd = Asi->DmaBuffer.Size - Asi->WriteOffset;
    *Syms = (Free < BytesToEnd ? Free : BytesToEnd) / 2;
    return (uint16_t*)(void*)(Asi->DmaBuffer.Data + Asi->WriteOffset);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- InsertNulls -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Count null packets coded into the buffer as far as Free bytes allow.
//
static DtapiResult InsertNulls(DtAsiTx* Asi, int64_t Count, size_t Free)
{
    static const uint8_t Null[204] = {0x47, 0x1F, 0xFF, 0x10, 0x00};
    const size_t Size = (size_t)Asi->Encoder.InSize;

    for (int64_t i = 0; i < Count && Free >= 2; i++)
    {
        size_t Done = 0;
        while (Done < Size && Free >= 2)
        {
            size_t Syms, BytesIn, SymbolsOut;
            uint16_t* Out = NextOutputSpan(Asi, Free, &Syms);
            DtAsiEnc_Encode(&Asi->Encoder, Null + Done, Size - Done, Out, Syms, &BytesIn,
                            &SymbolsOut);
            DtapiResult Result = CommitBytes(Asi, 2 * SymbolsOut);
            if (Result != DTAPI_OK)
                return Result;
            Done += BytesIn;
            Free -= 2 * SymbolsOut;
            if (BytesIn == 0 && SymbolsOut == 0)
                return DTAPI_OK;
        }
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EncodeFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// When the buffer has 1 MB of room, as much of what the FIFO holds as fits is coded into
// it. With the FIFO empty and less than a data word left in the buffer, K28.5 fill that
// word, so that the last symbols go out.
//
static DtapiResult EncodeFifo(DtAsiTx* Asi)
{
    if (Asi->Tx.TxControl == DTAPI_TXCTRL_IDLE)
        return DTAPI_OK;

    size_t Load;
    DtapiResult Result = DmaBufferLoad(Asi, &Load);
    if (Result != DTAPI_OK)
        return Result;
    size_t Free = Asi->MaxLoad - Load;

    if (Load > 1 && Load < Asi->PcieDataWidthInBytes && Asi->FifoLoad == 0)
    {
        uint16_t Pad[DT_ASITX_MAX_WORD_BYTES / 2];
        const size_t Syms = (Asi->PcieDataWidthInBytes - Load) / 2;
        DtAsiEnc_Pad(&Asi->Encoder, Pad, Syms);
        for (size_t i = 0; i < Syms; i++)
            memcpy(Asi->DmaBuffer.Data + (Asi->WriteOffset + 2 * i) % Asi->DmaBuffer.Size,
                   &Pad[i], 2);
        return CommitBytes(Asi, 2 * Syms);
    }
    if (Free < DT_ASITX_MIN_OUTPUT_FREE)
        return DTAPI_OK;

    while (Asi->FifoLoad > 0 && Free >= 2)
    {
        size_t BytesToEnd = DT_ASITX_FIFO_SIZE - Asi->FifoReadOffset;
        size_t InSize = Asi->FifoLoad < BytesToEnd ? Asi->FifoLoad : BytesToEnd;
        size_t Syms, BytesIn, SymbolsOut;
        uint16_t* Out = NextOutputSpan(Asi, Free, &Syms);

        DtAsiEnc_Encode(&Asi->Encoder, Asi->Fifo + Asi->FifoReadOffset, InSize, Out, Syms,
                        &BytesIn, &SymbolsOut);
        Asi->FifoReadOffset = (Asi->FifoReadOffset + BytesIn) % DT_ASITX_FIFO_SIZE;
        Asi->FifoLoad -= BytesIn;
        Result = CommitBytes(Asi, 2 * SymbolsOut);
        if (Result != DTAPI_OK)
            return Result;
        Free -= 2 * SymbolsOut;
        if (BytesIn == 0 && SymbolsOut == 0)
            break;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Stuff -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// With stuffing, fewer than 50 ms of symbols in the buffer is an underflow of a kind:
// null packets make up the difference.
//
static DtapiResult Stuff(DtAsiTx* Asi)
{
    size_t Load;
    DtapiResult Result = DmaBufferLoad(Asi, &Load);
    if (Result != DTAPI_OK)
        return Result;
    if (Load >= DT_ASITX_STUFF_TARGET_BYTES)
    {
        Asi->Stuffing = false;
        return DTAPI_OK;
    }

    Asi->Stuffing = Asi->StuffingLatched = true;
    const int64_t Bytes = DtAsiEnc_BytesOf(
        &Asi->Encoder, (int64_t)(DT_ASITX_STUFF_TARGET_BYTES - Load) / 2);
    const int64_t Packets = (Bytes + Asi->Encoder.OutSize - 1) / Asi->Encoder.OutSize;
    return InsertNulls(Asi, Packets, Asi->MaxLoad - Load);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Thread +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConverterThread -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Every 10 ms, or when woken, encodes, and stuffs while sending with stuffing. Wakes a
// write that waits for room after each pass.
//
static void ConverterThread(void* Context)
{
    DtAsiTx* Asi = (DtAsiTx*)Context;

    OsThread_SetName("DtAsiTx");

    OsThread_RaisePriority();
    OsMutex_Lock(Asi->Tx.Port.Lock);
    while (!Asi->StopRequested)
    {
        OsMutex_Unlock(Asi->Tx.Port.Lock);
        OsEvent_Wait(Asi->ConvertEvent, DT_ASITX_CONVERT_PERIOD_MS);
        OsMutex_Lock(Asi->Tx.Port.Lock);
        if (Asi->StopRequested)
            break;

        EncodeFifo(Asi);
        if (Asi->StuffMode != 0 && Asi->Tx.TxControl == DTAPI_TXCTRL_SEND)
            Stuff(Asi);
        OsEvent_Set(Asi->RoomEvent);
    }
    OsMutex_Unlock(Asi->Tx.Port.Lock);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StopConverterThread -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Stops the converter and waits for it, releasing the lock while it does, since the
// thread takes the lock to see that it must stop.
//
static void StopConverterThread(DtAsiTx* Asi)
{
    OsThread* Thread = Asi->ConverterThread;

    if (Thread == NULL)
        return;
    Asi->StopRequested = true;
    Asi->ConverterThread = NULL;
    OsEvent_Set(Asi->ConvertEvent);
    OsMutex_Unlock(Asi->Tx.Port.Lock);
    OsThread_Join(Thread);
    OsMutex_Lock(Asi->Tx.Port.Lock);
    Asi->StopRequested = false;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Slaves +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SlaveAt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtAsiTxSlave* SlaveAt(const DtAsiTx* Asi, size_t i)
{
    return (DtAsiTxSlave*)DtVec_At(&Asi->Slaves, i);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetSlavesOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Each slave's PHY to OpMode, a DT_FUNC_OPMODE_ value; the encoder, which has no standby,
// runs for STANDBY, but only on a slave whose direction's sub-value is an output, which a
// slave's never is.
//
static DtapiResult SetSlavesOpMode(DtAsiTx* Asi, int OpMode)
{
    for (size_t i = 0; i < DtVec_Count(&Asi->Slaves); i++)
    {
        const DtAsiTxSlave* Slave = SlaveAt(Asi, i);
        const bool Encoder =
            Slave->Txp.Uuid != 0 && (Slave->IoDirSubValue == DTAPI_IOCONFIG_OUTPUT ||
                                     Slave->IoDirSubValue == DTAPI_IOCONFIG_INTOUTPUT);
        const int TxpMode =
            OpMode == DT_FUNC_OPMODE_IDLE ? DT_BLOCK_OPMODE_IDLE : DT_BLOCK_OPMODE_RUN;

        DtapiResult Result = DTAPI_OK;
        if (Encoder && OpMode == DT_FUNC_OPMODE_RUN)
            Result = DtPcieCmd_SdiTxPSetOpMode(Asi->Drv, Slave->Txp, TxpMode);
        if (Result == DTAPI_OK)
            Result = DtPcieCmd_SdiTxPhySetOpMode(Asi->Drv, Slave->Phy, OpMode);
        if (Result == DTAPI_OK && Encoder && OpMode != DT_FUNC_OPMODE_RUN)
            Result = DtPcieCmd_SdiTxPSetOpMode(Asi->Drv, Slave->Txp, TxpMode);
        if (Result != DTAPI_OK)
            return Result;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReleaseSlaves -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Every PHY and encoder idle, the functions released.
//
static void ReleaseSlaves(DtAsiTx* Asi)
{
    for (size_t i = 0; i < DtVec_Count(&Asi->Slaves); i++)
    {
        DtAsiTxSlave* Slave = SlaveAt(Asi, i);
        if (Slave->Phy.Uuid != 0)
            DtPcieCmd_SdiTxPhySetOpMode(Asi->Drv, Slave->Phy, DT_FUNC_OPMODE_IDLE);
        if (Slave->Txp.Uuid != 0)
            DtPcieCmd_SdiTxPSetOpMode(Asi->Drv, Slave->Txp, DT_BLOCK_OPMODE_IDLE);
        if (Slave->HasExclusiveAccess)
            DtFunc_ExclAccess(Asi->Drv, &Slave->Function,
                              DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
        DtFunc_Release(&Slave->Function);
    }
    DtVec_Free(&Asi->Slaves);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindSlaves -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Every port whose direction is a double-buffered output or a monitor naming this port in
// ParXtra[0], taken exclusively through the function its direction gives, with its PHY
// and, when it has one, its encoder. A slave another user holds fails the attach with the
// driver's result.
//
static DtapiResult FindSlaves(DtAsiTx* Asi)
{
    const DtDevice* Device = Asi->Tx.Port.Device;
    const int Master = Asi->Tx.Port.Port;

    for (int Index = 0; Index < Device->NumPorts; Index++)
    {
        DtIoConfig Dir = {Index + 1, DTAPI_IOCONFIG_IODIR, -1, -1, {-1, -1}};
        DtapiResult Result = DtPcieCmd_GetIoConfig(Asi->Drv, &Dir);
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

        DtAsiTxSlave Slave;
        memset(&Slave, 0, sizeof(Slave));
        Slave.Port = Index + 1;
        Slave.IoDirSubValue = Dir.SubValue;
        DtVec_Init(&Slave.Function.Objects, sizeof(DtFuncObject));
        Result = DtFunc_Find(Asi->Drv, Index, Name, "", &Slave.Function);
        if (Result == DTAPI_OK)
            Result = DtFunc_ExclAccess(Asi->Drv, &Slave.Function,
                                       DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE);
        Slave.HasExclusiveAccess = Result == DTAPI_OK;
        if (Result == DTAPI_OK)
        {
            const DtFuncObject* Phy =
                DtFunc_FindObject(&Slave.Function, true, DT_FUNC_TYPE_SDITXPHY, "");
            const DtFuncObject* Txp =
                DtFunc_FindObject(&Slave.Function, false, DT_BLOCK_TYPE_SDITXP, "");
            Result = Phy == NULL ? DTAPI_E_NOT_FOUND : DTAPI_OK;
            if (Phy != NULL)
                Slave.Phy = Phy->Object;
            if (Txp != NULL)
                Slave.Txp = Txp->Object;
        }
        if (DtVec_Push(&Asi->Slaves, &Slave) != 0)
        {
            if (Slave.HasExclusiveAccess)
                DtFunc_ExclAccess(Asi->Drv, &Slave.Function,
                                  DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
            DtFunc_Release(&Slave.Function);
            return DTAPI_E_OUT_OF_MEM;
        }
        if (Result != DTAPI_OK)
            return Result;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetSlavesIoStdAsi -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The slaves set to the I/O standard ASI: one list for all of them.
//
static DtapiResult SetSlavesIoStdAsi(DtAsiTx* Asi)
{
    const size_t Count = DtVec_Count(&Asi->Slaves);
    if (Count == 0)
        return DTAPI_OK;

    DtIoConfig* Configs = (DtIoConfig*)DtAlloc_Malloc(Count * sizeof(DtIoConfig));
    if (Configs == NULL)
        return DTAPI_E_OUT_OF_MEM;
    for (size_t i = 0; i < Count; i++)
    {
        const DtIoConfig Config = {SlaveAt(Asi, i)->Port,
                                   DTAPI_IOCONFIG_IOSTD,
                                   DTAPI_IOCONFIG_ASI,
                                   -1,
                                   {-1, -1}};
        Configs[i] = Config;
    }
    DtapiResult Result = DtPcieCmd_SetIoConfigList(Asi->Drv, Configs, (int)Count);
    DtAlloc_Free(Configs);
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Flags +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- UpdateUfl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sets the underflow flag when the burst FIFO's count has moved since the last look, and
// latches it.
//
static DtapiResult UpdateUfl(DtAsiTx* Asi)
{
    uint32_t Count = 0;
    DtapiResult Result =
        DtPcieCmd_BurstFifoGetOvfUflCount(Asi->Drv, Asi->BurstFifo, &Count);
    if (Result != DTAPI_OK)
        return Result;
    Asi->FifoUfl = Count != Asi->LastBurstFifoUflCount;
    Asi->FifoUflLatched |= Asi->FifoUfl;
    Asi->LastBurstFifoUflCount = Count;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DTAPI_TX_FIFO_UFL takes the burst FIFO's count as it is now and clears stuffing;
// DTAPI_TX_SYNC_ERR the encoder's.
//
static DtapiResult ClearFlags(DtTx* Tx, int Flags)
{
    DtAsiTx* Asi = (DtAsiTx*)Tx;

    if ((Flags & DTAPI_TX_FIFO_UFL) != 0)
    {
        DtapiResult Result = DtPcieCmd_BurstFifoGetOvfUflCount(
            Asi->Drv, Asi->BurstFifo, &Asi->LastBurstFifoUflCount);
        if (Result != DTAPI_OK)
            return Result;
        Asi->FifoUfl = Asi->FifoUflLatched = false;
        Asi->Stuffing = Asi->StuffingLatched = false;
    }
    DtAsiEnc_ClearFlags(&Asi->Encoder, Flags);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult GetFlags(DtTx* Tx, int* Status, int* Latched)
{
    DtAsiTx* Asi = (DtAsiTx*)Tx;

    *Status = *Latched = 0;
    DtapiResult Result = UpdateUfl(Asi);
    if (Result != DTAPI_OK)
        return Result;
    DtAsiEnc_GetFlags(&Asi->Encoder, Status, Latched);
    if (Asi->FifoUfl || Asi->Stuffing)
        *Status |= DTAPI_TX_FIFO_UFL;
    if (Asi->FifoUflLatched || Asi->StuffingLatched)
        *Latched |= DTAPI_TX_FIFO_UFL;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Load +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReportedFifoLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// While holding, what was written; while sending, the FIFO and what the symbols in the
// buffer and the burst FIFO carry, at most the FIFO's size. With DTAPI_TXMODE_TXONTIME
// only the FIFO counts: without a fixed rate, the symbols in the buffer cannot be
// converted to the bytes they carry.
//
static DtapiResult ReportedFifoLoad(DtAsiTx* Asi, size_t* Load)
{
    *Load = 0;
    if (Asi->Tx.TxControl == DTAPI_TXCTRL_IDLE)
        return DTAPI_OK;
    if (Asi->Tx.TxControl == DTAPI_TXCTRL_HOLD)
    {
        *Load = Asi->LoadWhileHolding;
        return DTAPI_OK;
    }

    size_t Dma = 0;
    DtapiResult Result = DmaBufferLoad(Asi, &Dma);
    if (Result != DTAPI_OK)
        return Result;
    *Load = Asi->FifoLoad;
    if (!Asi->Encoder.TxOnTime && Dma >= Asi->PcieDataWidthInBytes)
    {
        int64_t Bytes = DtAsiEnc_BytesOf(&Asi->Encoder,
                                         (int64_t)(Dma + (size_t)Asi->BurstFifoSize) / 2);
        const int TsMode = Asi->Tx.TxMode & DTAPI_TXMODE_TS_MASK;
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
static DtapiResult GetFifoLoad(DtTx* Tx, int* FifoLoad)
{
    size_t Load = 0;
    DtapiResult Result = ReportedFifoLoad((DtAsiTx*)Tx, &Load);
    *FifoLoad = (int)Load;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetFifoSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtapiResult GetFifoSize(DtTx* Tx, int* FifoSize)
{
    (void)Tx;
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
static DtapiResult IdleToHold(DtAsiTx* Asi)
{
    OsDrv* Drv = Asi->Drv;

    DtapiResult Result = DtPcieCmd_CdmacIssueChannelFlush(Drv, Asi->Cdmac);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_AsiTxGClearInputState(Drv, Asi->AsiTxG);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacSetTxWriteOffset(Drv, Asi->Cdmac, 0);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacSetOpMode(Drv, Asi->Cdmac, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result =
            DtPcieCmd_BurstFifoSetOpMode(Drv, Asi->BurstFifo, DT_BLOCK_OPMODE_STANDBY);
    if (Result == DTAPI_OK)
        Result = DtAsiEnc_Start(&Asi->Encoder);
    if (Result != DTAPI_OK)
    {
        DtPcieCmd_BurstFifoSetOpMode(Drv, Asi->BurstFifo, DT_BLOCK_OPMODE_IDLE);
        DtPcieCmd_CdmacSetOpMode(Drv, Asi->Cdmac, DT_BLOCK_OPMODE_IDLE);
        return Result;
    }
    Asi->WriteOffset = 0;
    Asi->CommittedBytes = 0;
    Asi->LoadWhileHolding = 0;
    Asi->Tx.TxControl = DTAPI_TXCTRL_HOLD;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WaitForBurstFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Up to five milliseconds, reading every millisecond, for the burst FIFO to hold three
// quarters of itself, or of what the buffer holds when that is less; DTAPI_E_TIMEOUT
// otherwise.
//
static DtapiResult WaitForBurstFifo(DtAsiTx* Asi)
{
    size_t Load;
    DtapiResult Result = DmaBufferLoad(Asi, &Load);
    if (Result != DTAPI_OK)
        return Result;
    const size_t Size = (size_t)Asi->BurstFifoSize;
    const size_t Target = (Size < Load ? Size : Load) / 32 * 24;

    DtBurstFifoStatus Status;
    memset(&Status, 0, sizeof(Status));
    Result = DtPcieCmd_BurstFifoGetStatus(Asi->Drv, Asi->BurstFifo, &Status);
    for (int Poll = 0; Result == DTAPI_OK && (size_t)Status.CurLoad < Target &&
                       Poll < DT_ASITX_BURST_POLLS;
         Poll++)
    {
        OsTime_SleepMs(1);
        Result = DtPcieCmd_BurstFifoGetStatus(Asi->Drv, Asi->BurstFifo, &Status);
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
static DtapiResult HoldToSend(DtAsiTx* Asi)
{
    OsDrv* Drv = Asi->Drv;

    Asi->StopRequested = false;
    Asi->ConverterThread = OsThread_Start(ConverterThread, Asi);
    if (Asi->ConverterThread == NULL)
        return DTAPI_E_OUT_OF_MEM;

    DtapiResult Result = WaitForBurstFifo(Asi);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoClearMax(Drv, Asi->BurstFifo, true, true);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoGetOvfUflCount(Drv, Asi->BurstFifo,
                                                   &Asi->LastBurstFifoUflCount);
    if (Result == DTAPI_OK)
        Asi->FifoUfl = Asi->FifoUflLatched = false;
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacClearReorderBufMinMax(Drv, Asi->Cdmac);
    if (Result == DTAPI_OK && Asi->Phy.Uuid != 0)
        Result = DtPcieCmd_SdiTxPhyClearUnderflowFlag(Drv, Asi->Phy);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoSetOpMode(Drv, Asi->BurstFifo, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_AsiTxGSetOpMode(Drv, Asi->AsiTxG, DT_BLOCK_OPMODE_RUN);
    if (Result != DTAPI_OK)
    {
        StopConverterThread(Asi);
        return Result;
    }
    Asi->Tx.TxControl = DTAPI_TXCTRL_SEND;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SendToHold -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The gate and the burst FIFO in standby, the load kept as it was, and the thread
// stopped.
//
static DtapiResult SendToHold(DtAsiTx* Asi)
{
    DtapiResult Result =
        DtPcieCmd_AsiTxGSetOpMode(Asi->Drv, Asi->AsiTxG, DT_BLOCK_OPMODE_STANDBY);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoSetOpMode(Asi->Drv, Asi->BurstFifo,
                                              DT_BLOCK_OPMODE_STANDBY);
    if (Result == DTAPI_OK)
        Result = ReportedFifoLoad(Asi, &Asi->LoadWhileHolding);
    if (Result != DTAPI_OK)
        return Result;
    Asi->Tx.TxControl = DTAPI_TXCTRL_HOLD;
    StopConverterThread(Asi);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HoldToIdle -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The burst FIFO and CDMAC idle, CDMAC flushed and the gate's input cleared, the FIFO
// empty and DTAPI_TX_FIFO_UFL cleared.
//
static DtapiResult HoldToIdle(DtAsiTx* Asi)
{
    OsDrv* Drv = Asi->Drv;

    DtapiResult Result =
        DtPcieCmd_BurstFifoSetOpMode(Drv, Asi->BurstFifo, DT_BLOCK_OPMODE_IDLE);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacSetOpMode(Drv, Asi->Cdmac, DT_BLOCK_OPMODE_IDLE);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacIssueChannelFlush(Drv, Asi->Cdmac);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_AsiTxGClearInputState(Drv, Asi->AsiTxG);
    Asi->FifoReadOffset = Asi->FifoLoad = 0;
    if (Result == DTAPI_OK)
        Result = ClearFlags(&Asi->Tx, DTAPI_TX_FIFO_UFL);
    if (Result != DTAPI_OK)
        return Result;
    Asi->LoadWhileHolding = 0;
    Asi->Tx.TxControl = DTAPI_TXCTRL_IDLE;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetTxControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// IDLE to SEND goes through HOLD, and SEND to IDLE too.
//
static DtapiResult SetTxControl(DtTx* Tx, int TxControl)
{
    DtAsiTx* Asi = (DtAsiTx*)Tx;
    DtapiResult Result = DTAPI_OK;

    if (Tx->TxControl == TxControl)
        return DTAPI_OK;
    if (TxControl != DTAPI_TXCTRL_IDLE && TxControl != DTAPI_TXCTRL_HOLD &&
        TxControl != DTAPI_TXCTRL_SEND)
    {
        return DTAPI_E_INVALID_ARG;
    }

    if (Tx->TxControl == DTAPI_TXCTRL_IDLE)
        Result = IdleToHold(Asi);
    else if (Tx->TxControl == DTAPI_TXCTRL_SEND)
        Result = SendToHold(Asi);
    if (Result != DTAPI_OK || TxControl == DTAPI_TXCTRL_HOLD)
        return Result;
    return TxControl == DTAPI_TXCTRL_SEND ? HoldToSend(Asi) : HoldToIdle(Asi);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Idle, and DTAPI_TX_FIFO_UFL cleared.
//
static DtapiResult ClearFifo(DtTx* Tx)
{
    DtapiResult Result = SetTxControl(Tx, DTAPI_TXCTRL_IDLE);
    if (Result != DTAPI_OK)
        return Result;
    return ClearFlags(Tx, DTAPI_TX_FIFO_UFL);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Settings +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetTxMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// In any state: stuffing 0 or 1, and not with DTAPI_TXMODE_RAW, whatever flags it has.
//
static DtapiResult SetTxMode(DtTx* Tx, int TxMode, int StuffMode)
{
    DtAsiTx* Asi = (DtAsiTx*)Tx;

    if (StuffMode != 0 && StuffMode != 1)
        return DTAPI_E_INVALID_ARG;
    if (StuffMode == 1 && (TxMode & DTAPI_TXMODE_TS_MASK) == DTAPI_TXMODE_RAW)
        return DTAPI_E_INVALID_MODE;
    DtapiResult Result = DtAsiEnc_SetTxMode(&Asi->Encoder, TxMode);
    if (Result != DTAPI_OK)
        return Result;
    Tx->TxMode = TxMode;
    Asi->StuffMode = StuffMode;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetTsRateBps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult SetTsRateBps(DtTx* Tx, int TsRate)
{
    DtAsiTx* Asi = (DtAsiTx*)Tx;

    DtapiResult Result = DtAsiEnc_SetRate(&Asi->Encoder, TsRate);
    if (Result == DTAPI_OK)
        Asi->TsRateBps = TsRate;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetTsRateBps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult GetTsRateBps(DtTx* Tx, int* TsRate)
{
    *TsRate = (int)((DtAsiTx*)Tx)->TsRateBps;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetTxPolarity -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// DTAPI_TXPOL_NORMAL and INVERTED are the gate's values.
//
static DtapiResult SetTxPolarity(DtTx* Tx, int TxPolarity)
{
    DtAsiTx* Asi = (DtAsiTx*)Tx;
    return DtPcieCmd_AsiTxGSetPolarity(Asi->Drv, Asi->AsiTxG, TxPolarity);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BlocksToIdle -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Before an I/O configuration: the serialiser, the PHYs and the gate idle.
//
static DtapiResult BlocksToIdle(DtAsiTx* Asi)
{
    OsDrv* Drv = Asi->Drv;
    DtapiResult Result = DTAPI_OK;

    if (Asi->Ser.Uuid != 0)
        Result = DtPcieCmd_AsiTxSerSetOpMode(Drv, Asi->Ser, DT_BLOCK_OPMODE_IDLE);
    if (Result == DTAPI_OK && Asi->Phy.Uuid != 0)
        Result = DtPcieCmd_SdiTxPhySetOpMode(Drv, Asi->Phy, DT_FUNC_OPMODE_IDLE);
    if (Result == DTAPI_OK)
        Result = SetSlavesOpMode(Asi, DT_FUNC_OPMODE_IDLE);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_AsiTxGSetOpMode(Drv, Asi->AsiTxG, DT_BLOCK_OPMODE_IDLE);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BlocksToK285 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// After an I/O configuration, and at the attach: the gate, the PHYs and the serialiser
// run, so that the port sends K28.5.
//
static DtapiResult BlocksToK285(DtAsiTx* Asi)
{
    OsDrv* Drv = Asi->Drv;
    DtapiResult Result;

    Result = DtPcieCmd_AsiTxGSetOpMode(Drv, Asi->AsiTxG, DT_BLOCK_OPMODE_STANDBY);
    if (Result == DTAPI_OK)
        Result = SetSlavesOpMode(Asi, DT_FUNC_OPMODE_STANDBY);
    if (Result == DTAPI_OK && Asi->Phy.Uuid != 0)
        Result = DtPcieCmd_SdiTxPhySetOpMode(Drv, Asi->Phy, DT_FUNC_OPMODE_STANDBY);
    if (Result == DTAPI_OK)
        Result = SetSlavesOpMode(Asi, DT_FUNC_OPMODE_RUN);
    if (Result == DTAPI_OK && Asi->Phy.Uuid != 0)
        Result = DtPcieCmd_SdiTxPhySetOpMode(Drv, Asi->Phy, DT_FUNC_OPMODE_RUN);
    if (Result == DTAPI_OK && Asi->Ser.Uuid != 0)
        Result = DtPcieCmd_AsiTxSerSetOpMode(Drv, Asi->Ser, DT_BLOCK_OPMODE_RUN);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BeforeIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult BeforeIoConfig(DtTx* Tx)
{
    return BlocksToIdle((DtAsiTx*)Tx);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ApplyIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The blocks back to sending K28.5 whatever the setting gave, and the transmit mode
// applied again.
//
static DtapiResult ApplyIoConfig(DtTx* Tx, const DtIoConfig* Config,
                                 DtapiResult SetResult)
{
    DtAsiTx* Asi = (DtAsiTx*)Tx;
    (void)Config;

    DtapiResult Result = BlocksToK285(Asi);
    if (SetResult != DTAPI_OK)
        return SetResult;
    if (Result != DTAPI_OK)
        return Result;
    return SetTxMode(Tx, Tx->TxMode, Asi->StuffMode);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Writing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AppendToFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void AppendToFifo(DtAsiTx* Asi, const uint8_t* Data, size_t Size)
{
    const size_t At = (Asi->FifoReadOffset + Asi->FifoLoad) % DT_ASITX_FIFO_SIZE;
    const size_t First = DT_ASITX_FIFO_SIZE - At < Size ? DT_ASITX_FIFO_SIZE - At : Size;

    memcpy(Asi->Fifo + At, Data, First);
    memcpy(Asi->Fifo, Data + First, Size - First);
    Asi->FifoLoad += Size;
    if (Asi->Tx.TxControl == DTAPI_TXCTRL_HOLD)
        Asi->LoadWhileHolding += Size;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasRoom -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether the FIFO takes Size more bytes: its own room and the load reported.
//
static DtapiResult HasRoom(DtAsiTx* Asi, size_t Size, bool* Room)
{
    size_t Load = 0;
    DtapiResult Result = ReportedFifoLoad(Asi, &Load);
    *Room = Result == DTAPI_OK && Size <= DT_ASITX_FIFO_SIZE - Asi->FifoLoad &&
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
static DtapiResult Write(DtTx* Tx, const uint8_t* Data, size_t Size)
{
    DtAsiTx* Asi = (DtAsiTx*)Tx;
    bool Room = false;

    DtapiResult Result = HasRoom(Asi, Size, &Room);
    if (Result == DTAPI_OK && Room)
        AppendToFifo(Asi, Data, Size);
    while (Result == DTAPI_OK && !Room && Size > 0)
    {
        const size_t Block = Size < DT_ASITX_WRITE_BLOCK ? Size : DT_ASITX_WRITE_BLOCK;
        Result = HasRoom(Asi, Block, &Room);
        if (Result == DTAPI_OK && Room)
        {
            AppendToFifo(Asi, Data, Block);
            Data += Block;
            Size -= Block;
            Room = Size == 0;
            continue;
        }
        if (Result != DTAPI_OK)
            break;

        OsMutex_Unlock(Tx->Port.Lock);
        OsEvent_Wait(Asi->RoomEvent, DT_ASITX_WRITE_POLL_MS);
        OsMutex_Lock(Tx->Port.Lock);
        if (*Tx->Port.WaitingDetaches > 0)
            Result = DTAPI_E_CANCELLED;
        else if (Tx->TxControl == DTAPI_TXCTRL_IDLE)
            Result = DTAPI_E_IDLE;
        else if (Tx->TxControl == DTAPI_TXCTRL_HOLD)
            Result = EncodeFifo(Asi);
    }
    if (Result != DTAPI_OK)
        return Result;

    if (Tx->TxControl == DTAPI_TXCTRL_HOLD)
        return EncodeFifo(Asi);
    const double LoadMs =
        Asi->TsRateBps > 0 ? (double)Asi->FifoLoad * 8000.0 / (double)Asi->TsRateBps : 0;
    if (Asi->FifoLoad > DT_ASITX_WAKE_FIFO_BYTES || LoadMs > DT_ASITX_WAKE_FIFO_MS)
        OsEvent_Set(Asi->ConvertEvent);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WakeWaitingWrite -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void WakeWaitingWrite(DtTx* Tx)
{
    OsEvent_Set(((DtAsiTx*)Tx)->RoomEvent);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WaitUntilSent -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Until the load is a data word or less, looking every 10 ms without the lock. The load
// leaves out the burst FIFO once the buffer is empty, where up to half a megabyte of
// symbols, some 10 ms of the line, is still to go, so the burst FIFO is waited for as
// well. The wait gives up when the load has not gone down for a second.
//
static void WaitUntilSent(DtTx* Tx)
{
    DtAsiTx* Asi = (DtAsiTx*)Tx;
    size_t Load = 0, Lowest = SIZE_MAX;
    uint64_t LastDropMs = OsTime_MonotonicMs();

    OsEvent_Set(Asi->ConvertEvent);
    for (bool Burst = false; Tx->TxControl == DTAPI_TXCTRL_SEND;)
    {
        DtBurstFifoStatus Status;
        if (!Burst && (ReportedFifoLoad(Asi, &Load) != DTAPI_OK ||
                       Load <= Asi->PcieDataWidthInBytes))
        {
            Burst = true;
            Lowest = SIZE_MAX;
        }
        if (Burst)
        {
            if (DtPcieCmd_BurstFifoGetStatus(Asi->Drv, Asi->BurstFifo, &Status) !=
                    DTAPI_OK ||
                (size_t)Status.CurLoad <= Asi->PcieDataWidthInBytes)
                break;
            Load = (size_t)Status.CurLoad;
        }

        if (Load < Lowest)
        {
            Lowest = Load;
            LastDropMs = OsTime_MonotonicMs();
        }
        else if (OsTime_MonotonicMs() - LastDropMs >= DT_ASITX_SENT_STALL_MS)
            break;
        OsMutex_Unlock(Tx->Port.Lock);
        OsTime_SleepMs(DT_ASITX_SENT_POLL_MS);
        OsMutex_Lock(Tx->Port.Lock);
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Lifetime +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Release -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Idle, the thread stopped, the buffer let go of, every block idle, the functions and the
// slaves released. Failures are ignored.
//
static void Release(DtTx* Tx)
{
    DtAsiTx* Asi = (DtAsiTx*)Tx;
    OsDrv* Drv = Asi->Drv;

    if (Asi->HasExclusiveAccess)
        SetTxControl(Tx, DTAPI_TXCTRL_IDLE);
    StopConverterThread(Asi);
    if (Asi->BufferRegistered)
    {
        DtPcieCmd_CdmacSetOpMode(Drv, Asi->Cdmac, DT_BLOCK_OPMODE_IDLE);
        DtPcieCmd_CdmacFreeBuffer(Drv, Asi->Cdmac);
    }
    OsDmaBuffer_Free(&Asi->DmaBuffer);
    if (Asi->HasExclusiveAccess)
    {
        DtPcieCmd_BurstFifoSetOpMode(Drv, Asi->BurstFifo, DT_BLOCK_OPMODE_IDLE);
        if (Asi->Ser.Uuid != 0)
            DtPcieCmd_AsiTxSerSetOpMode(Drv, Asi->Ser, DT_BLOCK_OPMODE_IDLE);
        if (Asi->Phy.Uuid != 0)
            DtPcieCmd_SdiTxPhySetOpMode(Drv, Asi->Phy, DT_FUNC_OPMODE_IDLE);
        DtPcieCmd_AsiTxGSetOpMode(Drv, Asi->AsiTxG, DT_BLOCK_OPMODE_IDLE);
        DtFunc_ExclAccess(Drv, &Asi->TxFunction, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
        DtFunc_ExclAccess(Drv, &Asi->DmaFunction, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
    }
    ReleaseSlaves(Asi);
    DtFunc_Release(&Asi->TxFunction);
    DtFunc_Release(&Asi->DmaFunction);
    DtAlloc_Free(Asi->Fifo);
    OsEvent_Destroy(Asi->ConvertEvent);
    OsEvent_Destroy(Asi->RoomEvent);
    DtAlloc_Free(Asi);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindDriverBlocks -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The objects the side drives: CDMAC and BURSTFIFO of AF_DMA, ASITXG of AF_ASISDITX, and
// the port's SDITXPHY or ASITXSER, one of which it must have; and whether the driver is
// new enough for each.
//
static DtapiResult FindDriverBlocks(DtAsiTx* Asi)
{
    const DtDriverVersion* Version = &Asi->Tx.Port.Device->DriverVersion;
    DtapiResult Result =
        DtFunc_Find(Asi->Drv, Asi->PortIndex, "AF_ASISDITX", "", &Asi->TxFunction);
    if (Result == DTAPI_OK)
        Result = DtFunc_Find(Asi->Drv, Asi->PortIndex, "AF_DMA", "", &Asi->DmaFunction);
    if (Result != DTAPI_OK)
        return Result;

    const DtFuncObject* Cdmac =
        DtFunc_FindObject(&Asi->DmaFunction, false, DT_BLOCK_TYPE_CDMAC, "");
    const DtFuncObject* Burst =
        DtFunc_FindObject(&Asi->DmaFunction, false, DT_BLOCK_TYPE_BURSTFIFO, "");
    const DtFuncObject* Gate =
        DtFunc_FindObject(&Asi->TxFunction, false, DT_BLOCK_TYPE_ASITXG, "");
    const DtFuncObject* Phy =
        DtFunc_FindObject(&Asi->TxFunction, true, DT_FUNC_TYPE_SDITXPHY, "");
    const DtFuncObject* Ser =
        DtFunc_FindObject(&Asi->TxFunction, false, DT_BLOCK_TYPE_ASITXSER, "");
    if (Cdmac == NULL || Burst == NULL || Gate == NULL || (Phy == NULL && Ser == NULL))
        return DTAPI_E_NOT_FOUND;
    Asi->Cdmac = Cdmac->Object;
    Asi->BurstFifo = Burst->Object;
    Asi->AsiTxG = Gate->Object;
    if (Phy != NULL)
        Asi->Phy = Phy->Object;
    if (Ser != NULL)
        Asi->Ser = Ser->Object;

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
static DtapiResult RegisterBuffer(DtAsiTx* Asi)
{
    OsDrv* Drv = Asi->Drv;
    DtCdmacProps Props;

    memset(&Props, 0, sizeof(Props));
    DtapiResult Result = DtPcieCmd_CdmacSetOpMode(Drv, Asi->Cdmac, DT_BLOCK_OPMODE_IDLE);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacGetProps(Drv, Asi->Cdmac, &Props);
    if (Result == DTAPI_OK && (Props.Caps & DT_CDMAC_CAP_TX) == 0)
        Result = DTAPI_E_NOT_SUPPORTED;
    if (Result == DTAPI_OK && (Props.PrefetchSize <= 0 || Props.PcieDataWidth <= 0 ||
                               Props.PcieDataWidth % 32 != 0 ||
                               Props.PcieDataWidth / 8 > DT_ASITX_MAX_WORD_BYTES))
        Result = DTAPI_E_DEV_DRIVER;
    if (Result != DTAPI_OK)
        return Result;

    const size_t Unit = (size_t)DT_ASITX_PAGE_SIZE * (size_t)Props.PrefetchSize;
    const size_t Size = (DT_ASITX_BUF_SIZE + Unit - 1) / Unit * Unit;
    if (OsDmaBuffer_Alloc(Size, &Asi->DmaBuffer) != 0)
        return DTAPI_E_OUT_OF_MEM;
    Result =
        DtPcieCmd_CdmacAllocateBuffer(Drv, Asi->Cdmac, DT_CDMAC_DIR_TX, &Asi->DmaBuffer);
    Asi->BufferRegistered = Result == DTAPI_OK;
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacSetTestMode(Drv, Asi->Cdmac, DT_CDMAC_TESTMODE_NORMAL);
    Asi->PcieDataWidthInBytes = (size_t)Props.PcieDataWidth / 8;
    Asi->MaxLoad = Asi->DmaBuffer.Size - Asi->PcieDataWidthInBytes;
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
    .WakeWaitingWrite = WakeWaitingWrite,
    .WaitUntilSent = WaitUntilSent,
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAsiTx_Attach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtAsiTx_Attach(const DtTxAttachedPort* Port, DtTx** Tx)
{
    *Tx = NULL;
    DtAsiTx* Asi = (DtAsiTx*)DtAlloc_Malloc(sizeof(DtAsiTx));
    if (Asi == NULL)
        return DTAPI_E_OUT_OF_MEM;
    memset(Asi, 0, sizeof(*Asi));
    Asi->Tx.Backend = &g_AsiTxBackend;
    Asi->Tx.IsAsi = true;
    Asi->Tx.Port = *Port;
    Asi->Tx.TxControl = DTAPI_TXCTRL_IDLE;
    OsDrv* Drv = Asi->Drv = Port->Device->Drv;
    Asi->PortIndex = Port->Port - 1;
    DtVec_Init(&Asi->TxFunction.Objects, sizeof(DtFuncObject));
    DtVec_Init(&Asi->DmaFunction.Objects, sizeof(DtFuncObject));
    DtVec_Init(&Asi->Slaves, sizeof(DtAsiTxSlave));
    DtAsiEnc_Init(&Asi->Encoder);
    Asi->ConvertEvent = OsEvent_Create();
    Asi->RoomEvent = OsEvent_Create();
    Asi->Fifo = (uint8_t*)DtAlloc_Malloc(DT_ASITX_FIFO_SIZE);

    DtapiResult Result =
        Asi->ConvertEvent == NULL || Asi->RoomEvent == NULL || Asi->Fifo == NULL
            ? DTAPI_E_OUT_OF_MEM
            : FindDriverBlocks(Asi);
    if (Result == DTAPI_OK)
        Result =
            DtFunc_ExclAccess(Drv, &Asi->TxFunction, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE);
    if (Result == DTAPI_OK)
    {
        Result =
            DtFunc_ExclAccess(Drv, &Asi->DmaFunction, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE);
        if (Result != DTAPI_OK)
            DtFunc_ExclAccess(Drv, &Asi->TxFunction, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
    }
    Asi->HasExclusiveAccess = Result == DTAPI_OK;

    DtBurstFifoProps Burst;
    memset(&Burst, 0, sizeof(Burst));
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoGetProps(Drv, Asi->BurstFifo, &Burst);
    Asi->BurstFifoSize = Burst.FifoSize;
    if (Result == DTAPI_OK)
        Result = RegisterBuffer(Asi);
    if (Result == DTAPI_OK)
        Result = FindSlaves(Asi);
    if (Result == DTAPI_OK)
        Result = SetSlavesIoStdAsi(Asi);

    // K28.5 from here on: the pipeline idle and flushed, the gate in standby and the
    // PHYs running.
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacIssueChannelFlush(Drv, Asi->Cdmac);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_AsiTxGClearInputState(Drv, Asi->AsiTxG);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoSetOpMode(Drv, Asi->BurstFifo, DT_BLOCK_OPMODE_IDLE);
    if (Result == DTAPI_OK)
        Result = BlocksToK285(Asi);

    // The defaults, and the flags cleared.
    if (Result == DTAPI_OK)
        Result = SetTxPolarity(&Asi->Tx, DTAPI_TXPOL_NORMAL);
    if (Result == DTAPI_OK)
        Result = SetTxMode(&Asi->Tx, DTAPI_TXMODE_188 | DTAPI_TXMODE_BURST, 0);
    if (Result == DTAPI_OK)
        Result = SetTsRateBps(&Asi->Tx, 10000000);
    if (Result == DTAPI_OK)
        Result = ClearFlags(&Asi->Tx, -1);

    if (Result != DTAPI_OK)
    {
        Release(&Asi->Tx);
        return Result;
    }
    *Tx = &Asi->Tx;
    return DTAPI_OK;
}
