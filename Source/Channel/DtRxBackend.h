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
#include "Device/DtDevice.h" // The device and its port capabilities.
#include "DtPcieCmd.h"       // DtIoConfig.
#include "cdtapi.h"          // Results and DtTimeOfDay.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtRxBackend +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// DTAPI's AsiSdiInpChannel_Bb2 keeps one implementation of the port's receiver, an
// AsiRxImpl_Bb2 or an SdiRxImpl_Bb2 by the I/O standard, behind the virtual functions of
// AsiSdiRxImpl_Bb2; a function an implementation does not have gives DTAPI's default.
// Here the implementation is a DtRx, a struct that each side's own begins with, and the
// virtual functions are the side's DtRxBackend. DtInpChannel.c keeps the checks that do
// not depend on the side, the lock, detaching and the waits of a read, and calls these
// with the lock held but where a function says otherwise (0011).
//
// A function that is NULL gives the default the function says.
//

typedef struct DtRxBackend DtRxBackend;

// The port the channel attached to. Device is the channel's own, and lives as long as the
// side does.
typedef struct DtRxPort
{
    DtDevice* Device;
    int Port; // From 1
    int PortIndex;
    uint32_t Caps; // DT_CAP_ flags of the port
} DtRxPort;

// What every side has: its functions, its port, and the receive mode and control, which
// the checks in DtInpChannel.c read.
typedef struct DtRx
{
    const DtRxBackend* Ops;
    DtRxPort Port;
    int RxMode;
    int RxControl;
} DtRx;

// What a read waits with, copied from the side while the lock is held, so that the wait
// needs nothing the lock guards.
typedef struct DtRxWait
{
    const DtRxBackend* Ops; // Of the side that prepared the wait
    OsDrv* Drv;
    int Uuid;
    int PortIndex;
    int MaxMs;      // The longest a wait lasts
    bool OutOfSync; // Set by Wait for AfterWait
} DtRxWait;

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

    // ReadFrame: CheckFrame checks a buffer of FrameSize bytes and gives the size of a
    // frame, TakeFrame delivers one when there is one. NULL gives DTAPI_E_NOT_SDI_MODE.
    DtapiResult (*CheckFrame)(DtRx* Rx, int FrameSize, size_t* RawSize);
    DtapiResult (*TakeFrame)(DtRx* Rx, uint8_t* Buffer, DtTimeOfDay* ArrivalTime,
                             bool* Taken);

    // A read's wait while receiving: PrepareWait fills Wait, Wait waits up to Ms without
    // the lock, and AfterWait, with the lock and while no detach waits, deals with what
    // the wait saw.
    void (*PrepareWait)(DtRx* Rx, DtRxWait* Wait);
    DtapiResult (*Wait)(DtRxWait* Wait, int Ms);
    DtapiResult (*AfterWait)(DtRx* Rx, const DtRxWait* Wait);
};
