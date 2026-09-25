// #*#*#*#*#*#*#*#*#*#*#*#*#* DtPcieCmdClock.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - DtPcie driver commands: the device's genlock, time-of-day and transmit clocks
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The genlock controller, the time-of-day clock control and the transmit-clock counters
// belong to the device rather than to a port. Their answers are converted to DTAPI's
// terms here, as DTAPI converts them, so that the layers above see no driver values.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"   // The answer with the clocks.
#include "DtPcieAbi.h"      // Vendored driver structures and IOCTL codes.
#include "DtPcieCmd.h"      // Interface being implemented.
#include "DtPcieCmdIssue.h" // Issuing commands.
#include "DtPcieVidStd.h"   // The driver's video standards.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GenlockStateFromDriver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int GenlockStateFromDriver(int DrvState)
{
    switch (DrvState)
    {
    case DT_GENLOCKCTRL_STATE_INVALID_REF:
        return DTAPI_GENL_INVALID;
    case DT_GENLOCKCTRL_STATE_LOCKING:
        return DTAPI_GENL_LOCKING;
    case DT_GENLOCKCTRL_STATE_LOCKED:
    case DT_GENLOCKCTRL_STATE_FREE_RUN:
        return DTAPI_GENL_LOCKED;
    default:
        return DTAPI_GENL_NO_REF;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TimeOfDayFromNs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A time of day the driver gives in nanoseconds.
//
static DtTimeOfDay TimeOfDayFromNs(int64_t Ns)
{
    DtTimeOfDay Time;
    uint64_t Value = (uint64_t)Ns;

    Time.Seconds = (uint32_t)(Value / 1000000000u);
    Time.Nanoseconds = (uint32_t)(Value % 1000000000u);
    return Time;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TodReferenceFromDriver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int TodReferenceFromDriver(int DrvReference)
{
    return DrvReference == DT_TODCLOCKCTRL_REF_STEADYCLOCK ? DTAPI_TODREF_STEADYCLOCK
                                                           : DTAPI_TODREF_INTERNAL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TodStateFromDriver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int TodStateFromDriver(int DrvState)
{
    switch (DrvState)
    {
    case DT_TODCLOCKCTRL_STATE_INVALID_REF:
        return DTAPI_TODCLK_INVALID_REF;
    case DT_TODCLOCKCTRL_STATE_LOCKING:
        return DTAPI_TODCLK_LOCKING;
    case DT_TODCLOCKCTRL_STATE_LOCKED:
        return DTAPI_TODCLK_LOCKED;
    default:
        return DTAPI_TODCLK_FREE_RUN;
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_ClkCntGetTickCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_ClkCntGetTickCount(OsDrv* Drv, DtDrvObject Object, uint32_t* Count,
                                         int* FrequencyHz)
{
    if (Count != NULL)
        *Count = 0;
    if (FrequencyHz != NULL)
        *FrequencyHz = 0;
    if (Drv == NULL || Count == NULL || FrequencyHz == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlClkCntCmdGetTickCountOutput Out;
    DtapiResult Result = DtPcieCmd_IssueHeaderOnly(Drv, DT_IOCTL(DT_IOCTL_CLKCNT_CMD),
                                                   DT_CLKCNT_CMD_GET_TICK_COUNT, Object,
                                                   &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *Count = Out.m_TickCount;
    *FrequencyHz = Out.m_ClockFreqHz;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_GenlockGetClockProps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The answer ends in an array of MaxProps entries, so it is allocated to that size. The
// driver answers a request for fewer clocks than it has with success and the count of
// its clocks, without filling the entries; the count is what tells.
//
DtapiResult DtPcieCmd_GenlockGetClockProps(OsDrv* Drv, DtDrvObject Object,
                                           DtClockProps* Props, int MaxProps,
                                           int* NumProps)
{
    if (NumProps != NULL)
        *NumProps = 0;
    if (Drv == NULL || NumProps == NULL || MaxProps < 0 ||
        (Props == NULL && MaxProps > 0))
        return DTAPI_E_INVALID_ARG;
    if ((size_t)MaxProps >
        (SIZE_MAX - sizeof(DtIoctlGenLockCtrlCmdGetDcoClockPropsOutput)) /
            sizeof(DtIoctlGenLockCtrlClockProps))
    {
        return DTAPI_E_INVALID_ARG;
    }

    const size_t OutSize = sizeof(DtIoctlGenLockCtrlCmdGetDcoClockPropsOutput) +
                           (size_t)MaxProps * sizeof(DtIoctlGenLockCtrlClockProps);
    DtIoctlGenLockCtrlCmdGetDcoClockPropsOutput* Out =
        (DtIoctlGenLockCtrlCmdGetDcoClockPropsOutput*)DtAlloc_Malloc(OutSize);
    if (Out == NULL)
        return DTAPI_E_OUT_OF_MEM;
    memset(Out, 0, OutSize);

    DtIoctlGenLockCtrlCmdGetDcoClockPropsInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_GENLOCKCTRL_CMD_GET_DCO_CLK_PROPS, Object);
    In.m_MaxNumEntries = MaxProps;

    DtapiResult Result = DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_GENLOCKCTRL_CMD), &In,
                                         sizeof(In), Out, OutSize);
    if (DT_SUCCEEDED(Result) && Out->m_NumEntries < 0)
        Result = DTAPI_E_DEV_DRIVER;
    else if (DT_SUCCEEDED(Result))
    {
        *NumProps = Out->m_NumEntries;
        if (Out->m_NumEntries > MaxProps)
            Result = DTAPI_E_BUF_TOO_SMALL;
    }

    for (int i = 0; DT_SUCCEEDED(Result) && i < Out->m_NumEntries; i++)
    {
        const DtIoctlGenLockCtrlClockProps* Entry = &Out->m_Properties[i];
        DtClockProps* ClockProps = &Props[i];

        if (Entry->m_ClockType == DT_GENLOCKCTR_CLKTYPE_FRACTIONAL)
            ClockProps->ClockType = DTAPI_TXCLK_FRACTIONAL;
        else if (Entry->m_ClockType == DT_GENLOCKCTR_CLKTYPE_NON_FRACTIONAL)
            ClockProps->ClockType = DTAPI_TXCLK_NON_FRACTIONAL;
        else
            Result = DTAPI_E_DEV_DRIVER;
        ClockProps->ClockIndex = Entry->m_ClockIdx;
        ClockProps->StepSizePpt = Entry->m_StepSizePpt;
        ClockProps->RangePpt = Entry->m_RangePpt;
        ClockProps->FrequencyMicroHz = Entry->m_FrequencyuHz;
    }
    if (Result != DTAPI_OK && Result != DTAPI_E_BUF_TOO_SMALL)
        *NumProps = 0;
    DtAlloc_Free(Out);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_GenlockGetFreqOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_GenlockGetFreqOffset(OsDrv* Drv, DtDrvObject Object, int ClockIndex,
                                           int* OffsetPpt, int64_t* FrequencyMicroHz)
{
    if (OffsetPpt != NULL)
        *OffsetPpt = 0;
    if (FrequencyMicroHz != NULL)
        *FrequencyMicroHz = 0;
    if (Drv == NULL || OffsetPpt == NULL || FrequencyMicroHz == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlGenLockCtrlCmdGetDcoFreqOffsetInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_GENLOCKCTRL_CMD_GET_DCO_FREQ_OFFSET, Object);
    In.m_ClockIdx = ClockIndex;
    DtIoctlGenLockCtrlCmdGetDcoFreqOffsetOutput Out;
    memset(&Out, 0, sizeof(Out));

    DtapiResult Result = DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_GENLOCKCTRL_CMD), &In,
                                         sizeof(In), &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *OffsetPpt = Out.m_DcoFreqOffsetPpt;
    *FrequencyMicroHz = Out.m_DcoFrequencyMicroHz;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_GenlockGetState -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_GenlockGetState(OsDrv* Drv, DtDrvObject Object,
                                      DtGenlockState* State)
{
    if (State != NULL)
        memset(State, 0, sizeof(*State));
    if (Drv == NULL || State == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlGenLockCtrlCmdGetState2Output Out;
    DtapiResult Result = DtPcieCmd_IssueHeaderOnly(
        Drv, DT_IOCTL(DT_IOCTL_GENLOCKCTRL_CMD), DT_GENLOCKCTRL_CMD_GET_STATE2, Object,
        &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    State->State = GenlockStateFromDriver(Out.m_GenLockState);
    State->RefVidStd = DtPcieVidStd_FromDriver(Out.m_RefVidStd);
    State->DetVidStd = DtPcieVidStd_FromDriver(Out.m_DetVidStd);
    State->TofTimeValid = Out.m_IsSofValid != 0;
    State->TofTime.Seconds = Out.m_SofTime.m_Seconds;
    State->TofTime.Nanoseconds = Out.m_SofTime.m_Nanoseconds;
    State->TimeSinceLastTof = Out.m_TimeSinceLastSof;
    State->RefFrameNum = (int64_t)Out.m_SofCount;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_GenlockSetFreqOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_GenlockSetFreqOffset(OsDrv* Drv, DtDrvObject Object, int ClockIndex,
                                           int OffsetPpt)
{
    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlGenLockCtrlCmdSetDcoFreqOffsetInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_GENLOCKCTRL_CMD_SET_DCO_FREQ_OFFSET, Object);
    In.m_ClockIdx = ClockIndex;
    In.m_DcoFreqOffsetPpt = OffsetPpt;

    return DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_GENLOCKCTRL_CMD), &In, sizeof(In), NULL,
                           0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_TodClkCtrlGetState -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_TodClkCtrlGetState(OsDrv* Drv, DtDrvObject Object,
                                         DtTimeOfDayState* State)
{
    if (State != NULL)
        memset(State, 0, sizeof(*State));
    if (Drv == NULL || State == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlTodClockCtrlCmdGetStateOutput Out;
    DtapiResult Result = DtPcieCmd_IssueHeaderOnly(
        Drv, DT_IOCTL(DT_IOCTL_TODCLOCKCTRL_CMD), DT_TODCLOCKCTRL_CMD_GET_STATE, Object,
        &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    State->State = TodStateFromDriver(Out.m_TodClockCtrlState);
    State->TodReference = TodReferenceFromDriver(Out.m_TodReference);
    State->RefDeviation = Out.m_DeviationPpm;
    State->TodTimestamp = TimeOfDayFromNs(Out.m_TodTimestamp);
    State->RefTimestamp = TimeOfDayFromNs(Out.m_RefTimestamp);
    return DTAPI_OK;
}
