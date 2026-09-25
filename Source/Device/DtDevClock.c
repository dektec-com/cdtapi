// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtDevClock.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Device layer: the device's genlock, time-of-day and transmit clocks
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The public functions check their arguments, then that the device is attached, then
// that it has the object they need, and hand the rest to the driver command, which
// converts the answer to DTAPI's terms. The transmit clocks are converted here from the
// driver's units, and their ports are the device's ASI and SDI outputs, as in DTAPI.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <limits.h>
#include <math.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h" // Room for more clocks than expected.
#include "DtDevClock.h"   // Interface being implemented.
#include "DtFunc.h"       // Finding the objects.
#include "DtPcieAbi.h"    // The object types.
#include "DtPcieCmd.h"    // The clock commands.
#include "cdtapi.h"       // The public functions and types.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The clocks asked for at first; a device with more is asked again.
#define LOCAL_CLOCKS 4

// The device's transmit clocks, in Local unless there were more than it holds.
typedef struct Clocks
{
    DtClockProps Local[LOCAL_CLOCKS];
    DtClockProps* Props;
    int Num;
} Clocks;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckDevice -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// What every function checks after its arguments: that the device is attached, and has
// Object.
//
static DtapiResult CheckDevice(const DtDevice* Device, const DtDevObject* Object)
{
    if (Device->Drv == NULL)
        return DTAPI_E_NOT_ATTACHED;
    return Object->Found;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindObject -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The object of Instance that is a driver function when IsDf, of Type and with Role.
//
static DtDevObject FindObject(const DtDevice* Device, const DtFuncInstance* Instance,
                              bool IsDf, int Type, const char* Role)
{
    DtDevObject Object;
    const DtFuncObject* Found = DtFunc_Get(Instance, IsDf, Type, Role);

    memset(&Object, 0, sizeof(Object));
    Object.Found = DTAPI_E_NOT_SUPPORTED;
    if (Found != NULL)
    {
        Object.Found = DtFunc_CheckDriverVersion(&Device->DriverVersion, IsDf, Type);
        Object.Ref = Found->Ref;
    }
    return Object;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FreeClocks -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void FreeClocks(Clocks* List)
{
    if (List->Props != List->Local)
        DtAlloc_Free(List->Props);
    List->Props = List->Local;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadClocks -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the device's transmit clocks into List, asking again with room for as many as
// the driver says there are. A driver whose count keeps growing is not believed. Free
// List with FreeClocks, whatever the result.
//
static DtapiResult ReadClocks(const DtDevice* Device, Clocks* List)
{
    int Max = LOCAL_CLOCKS;

    List->Props = List->Local;
    List->Num = 0;
    for (int Try = 0; Try < 3; Try++)
    {
        DtapiResult Result = DtPcieCmd_GenlockGetClockProps(
            Device->Drv, Device->Genlock.Ref, List->Props, Max, &List->Num);
        if (Result != DTAPI_E_BUF_TOO_SMALL)
            return Result;

        FreeClocks(List);
        Max = List->Num;
        List->Props = (DtClockProps*)DtAlloc_Malloc((size_t)Max * sizeof(DtClockProps));
        if (List->Props == NULL)
        {
            List->Props = List->Local;
            return DTAPI_E_OUT_OF_MEM;
        }
    }
    return DTAPI_E_DEV_DRIVER;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Attach +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevClock_OnAttach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Each API function is looked for at device level, with the empty role. A function that
// cannot be read for want of memory stops the attach; any other failure is its absence.
//
DtapiResult DtDevClock_OnAttach(DtDevice* Device)
{
    DtFuncInstance Instance;

    DtapiResult Result =
        DtFunc_Find(Device->Drv, DT_PROPERTY_DEVICE, "AF_GENLOCKCTRL_AF", "", &Instance);
    if (Result == DTAPI_E_OUT_OF_MEM)
        return Result;
    if (Result == DTAPI_OK)
    {
        Device->Genlock =
            FindObject(Device, &Instance, true, DT_FUNC_TYPE_GENLOCKCTRL, "");
        DtFunc_Release(&Instance);
    }

    Result =
        DtFunc_Find(Device->Drv, DT_PROPERTY_DEVICE, "AF_TODCLKCTRL_AF", "", &Instance);
    if (Result == DTAPI_E_OUT_OF_MEM)
        return Result;
    if (Result == DTAPI_OK)
    {
        Device->TodClkCtrl =
            FindObject(Device, &Instance, true, DT_FUNC_TYPE_TODCLKCTRL, "");
        DtFunc_Release(&Instance);
    }

    Result = DtFunc_Find(Device->Drv, DT_PROPERTY_DEVICE, "AF_TXCLKCNTRS", "", &Instance);
    if (Result == DTAPI_E_OUT_OF_MEM)
        return Result;
    if (Result == DTAPI_OK)
    {
        Device->ClkCnt[DTAPI_TXCLK_FRACTIONAL] =
            FindObject(Device, &Instance, false, DT_BLOCK_TYPE_CLKCNT, "FRAC_CLK");
        Device->ClkCnt[DTAPI_TXCLK_NON_FRACTIONAL] =
            FindObject(Device, &Instance, false, DT_BLOCK_TYPE_CLKCNT, "NON_FRAC_CLK");
        DtFunc_Release(&Instance);
    }
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Public functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_GetGenlockState -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtDevice_GetGenlockState(const DtDevice* Device, DtGenlockState* State)
{
    if (Device == NULL || State == NULL)
        return DTAPI_E_INVALID_ARG;
    memset(State, 0, sizeof(*State));

    DtapiResult Result = CheckDevice(Device, &Device->Genlock);
    if (Result != DTAPI_OK)
        return Result;
    return DtPcieCmd_GenlockGetState(Device->Drv, Device->Genlock.Ref, State);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_GetTimeOfDayState -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtDevice_GetTimeOfDayState(const DtDevice* Device, DtTimeOfDayState* State)
{
    if (Device == NULL || State == NULL)
        return DTAPI_E_INVALID_ARG;
    memset(State, 0, sizeof(*State));

    DtapiResult Result = CheckDevice(Device, &Device->TodClkCtrl);
    if (Result != DTAPI_OK)
        return Result;
    return DtPcieCmd_TodClkCtrlGetState(Device->Drv, Device->TodClkCtrl.Ref, State);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_GetTxClockCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The clock is looked up among the clocks first, as DTAPI does, and counted by the
// counter of its type.
//
DtapiResult DtDevice_GetTxClockCount(const DtDevice* Device, int TxClockId,
                                     uint32_t* TxClockCount)
{
    if (Device == NULL || TxClockCount == NULL)
        return DTAPI_E_INVALID_ARG;
    *TxClockCount = 0;

    DtapiResult Result = CheckDevice(Device, &Device->Genlock);
    if (Result != DTAPI_OK)
        return Result;

    Clocks List;
    Result = ReadClocks(Device, &List);
    int Type = -1;
    for (int i = 0; Result == DTAPI_OK && i < List.Num; i++)
    {
        if (List.Props[i].ClockIndex == TxClockId)
            Type = List.Props[i].ClockType;
    }
    FreeClocks(&List);
    if (Result != DTAPI_OK)
        return Result;
    if (Type < 0)
        return DTAPI_E_NOT_FOUND;

    const DtDevObject* Counter = &Device->ClkCnt[Type];
    if (Counter->Found != DTAPI_OK)
        return Counter->Found;
    int FrequencyHz;
    return DtPcieCmd_ClkCntGetTickCount(Device->Drv, Counter->Ref, TxClockCount,
                                        &FrequencyHz);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_GetTxClockOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtDevice_GetTxClockOffset(const DtDevice* Device, int TxClockId,
                                      double* OffsetPpm)
{
    if (Device == NULL || OffsetPpm == NULL)
        return DTAPI_E_INVALID_ARG;
    *OffsetPpm = 0.0;

    DtapiResult Result = CheckDevice(Device, &Device->Genlock);
    if (Result != DTAPI_OK)
        return Result;
    if (TxClockId < 0)
        return DTAPI_E_INVALID_ARG;

    int OffsetPpt;
    int64_t FrequencyMicroHz;
    Result = DtPcieCmd_GenlockGetFreqOffset(Device->Drv, Device->Genlock.Ref, TxClockId,
                                            &OffsetPpt, &FrequencyMicroHz);
    if (Result == DTAPI_OK)
        *OffsetPpm = OffsetPpt / 1e6;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_GetTxClockProperties -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Every clock lists the same ports: the public ports that can be an output and support
// ASI or SDI, as DTAPI lists them, up to DTAPI_TXCLK_MAX_PORTS.
//
DtapiResult DtDevice_GetTxClockProperties(const DtDevice* Device, int NumEntries,
                                          int* NumEntriesResult,
                                          DtTxClockProperties* Props)
{
    if (Device == NULL || NumEntriesResult == NULL || NumEntries < 0)
        return DTAPI_E_INVALID_ARG;
    *NumEntriesResult = 0;
    if (Props == NULL && NumEntries != 0)
        return DTAPI_E_INVALID_BUF;

    DtapiResult Result = CheckDevice(Device, &Device->Genlock);
    if (Result != DTAPI_OK)
        return Result;

    Clocks List;
    Result = ReadClocks(Device, &List);
    if (Result == DTAPI_OK)
        *NumEntriesResult = List.Num;
    if (Result == DTAPI_OK && List.Num > NumEntries)
        Result = DTAPI_E_BUF_TOO_SMALL;

    for (int i = 0; Result == DTAPI_OK && i < List.Num; i++)
    {
        const DtClockProps* Clock = &List.Props[i];
        DtTxClockProperties* Out = &Props[i];

        memset(Out, 0, sizeof(*Out));
        Out->TxClockId = Clock->ClockIndex;
        Out->ClockType = Clock->ClockType;
        Out->Frequency = (double)Clock->FrequencyMicroHz / 1e6;
        Out->RangePpm = Clock->RangePpt / 1e6;
        Out->StepSizePpm = Clock->StepSizePpt / 1e6;
        for (int Port = 1;
             Port <= Device->NumPublicPorts && Out->NumPorts < DTAPI_TXCLK_MAX_PORTS;
             Port++)
        {
            if (DtDevice_PortHasAllCaps(Device, Port, DT_CAP_OUTPUT) &&
                (DtDevice_PortHasAsiCaps(Device, Port) ||
                 DtDevice_PortHasSdiCaps(Device, Port)))
            {
                Out->Ports[Out->NumPorts++] = Port;
            }
        }
    }
    FreeClocks(&List);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_SetTxClockOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The offset is rounded to the nearest part per trillion, halves away from zero.
//
DtapiResult DtDevice_SetTxClockOffset(DtDevice* Device, int TxClockId, double OffsetPpm)
{
    if (Device == NULL)
        return DTAPI_E_INVALID_ARG;

    DtapiResult Result = CheckDevice(Device, &Device->Genlock);
    if (Result != DTAPI_OK)
        return Result;

    double Ppt = OffsetPpm * 1e6;
    if (!isfinite(Ppt) || Ppt <= (double)INT_MIN - 0.5 || Ppt >= (double)INT_MAX + 0.5 ||
        TxClockId < 0)
    {
        return DTAPI_E_INVALID_ARG;
    }
    int OffsetPpt = (int)(Ppt < 0.0 ? Ppt - 0.5 : Ppt + 0.5);
    return DtPcieCmd_GenlockSetFreqOffset(Device->Drv, Device->Genlock.Ref, TxClockId,
                                          OffsetPpt);
}
