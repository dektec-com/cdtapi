// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtAvPipe.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - A pipe of an IP port, its shared buffer, and the packets in the buffer
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "DtAvPipe.h"       // Interface being implemented.
#include "DtPcie/DtEthIp.h" // Packet headers.
#include "DtPcieAbi.h"      // Pipe types, capabilities and modes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Pipe +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// DTAPI rounds a buffer to 4 KB pages, whatever the operating system's page size.
#define PIPE_PAGE 4096

// A transmit pipe keeps this many bytes free before its read offset.
#define TX_GAP 4

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPipe_Open -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtAvPipe_Open(DtAvPipe* Pipe, OsDrv* Drv, DtPartRef Nw, int Type,
                          int Fallback)
{
    memset(Pipe, 0, sizeof(*Pipe));
    Pipe->Drv = Drv;
    Pipe->Nw = Nw;
    DtapiResult Result = DtPcieCmd_NwOpenPipe(Drv, Nw, Type, Fallback, &Pipe->Ref);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_PipeGetProps(Drv, Pipe->Ref, &Pipe->Props);
    if (Result == DTAPI_OK &&
        (Pipe->Props.DataWidth < 8 || Pipe->Props.DataWidth % 32 != 0 ||
         Pipe->Props.PrefetchSize < 1))
    {
        Result = DTAPI_E_DEV_DRIVER;
    }
    if (Result != DTAPI_OK)
        DtAvPipe_Close(Pipe);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPipe_SetBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DtPalPipe_Nw::AllocateSharedBuffer: idle, a whole number of prefetch sizes, handed to
// the driver.
//
DtapiResult DtAvPipe_SetBuffer(DtAvPipe* Pipe, size_t Size)
{
    if (Pipe->Ref.Uuid == 0 || Pipe->Buf.Data != NULL || Size == 0 ||
        Size > INT32_MAX / 2)
        return DTAPI_E_INVALID_ARG;
    DtapiResult Result =
        DtPcieCmd_PipeSetOpMode(Pipe->Drv, Pipe->Ref, DT_PIPE_OPMODE_IDLE);
    if (Result != DTAPI_OK)
        return Result;

    size_t Unit = (size_t)PIPE_PAGE * (size_t)Pipe->Props.PrefetchSize;
    size_t Rounded = (Size + Unit - 1) / Unit * Unit;
    if (OsDmaBuffer_Alloc(Rounded, &Pipe->Buf) != 0)
        return DTAPI_E_OUT_OF_MEM;
    Result = DtPcieCmd_PipeSetSharedBuffer(Pipe->Drv, Pipe->Ref, &Pipe->Buf);
    if (Result != DTAPI_OK)
    {
        OsDmaBuffer_Free(&Pipe->Buf);
        return Result;
    }
    Pipe->BufferSet = true;
    Pipe->Size = (uint32_t)Pipe->Buf.Size;
    Pipe->Offset = 0;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPipe_Close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtAvPipe_Close(DtAvPipe* Pipe)
{
    if (Pipe->Ref.Uuid != 0)
    {
        DtPcieCmd_PipeSetOpMode(Pipe->Drv, Pipe->Ref, DT_PIPE_OPMODE_IDLE);
        if (Pipe->BufferSet)
            DtPcieCmd_PipeReleaseSharedBuffer(Pipe->Drv, Pipe->Ref);
        DtPcieCmd_NwClosePipe(Pipe->Drv, Pipe->Ref);
    }
    OsDmaBuffer_Free(&Pipe->Buf);
    Pipe->BufferSet = false;
    Pipe->Ref.Uuid = 0;
    Pipe->Size = 0;
    Pipe->Offset = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPipe_IsHardware -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtAvPipe_IsHardware(const DtAvPipe* Pipe)
{
    return (Pipe->Props.Caps & DT_PIPE_CAP_HWP) != 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPipe_IsJumbo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtAvPipe_IsJumbo(const DtAvPipe* Pipe)
{
    return (Pipe->Props.Caps & DT_PIPE_CAP_JFRAME) != 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPipe_Alignment -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtAvPipe_Alignment(const DtAvPipe* Pipe)
{
    return Pipe->Props.DataWidth / 8;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPipe_MaxLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint32_t DtAvPipe_MaxLoad(const DtAvPipe* Pipe)
{
    return Pipe->Size - (uint32_t)DtAvPipe_Alignment(Pipe);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriterBegin -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static uint8_t* WriterBegin(void* Context, int MaxSize)
{
    DtAvWriter* Writer = (DtAvWriter*)Context;
    DtAvPipe* Pipe = Writer->Pipe;

    Writer->InScratch = Pipe->Size - Pipe->Offset < (uint32_t)MaxSize;
    return Writer->InScratch ? Writer->Scratch : Pipe->Buf.Data + Pipe->Offset;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriterCommit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void WriterCommit(void* Context, int Size)
{
    DtAvWriter* Writer = (DtAvWriter*)Context;
    DtAvPipe* Pipe = Writer->Pipe;

    if (Writer->InScratch)
    {
        uint32_t First = Pipe->Size - Pipe->Offset;
        if (First > (uint32_t)Size)
            First = (uint32_t)Size;
        memcpy(Pipe->Buf.Data + Pipe->Offset, Writer->Scratch, First);
        memcpy(Pipe->Buf.Data, Writer->Scratch + First, (size_t)Size - First);
    }
    Pipe->Offset = (Pipe->Offset + (uint32_t)Size) % Pipe->Size;
    if (++Writer->Unflushed >= DT_AV_WRITER_BATCH)
    {
        DtapiResult Result = DtAvWriter_Flush(Writer);
        if (Writer->Result == DTAPI_OK)
            Writer->Result = Result;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvWriter_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtAvWriter_Init(DtAvWriter* Writer, DtAvPipe* Pipe)
{
    Writer->Pipe = Pipe;
    Writer->Sink.Begin = WriterBegin;
    Writer->Sink.Commit = WriterCommit;
    Writer->Sink.Context = Writer;
    Writer->InScratch = false;
    Writer->Unflushed = 0;
    Writer->Result = DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvWriter_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtAvWriter_Free(DtAvWriter* Writer, uint32_t* Free)
{
    DtAvPipe* Pipe = Writer->Pipe;
    uint32_t ReadOffset = 0;

    *Free = 0;
    DtapiResult Result = DtPcieCmd_PipeGetTxReadOffset(Pipe->Drv, Pipe->Ref, &ReadOffset);
    if (Result != DTAPI_OK)
        return Result;
    if (ReadOffset >= Pipe->Size)
        return DTAPI_E_DEV_DRIVER;
    *Free = (ReadOffset + Pipe->Size - TX_GAP - Pipe->Offset) % Pipe->Size;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvWriter_Flush -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtAvWriter_Flush(DtAvWriter* Writer)
{
    DtAvPipe* Pipe = Writer->Pipe;
    DtapiResult Result = Writer->Result;

    Writer->Result = DTAPI_OK;
    if (Writer->Unflushed == 0)
        return Result;
    Writer->Unflushed = 0;
    DtapiResult Set = DtPcieCmd_PipeSetTxWriteOffset(Pipe->Drv, Pipe->Ref, Pipe->Offset);
    return Result != DTAPI_OK ? Result : Set;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reception +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvReader_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtAvReader_Init(DtAvReader* Reader, DtAvPipe* Pipe)
{
    Reader->Pipe = Pipe;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Gather -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The Size bytes at Offset in one piece: in place, or copied into the scratch packet.
//
static const uint8_t* Gather(DtAvReader* Reader, uint32_t Offset, uint32_t Size)
{
    DtAvPipe* Pipe = Reader->Pipe;
    uint32_t First = Pipe->Size - Offset;

    if (First >= Size)
        return Pipe->Buf.Data + Offset;
    memcpy(Reader->Scratch, Pipe->Buf.Data + Offset, First);
    memcpy(Reader->Scratch + First, Pipe->Buf.Data, Size - First);
    return Reader->Scratch;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvReader_Pass -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtAvReader_Pass(DtAvReader* Reader, DtAvPacketFunc Func, void* Context,
                            int* Packets, bool* LostSync)
{
    DtAvPipe* Pipe = Reader->Pipe;
    uint32_t WriteOffset = 0;

    *Packets = 0;
    *LostSync = false;
    DtapiResult Result =
        DtPcieCmd_PipeGetRxWriteOffset(Pipe->Drv, Pipe->Ref, &WriteOffset);
    if (Result != DTAPI_OK)
        return Result;
    if (WriteOffset >= Pipe->Size)
        return DTAPI_E_DEV_DRIVER;

    uint32_t Offset = Pipe->Offset;
    uint32_t Load = (WriteOffset + Pipe->Size - Offset) % Pipe->Size;
    while (Load >= DT_ETHIP_HEADER_SIZE)
    {
        DtEthIpFields Header;
        bool Valid = DtEthIp_Read(Gather(Reader, Offset, DT_ETHIP_HEADER_SIZE), &Header);
        uint32_t PacketSize = (uint32_t)Header.NumWords * DT_ETHIP_WORD_SIZE;
        if (!Valid || PacketSize < DT_ETHIP_HEADER_SIZE ||
            PacketSize > DT_AV_PIPE_MAX_PACKET)
        {
            *LostSync = true;
            Offset = WriteOffset;
            break;
        }
        if (Load < PacketSize)
            break;
        Func(Context, Gather(Reader, Offset, PacketSize), (int)PacketSize);
        Offset = (Offset + PacketSize) % Pipe->Size;
        Load -= PacketSize;
        (*Packets)++;
    }
    if (Offset == Pipe->Offset)
        return DTAPI_OK;
    Pipe->Offset = Offset;
    return DtPcieCmd_PipeSetRxReadOffset(Pipe->Drv, Pipe->Ref, Offset);
}
