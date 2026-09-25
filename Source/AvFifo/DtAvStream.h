// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAvStream.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - What the packetizers and parsers of every substandard share
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// CDTAPI includes
#include "DtAvFrame.h"  // Frames and their pool.
#include "DtAvPacket.h" // Packet headers.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A packetizer turns a frame into packets and hands each to a sink, which gives it room
// for the packet and takes it once written: a transmit FIFO writes into the pipe's shared
// buffer, a test into memory. The sink always has room: whoever calls the packetizer
// checked beforehand that the frame's packets fit.
//

typedef struct DtAvTxSink
{
    // Room for a packet of up to MaxSize bytes.
    uint8_t* (*ReserveRoom)(void* Context, int MaxSize);

    // The packet is written, Size bytes including its padding.
    void (*CommitPacket)(void* Context, int Size);

    void* Context;
} DtAvTxSink;

// What every packet of a transmitted stream carries.
typedef struct DtAvTxStream
{
    DtAvNet Net;
    int PayloadType;             // RTP payload type
    uint32_t Ssrc;               // RTP synchronisation source
    int64_t OutputDelayNs;       // How much later than its time a packet leaves the card
    uint32_t NextSequenceNumber; // Of the next packet; ST 2110-20 carries all 32 bits
} DtAvTxStream;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reception +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A parser takes the packets of a stream one at a time, gets frames from a pool, and
// delivers each complete frame. A delivery that is refused, because the FIFO is full,
// counts a dropped frame and returns the frame to the pool.
//

typedef struct DtAvRxSink
{
    DtAvFramePool* Pool;

    // Takes a complete frame; false when there is no room for it.
    bool (*Deliver)(void* Context, DtAvFrame* Frame);

    void* Context;
} DtAvRxSink;

// Counts a received frame and delivers it.
static inline void DtAvRxSink_Deliver(const DtAvRxSink* Target, RxStatistics* Stats,
                                      DtAvFrame* Frame)
{
    Stats->FramesOk++;
    if (!Target->Deliver(Target->Context, Frame))
    {
        Stats->DroppedFrames++;
        DtAvFramePool_Return(Target->Pool, &Frame->Frame);
    }
}
