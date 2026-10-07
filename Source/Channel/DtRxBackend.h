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
// An input channel receives through one "side", chosen by the port's I/O standard:
// DtAsiRx.c for ASI, DtSdiRx.c for raw SDI frames. Each side's struct begins with a DtRx,
// whose Backend is the side's table of functions.
//
// DtInpChannel.c does what is the same for every side: the argument checks, the lock,
// detaching and waiting in a read. It calls the side's functions with the lock held,
// unless a function says otherwise. A function may be NULL where the comment says what
// that gives.
//

typedef struct DtRxBackend DtRxBackend;

// The port the channel is attached to.
typedef struct DtRxAttachedPort
{
    DtDevice* Device; // The channel's own device; it lives as long as the side does
    int Port;         // From 1
    uint64_t Caps;    // The port's DT_CAP_ flags
} DtRxAttachedPort;

// The part that every side has. DtInpChannel.c reads it for its checks.
typedef struct DtRx
{
    const DtRxBackend* Backend; // The side's functions
    DtRxAttachedPort Port;      // The port
    int RxMode;                 // The receive mode, a DTAPI_RXMODE_ value
    int RxControl;              // DTAPI_RXCTRL_IDLE or DTAPI_RXCTRL_RCV
    bool IsAsi; // True: the side is DtAsiRx.c, and Read delivers a transport stream
} DtRx;

// What a read needs to wait without the lock. The side fills it while the lock is held.
typedef struct DtRxWaitState
{
    const DtRxBackend* Backend; // The side that filled it
    OsDrv* Drv;                 // The driver handle to wait through
    DtDrvObject WaitObject;     // The object the side waits on
    int MaxMs;                  // The longest one wait may last
    bool EventOutOfSync;        // True when the event Wait received says the signal
                                // is out of sync; read by AfterWait
} DtRxWaitState;

// The functions of a side.
struct DtRxBackend
{
    // Stops what the side does, frees what it holds, and frees Rx. Failures are ignored.
    void (*Release)(DtRx* Rx);

    // The side's part of the channel functions of the same name.
    DtapiResult (*SetRxMode)(DtRx* Rx, int RxMode);
    DtapiResult (*SetRxControl)(DtRx* Rx, int RxControl);
    DtapiResult (*ClearFifo)(DtRx* Rx);
    DtapiResult (*ClearFlags)(DtRx* Rx, int Latched);
    DtapiResult (*GetFlags)(DtRx* Rx, int* Flags, int* Latched);
    DtapiResult (*GetFifoLoad)(DtRx* Rx, int* FifoLoad);
    DtapiResult (*GetMaxFifoSize)(DtRx* Rx, int* MaxFifoSize);

    // Updates the side after DtInpChannel.c has set Config on the port, when the new
    // configuration keeps the same side.
    DtapiResult (*ApplyIoConfig)(DtRx* Rx, const DtIoConfig* Config);

    // DetectIoStd. NULL gives DTAPI_E_NOT_SUPPORTED.
    DtapiResult (*DetectIoStd)(DtRx* Rx, int* Value, int* SubValue);

    // Splits the side's work over the threads of Pool, in NumThreads pieces; with 0, in
    // as many as the signal needs. With a NULL Pool, the reading thread does all the
    // work. The channel keeps the pool and passes it to every side it attaches. It calls
    // this only while no read is busy. NULL, for a side with nothing to split, gives
    // DTAPI_OK.
    //
    // Returns DTAPI_OK, or DTAPI_E_OUT_OF_MEM when there is not enough memory for the
    // pieces' buffers; the reading thread then does all the work.
    DtapiResult (*SetWorkerPool)(DtRx* Rx, DtWorkerPool* Pool, int NumThreads);

    // The two halves of ReadFrame. NULL gives DTAPI_E_NOT_SDI_MODE.
    // - CheckFrameBuffer checks that a buffer of FrameSize bytes can hold a frame, and
    //   returns the size of a frame in *RawSize.
    // - DeliverFrame copies one frame into Buffer if one is there, and sets *Delivered.
    DtapiResult (*CheckFrameBuffer)(DtRx* Rx, int FrameSize, size_t* RawSize);
    DtapiResult (*DeliverFrame)(DtRx* Rx, uint8_t* Buffer, DtTimeOfDay* ArrivalTime,
                                bool* Delivered);

    // AcquireFrame and ReleaseFrame. NULL gives DTAPI_E_NOT_SDI_MODE.
    // - LendFrame points View at the next frame where it lies, held by Holder, if one is
    //   there, and sets *Lent; DTAPI_E_INVALID_MODE when the receive mode is not 10 bits.
    // - ReturnFrame gives the lent frame back, and View describes no frame.
    // Detaching, a new I/O standard and clearing the FIFO make the lent view describe no
    // frame too.
    DtapiResult (*LendFrame)(DtRx* Rx, DtSdiView* View, void* Holder,
                             DtTimeOfDay* ArrivalTime, bool* Lent);
    DtapiResult (*ReturnFrame)(DtRx* Rx, DtSdiView* View);

    // How a read waits while the channel receives:
    // - PrepareWait fills *Wait, with the lock held.
    // - Wait waits up to Ms milliseconds, without the lock.
    // - AfterWait handles what the wait saw, with the lock held again. It is called only
    //   when Wait succeeded and no detach is waiting.
    void (*PrepareWait)(DtRx* Rx, DtRxWaitState* Wait);
    DtapiResult (*Wait)(DtRxWaitState* Wait, int Ms);
    DtapiResult (*AfterWait)(DtRx* Rx, const DtRxWaitState* Wait);

    // The two halves of Read. NULL gives DTAPI_E_NOT_SUPPORTED.
    // - GetDeliverableBytes returns in *Load how many bytes a read can deliver now.
    // - DeliverBytes copies Size of those bytes to Out; Size is at most that count.
    DtapiResult (*GetDeliverableBytes)(DtRx* Rx, size_t* Load);
    DtapiResult (*DeliverBytes)(DtRx* Rx, uint8_t* Out, size_t Size);

    // The side's part of GetStatus, GetTsRateBps, GetViolCount and PolarityControl. NULL
    // gives DTAPI_E_NOT_SUPPORTED.
    DtapiResult (*GetStatus)(DtRx* Rx, int* PacketSize, int* NumInv, int* ClkDet,
                             int* AsiLock, int* RateOk, int* AsiInv);
    DtapiResult (*GetTsRateBps)(DtRx* Rx, int* TsRate);
    DtapiResult (*GetViolCount)(DtRx* Rx, int* ViolCount);
    DtapiResult (*PolarityControl)(DtRx* Rx, int Polarity);
};
