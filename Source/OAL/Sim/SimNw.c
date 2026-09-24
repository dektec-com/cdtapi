// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# SimNw.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The emulated network function of an IP port, its pipes and test controls
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdlib.h>
#include <string.h>
#include <time.h>

// CDTAPI includes
#include "Core/DtAlloc.h"   // Allocation seam.
#include "Core/DtVec.h"     // Queues of packets.
#include "DtPcie/DtEthIp.h" // The header of the packets in a pipe's buffer.
#include "DtPcieAbi.h"      // The driver ABI the emulator answers in.
#include "SimDtPcie.h"      // The lock.
#include "SimDta2110.h"     // The port's MAC address, pipes and alignment.
#include "SimNw.h"          // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The most bytes each of the driver's own queues holds: the common receive queue and the
// queue of the scheduler.
#define SIM_NW_QUEUE_BYTES (128u * 1024 * 1024)

// The pages of a hardware pipe's buffer are this size.
#define SIM_NW_PAGE_SIZE 4096

// The largest buffer a hardware pipe takes.
#define SIM_NW_MAX_HWP_BUFFER (256u * 1024 * 1024)

// The hardware pipes: transmit, then receive.
#define SIM_NW_HW_PIPES (2 * SIM_DTA2110_HW_PIPES)

typedef struct SimPipe
{
    int Id;
    int Type;
    bool Hw;
    bool Rx;
    bool InUse;
    void* Owner; // The handle that opened it
    int OpMode;
    bool BufferSet;
    uint8_t* Buffer;
    size_t BufferSize;
    uint32_t ReadOffset;
    uint32_t WriteOffset;
    uint32_t CachedWriteOffset; // A hardware transmit pipe's write offset in STANDBY
    uint32_t ErrorFlags;
    bool FilterSet;
    DtIoctlPipeCmdSetIpFilterInput Filter;
} SimPipe;

// A packet in one of the queues: a whole packet of a pipe's buffer in the scheduler, or
// an Ethernet frame arriving at or waiting at the receive side.
typedef struct SimItem
{
    uint64_t TimeNs; // When it is sent or arrives
    int PipeId;      // The pipe it came from, 0 for none
    uint8_t* Data;
    size_t Size;
} SimItem;

typedef struct SimQueue
{
    DtVec Items; // SimItem, ordered by time from Head
    size_t Head;
    size_t Bytes;
} SimQueue;

static struct
{
    bool Initialised;
    SimPipe HwPipes[SIM_NW_HW_PIPES];
    SimPipe* SwPipes[SIM_NW_MAX_PIPES + 1]; // By pipe number
    int LastSwPipe;                         // The highest number a software pipe had
    SimPipe* Candidates[SIM_NW_MAX_PIPES];  // Software transmit pipes in an interval
    SimQueue Scheduler;
    SimQueue Arriving;
    SimQueue Received;
    SimItem Kept[SIM_NW_KEPT_PACKETS]; // A ring of the frames sent last
    int KeptFirst;
    int NumKept;
    int Sent;
    SimNwCounters Counters;
    bool AsLinux;
    bool LinkUp;
    bool Loopback;
    bool ManualTime;
    uint64_t TimeNs;
    uint64_t DoneNs; // How far packets have moved
} g_Nw;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EnsureNw -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void EnsureNw(void)
{
    if (!g_Nw.Initialised)
        SimNw_Reset();
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Queues +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- QueueCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static size_t QueueCount(const SimQueue* Queue)
{
    return DtVec_Count(&Queue->Items) - Queue->Head;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- QueueFront -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static SimItem* QueueFront(const SimQueue* Queue)
{
    return QueueCount(Queue) > 0 ? (SimItem*)DtVec_At(&Queue->Items, Queue->Head) : NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- QueuePop -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Takes the front item out; its data becomes the caller's. The storage in front of the
// head is given back once it is most of the vector.
//
static SimItem QueuePop(SimQueue* Queue)
{
    SimItem Item = *QueueFront(Queue);

    Queue->Head++;
    Queue->Bytes -= Item.Size;
    size_t Count = DtVec_Count(&Queue->Items);
    if (Queue->Head == Count)
    {
        DtVec_Clear(&Queue->Items);
        Queue->Head = 0;
    }
    else if (Queue->Head > 1024 && Queue->Head > Count / 2)
    {
        memmove(Queue->Items.Data, (uint8_t*)DtVec_At(&Queue->Items, Queue->Head),
                (Count - Queue->Head) * sizeof(SimItem));
        DtVec_Resize(&Queue->Items, Count - Queue->Head);
        Queue->Head = 0;
    }
    return Item;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- QueueAdd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Puts Item behind every item that is not later, and takes over its data. False, freeing
// nothing, when there is no memory.
//
static bool QueueAdd(SimQueue* Queue, const SimItem* Item)
{
    if (DtVec_Push(&Queue->Items, Item) != 0)
        return false;

    size_t Last = DtVec_Count(&Queue->Items) - 1;
    size_t To = Last;
    while (To > Queue->Head &&
           ((SimItem*)DtVec_At(&Queue->Items, To - 1))->TimeNs > Item->TimeNs)
    {
        To--;
    }
    if (To != Last)
    {
        SimItem* Items = (SimItem*)Queue->Items.Data;
        memmove(&Items[To + 1], &Items[To], (Last - To) * sizeof(SimItem));
        Items[To] = *Item;
    }
    Queue->Bytes += Item->Size;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- QueueFree -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void QueueFree(SimQueue* Queue)
{
    while (QueueCount(Queue) > 0)
        DtAlloc_Free(QueuePop(Queue).Data);
    DtVec_Free(&Queue->Items);
    DtVec_Init(&Queue->Items, sizeof(SimItem));
    Queue->Head = 0;
    Queue->Bytes = 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Pipes +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindPipe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The pipe with number Id; NULL for the driver's own queues and for a pipe that does not
// exist.
//
static SimPipe* FindPipe(int Id)
{
    if (Id >= SIM_NW_FIRST_TX_HWP && Id < SIM_NW_FIRST_SWP)
        return &g_Nw.HwPipes[Id - SIM_NW_FIRST_TX_HWP];
    if (Id >= SIM_NW_FIRST_SWP && Id <= SIM_NW_MAX_PIPES)
        return g_Nw.SwPipes[Id];
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsTx -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool IsTx(const SimPipe* Pipe)
{
    return !Pipe->Rx;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Load -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The bytes between the read and the write offset.
//
static size_t Load(const SimPipe* Pipe)
{
    size_t Size = Pipe->BufferSize;
    return ((size_t)Pipe->WriteOffset % Size + Size - (size_t)Pipe->ReadOffset % Size) %
           Size;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopyOut -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Count bytes of the buffer from Offset, across its end.
//
static void CopyOut(const SimPipe* Pipe, size_t Offset, uint8_t* To, size_t Count)
{
    size_t From = Offset % Pipe->BufferSize;
    size_t First = Pipe->BufferSize - From < Count ? Pipe->BufferSize - From : Count;

    memcpy(To, Pipe->Buffer + From, First);
    memcpy(To + First, Pipe->Buffer, Count - First);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WritePacket -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes a packet of Size bytes into a receive pipe at its write offset, as the driver
// does: one data word stays free, a packet that does not fit sets the overflow error and
// is lost, and one that fits clears it.
//
static bool WritePacket(SimPipe* Pipe, const uint8_t* Packet, size_t Size)
{
    size_t MaxLoad = Pipe->BufferSize - DT_ETHIP_WORD_SIZE;
    size_t Free = MaxLoad;

    if (Pipe->ReadOffset != Pipe->WriteOffset)
    {
        Free = (Pipe->ReadOffset % Pipe->BufferSize + Pipe->BufferSize -
                Pipe->WriteOffset % Pipe->BufferSize) %
               Pipe->BufferSize;
        if (Free > MaxLoad)
            Free = MaxLoad;
    }
    if (Free <= Size)
    {
        Pipe->ErrorFlags |= DT_PIPE_ERROR_OVERFLOW;
        g_Nw.Counters.Lost++;
        return false;
    }
    Pipe->ErrorFlags &= ~(uint32_t)DT_PIPE_ERROR_OVERFLOW;

    size_t Offset = Pipe->WriteOffset % Pipe->BufferSize;
    size_t First = Pipe->BufferSize - Offset < Size ? Pipe->BufferSize - Offset : Size;
    memcpy(Pipe->Buffer + Offset, Packet, First);
    memcpy(Pipe->Buffer, Packet + First, Size - First);
    Pipe->WriteOffset = (uint32_t)((Offset + Size) % Pipe->BufferSize);
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClosePipe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Takes the pipe out of use, idle and without a buffer, and frees a software pipe. A
// hardware pipe keeps its filter, offsets and error flags, and packets already in the
// scheduler are still sent.
//
static void ClosePipe(SimPipe* Pipe)
{
    Pipe->OpMode = DT_PIPE_OPMODE_IDLE;
    Pipe->BufferSet = false;
    Pipe->Buffer = NULL;
    Pipe->BufferSize = 0;
    Pipe->InUse = false;
    Pipe->Owner = NULL;
    if (!Pipe->Hw)
    {
        g_Nw.SwPipes[Pipe->Id] = NULL;
        DtAlloc_Free(Pipe);
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Packets +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// What the receive side reads of a frame.
typedef struct SimFrameInfo
{
    int PacketType; // A DT_ETHIP_TYPE_ value
    bool Udp;
    int IpOffset;   // Bytes from the start of the frame to the source IP address
    int PortOffset; // Bytes from the frame's start to the transport header, 0 for none
    const uint8_t* SrcIp;
    const uint8_t* DstIp;
    uint16_t SrcPort;
    uint16_t DstPort;
    uint32_t VlanFlags; // DT_PIPE_IPFLT_FLAG_VLAN0_1AD and _VLAN1_1AD for each tag
    int VlanId[2];
} SimFrameInfo;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Get16 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The big-endian 16 bits at Bytes.
//
static uint16_t Get16(const uint8_t* Bytes)
{
    return (uint16_t)(Bytes[0] << 8 | Bytes[1]);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the Ethernet, VLAN, IP and UDP headers of a frame. A frame too short for its IP
// header is of the other type; one too short for the ports has no port offset.
//
static void ReadFrame(const uint8_t* Frame, size_t Size, SimFrameInfo* Info)
{
    memset(Info, 0, sizeof(*Info));
    Info->PacketType = DT_ETHIP_TYPE_OTHER;
    if (Size < 14)
        return;

    size_t Offset = 12;
    uint16_t Type = Get16(Frame + Offset);
    for (int Tag = 0; Tag < 2 && (Type == 0x8100 || Type == 0x88A8) && Size >= Offset + 8;
         Tag++)
    {
        Info->VlanFlags |=
            Tag == 0 ? DT_PIPE_IPFLT_FLAG_VLAN0_1AD : DT_PIPE_IPFLT_FLAG_VLAN1_1AD;
        Info->VlanId[Tag] = Get16(Frame + Offset + 2) & 0xFFF;
        Offset += 4;
        Type = Get16(Frame + Offset);
    }
    Offset += 2;

    size_t Next;
    int IsUdp;
    if (Type == 0x0800 && Size >= Offset + 20)
    {
        Info->PacketType = DT_ETHIP_TYPE_IPV4;
        Info->IpOffset = (int)Offset + 12;
        Info->SrcIp = Frame + Offset + 12;
        Info->DstIp = Frame + Offset + 16;
        IsUdp = Frame[Offset + 9];
        Next = Offset + 4 * (size_t)(Frame[Offset] & 0xF);
    }
    else if (Type == 0x86DD && Size >= Offset + 40)
    {
        Info->PacketType = DT_ETHIP_TYPE_IPV6;
        Info->IpOffset = (int)Offset + 8;
        Info->SrcIp = Frame + Offset + 8;
        Info->DstIp = Frame + Offset + 24;
        IsUdp = Frame[Offset + 6];
        Next = Offset + 40;
    }
    else
        return;

    if (Size < Next + 8)
        return;
    Info->Udp = IsUdp == 17;
    Info->PortOffset = (int)Next;
    Info->SrcPort = Get16(Frame + Next);
    Info->DstPort = Get16(Frame + Next + 2);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IpMatches -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The address, version and VLAN part of a filter, the same for both kinds of pipe.
//
static bool IpMatches(const DtIoctlPipeCmdSetIpFilterInput* Filter,
                      const SimFrameInfo* Info)
{
    uint32_t Flags = (uint32_t)Filter->m_Flags;
    bool V4 = Info->PacketType == DT_ETHIP_TYPE_IPV4;
    size_t Length = V4 ? 4 : 16;

    if (Info->PacketType != DT_ETHIP_TYPE_IPV4 && Info->PacketType != DT_ETHIP_TYPE_IPV6)
        return false;
    if (V4 && (Flags & (DT_PIPE_IPFLT_FLAG_EN_SRCIP_IPV6 |
                        DT_PIPE_IPFLT_FLAG_EN_DSTIP_IPV6)) != 0)
        return false;
    if (!V4 && (Flags & (DT_PIPE_IPFLT_FLAG_EN_SRCIP_IPV4 |
                         DT_PIPE_IPFLT_FLAG_EN_DSTIP_IPV4)) != 0)
        return false;
    uint32_t VlanFlags = DT_PIPE_IPFLT_FLAG_VLAN0_1AD | DT_PIPE_IPFLT_FLAG_VLAN1_1AD;
    if ((Info->VlanFlags & VlanFlags) != (Flags & VlanFlags))
        return false;
    if ((Flags & (DT_PIPE_IPFLT_FLAG_EN_SRCIP_IPV4 | DT_PIPE_IPFLT_FLAG_EN_SRCIP_IPV6)) !=
            0 &&
        memcmp(Filter->m_SrcIp, Info->SrcIp, Length) != 0)
    {
        return false;
    }
    if ((Flags & (DT_PIPE_IPFLT_FLAG_EN_DSTIP_IPV4 | DT_PIPE_IPFLT_FLAG_EN_DSTIP_IPV6)) !=
            0 &&
        memcmp(Filter->m_DstIp, Info->DstIp, Length) != 0)
    {
        return false;
    }
    if ((Flags & DT_PIPE_IPFLT_FLAG_EN_VLAN) != 0)
    {
        if ((Flags & DT_PIPE_IPFLT_FLAG_VLAN0_1AD) != 0 &&
            Filter->m_VlanId[0] != Info->VlanId[0])
            return false;
        if ((Flags & DT_PIPE_IPFLT_FLAG_VLAN1_1AD) != 0 &&
            Filter->m_VlanId[1] != Info->VlanId[1])
            return false;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PortIndex -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Which of the filter's three ports, enabled by FirstFlag and the two flags after it,
// equals Port: its index; -1 when no enabled one does, 3 when none is enabled.
//
static int PortIndex(uint32_t Flags, uint32_t FirstFlag, const UInt16* Ports,
                     uint16_t Port)
{
    bool Enabled = false;

    for (int i = 0; i < 3; i++)
    {
        if ((Flags & FirstFlag << i) == 0)
            continue;
        Enabled = true;
        if (Ports[i] == Port)
            return i;
    }
    return Enabled ? -1 : 3;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HwPipeTakes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether a hardware receive pipe's router takes the frame: the filter is on, the
// addresses match, and the ports match one of the enabled ones. *SubStream receives the
// index of the destination port that matched, or of the source port, or 0.
//
static bool HwPipeTakes(const SimPipe* Pipe, const SimFrameInfo* Info, int* SubStream)
{
    const DtIoctlPipeCmdSetIpFilterInput* Filter = &Pipe->Filter;
    uint32_t Flags = (uint32_t)Filter->m_Flags;

    if (!Pipe->FilterSet || (Flags & DT_PIPE_IPFLT_FLAG_EN_FILT) == 0 ||
        Info->PortOffset == 0 || !IpMatches(Filter, Info))
    {
        return false;
    }
    int Src = PortIndex(Flags, DT_PIPE_IPFLT_FLAG_EN_SRCPORT0, Filter->m_SrcPort,
                        Info->SrcPort);
    int Dst = PortIndex(Flags, DT_PIPE_IPFLT_FLAG_EN_DSTPORT0, Filter->m_DstPort,
                        Info->DstPort);
    if (Src < 0 || Dst < 0)
        return false;
    *SubStream = Dst < 3 ? Dst : Src < 3 ? Src : 0;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SwPipeTakes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether the pipe's filter takes a frame of the common receive queue, which is of
// substream 0, found through the destination ports the pipe listens on.
//
static bool SwPipeTakes(const SimPipe* Pipe, const SimFrameInfo* Info)
{
    const DtIoctlPipeCmdSetIpFilterInput* Filter = &Pipe->Filter;
    uint32_t Flags = (uint32_t)Filter->m_Flags;

    if (!Pipe->FilterSet || Pipe->OpMode != DT_PIPE_OPMODE_RUN ||
        (Flags & DT_PIPE_IPFLT_FLAG_EN_FILT) == 0 || Info->PortOffset == 0 ||
        PortIndex(Flags, DT_PIPE_IPFLT_FLAG_EN_DSTPORT0, Filter->m_DstPort,
                  Info->DstPort) < 0 ||
        (Flags & (DT_PIPE_IPFLT_FLAG_EN_DSTPORT0 | DT_PIPE_IPFLT_FLAG_EN_DSTPORT1 |
                  DT_PIPE_IPFLT_FLAG_EN_DSTPORT2)) == 0 ||
        !IpMatches(Filter, Info))
    {
        return false;
    }
    if ((Flags & DT_PIPE_IPFLT_FLAG_EN_SRCPORT0) != 0 &&
        Filter->m_SrcPort[0] != Info->SrcPort)
        return false;
    if ((Flags & DT_PIPE_IPFLT_FLAG_EN_DSTPORT0) != 0 &&
        Filter->m_DstPort[0] != Info->DstPort)
        return false;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BuildPacket -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A packet of a receive pipe's buffer for a frame that arrived at TimeNs, allocated with
// its size in *PacketSize; NULL when there is no memory.
//
static uint8_t* BuildPacket(const uint8_t* Frame, size_t Size, const SimFrameInfo* Info,
                            int SubStream, uint64_t TimeNs, size_t* PacketSize)
{
    DtEthIpFields Header;

    memset(&Header, 0, sizeof(Header));
    Header.FrameSize = (int)Size;
    Header.HeaderV2 = Size > DT_ETHIP_MAX_FRAME_V1;
    Header.NumWords = DtEthIp_NumWords((int)Size, SIM_DTA2110_PACKET_ALIGNMENT);
    Header.PacketType = Info->PacketType;
    Header.IsUdp = Info->Udp ? DT_ETHIP_PROTO_UDP : 0;
    if (Info->PacketType != DT_ETHIP_TYPE_OTHER)
    {
        Header.IpAddressOffset = DT_ETHIP_HEADER_SIZE + Info->IpOffset;
        Header.PortOffset =
            Info->PortOffset != 0 ? DT_ETHIP_HEADER_SIZE + Info->PortOffset : 0;
    }
    Header.SubStream = SubStream;
    Header.TimestampValid = true;
    Header.Seconds = (uint32_t)(TimeNs / 1000000000u);
    Header.Nanoseconds = (uint32_t)(TimeNs % 1000000000u);

    *PacketSize = (size_t)Header.NumWords * DT_ETHIP_WORD_SIZE;
    uint8_t* Packet = (uint8_t*)DtAlloc_Malloc(*PacketSize);
    if (Packet == NULL)
        return NULL;
    memset(Packet, 0, *PacketSize);
    DtEthIp_Write(&Header, Packet);
    memcpy(Packet + DT_ETHIP_HEADER_SIZE, Frame, Size);
    return Packet;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Keep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Keeps a sent frame, whose data becomes the ring's, dropping the oldest when full.
//
static void Keep(const SimItem* Frame)
{
    int Slot = (g_Nw.KeptFirst + g_Nw.NumKept) % SIM_NW_KEPT_PACKETS;

    if (g_Nw.NumKept == SIM_NW_KEPT_PACKETS)
    {
        DtAlloc_Free(g_Nw.Kept[g_Nw.KeptFirst].Data);
        g_Nw.KeptFirst = (g_Nw.KeptFirst + 1) % SIM_NW_KEPT_PACKETS;
        g_Nw.NumKept--;
    }
    g_Nw.Kept[Slot] = *Frame;
    g_Nw.NumKept++;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Arrive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A frame arrives at the receive side: a hardware receive pipe takes it, or it waits in
// the common receive queue for the next interval, or it is lost when that is full. The
// frame's data is freed unless the queue takes it over.
//
static void Arrive(SimItem* Frame)
{
    SimFrameInfo Info;

    ReadFrame(Frame->Data, Frame->Size, &Info);
    for (int i = 0; i < SIM_DTA2110_HW_PIPES; i++)
    {
        SimPipe* Pipe = &g_Nw.HwPipes[SIM_DTA2110_HW_PIPES + i];
        int SubStream = 0;

        if (!HwPipeTakes(Pipe, &Info, &SubStream))
            continue;
        size_t Size = 0;
        uint8_t* Packet = NULL;
        if (Pipe->OpMode == DT_PIPE_OPMODE_RUN && Pipe->BufferSet)
            Packet = BuildPacket(Frame->Data, Frame->Size, &Info, SubStream,
                                 Frame->TimeNs, &Size);
        if (Packet != NULL)
            WritePacket(Pipe, Packet, Size);
        else
            g_Nw.Counters.Lost++;
        DtAlloc_Free(Packet);
        DtAlloc_Free(Frame->Data);
        return;
    }

    if (g_Nw.Received.Bytes + Frame->Size > SIM_NW_QUEUE_BYTES ||
        !QueueAdd(&g_Nw.Received, Frame))
    {
        g_Nw.Counters.Lost++;
        DtAlloc_Free(Frame->Data);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Send -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The scheduler sends a packet of a pipe's buffer: its frame is kept and, with the
// loopback on, arrives at the receive side. Frees the packet.
//
static void Send(SimItem* Packet)
{
    DtEthIpFields Header;

    DtEthIp_Read(Packet->Data, &Header);
    int HeaderSize = DtEthIp_HeaderSize(Header.PacketType);
    SimItem Frame = {Packet->TimeNs, Packet->PipeId, NULL, (size_t)Header.FrameSize};

    g_Nw.Sent++;
    Frame.Data = (uint8_t*)DtAlloc_Malloc(Frame.Size > 0 ? Frame.Size : 1);
    if (Frame.Data != NULL)
    {
        memcpy(Frame.Data, Packet->Data + HeaderSize, Frame.Size);
        Keep(&Frame);
        if (g_Nw.Loopback)
        {
            SimItem Copy = Frame;
            Copy.Data = (uint8_t*)DtAlloc_Malloc(Frame.Size > 0 ? Frame.Size : 1);
            if (Copy.Data != NULL)
            {
                memcpy(Copy.Data, Frame.Data, Frame.Size);
                if (!QueueAdd(&g_Nw.Arriving, &Copy))
                    DtAlloc_Free(Copy.Data);
            }
        }
    }
    DtAlloc_Free(Packet->Data);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TodOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static uint64_t TodOf(const DtEthIpFields* Header)
{
    return (uint64_t)Header->Seconds * 1000000000u + Header->Nanoseconds;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HeadPacket -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the header of the packet at a transmit pipe's read offset. False when the pipe
// holds no whole packet; a header that does not check is counted, and the pipe's data
// skipped.
//
static bool HeadPacket(SimPipe* Pipe, DtEthIpFields* Header)
{
    uint8_t Bytes[DT_ETHIP_HEADER_SIZE];
    size_t Available = Load(Pipe);

    if (Available < DT_ETHIP_HEADER_SIZE)
        return false;
    CopyOut(Pipe, Pipe->ReadOffset, Bytes, sizeof(Bytes));
    if (!DtEthIp_Read(Bytes, Header) || Header->NumWords == 0)
    {
        g_Nw.Counters.HeaderErrors++;
        Pipe->ReadOffset = Pipe->WriteOffset;
        return false;
    }
    return (size_t)Header->NumWords * DT_ETHIP_WORD_SIZE <= Available;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsFarFrom -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool IsFarFrom(uint64_t A, uint64_t B)
{
    return (A > B ? A - B : B - A) > SIM_NW_MAX_DELAY_NS;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Schedule -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Moves the packet at a transmit pipe's read offset into the scheduler at NowNs, to be
// sent at its time or at once. False when the scheduler has no room.
//
static bool Schedule(SimPipe* Pipe, const DtEthIpFields* Header, uint64_t NowNs)
{
    size_t Size = (size_t)Header->NumWords * DT_ETHIP_WORD_SIZE;
    uint64_t Tod = TodOf(Header);
    SimItem Item = {Tod > NowNs ? Tod : NowNs, Pipe->Id, NULL, Size};

    if (g_Nw.Scheduler.Bytes + Size > SIM_NW_QUEUE_BYTES)
        return false;
    Item.Data = (uint8_t*)DtAlloc_Malloc(Size);
    if (Item.Data == NULL)
        return false;
    CopyOut(Pipe, Pipe->ReadOffset, Item.Data, Size);
    if (!QueueAdd(&g_Nw.Scheduler, &Item))
    {
        DtAlloc_Free(Item.Data);
        return false;
    }
    Pipe->ReadOffset = (uint32_t)((Pipe->ReadOffset + Size) % Pipe->BufferSize);
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CanTransmit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool CanTransmit(const SimPipe* Pipe)
{
    return Pipe != NULL && IsTx(Pipe) && Pipe->OpMode == DT_PIPE_OPMODE_RUN &&
           Pipe->BufferSet && (Pipe->ErrorFlags & DT_PIPE_ERROR_INVALID_TIME) == 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TakeHwPackets -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The hardware transmit pipes hand their whole packets to the scheduler at once.
//
static void TakeHwPackets(uint64_t NowNs)
{
    for (int i = 0; i < SIM_DTA2110_HW_PIPES; i++)
    {
        SimPipe* Pipe = &g_Nw.HwPipes[i];
        DtEthIpFields Header;

        while (CanTransmit(Pipe) && HeadPacket(Pipe, &Header))
        {
            if (IsFarFrom(TodOf(&Header), NowNs))
            {
                Pipe->ErrorFlags |= DT_PIPE_ERROR_INVALID_TIME;
                break;
            }
            if (!Schedule(Pipe, &Header, NowNs))
                break;
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Deliver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sends what the scheduler has due by NowNs, then lets what is due arrive.
//
static void Deliver(uint64_t NowNs)
{
    while (QueueCount(&g_Nw.Scheduler) > 0 &&
           QueueFront(&g_Nw.Scheduler)->TimeNs <= NowNs)
    {
        SimItem Packet = QueuePop(&g_Nw.Scheduler);
        Send(&Packet);
    }
    while (QueueCount(&g_Nw.Arriving) > 0 && QueueFront(&g_Nw.Arriving)->TimeNs <= NowNs)
    {
        SimItem Frame = QueuePop(&g_Nw.Arriving);
        Arrive(&Frame);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Interval -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The periodic interval at TickNs: the software transmit pipes first, then the common
// receive queue.
//
static void Interval(uint64_t TickNs)
{
    uint64_t EndNs = TickNs + SIM_NW_LOOKAHEAD_NS;
    int NumCandidates = 0;

    for (int Id = SIM_NW_FIRST_SWP; Id <= g_Nw.LastSwPipe; Id++)
    {
        if (CanTransmit(g_Nw.SwPipes[Id]))
            g_Nw.Candidates[NumCandidates++] = g_Nw.SwPipes[Id];
    }
    for (;;)
    {
        int Earliest = -1;
        uint64_t EarliestTod = 0;
        DtEthIpFields EarliestHeader = {0};

        for (int i = 0; i < NumCandidates; i++)
        {
            SimPipe* Pipe = g_Nw.Candidates[i];
            DtEthIpFields Header = {0};
            uint64_t Tod = 0;
            bool Due = CanTransmit(Pipe) && HeadPacket(Pipe, &Header);

            if (Due)
            {
                Tod = TodOf(&Header);
                if (IsFarFrom(Tod, EndNs))
                    Pipe->ErrorFlags |= DT_PIPE_ERROR_INVALID_TIME;
                Due = !IsFarFrom(Tod, EndNs) && Tod < EndNs;
            }
            if (!Due)
            {
                g_Nw.Candidates[i--] = g_Nw.Candidates[--NumCandidates];
                continue;
            }
            if (Earliest < 0 || Tod < EarliestTod)
            {
                Earliest = i;
                EarliestTod = Tod;
                EarliestHeader = Header;
            }
        }
        if (Earliest < 0 || !Schedule(g_Nw.Candidates[Earliest], &EarliestHeader, TickNs))
            break;
    }

    while (QueueCount(&g_Nw.Received) > 0)
    {
        SimItem Frame = QueuePop(&g_Nw.Received);
        SimFrameInfo Info;
        bool Taken = false;

        ReadFrame(Frame.Data, Frame.Size, &Info);
        for (int Id = SIM_NW_FIRST_SWP; Id <= g_Nw.LastSwPipe; Id++)
        {
            SimPipe* Pipe = g_Nw.SwPipes[Id];

            if (Pipe == NULL || !Pipe->Rx || !SwPipeTakes(Pipe, &Info))
                continue;
            Taken = true;
            size_t Size = 0;
            uint8_t* Packet = NULL;
            if (Pipe->BufferSet)
                Packet =
                    BuildPacket(Frame.Data, Frame.Size, &Info, 0, Frame.TimeNs, &Size);
            if (Packet != NULL)
                WritePacket(Pipe, Packet, Size);
            else
                g_Nw.Counters.Lost++;
            DtAlloc_Free(Packet);
        }
        if (!Taken)
            g_Nw.Counters.ToOperatingSystem++;
        DtAlloc_Free(Frame.Data);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NextWorkNs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The earliest time from the interval at TickNs on at which anything can move: that
// interval for a frame in the receive queue, or for a software transmit pipe whose first
// packet is due there or is far from its time; otherwise the scheduler's first packet,
// the first arriving frame, and the time a software transmit pipe's first packet comes
// within reach. UINT64_MAX for none.
//
static uint64_t NextWorkNs(uint64_t TickNs)
{
    uint64_t Next = UINT64_MAX;
    uint64_t EndNs = TickNs + SIM_NW_LOOKAHEAD_NS;

    if (QueueCount(&g_Nw.Received) > 0)
        return TickNs;
    if (QueueCount(&g_Nw.Scheduler) > 0)
        Next = QueueFront(&g_Nw.Scheduler)->TimeNs;
    if (QueueCount(&g_Nw.Arriving) > 0 && QueueFront(&g_Nw.Arriving)->TimeNs < Next)
        Next = QueueFront(&g_Nw.Arriving)->TimeNs;
    for (int Id = SIM_NW_FIRST_SWP; Id <= g_Nw.LastSwPipe; Id++)
    {
        SimPipe* Pipe = g_Nw.SwPipes[Id];
        uint8_t Bytes[DT_ETHIP_HEADER_SIZE];
        DtEthIpFields Header;

        if (!CanTransmit(Pipe) || Load(Pipe) < DT_ETHIP_HEADER_SIZE)
            continue;
        CopyOut(Pipe, Pipe->ReadOffset, Bytes, sizeof(Bytes));
        if (!DtEthIp_Read(Bytes, &Header))
            return TickNs;
        uint64_t Tod = TodOf(&Header);
        if (Tod < EndNs || IsFarFrom(Tod, EndNs))
            return TickNs;
        if (Tod - SIM_NW_LOOKAHEAD_NS < Next)
            Next = Tod - SIM_NW_LOOKAHEAD_NS;
    }
    return Next;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Run -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Moves packets from where they were left up to now. The intervals before the first at
// which anything can move are skipped.
//
static void Run(void)
{
    uint64_t NowNs = SimNw_Now();

    if (NowNs < g_Nw.DoneNs)
        g_Nw.DoneNs = NowNs;
    TakeHwPackets(NowNs);
    for (;;)
    {
        uint64_t Tick = (g_Nw.DoneNs / SIM_NW_INTERVAL_NS + 1) * SIM_NW_INTERVAL_NS;
        if (Tick > NowNs)
            break;
        uint64_t Work = NextWorkNs(Tick);
        if (Work == UINT64_MAX)
            break;
        if (Work > Tick)
        {
            uint64_t First =
                (Work + SIM_NW_INTERVAL_NS - 1) / SIM_NW_INTERVAL_NS * SIM_NW_INTERVAL_NS;
            if (First > NowNs)
                break;
            g_Nw.DoneNs = First - SIM_NW_INTERVAL_NS;
            continue;
        }
        Deliver(Tick);
        Interval(Tick);
        g_Nw.DoneNs = Tick;
    }
    Deliver(NowNs);
    g_Nw.DoneNs = NowNs;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// What the driver's I/O stub knows of a command: its sizes. None of the modelled commands
// needs exclusive access.
typedef struct SimNwCmdProps
{
    int FunctionCode;
    int Cmd;
    size_t InSize;
    size_t OutSize;
} SimNwCmdProps;

#define HDR sizeof(DtIoctlInputDataHdr)

static const SimNwCmdProps g_Cmds[] = {
    {DT_FUNC_CODE_EMAC_CMD, DT_EMAC_CMD_GET_MACADDRESS, HDR,
     sizeof(DtIoctlEMACCmdGetMacAddressOutput)},
    {DT_FUNC_CODE_EMAC_CMD, DT_EMAC_CMD_GET_PHY_SPEED, HDR,
     sizeof(DtIoctlEMACCmdGetPhySpeedOutput)},
    {DT_FUNC_CODE_NW_CMD, DT_NW_CMD_PIPE_OPEN, sizeof(DtIoctlNwCmdPipeOpenInput),
     sizeof(DtIoctlNwCmdPipeOpenOutput)},
    {DT_FUNC_CODE_NW_CMD, DT_NW_CMD_PIPE_CLOSE, sizeof(DtIoctlNwCmdPipeCloseInput), 0},
    {DT_FUNC_CODE_PIPE_CMD, DT_PIPE_CMD_SET_SHARED_BUFFER,
     sizeof(DtIoctlPipeCmdSetSharedBufferInput), 0},
    {DT_FUNC_CODE_PIPE_CMD, DT_PIPE_CMD_RELEASE_SHARED_BUFFER,
     sizeof(DtIoctlPipeCmdReleaseSharedBufferInput), 0},
    {DT_FUNC_CODE_PIPE_CMD, DT_PIPE_CMD_GET_PROPERTIES,
     sizeof(DtIoctlPipeCmdGetPropertiesInput), sizeof(DtIoctlPipeCmdGetPropertiesOutput)},
    {DT_FUNC_CODE_PIPE_CMD, DT_PIPE_CMD_GET_STATUS, sizeof(DtIoctlPipeCmdGetStatusInput),
     sizeof(DtIoctlPipeCmdGetStatusOutput)},
    {DT_FUNC_CODE_PIPE_CMD, DT_PIPE_CMD_ISSUE_PIPE_FLUSH,
     sizeof(DtIoctlPipeCmdIssuePipeFlushInput), 0},
    {DT_FUNC_CODE_PIPE_CMD, DT_PIPE_CMD_SET_OPERATIONAL_MODE,
     sizeof(DtIoctlPipeCmdSetOpModeInput), 0},
    {DT_FUNC_CODE_PIPE_CMD, DT_PIPE_CMD_SET_RX_READ_OFFSET,
     sizeof(DtIoctlPipeCmdSetRxReadOffsetInput), 0},
    {DT_FUNC_CODE_PIPE_CMD, DT_PIPE_CMD_GET_RX_WRITE_OFFSET,
     sizeof(DtIoctlPipeCmdGetRxWriteOffsetInput),
     sizeof(DtIoctlPipeCmdGetRxWriteOffsetOutput)},
    {DT_FUNC_CODE_PIPE_CMD, DT_PIPE_CMD_SET_TX_WRITE_OFFSET,
     sizeof(DtIoctlPipeCmdSetTxWriteOffsetInput), 0},
    {DT_FUNC_CODE_PIPE_CMD, DT_PIPE_CMD_GET_TX_READ_OFFSET,
     sizeof(DtIoctlPipeCmdGetTxReadOffsetInput),
     sizeof(DtIoctlPipeCmdGetTxReadOffsetOutput)},
    {DT_FUNC_CODE_PIPE_CMD, DT_PIPE_CMD_SET_IPFILTER,
     sizeof(DtIoctlPipeCmdSetIpFilterInput), 0},
};

#undef HDR

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static const SimNwCmdProps* FindCmd(int FunctionCode, int Cmd)
{
    for (size_t i = 0; i < sizeof(g_Cmds) / sizeof(g_Cmds[0]); i++)
    {
        if (g_Cmds[i].FunctionCode == FunctionCode && g_Cmds[i].Cmd == Cmd)
            return &g_Cmds[i];
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OpenPipe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Opens a pipe of one type: the types the driver keeps for its own queues are in use, a
// hardware pipe is the first free one of its kind, and a software pipe is allocated. *Id
// receives the pipe's number.
//
static uint32_t OpenPipe(void* Handle, int Type, int* Id)
{
    switch (Type)
    {
    case DT_PIPE_RX_NRT:
    case DT_PIPE_TX_NRT:
    case DT_PIPE_RX_HWQ:
    case DT_PIPE_TX_RT_HWQ:
        return DT_STATUS_IN_USE;
    case DT_PIPE_TX_RT_HWP:
    case DT_PIPE_RX_RT_HWP:
    {
        int First = Type == DT_PIPE_TX_RT_HWP ? SIM_NW_FIRST_TX_HWP : SIM_NW_FIRST_RX_HWP;

        for (int i = First; i < First + SIM_DTA2110_HW_PIPES; i++)
        {
            SimPipe* Pipe = FindPipe(i);
            if (Pipe->InUse)
                continue;
            Pipe->InUse = true;
            Pipe->Owner = Handle;
            *Id = i;
            return DT_STATUS_OK;
        }
        return DT_STATUS_IN_USE;
    }
    case DT_PIPE_RX_RT_SWP:
    case DT_PIPE_TX_RT_SWP:
    {
        int Free = SIM_NW_FIRST_SWP;

        while (Free <= SIM_NW_MAX_PIPES && g_Nw.SwPipes[Free] != NULL)
            Free++;
        if (Free > SIM_NW_MAX_PIPES)
            return DT_STATUS_OUT_OF_RESOURCES;
        SimPipe* Pipe = (SimPipe*)DtAlloc_Malloc(sizeof(SimPipe));
        if (Pipe == NULL)
            return DT_STATUS_OUT_OF_MEMORY;
        memset(Pipe, 0, sizeof(*Pipe));
        Pipe->Id = Free;
        Pipe->Type = Type;
        Pipe->Rx = Type == DT_PIPE_RX_RT_SWP;
        Pipe->InUse = true;
        Pipe->Owner = Handle;
        Pipe->OpMode = DT_PIPE_OPMODE_IDLE;
        g_Nw.SwPipes[Free] = Pipe;
        if (Free > g_Nw.LastSwPipe)
            g_Nw.LastSwPipe = Free;
        *Id = Free;
        return DT_STATUS_OK;
    }
    default:
        return DT_STATUS_NOT_SUPPORTED;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NwCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static uint32_t NwCmd(void* Handle, int Uuid, int Cmd, const void* In, void* Out,
                      size_t* OutSize)
{
    if (Cmd == DT_NW_CMD_PIPE_OPEN)
    {
        const DtIoctlNwCmdPipeOpenInput* Request = (const DtIoctlNwCmdPipeOpenInput*)In;
        int Id = 0;

        uint32_t Status = OpenPipe(Handle, Request->m_PipeType, &Id);
        if (Status == DT_STATUS_IN_USE && Request->m_PipeTypeFallback != -1)
            Status = OpenPipe(Handle, Request->m_PipeTypeFallback, &Id);
        if (Status != DT_STATUS_OK)
            return Status;
        ((DtIoctlNwCmdPipeOpenOutput*)Out)->m_PipeUuid =
            (UInt)(Uuid & (DT_UUID_FLAG_MASK | DT_UUID_INDEX_MASK)) | (UInt)Id << 20;
        *OutSize = sizeof(DtIoctlNwCmdPipeOpenOutput);
        return DT_STATUS_OK;
    }

    // DT_NW_CMD_PIPE_CLOSE, with the checks the driver makes.
    int Id = (int)((UInt)Uuid >> 20);
    if (Id == 0 || Id > SIM_NW_MAX_PIPES)
        return DT_STATUS_INVALID_PARAMETER;
    if (Id < SIM_NW_FIRST_TX_HWP)
        return DT_STATUS_NOT_FOUND;
    SimPipe* Pipe = FindPipe(Id);
    if (Pipe == NULL)
        return DT_STATUS_INVALID_PARAMETER;
    if (!Pipe->InUse)
        return DT_STATUS_NOT_INITIALISED;
    if (Pipe->Owner != Handle)
        return DT_STATUS_NOT_FOUND;
    ClosePipe(Pipe);
    return DT_STATUS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Registers the pipe's shared buffer, which is a buffer of the process.
//
static uint32_t SetBuffer(SimPipe* Pipe, const void* In, void* Out, size_t* OutSize)
{
    const DtIoctlPipeCmdSetSharedBufferInput* Request =
        (const DtIoctlPipeCmdSetSharedBufferInput*)In;
    uint8_t* Buffer;
    size_t Size;

    if (Pipe->BufferSet)
        return DT_STATUS_IN_USE;
    if (g_Nw.AsLinux)
    {
        Buffer = (uint8_t*)(uintptr_t)Request->m_BufferAddr;
        Size = Request->m_BufferSize > 0 ? (size_t)Request->m_BufferSize : 0;
    }
    else
    {
        Buffer = (uint8_t*)Out;
        Size = OutSize != NULL ? *OutSize : 0;
    }
    if (Buffer == NULL || Size == 0 || Size > 0x7FFFFFFFu)
        return DT_STATUS_INVALID_PARAMETER;
    if (Pipe->Hw)
    {
        if ((uintptr_t)Buffer % SIM_NW_PAGE_SIZE != 0 ||
            Size % (SIM_NW_PAGE_SIZE * SIM_NW_HWP_PREFETCH_PAGES) != 0)
        {
            return DT_STATUS_INVALID_PARAMETER;
        }
        if (Size > SIM_NW_MAX_HWP_BUFFER)
            return DT_STATUS_BUF_TOO_LARGE;
    }

    Pipe->BufferSet = true;
    Pipe->Buffer = Buffer;
    Pipe->BufferSize = Size;
    Pipe->ReadOffset = 0;
    Pipe->WriteOffset = 0;
    Pipe->CachedWriteOffset = 0;
    if (g_Nw.AsLinux && OutSize != NULL)
        *OutSize = sizeof(DtIoctlPipeCmdSetSharedBufferOutput);
    return DT_STATUS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DropScheduled -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Takes the packets of a pipe out of the scheduler.
//
static void DropScheduled(int Id)
{
    size_t Count = DtVec_Count(&g_Nw.Scheduler.Items);
    size_t To = g_Nw.Scheduler.Head;

    for (size_t From = g_Nw.Scheduler.Head; From < Count; From++)
    {
        SimItem* Item = (SimItem*)DtVec_At(&g_Nw.Scheduler.Items, From);
        if (Item->PipeId == Id)
        {
            g_Nw.Scheduler.Bytes -= Item->Size;
            DtAlloc_Free(Item->Data);
            continue;
        }
        *(SimItem*)DtVec_At(&g_Nw.Scheduler.Items, To++) = *Item;
    }
    DtVec_Resize(&g_Nw.Scheduler.Items, To);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Scheduled -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static int Scheduled(int Id)
{
    int Count = 0;

    for (size_t i = g_Nw.Scheduler.Head; i < DtVec_Count(&g_Nw.Scheduler.Items); i++)
    {
        if (((SimItem*)DtVec_At(&g_Nw.Scheduler.Items, i))->PipeId == Id)
            Count++;
    }
    return Count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sets the pipe's operational mode. A software pipe takes any value; a hardware pipe's
// DMA controller only the three modes, and any except IDLE only with a buffer. A hardware
// transmit pipe going from STANDBY to RUN applies the write offset it kept.
//
static uint32_t SetOpMode(SimPipe* Pipe, int OpMode)
{
    if (Pipe->Hw)
    {
        if (OpMode != DT_PIPE_OPMODE_IDLE && OpMode != DT_PIPE_OPMODE_STANDBY &&
            OpMode != DT_PIPE_OPMODE_RUN)
        {
            return DT_STATUS_INVALID_PARAMETER;
        }
        if (OpMode != DT_PIPE_OPMODE_IDLE && !Pipe->BufferSet)
            return DT_STATUS_NOT_INITIALISED;
        if (IsTx(Pipe) && OpMode == DT_PIPE_OPMODE_RUN &&
            Pipe->OpMode == DT_PIPE_OPMODE_STANDBY)
        {
            Pipe->WriteOffset = Pipe->CachedWriteOffset;
        }
    }
    Pipe->OpMode = OpMode;
    return DT_STATUS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PipeCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static uint32_t PipeCmd(int Uuid, int Cmd, const void* In, void* Out, size_t* OutSize)
{
    SimPipe* Pipe = FindPipe((int)(((UInt)Uuid >> 20) & 0xFFF));

    if (Pipe == NULL)
        return DT_STATUS_INVALID_PARAMETER;

    switch (Cmd)
    {
    case DT_PIPE_CMD_SET_SHARED_BUFFER:
        return SetBuffer(Pipe, In, Out, OutSize);
    case DT_PIPE_CMD_RELEASE_SHARED_BUFFER:
        Pipe->BufferSet = false;
        Pipe->Buffer = NULL;
        Pipe->BufferSize = 0;
        return DT_STATUS_OK;
    case DT_PIPE_CMD_GET_PROPERTIES:
    {
        DtIoctlPipeCmdGetPropertiesOutput* Props =
            (DtIoctlPipeCmdGetPropertiesOutput*)Out;

        Props->m_Capabilities = (Pipe->Rx ? DT_PIPE_CAP_RX : DT_PIPE_CAP_TX) |
                                (Pipe->Hw ? DT_PIPE_CAP_HWP : DT_PIPE_CAP_SWP) |
                                DT_PIPE_CAP_RT | DT_PIPE_CAP_JFRAME;
        Props->m_PrefetchSize = Pipe->Hw ? SIM_NW_HWP_PREFETCH_PAGES : 1;
        Props->m_PipeDataWidth = SIM_DTA2110_PACKET_ALIGNMENT * 8;
        Props->m_PipeType = Pipe->Type;
        *OutSize = sizeof(*Props);
        return DT_STATUS_OK;
    }
    case DT_PIPE_CMD_GET_STATUS:
    {
        DtIoctlPipeCmdGetStatusOutput* Status = (DtIoctlPipeCmdGetStatusOutput*)Out;

        switch (Pipe->OpMode)
        {
        case DT_PIPE_OPMODE_IDLE:
            Status->m_OpStatus = DT_BLOCK_OPSTATUS_IDLE;
            break;
        case DT_PIPE_OPMODE_STANDBY:
            Status->m_OpStatus = DT_BLOCK_OPSTATUS_STANDBY;
            break;
        case DT_PIPE_OPMODE_RUN:
            Status->m_OpStatus = DT_BLOCK_OPSTATUS_RUN;
            break;
        default:
            Status->m_OpStatus = DT_BLOCK_OPSTATUS_ERROR;
            break;
        }
        Status->m_StatusFlags = 0;
        if (Pipe->Hw && IsTx(Pipe) && Scheduled(Pipe->Id) > 0)
            Status->m_StatusFlags = DT_PIPE_STATUS_PACKET_WAITING;
        Status->m_ErrorFlags = Pipe->ErrorFlags;
        *OutSize = sizeof(*Status);
        return DT_STATUS_OK;
    }
    case DT_PIPE_CMD_ISSUE_PIPE_FLUSH:
        if (!Pipe->Hw)
        {
            Pipe->ReadOffset = 0;
            Pipe->WriteOffset = 0;
            Pipe->ErrorFlags &= ~(uint32_t)DT_PIPE_ERROR_INVALID_TIME;
        }
        else if (Pipe->Rx)
            Pipe->ReadOffset = Pipe->WriteOffset;
        else
        {
            Pipe->WriteOffset = Pipe->ReadOffset;
            Pipe->CachedWriteOffset = Pipe->ReadOffset;
            DropScheduled(Pipe->Id);
            Pipe->ErrorFlags &= ~(uint32_t)DT_PIPE_ERROR_INVALID_TIME;
        }
        return DT_STATUS_OK;
    case DT_PIPE_CMD_SET_OPERATIONAL_MODE:
        return SetOpMode(Pipe, ((const DtIoctlPipeCmdSetOpModeInput*)In)->m_OpMode);
    case DT_PIPE_CMD_SET_RX_READ_OFFSET:
    {
        UInt Offset = ((const DtIoctlPipeCmdSetRxReadOffsetInput*)In)->m_RxReadOffset;

        if (!Pipe->Rx)
            return DT_STATUS_NOT_SUPPORTED;
        if (Pipe->Hw && (!Pipe->BufferSet || Offset >= Pipe->BufferSize))
            return DT_STATUS_INVALID_PARAMETER;
        Pipe->ReadOffset = Offset;
        return DT_STATUS_OK;
    }
    case DT_PIPE_CMD_GET_RX_WRITE_OFFSET:
        if (!Pipe->Rx)
            return DT_STATUS_NOT_SUPPORTED;
        ((DtIoctlPipeCmdGetRxWriteOffsetOutput*)Out)->m_RxWriteOffset = Pipe->WriteOffset;
        *OutSize = sizeof(DtIoctlPipeCmdGetRxWriteOffsetOutput);
        return DT_STATUS_OK;
    case DT_PIPE_CMD_SET_TX_WRITE_OFFSET:
    {
        UInt Offset = ((const DtIoctlPipeCmdSetTxWriteOffsetInput*)In)->m_TxWriteOffset;

        if (!IsTx(Pipe))
            return DT_STATUS_NOT_SUPPORTED;
        if (Pipe->Hw && (!Pipe->BufferSet || Offset >= Pipe->BufferSize))
            return DT_STATUS_INVALID_PARAMETER;
        if (!Pipe->Hw || Pipe->OpMode != DT_PIPE_OPMODE_STANDBY)
            Pipe->WriteOffset = Offset;
        Pipe->CachedWriteOffset = Offset;
        TakeHwPackets(SimNw_Now());
        return DT_STATUS_OK;
    }
    case DT_PIPE_CMD_GET_TX_READ_OFFSET:
        if (!IsTx(Pipe))
            return DT_STATUS_NOT_SUPPORTED;
        ((DtIoctlPipeCmdGetTxReadOffsetOutput*)Out)->m_TxReadOffset = Pipe->ReadOffset;
        *OutSize = sizeof(DtIoctlPipeCmdGetTxReadOffsetOutput);
        return DT_STATUS_OK;
    default: // DT_PIPE_CMD_SET_IPFILTER
        if (!Pipe->Rx)
            return DT_STATUS_NOT_SUPPORTED;
        memcpy(&Pipe->Filter, In, sizeof(Pipe->Filter));
        Pipe->FilterSet = true;
        return DT_STATUS_OK;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimNw_Takes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool SimNw_Takes(int FunctionCode)
{
    return FunctionCode == DT_FUNC_CODE_EMAC_CMD || FunctionCode == DT_FUNC_CODE_NW_CMD ||
           FunctionCode == DT_FUNC_CODE_PIPE_CMD;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimNw_Cmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint32_t SimNw_Cmd(void* Handle, int Uuid, int FunctionCode, int Cmd, const void* In,
                   size_t InSize, void* Out, size_t* OutSize)
{
    EnsureNw();

    const SimNwCmdProps* Props = FindCmd(FunctionCode, Cmd);
    if (Props == NULL)
        return DT_STATUS_NOT_SUPPORTED;
    if (InSize < Props->InSize ||
        (Props->OutSize > 0 &&
         (Out == NULL || OutSize == NULL || *OutSize < Props->OutSize)))
    {
        return DT_STATUS_INVALID_PARAMETER;
    }

    Run();
    switch (FunctionCode)
    {
    case DT_FUNC_CODE_EMAC_CMD:
        if (Cmd == DT_EMAC_CMD_GET_MACADDRESS)
        {
            static const uint8_t Mac[6] = SIM_DTA2110_MAC_ADDRESS;

            memcpy(((DtIoctlEMACCmdGetMacAddressOutput*)Out)->m_Address, Mac,
                   sizeof(Mac));
            *OutSize = sizeof(DtIoctlEMACCmdGetMacAddressOutput);
            return DT_STATUS_OK;
        }
        ((DtIoctlEMACCmdGetPhySpeedOutput*)Out)->m_Speed =
            g_Nw.LinkUp ? DT_PHY_SPEED_10000 : DT_PHY_SPEED_NO_LINK;
        *OutSize = sizeof(DtIoctlEMACCmdGetPhySpeedOutput);
        return DT_STATUS_OK;
    case DT_FUNC_CODE_NW_CMD:
        return NwCmd(Handle, Uuid, Cmd, In, Out, OutSize);
    default: // DT_FUNC_CODE_PIPE_CMD
        return PipeCmd(Uuid, Cmd, In, Out, OutSize);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimNw_CloseHandle -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Closes every pipe the handle owns, as the driver does when a file handle closes.
//
void SimNw_CloseHandle(void* Handle)
{
    EnsureNw();
    for (int Id = SIM_NW_FIRST_TX_HWP; Id <= SIM_NW_MAX_PIPES; Id++)
    {
        SimPipe* Pipe = FindPipe(Id);

        if (Handle != NULL && Pipe != NULL && Pipe->InUse && Pipe->Owner == Handle)
            ClosePipe(Pipe);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimNw_Now -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint64_t SimNw_Now(void)
{
    EnsureNw();
    if (g_Nw.ManualTime)
        return g_Nw.TimeNs;

    struct timespec Now;
    timespec_get(&Now, TIME_UTC);
    return (uint64_t)Now.tv_sec * 1000000000u + (uint64_t)Now.tv_nsec;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimNw_Reset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimNw_Reset(void)
{
    if (g_Nw.Initialised)
    {
        for (int Id = SIM_NW_FIRST_SWP; Id <= SIM_NW_MAX_PIPES; Id++)
            DtAlloc_Free(g_Nw.SwPipes[Id]);
        QueueFree(&g_Nw.Scheduler);
        QueueFree(&g_Nw.Arriving);
        QueueFree(&g_Nw.Received);
        DtVec_Free(&g_Nw.Scheduler.Items);
        DtVec_Free(&g_Nw.Arriving.Items);
        DtVec_Free(&g_Nw.Received.Items);
        for (int i = 0; i < g_Nw.NumKept; i++)
            DtAlloc_Free(g_Nw.Kept[(g_Nw.KeptFirst + i) % SIM_NW_KEPT_PACKETS].Data);
    }
    memset(&g_Nw, 0, sizeof(g_Nw));

    // As the DTA-2110 itself in SimDtPcie_Reset: a program that calls no test control
    // asks for the loopback through the environment.
    const char* Loopback = getenv("CDTAPI_SIM_LOOPBACK");
    g_Nw.Loopback = Loopback != NULL && Loopback[0] != '\0' && strcmp(Loopback, "0") != 0;

    for (int i = 0; i < SIM_NW_HW_PIPES; i++)
    {
        SimPipe* Pipe = &g_Nw.HwPipes[i];

        Pipe->Id = SIM_NW_FIRST_TX_HWP + i;
        Pipe->Hw = true;
        Pipe->Rx = i >= SIM_DTA2110_HW_PIPES;
        Pipe->Type = Pipe->Rx ? DT_PIPE_RX_RT_HWP : DT_PIPE_TX_RT_HWP;
        Pipe->OpMode = DT_PIPE_OPMODE_IDLE;
    }
    DtVec_Init(&g_Nw.Scheduler.Items, sizeof(SimItem));
    DtVec_Init(&g_Nw.Arriving.Items, sizeof(SimItem));
    DtVec_Init(&g_Nw.Received.Items, sizeof(SimItem));
#if defined(_WIN32) || defined(_WIN64)
    g_Nw.AsLinux = false;
#else
    g_Nw.AsLinux = true;
#endif
    g_Nw.LinkUp = true;
    g_Nw.LastSwPipe = SIM_NW_FIRST_SWP - 1;
    g_Nw.Initialised = true;
    g_Nw.DoneNs = SimNw_Now();
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test controls +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_RegisterPipeBufferAsLinux -.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_RegisterPipeBufferAsLinux(bool AsLinux)
{
    SimDtPcie_Lock();
    EnsureNw();
    g_Nw.AsLinux = AsLinux;
    SimDtPcie_Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_SetNwLink -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_SetNwLink(bool Up)
{
    SimDtPcie_Lock();
    EnsureNw();
    g_Nw.LinkUp = Up;
    SimDtPcie_Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_SetNwTime -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Packets move up to the time of the clock that is left first.
//
void SimDtPcie_SetNwTime(uint64_t TodNs)
{
    SimDtPcie_Lock();
    EnsureNw();
    Run();
    g_Nw.ManualTime = true;
    g_Nw.TimeNs = TodNs;
    g_Nw.DoneNs = TodNs;
    SimDtPcie_Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_AdvanceNwTime -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_AdvanceNwTime(uint64_t Ns)
{
    SimDtPcie_Lock();
    EnsureNw();
    if (g_Nw.ManualTime)
    {
        g_Nw.TimeNs += Ns;
        Run();
    }
    SimDtPcie_Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_SetNwLoopback -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_SetNwLoopback(bool Loopback)
{
    SimDtPcie_Lock();
    EnsureNw();
    g_Nw.Loopback = Loopback;
    SimDtPcie_Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_InjectNwFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool SimDtPcie_InjectNwFrame(const uint8_t* Frame, size_t Size, uint64_t TodNs)
{
    SimItem Item = {TodNs, 0, NULL, Size};

    if (Frame == NULL || Size == 0 || Size > DT_ETHIP_MAX_FRAME_V2)
        return false;
    SimDtPcie_Lock();
    EnsureNw();
    Run();
    Item.Data = (uint8_t*)DtAlloc_Malloc(Size);
    bool Kept = Item.Data != NULL;
    if (Kept)
    {
        memcpy(Item.Data, Frame, Size);
        Kept = QueueAdd(&g_Nw.Arriving, &Item);
        if (!Kept)
            DtAlloc_Free(Item.Data);
    }
    if (Kept)
        Run();
    SimDtPcie_Unlock();
    return Kept;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_NwSentCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int SimDtPcie_NwSentCount(void)
{
    SimDtPcie_Lock();
    EnsureNw();
    Run();
    int Sent = g_Nw.Sent;
    SimDtPcie_Unlock();
    return Sent;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_NwKeptCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int SimDtPcie_NwKeptCount(void)
{
    SimDtPcie_Lock();
    EnsureNw();
    Run();
    int Kept = g_Nw.NumKept;
    SimDtPcie_Unlock();
    return Kept;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_GetNwSent -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool SimDtPcie_GetNwSent(int Index, SimNwPacket* Packet)
{
    memset(Packet, 0, sizeof(*Packet));
    SimDtPcie_Lock();
    EnsureNw();
    Run();
    bool Found = Index >= 0 && Index < g_Nw.NumKept;
    if (Found)
    {
        const SimItem* Kept = &g_Nw.Kept[(g_Nw.KeptFirst + Index) % SIM_NW_KEPT_PACKETS];
        Packet->TodNs = Kept->TimeNs;
        Packet->PipeId = Kept->PipeId;
        Packet->Frame = Kept->Data;
        Packet->Size = Kept->Size;
    }
    SimDtPcie_Unlock();
    return Found;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_GetNwPipeState -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcie_GetNwPipeState(int PipeId, SimNwPipeState* State)
{
    memset(State, 0, sizeof(*State));
    SimDtPcie_Lock();
    EnsureNw();
    Run();
    const SimPipe* Pipe = FindPipe(PipeId);
    if (Pipe != NULL)
    {
        State->Exists = true;
        State->InUse = Pipe->InUse;
        State->Type = Pipe->Type;
        State->OpMode = Pipe->OpMode;
        State->BufferSet = Pipe->BufferSet;
        State->BufferSize = Pipe->BufferSize;
        State->ReadOffset = Pipe->ReadOffset;
        State->WriteOffset = Pipe->WriteOffset;
        State->ErrorFlags = Pipe->ErrorFlags;
        State->FilterSet = Pipe->FilterSet;
        State->FilterFlags = (uint32_t)Pipe->Filter.m_Flags;
        State->Scheduled = Scheduled(PipeId);
    }
    SimDtPcie_Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_GetNwCounters -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_GetNwCounters(SimNwCounters* Counters)
{
    SimDtPcie_Lock();
    EnsureNw();
    Run();
    *Counters = g_Nw.Counters;
    SimDtPcie_Unlock();
}
