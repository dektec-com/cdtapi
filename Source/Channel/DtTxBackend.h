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
#include "Core/DtWork.h"     // The pool a frame's lines are coded over.
#include "Device/DtDevice.h" // The device and its port capabilities.
#include "DtPcieCmd.h"       // DtIoConfig.
#include "OAL/OsThread.h"    // The channel's lock.
#include "cdtapi.h"          // Results.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtTxBackend +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// An output channel keeps one side that transmits, chosen by the port's I/O standard:
// DtAsiTx.c for ASI, DtSdiTx.c for raw SDI frames. The side is a DtTx, a struct that each
// side's own begins with, and its functions are the side's DtTxBackend, as DtRxBackend.h
// describes for the input. DtOutpChannel.c keeps the checks that do not depend on the
// side, the lock and detaching, and calls these with the lock held. A side may release
// the lock while it waits, and has the channel's lock and its count of waiting detaches
// for that; after a wait it gives DTAPI_E_CANCELLED while a detach waits.
//
// A function that is NULL gives the default the function says.
//

// The deadline of a wait without a time limit.
#define DT_TX_NO_DEADLINE UINT64_MAX

typedef struct DtTxBackend DtTxBackend;

// The port the channel attached to, and what a side needs of the channel to wait. Device
// is the channel's own, and lives as long as the side does.
typedef struct DtTxPort
{
    DtDevice* Device;
    int Port; // From 1
    int PortIndex;
    uint64_t Caps;        // DT_CAP_ flags of the port
    OsMutex* Lock;        // The channel's
    const int* Detachers; // Detaches waiting for a write to return
} DtTxPort;

// What every side has: its functions, its port, and the transmit mode and control, which
// the checks in DtOutpChannel.c read.
typedef struct DtTx
{
    const DtTxBackend* Ops;
    DtTxPort Port;
    int TxMode;
    int TxControl;
} DtTx;

struct DtTxBackend
{
    // Lets go of what the side holds, ignoring failures, and frees Tx.
    void (*Release)(DtTx* Tx);

    // The side's part of SetTxControl, ClearFifo, GetFifoLoad, GetFifoSize,
    // GetMaxFifoSize and GetFlags.
    DtapiResult (*SetTxControl)(DtTx* Tx, int TxControl);
    DtapiResult (*ClearFifo)(DtTx* Tx);
    DtapiResult (*GetFifoLoad)(DtTx* Tx, int* FifoLoad);
    DtapiResult (*GetFifoSize)(DtTx* Tx, int* FifoSize);
    DtapiResult (*GetMaxFifoSize)(DtTx* Tx, int* MaxFifoSize);
    DtapiResult (*GetFlags)(DtTx* Tx, int* Status, int* Latched);

    // SetTxMode once DtOutpChannel.c has checked what does not depend on the side.
    DtapiResult (*SetTxMode)(DtTx* Tx, int TxMode, int StuffMode);

    // ClearFlags.
    DtapiResult (*ClearFlags)(DtTx* Tx, int Latched);

    // What the side does around DtOutpChannel.c setting Config on the port, the side
    // staying the same: BeforeIoConfig first, when there is one, and ApplyIoConfig with
    // the result of that or of the setting, which it returns when it is a failure, and
    // its own result otherwise.
    DtapiResult (*BeforeIoConfig)(DtTx* Tx);
    DtapiResult (*ApplyIoConfig)(DtTx* Tx, const DtIoConfig* Config,
                                 DtapiResult SetResult);

    // SetTxPolarity.
    DtapiResult (*SetTxPolarity)(DtTx* Tx, int TxPolarity);

    // GetTsRateBps and SetTsRateBps. NULL gives DTAPI_E_NOT_SUPPORTED.
    DtapiResult (*GetTsRateBps)(DtTx* Tx, int* TsRate);
    DtapiResult (*SetTsRateBps)(DtTx* Tx, int TsRate);

    // Divides the side's work over Pool, NULL for the writing thread alone, in NumThreads
    // pieces, or with 0 in as many as the signal calls for. The channel holds the pool
    // and gives it again to every side it attaches, and calls this with no write going
    // on. DTAPI_E_OUT_OF_MEM when the working buffers cannot be had for those pieces,
    // after which the side works in the writing thread. NULL where the side has nothing
    // to divide.
    DtapiResult (*SetWorkPool)(DtTx* Tx, DtWorkPool* Pool, int NumThreads);

    // Write, while not idle and with no other write going on.
    DtapiResult (*Write)(DtTx* Tx, const uint8_t* Data, size_t Size);

    // WriteFrame, likewise, waiting for room until the monotonic clock reaches Deadline.
    // NULL gives DTAPI_E_NOT_SDI_MODE.
    DtapiResult (*WriteFrame)(DtTx* Tx, const uint8_t* Frame, int FrameSize,
                              uint64_t Deadline);

    // Wakes a write that waits for room, for a detach.
    void (*Wake)(DtTx* Tx);

    // A detach with DTAPI_WAIT_UNTIL_SENT while sending: returns when what was written
    // has gone out, or when it stalls.
    void (*WaitUntilSent)(DtTx* Tx);
};
