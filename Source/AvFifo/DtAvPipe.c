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

// A buffer is rounded to 4 KB pages, whatever the operating system's page size.
#define PIPE_PAGE_BYTES 4096

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPipe_Open -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtAvPipe_Open(DtAvPipe* Pipe, OsDrv* Drv, DtDrvObject Nw, int Type,
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
DtapiResult DtAvPipe_SetBuffer(DtAvPipe* Pipe, size_t Size)
{
    if (Pipe->Ref.Uuid == 0 || Pipe->SharedBuffer.Data != NULL || Size == 0 ||
        Size > INT32_MAX / 2)
        return DTAPI_E_INVALID_ARG;
    DtapiResult Result =
        DtPcieCmd_PipeSetOpMode(Pipe->Drv, Pipe->Ref, DT_PIPE_OPMODE_IDLE);
    if (Result != DTAPI_OK)
        return Result;

    size_t Unit = (size_t)PIPE_PAGE_BYTES * (size_t)Pipe->Props.PrefetchSize;
    size_t Rounded = (Size + Unit - 1) / Unit * Unit;
    if (OsDmaBuffer_Alloc(Rounded, &Pipe->SharedBuffer) != 0)
        return DTAPI_E_OUT_OF_MEM;
    Result = DtPcieCmd_PipeSetSharedBuffer(Pipe->Drv, Pipe->Ref, &Pipe->SharedBuffer);
    if (Result != DTAPI_OK)
    {
        OsDmaBuffer_Free(&Pipe->SharedBuffer);
        return Result;
    }
    Pipe->BufferRegistered = true;
    Pipe->BufferSize = (uint32_t)Pipe->SharedBuffer.Size;
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
        if (Pipe->BufferRegistered)
            DtPcieCmd_PipeReleaseSharedBuffer(Pipe->Drv, Pipe->Ref);
        DtPcieCmd_NwClosePipe(Pipe->Drv, Pipe->Ref);
    }
    OsDmaBuffer_Free(&Pipe->SharedBuffer);
    Pipe->BufferRegistered = false;
    Pipe->Ref.Uuid = 0;
    Pipe->BufferSize = 0;
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPipe_UsableBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint32_t DtAvPipe_UsableBytes(const DtAvPipe* Pipe)
{
    return Pipe->BufferSize - (uint32_t)DtAvPipe_Alignment(Pipe);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriterBegin -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static uint8_t* WriterBegin(void* Context, int MaxSize)
{
    DtAvWriter* Writer = (DtAvWriter*)Context;
    DtAvPipe* Pipe = Writer->Pipe;

    Writer->IsInWrapPacket = Pipe->BufferSize - Pipe->Offset < (uint32_t)MaxSize;
    return Writer->IsInWrapPacket ? Writer->WrapPacket
                                  : Pipe->SharedBuffer.Data + Pipe->Offset;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriterCommit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void WriterCommit(void* Context, int Size)
{
    DtAvWriter* Writer = (DtAvWriter*)Context;
    DtAvPipe* Pipe = Writer->Pipe;

    if (Writer->IsInWrapPacket)
    {
        uint32_t BytesToEnd = Pipe->BufferSize - Pipe->Offset;
        if (BytesToEnd > (uint32_t)Size)
            BytesToEnd = (uint32_t)Size;
        memcpy(Pipe->SharedBuffer.Data + Pipe->Offset, Writer->WrapPacket, BytesToEnd);
        memcpy(Pipe->SharedBuffer.Data, Writer->WrapPacket + BytesToEnd,
               (size_t)Size - BytesToEnd);
    }
    Pipe->Offset = (Pipe->Offset + (uint32_t)Size) % Pipe->BufferSize;
    if (++Writer->UnflushedPackets >= DT_AV_WRITER_FLUSH_EVERY_PACKETS)
    {
        DtapiResult Result = DtAvWriter_Flush(Writer);
        if (Writer->FirstFlushFailure == DTAPI_OK)
            Writer->FirstFlushFailure = Result;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvWriter_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtAvWriter_Init(DtAvWriter* Writer, DtAvPipe* Pipe)
{
    Writer->Pipe = Pipe;
    Writer->Sink.ReserveRoom = WriterBegin;
    Writer->Sink.CommitPacket = WriterCommit;
    Writer->Sink.Context = Writer;
    Writer->IsInWrapPacket = false;
    Writer->UnflushedPackets = 0;
    Writer->FirstFlushFailure = DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvWriter_FreeBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// One data word before the read offset stays free, so that a full buffer and an empty one
// have different offsets; DtAvPipe_UsableBytes keeps the same word.
//
DtapiResult DtAvWriter_FreeBytes(DtAvWriter* Writer, uint32_t* Free)
{
    DtAvPipe* Pipe = Writer->Pipe;
    uint32_t ReadOffset = 0;

    *Free = 0;
    DtapiResult Result = DtPcieCmd_PipeGetTxReadOffset(Pipe->Drv, Pipe->Ref, &ReadOffset);
    if (Result != DTAPI_OK)
        return Result;
    if (ReadOffset >= Pipe->BufferSize)
        return DTAPI_E_DEV_DRIVER;
    const uint32_t Word = (uint32_t)DtAvPipe_Alignment(Pipe);
    *Free = (ReadOffset + Pipe->BufferSize - Word - Pipe->Offset) % Pipe->BufferSize;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvWriter_Flush -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtAvWriter_Flush(DtAvWriter* Writer)
{
    DtAvPipe* Pipe = Writer->Pipe;
    DtapiResult Result = Writer->FirstFlushFailure;

    Writer->FirstFlushFailure = DTAPI_OK;
    if (Writer->UnflushedPackets == 0)
        return Result;
    Writer->UnflushedPackets = 0;
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PacketInOnePiece -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The Size bytes at Offset in one piece: in place, or copied into the scratch packet.
//
static const uint8_t* PacketInOnePiece(DtAvReader* Reader, uint32_t Offset, uint32_t Size)
{
    DtAvPipe* Pipe = Reader->Pipe;
    uint32_t BytesToEnd = Pipe->BufferSize - Offset;

    if (BytesToEnd >= Size)
        return Pipe->SharedBuffer.Data + Offset;
    memcpy(Reader->WrapPacket, Pipe->SharedBuffer.Data + Offset, BytesToEnd);
    memcpy(Reader->WrapPacket + BytesToEnd, Pipe->SharedBuffer.Data, Size - BytesToEnd);
    return Reader->WrapPacket;
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
    if (WriteOffset >= Pipe->BufferSize)
        return DTAPI_E_DEV_DRIVER;

    uint32_t Offset = Pipe->Offset;
    uint32_t Load = (WriteOffset + Pipe->BufferSize - Offset) % Pipe->BufferSize;
    while (Load >= DT_ETHIP_HEADER_SIZE)
    {
        DtEthIpFields Header;
        bool Valid =
            DtEthIp_Read(PacketInOnePiece(Reader, Offset, DT_ETHIP_HEADER_SIZE), &Header);
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
        Func(Context, PacketInOnePiece(Reader, Offset, PacketSize), (int)PacketSize);
        Offset = (Offset + PacketSize) % Pipe->BufferSize;
        Load -= PacketSize;
        (*Packets)++;
    }
    if (Offset == Pipe->Offset)
        return DTAPI_OK;
    Pipe->Offset = Offset;
    return DtPcieCmd_PipeSetRxReadOffset(Pipe->Drv, Pipe->Ref, Offset);
}
