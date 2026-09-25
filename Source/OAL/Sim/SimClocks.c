// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# SimClocks.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The emulated card's genlock, time-of-day clock control and transmit clocks
//
// SPDX-License-Identifier: BSD-3-Clause
//
// As much of the three as the library uses: their states, the clocks and their offsets,
// and the counters. The refusals are the driver's: an offset is set only while the
// genlock runs free, and only within the clock's range.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "DtPcieAbi.h" // Vendored driver structures and status codes.
#include "SimClocks.h" // Interface being implemented.
#include "SimNw.h"     // The time of day.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define NUM_CLOCKS 2

// How far an offset reaches either way, and its step, as a DTA-2178 reports them.
#define CLOCK_RANGE_PPT 200000000
#define FRAC_STEP_PPT 20
#define NON_FRAC_STEP_PPT 11

// The frame period of the 625i50 reference, in nanoseconds.
#define REF_FRAME_NS 40000000u

// The clocks as a DTA-2178 lists them: the fractional one first.
static const int64_t g_Nominal[NUM_CLOCKS] = {148351648351648, 148500000000000};
static const int g_StepPpt[NUM_CLOCKS] = {FRAC_STEP_PPT, NON_FRAC_STEP_PPT};

static int g_GenlockState;
static int g_RefVidStd;
static int g_DetVidStd;
static int g_TodState;
static int g_TodReference;
static int g_TodDeviation;
static int g_ClockType[NUM_CLOCKS];
static int g_OffsetPpt[NUM_CLOCKS];

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FrequencyOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The frequency of a clock with its offset, in micro-Hertz: the nominal frequency in
// whole Hertz times the offset in parts per trillion, divided by a million, is the
// change.
//
static int64_t FrequencyOf(int Clock)
{
    return g_Nominal[Clock] + g_Nominal[Clock] / 1000000 * g_OffsetPpt[Clock] / 1000000;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OutFits -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool OutFits(const void* Out, const size_t* OutSize, size_t Size)
{
    return Out != NULL && OutSize != NULL && *OutSize >= Size;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClkCntCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A counter with role NON_FRAC_CLK counts the non-fractional clock, one with FRAC_CLK the
// fractional one. The count is how many periods of the clock the time of day holds.
//
static uint32_t ClkCntCmd(const char* Role, int Cmd, size_t InSize, void* Out,
                          size_t* OutSize)
{
    if (Cmd != DT_CLKCNT_CMD_GET_TICK_COUNT)
        return DT_STATUS_NOT_SUPPORTED;
    if (InSize < sizeof(DtIoctlClkCntCmdGetTickCountInput) ||
        !OutFits(Out, OutSize, sizeof(DtIoctlClkCntCmdGetTickCountOutput)))
    {
        return DT_STATUS_INVALID_PARAMETER;
    }

    int Clock = strcmp(Role, "FRAC_CLK") == 0 ? 0 : 1;
    int64_t FrequencyUHz = FrequencyOf(Clock);
    uint64_t NowNs = SimNw_Now();
    uint64_t Seconds = NowNs / 1000000000u;
    uint64_t Fraction = NowNs % 1000000000u;
    double Ticks = (double)Seconds * ((double)FrequencyUHz / 1e6) +
                   (double)Fraction * ((double)FrequencyUHz / 1e15);

    DtIoctlClkCntCmdGetTickCountOutput* Answer = (DtIoctlClkCntCmdGetTickCountOutput*)Out;
    memset(Answer, 0, sizeof(*Answer));
    Answer->m_TickCount = (UInt32)(uint64_t)Ticks;
    Answer->m_ClockFreqHz = (Int)(g_Nominal[Clock] / 1000000);
    *OutSize = sizeof(*Answer);
    return DT_STATUS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetClockProps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Asked for fewer clocks than there are, the driver answers with success and the count
// of its clocks, filling none of them; the emulator does the same.
//
static uint32_t GetClockProps(const void* In, size_t InSize, void* Out, size_t* OutSize)
{
    if (InSize < sizeof(DtIoctlGenLockCtrlCmdGetDcoClockPropsInput))
        return DT_STATUS_INVALID_PARAMETER;
    const DtIoctlGenLockCtrlCmdGetDcoClockPropsInput* Request =
        (const DtIoctlGenLockCtrlCmdGetDcoClockPropsInput*)In;
    if (Request->m_MaxNumEntries < 0 ||
        !OutFits(Out, OutSize,
                 sizeof(DtIoctlGenLockCtrlCmdGetDcoClockPropsOutput) +
                     (size_t)Request->m_MaxNumEntries *
                         sizeof(DtIoctlGenLockCtrlClockProps)))
    {
        return DT_STATUS_INVALID_PARAMETER;
    }

    DtIoctlGenLockCtrlCmdGetDcoClockPropsOutput* Answer =
        (DtIoctlGenLockCtrlCmdGetDcoClockPropsOutput*)Out;
    Answer->m_NumEntries = NUM_CLOCKS;
    for (int i = 0; i < NUM_CLOCKS && Request->m_MaxNumEntries >= NUM_CLOCKS; i++)
    {
        DtIoctlGenLockCtrlClockProps* Clock = &Answer->m_Properties[i];
        Clock->m_ClockIdx = i;
        Clock->m_ClockType = g_ClockType[i];
        Clock->m_StepSizePpt = g_StepPpt[i];
        Clock->m_RangePpt = CLOCK_RANGE_PPT;
        Clock->m_FrequencyuHz = g_Nominal[i];
    }
    return DT_STATUS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetGenlockState -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The top of frame falls on whole frame periods of the time of day.
//
static uint32_t GetGenlockState(size_t InSize, void* Out, size_t* OutSize)
{
    if (InSize < sizeof(DtIoctlGenLockCtrlCmdGetState2Input) ||
        !OutFits(Out, OutSize, sizeof(DtIoctlGenLockCtrlCmdGetState2Output)))
    {
        return DT_STATUS_INVALID_PARAMETER;
    }

    uint64_t NowNs = SimNw_Now();
    uint64_t Frames = NowNs / REF_FRAME_NS;
    uint64_t TofNs = Frames * REF_FRAME_NS;

    DtIoctlGenLockCtrlCmdGetState2Output* Answer =
        (DtIoctlGenLockCtrlCmdGetState2Output*)Out;
    memset(Answer, 0, sizeof(*Answer));
    Answer->m_GenLockState = g_GenlockState;
    Answer->m_RefVidStd = g_RefVidStd;
    Answer->m_DetVidStd = g_DetVidStd;
    Answer->m_SofCount = Frames;
    Answer->m_IsSofValid = g_GenlockState != DT_GENLOCKCTRL_STATE_NO_REF &&
                           g_GenlockState != DT_GENLOCKCTRL_STATE_INVALID_REF;
    Answer->m_SofTime.m_Seconds = (UInt32)(TofNs / 1000000000u);
    Answer->m_SofTime.m_Nanoseconds = (UInt32)(TofNs % 1000000000u);
    Answer->m_TimeSinceLastSof = (Int)(NowNs - TofNs);
    *OutSize = sizeof(*Answer);
    return DT_STATUS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetFreqOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static uint32_t GetFreqOffset(const void* In, size_t InSize, void* Out, size_t* OutSize)
{
    if (InSize < sizeof(DtIoctlGenLockCtrlCmdGetDcoFreqOffsetInput) ||
        !OutFits(Out, OutSize, sizeof(DtIoctlGenLockCtrlCmdGetDcoFreqOffsetOutput)))
    {
        return DT_STATUS_INVALID_PARAMETER;
    }
    int Clock = ((const DtIoctlGenLockCtrlCmdGetDcoFreqOffsetInput*)In)->m_ClockIdx;
    if (Clock < 0 || Clock >= NUM_CLOCKS)
        return DT_STATUS_INVALID_PARAMETER;

    DtIoctlGenLockCtrlCmdGetDcoFreqOffsetOutput* Answer =
        (DtIoctlGenLockCtrlCmdGetDcoFreqOffsetOutput*)Out;
    memset(Answer, 0, sizeof(*Answer));
    Answer->m_DcoFreqOffsetPpt = g_OffsetPpt[Clock];
    Answer->m_DcoFrequencyMicroHz = FrequencyOf(Clock);
    *OutSize = sizeof(*Answer);
    return DT_STATUS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetFreqOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// In the driver's order: the clock, then whether the genlock runs free, then the range.
//
static uint32_t SetFreqOffset(const void* In, size_t InSize)
{
    if (InSize < sizeof(DtIoctlGenLockCtrlCmdSetDcoFreqOffsetInput))
        return DT_STATUS_INVALID_PARAMETER;
    const DtIoctlGenLockCtrlCmdSetDcoFreqOffsetInput* Request =
        (const DtIoctlGenLockCtrlCmdSetDcoFreqOffsetInput*)In;
    if (Request->m_ClockIdx < 0 || Request->m_ClockIdx >= NUM_CLOCKS)
        return DT_STATUS_INVALID_PARAMETER;
    if (g_GenlockState != DT_GENLOCKCTRL_STATE_FREE_RUN)
        return DT_STATUS_IN_USE;
    if (Request->m_DcoFreqOffsetPpt < -CLOCK_RANGE_PPT ||
        Request->m_DcoFreqOffsetPpt > CLOCK_RANGE_PPT)
    {
        return DT_STATUS_INVALID_PARAMETER;
    }

    g_OffsetPpt[Request->m_ClockIdx] = Request->m_DcoFreqOffsetPpt;
    return DT_STATUS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GenlockCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static uint32_t GenlockCmd(int Cmd, const void* In, size_t InSize, void* Out,
                           size_t* OutSize)
{
    switch (Cmd)
    {
    case DT_GENLOCKCTRL_CMD_GET_STATE2:
        return GetGenlockState(InSize, Out, OutSize);
    case DT_GENLOCKCTRL_CMD_GET_DCO_CLK_PROPS:
        return GetClockProps(In, InSize, Out, OutSize);
    case DT_GENLOCKCTRL_CMD_GET_DCO_FREQ_OFFSET:
        return GetFreqOffset(In, InSize, Out, OutSize);
    case DT_GENLOCKCTRL_CMD_SET_DCO_FREQ_OFFSET:
        return SetFreqOffset(In, InSize);
    default:
        return DT_STATUS_NOT_SUPPORTED;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TodClkCtrlCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Both timestamps are the time of day, as a card without a reference gives them.
//
static uint32_t TodClkCtrlCmd(int Cmd, size_t InSize, void* Out, size_t* OutSize)
{
    if (Cmd != DT_TODCLOCKCTRL_CMD_GET_STATE)
        return DT_STATUS_NOT_SUPPORTED;
    if (InSize < sizeof(DtIoctlTodClockCtrlCmdGetStateInput) ||
        !OutFits(Out, OutSize, sizeof(DtIoctlTodClockCtrlCmdGetStateOutput)))
    {
        return DT_STATUS_INVALID_PARAMETER;
    }

    int64_t NowNs = (int64_t)SimNw_Now();
    DtIoctlTodClockCtrlCmdGetStateOutput* Answer =
        (DtIoctlTodClockCtrlCmdGetStateOutput*)Out;
    memset(Answer, 0, sizeof(*Answer));
    Answer->m_TodClockCtrlState = g_TodState;
    Answer->m_DeviationPpm = g_TodDeviation;
    Answer->m_TodReference = g_TodReference;
    Answer->m_TodTimestamp = NowNs;
    Answer->m_RefTimestamp = NowNs;
    *OutSize = sizeof(*Answer);
    return DT_STATUS_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Interface +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimClocks_Cmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint32_t SimClocks_Cmd(int FunctionCode, bool IsDf, int Type, const char* Role, int Cmd,
                       const void* In, size_t InSize, void* Out, size_t* OutSize)
{
    if (FunctionCode == DT_FUNC_CODE_GENLOCKCTRL_CMD && IsDf &&
        Type == DT_FUNC_TYPE_GENLOCKCTRL)
    {
        return GenlockCmd(Cmd, In, InSize, Out, OutSize);
    }
    if (FunctionCode == DT_FUNC_CODE_TODCLOCKCTRL_CMD && IsDf &&
        Type == DT_FUNC_TYPE_TODCLKCTRL)
    {
        return TodClkCtrlCmd(Cmd, InSize, Out, OutSize);
    }
    if (FunctionCode == DT_FUNC_CODE_CLKCNT_CMD && !IsDf && Type == DT_BLOCK_TYPE_CLKCNT)
        return ClkCntCmd(Role, Cmd, InSize, Out, OutSize);
    return DT_STATUS_NOT_SUPPORTED;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimClocks_Reset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimClocks_Reset(void)
{
    g_GenlockState = DT_GENLOCKCTRL_STATE_FREE_RUN;
    g_RefVidStd = DT_VIDSTD_625I50;
    g_DetVidStd = DT_VIDSTD_UNKNOWN;
    g_TodState = DT_TODCLOCKCTRL_STATE_FREE_RUN;
    g_TodReference = DT_TODCLOCKCTRL_REF_INTERNAL;
    g_TodDeviation = 0;
    g_ClockType[0] = DT_GENLOCKCTR_CLKTYPE_FRACTIONAL;
    g_ClockType[1] = DT_GENLOCKCTR_CLKTYPE_NON_FRACTIONAL;
    g_OffsetPpt[0] = 0;
    g_OffsetPpt[1] = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimClocks_SetClockType -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimClocks_SetClockType(int ClockIndex, int Type)
{
    if (ClockIndex >= 0 && ClockIndex < NUM_CLOCKS)
        g_ClockType[ClockIndex] = Type;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimClocks_SetGenlock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimClocks_SetGenlock(int State, int RefVidStd, int DetVidStd)
{
    g_GenlockState = State;
    g_RefVidStd = RefVidStd;
    g_DetVidStd = DetVidStd;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimClocks_SetTod -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimClocks_SetTod(int State, int Reference, int DeviationPpm)
{
    g_TodState = State;
    g_TodReference = Reference;
    g_TodDeviation = DeviationPpm;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimClocks_Takes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool SimClocks_Takes(int FunctionCode)
{
    return FunctionCode == DT_FUNC_CODE_GENLOCKCTRL_CMD ||
           FunctionCode == DT_FUNC_CODE_TODCLOCKCTRL_CMD ||
           FunctionCode == DT_FUNC_CODE_CLKCNT_CMD;
}
