// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtTxBackend.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - What an output channel asks of the side that transmits
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "Core/DtWorkerPool.h" // The pool a frame's lines are encoded over.
#include "Device/DtDevice.h"   // The device and its port capabilities.
#include "DtPcieCmd.h"         // DtIoConfig.
#include "OAL/OsThread.h"      // The channel's lock.
#include "cdtapi.h"            // Results.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtTxBackend +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// An output channel transmits through one "side", chosen by the port's I/O standard:
// DtAsiTx.c for ASI, DtSdiTx.c for raw SDI frames. Each side's struct begins with a DtTx,
// whose Backend is the side's table of functions. This is the output's counterpart of
// DtRxBackend.h.
//
// DtOutpChannel.c does what is the same for every side: the argument checks, the lock
// and detaching. It calls the side's functions with the lock held. A side may unlock
// while it waits; DtTxAttachedPort gives it the lock and the count of waiting detaches
// for that. After such a wait, the side returns DTAPI_E_CANCELLED if a detach is waiting.
// A function may be NULL where the comment says what that gives.
//

// The deadline of a wait without a time limit.
#define DT_TX_NO_DEADLINE UINT64_MAX

typedef struct DtTxBackend DtTxBackend;

// The port the channel is attached to, and what a side needs of the channel to wait.
typedef struct DtTxAttachedPort
{
    DtDevice* Device; // The channel's own device; it lives as long as the side does
    int Port;         // From 1
    uint64_t Caps;    // The port's DT_CAP_ flags
    OsMutex* Lock;    // The channel's lock
    const int* WaitingDetaches; // The number of detaches waiting for a write to return
} DtTxAttachedPort;

// The part that every side has. DtOutpChannel.c reads it for its checks.
typedef struct DtTx
{
    const DtTxBackend* Backend; // The side's functions
    DtTxAttachedPort Port;      // The port
    int TxMode;                 // The transmit mode, a DTAPI_TXMODE_ value
    int TxControl;              // A DTAPI_TXCTRL_ value
    bool IsAsi; // True: the side is DtAsiTx.c, and Write takes a transport stream
} DtTx;

// The functions of a side.
struct DtTxBackend
{
    // Stops what the side does, frees what it holds, and frees Tx. Failures are ignored.
    void (*Release)(DtTx* Tx);

    // The side's part of the channel functions of the same name.
    DtapiResult (*SetTxControl)(DtTx* Tx, int TxControl);
    DtapiResult (*ClearFifo)(DtTx* Tx);
    DtapiResult (*GetFifoLoad)(DtTx* Tx, int* FifoLoad);
    DtapiResult (*GetFifoSize)(DtTx* Tx, int* FifoSize);
    DtapiResult (*GetMaxFifoSize)(DtTx* Tx, int* MaxFifoSize);
    DtapiResult (*GetFlags)(DtTx* Tx, int* Status, int* Latched);

    // SetTxMode, after DtOutpChannel.c has done the checks that are the same for every
    // side.
    DtapiResult (*SetTxMode)(DtTx* Tx, int TxMode, int StuffMode);

    // ClearFlags.
    DtapiResult (*ClearFlags)(DtTx* Tx, int Latched);

    // Update the side around a new I/O configuration that keeps the same side.
    // DtOutpChannel.c calls BeforeIoConfig first, if there is one, and then sets Config
    // on the port if that succeeded. It then always calls ApplyIoConfig, with in
    // SetResult the failure so far or DTAPI_OK. ApplyIoConfig returns SetResult when that
    // is a failure, and its own result otherwise.
    DtapiResult (*BeforeIoConfig)(DtTx* Tx);
    DtapiResult (*ApplyIoConfig)(DtTx* Tx, const DtIoConfig* Config,
                                 DtapiResult SetResult);

    // SetTxPolarity.
    DtapiResult (*SetTxPolarity)(DtTx* Tx, int TxPolarity);

    // GetTsRateBps and SetTsRateBps. NULL gives DTAPI_E_NOT_SUPPORTED.
    DtapiResult (*GetTsRateBps)(DtTx* Tx, int* TsRate);
    DtapiResult (*SetTsRateBps)(DtTx* Tx, int TsRate);

    // Splits the side's work over the threads of Pool, in NumThreads pieces; with 0, in
    // as many as the signal needs. With a NULL Pool, the writing thread does all the
    // work. The channel keeps the pool and passes it to every side it attaches. It calls
    // this only while no write is busy. NULL, for a side with nothing to split, gives
    // DTAPI_OK.
    //
    // Returns DTAPI_OK, or DTAPI_E_OUT_OF_MEM when there is not enough memory for the
    // pieces' buffers; the writing thread then does all the work.
    DtapiResult (*SetWorkerPool)(DtTx* Tx, DtWorkerPool* Pool, int NumThreads);

    // Write. Called only while the channel is not idle and no other write is busy.
    DtapiResult (*Write)(DtTx* Tx, const uint8_t* Data, size_t Size);

    // WriteFrame, called as Write. Waits for room until the monotonic clock reaches
    // Deadline (DT_TX_NO_DEADLINE: no limit). NULL gives DTAPI_E_NOT_SDI_MODE.
    DtapiResult (*WriteFrame)(DtTx* Tx, const uint8_t* Frame, int FrameSize,
                              uint64_t Deadline);

    // These two lend room for a frame in the buffer, for AcquireFrame and CommitFrame.
    // They are called as Write. Without them (NULL), those calls return
    // DTAPI_E_NOT_SDI_MODE.
    // - LendFrame waits until Deadline for room for one frame, then points View at it and
    //   stores Holder in the view. It returns what WriteFrame returns, and DTAPI_E_IN_USE
    //   when a frame is lent already. It starts a lending run, in which the side inserts
    //   no black frames and refuses Write and WriteFrame.
    // - CommitLentFrame hands the frame lent to View to the card. It returns
    //   DTAPI_E_INVALID_ARG when View holds no frame this side lent, and DTAPI_E_STATE
    //   when the builder has not built the frame.
    // Going idle, clearing the FIFO, detaching and changing the standard drop a lent
    // frame and end the lending run.
    DtapiResult (*LendFrame)(DtTx* Tx, DtSdiView* View, void* Holder, uint64_t Deadline);
    DtapiResult (*CommitLentFrame)(DtTx* Tx, DtSdiView* View);

    // GetNextFrameTime: the time of day at which the next frame the program hands over,
    // a lent frame or the next one written, starts on the cable, predicted from the
    // start of the last frame the card stamped. Returns DTAPI_E_NOT_STARTED when the
    // card has stamped no frame of this run. Called while the channel is not idle. NULL
    // gives DTAPI_E_NOT_SDI_MODE.
    DtapiResult (*GetNextFrameTime)(DtTx* Tx, DtTimeOfDay* StartTime);

    // Wakes a write that waits for room, so that a detach can go ahead.
    void (*WakeWaitingWrite)(DtTx* Tx);

    // For a detach with DTAPI_WAIT_UNTIL_SENT while the channel sends: returns when all
    // that was written has been sent, or when sending stalls.
    void (*WaitUntilSent)(DtTx* Tx);
};
