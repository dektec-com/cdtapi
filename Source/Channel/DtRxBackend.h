// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtRxBackend.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - What an input channel asks of the side that receives
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "Core/DtWorkerPool.h" // The pool a frame's lines are decoded over.
#include "Device/DtDevice.h"   // The device and its port capabilities.
#include "DtPcieCmd.h"         // DtIoConfig and DtDrvObject.
#include "cdtapi.h"            // Results and DtTimeOfDay.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtRxBackend +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// An input channel keeps one side that receives, chosen by the port's I/O standard:
// DtAsiRx.c for ASI, DtSdiRx.c for raw SDI frames. The side is a DtRx, a struct that each
// side's own begins with, and its function table is the side's backend, a DtRxBackend.
// DtInpChannel.c keeps the checks that do not depend on the side, the lock, detaching and
// the waits of a read, and calls these with the lock held, except where a function says
// otherwise (plan 0011).
//
// A function that is NULL gives the default the function says.
//

typedef struct DtRxBackend DtRxBackend;

// The port the channel attached to. Device is the channel's own, and lives as long as the
// side does.
typedef struct DtRxAttachedPort
{
    DtDevice* Device;
    int Port;      // From 1
    uint64_t Caps; // DT_CAP_ flags of the port
} DtRxAttachedPort;

// What every side has: its backend, its port, and the receive mode and control, which
// the checks in DtInpChannel.c read.
typedef struct DtRx
{
    const DtRxBackend* Backend;
    DtRxAttachedPort Port;
    int RxMode;
    int RxControl;
    bool IsAsi; // The side is DtAsiRx.c, and Read delivers a transport stream
} DtRx;

// What a read waits with, copied from the side while the lock is held, so that the wait
// needs nothing the lock guards.
typedef struct DtRxWaitState
{
    const DtRxBackend* Backend; // Of the side that prepared the wait
    OsDrv* Drv;
    DtDrvObject WaitObject; // What the side waits on
    int MaxMs;              // The longest a wait lasts
    bool EventOutOfSync;    // What the event reported, for AfterWait
} DtRxWaitState;

struct DtRxBackend
{
    // Lets go of what the side holds, ignoring failures, and frees Rx.
    void (*Release)(DtRx* Rx);

    // The side's part of SetRxMode, SetRxControl, ClearFifo, ClearFlags, GetFlags,
    // GetFifoLoad and GetMaxFifoSize.
    DtapiResult (*SetRxMode)(DtRx* Rx, int RxMode);
    DtapiResult (*SetRxControl)(DtRx* Rx, int RxControl);
    DtapiResult (*ClearFifo)(DtRx* Rx);
    DtapiResult (*ClearFlags)(DtRx* Rx, int Latched);
    DtapiResult (*GetFlags)(DtRx* Rx, int* Flags, int* Latched);
    DtapiResult (*GetFifoLoad)(DtRx* Rx, int* FifoLoad);
    DtapiResult (*GetMaxFifoSize)(DtRx* Rx, int* MaxFifoSize);

    // What the side does after DtInpChannel.c has set Config on the port, the side
    // staying the same.
    DtapiResult (*ApplyIoConfig)(DtRx* Rx, const DtIoConfig* Config);

    // DetectIoStd; NULL gives DTAPI_E_NOT_SUPPORTED.
    DtapiResult (*DetectIoStd)(DtRx* Rx, int* Value, int* SubValue);

    // Divides the side's work over Pool, NULL for the reading thread alone, in NumThreads
    // pieces, or with 0 in as many as the signal calls for. The channel holds the pool
    // and gives it again to every side it attaches, and calls this with no read going on.
    // DTAPI_E_OUT_OF_MEM when the working buffers cannot be had for those pieces, after
    // which the side works in the reading thread. NULL where the side has nothing to
    // divide, which gives DTAPI_OK.
    DtapiResult (*SetWorkerPool)(DtRx* Rx, DtWorkerPool* Pool, int NumThreads);

    // ReadFrame: CheckFrameBuffer checks a buffer of FrameSize bytes and gives the size
    // of a frame, DeliverFrame delivers one when there is one. NULL gives
    // DTAPI_E_NOT_SDI_MODE.
    DtapiResult (*CheckFrameBuffer)(DtRx* Rx, int FrameSize, size_t* RawSize);
    DtapiResult (*DeliverFrame)(DtRx* Rx, uint8_t* Buffer, DtTimeOfDay* ArrivalTime,
                                bool* Delivered);

    // A read's wait while receiving: PrepareWait fills Wait, Wait waits up to Ms without
    // the lock, and AfterWait, with the lock and while no detach waits, deals with what
    // the wait saw.
    void (*PrepareWait)(DtRx* Rx, DtRxWaitState* Wait);
    DtapiResult (*Wait)(DtRxWaitState* Wait, int Ms);
    DtapiResult (*AfterWait)(DtRx* Rx, const DtRxWaitState* Wait);

    // Read: GetDeliverableBytes gives the bytes a read would deliver now, and
    // DeliverBytes delivers Size of them, which that count holds. NULL gives
    // DTAPI_E_NOT_SUPPORTED.
    DtapiResult (*GetDeliverableBytes)(DtRx* Rx, size_t* Load);
    DtapiResult (*DeliverBytes)(DtRx* Rx, uint8_t* Out, size_t Size);

    // GetStatus, GetTsRateBps, GetViolCount and PolarityControl. NULL gives
    // DTAPI_E_NOT_SUPPORTED.
    DtapiResult (*GetStatus)(DtRx* Rx, int* PacketSize, int* NumInv, int* ClkDet,
                             int* AsiLock, int* RateOk, int* AsiInv);
    DtapiResult (*GetTsRateBps)(DtRx* Rx, int* TsRate);
    DtapiResult (*GetViolCount)(DtRx* Rx, int* ViolCount);
    DtapiResult (*PolarityControl)(DtRx* Rx, int Polarity);
};
