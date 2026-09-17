// #*#*#*#*#*#*#*#*#*#*#*#*#* DtPcieCmdSdiTx.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - DtPcie driver commands: exclusive access and the SDI transmit blocks
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <limits.h>
#include <stddef.h>
#include <string.h>

// CDtapiLite includes
#include "DtPcieAbi.h"      // Vendored driver structures and IOCTL codes.
#include "DtPcieCmd.h"      // Interface being implemented.
#include "DtPcieCmdIssue.h" // Issuing commands.
#include "DtPcieStatus.h"   // Driver status to result.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Every block's operational-mode command takes the header and one Int. The requests are
// laid out once here; the assertions hold each block's structure to the same bytes.
//

typedef struct OpModeInput
{
    DtIoctlInputDataHdr m_CmdHdr;
    Int m_OpMode;
} OpModeInput;

_Static_assert(sizeof(OpModeInput) == sizeof(DtIoctlCDmaCCmdSetOpModeInput) &&
                   offsetof(OpModeInput, m_OpMode) ==
                       offsetof(DtIoctlCDmaCCmdSetOpModeInput, m_OpMode),
               "CDMAC's mode request must have the common layout");
_Static_assert(sizeof(OpModeInput) == sizeof(DtIoctlBurstFifoCmdSetOpModeInput) &&
                   offsetof(OpModeInput, m_OpMode) ==
                       offsetof(DtIoctlBurstFifoCmdSetOpModeInput, m_OpMode),
               "BURSTFIFO's mode request must have the common layout");
_Static_assert(sizeof(OpModeInput) == sizeof(DtIoctlSdiTxFCmdSetOpModeInput) &&
                   offsetof(OpModeInput, m_OpMode) ==
                       offsetof(DtIoctlSdiTxFCmdSetOpModeInput, m_OpMode),
               "SDITXF's mode request must have the common layout");
_Static_assert(sizeof(OpModeInput) == sizeof(DtIoctlSwitchCmdSetOpModeInput) &&
                   offsetof(OpModeInput, m_OpMode) ==
                       offsetof(DtIoctlSwitchCmdSetOpModeInput, m_OpMode),
               "SWITCH's mode request must have the common layout");
_Static_assert(sizeof(OpModeInput) == sizeof(DtIoctlSdiDmx12GCmdSetOpModeInput) &&
                   offsetof(OpModeInput, m_OpMode) ==
                       offsetof(DtIoctlSdiDmx12GCmdSetOpModeInput, m_OpMode),
               "SDIDMX12G's mode request must have the common layout");
_Static_assert(sizeof(OpModeInput) == sizeof(DtIoctlSdiTxPCmdSetOpModeInput) &&
                   offsetof(OpModeInput, m_OpMode) ==
                       offsetof(DtIoctlSdiTxPCmdSetOpModeInput, m_OpMode),
               "SDITXP's mode request must have the common layout");
_Static_assert(sizeof(OpModeInput) == sizeof(DtIoctlSdiTxPhyCmdSetOpModeInput) &&
                   offsetof(OpModeInput, m_OpMode) ==
                       offsetof(DtIoctlSdiTxPhyCmdSetOpModeInput, m_OpMode),
               "SDITXPHY's mode request must have the common layout");

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The block and function values are the same numbers, so one check serves both.
//
_Static_assert((int)DT_BLOCK_OPMODE_IDLE == (int)DT_FUNC_OPMODE_IDLE &&
                   (int)DT_BLOCK_OPMODE_STANDBY == (int)DT_FUNC_OPMODE_STANDBY &&
                   (int)DT_BLOCK_OPMODE_RUN == (int)DT_FUNC_OPMODE_RUN,
               "Block and function operational modes must be the same numbers");

static DtapiResult SetOpMode(OsDrv* Drv, uint32_t Code, int Cmd, int Uuid, int PortIndex,
                             int OpMode)
{
    OpModeInput In;

    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;
    if (OpMode != DT_BLOCK_OPMODE_IDLE && OpMode != DT_BLOCK_OPMODE_STANDBY &&
        OpMode != DT_BLOCK_OPMODE_RUN)
    {
        return DTAPI_E_INVALID_ARG;
    }

    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, Cmd, Uuid, PortIndex);
    In.m_OpMode = OpMode;
    return DtPcieCmd_Issue(Drv, Code, &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Plain -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A command that is only its header, answered with Out of OutSize bytes or nothing.
//
static DtapiResult Plain(OsDrv* Drv, uint32_t Code, int Cmd, int Uuid, int PortIndex,
                         void* Out, size_t OutSize)
{
    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlInputDataHdr In;
    DtPcieCmd_InitHeader(&In, Cmd, Uuid, PortIndex);
    if (Out != NULL)
        memset(Out, 0, OutSize);
    return DtPcieCmd_Issue(Drv, Code, &In, sizeof(In), Out, OutSize);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Exclusive access +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_ExclAccess -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_ExclAccess(OsDrv* Drv, int Uuid, int PortIndex, int Cmd)
{
    return Plain(Drv, DT_IOCTL(DT_IOCTL_EXCL_ACCESS_CMD), Cmd, Uuid, PortIndex, NULL, 0);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= CDMAC +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_CdmacGetProps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_CdmacGetProps(OsDrv* Drv, int Uuid, int PortIndex,
                                    DtCdmacProps* Props)
{
    if (Props != NULL)
        memset(Props, 0, sizeof(*Props));
    if (Props == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlCDmaCCmdGetPropertiesOutput Out;
    DtapiResult Result =
        Plain(Drv, DT_IOCTL(DT_IOCTL_CDMAC_CMD), DT_CDMAC_CMD_GET_PROPERTIES, Uuid,
              PortIndex, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    Props->Caps = Out.m_Capabilities;
    Props->PrefetchSize = Out.m_PrefetchSize;
    Props->PcieDataWidth = Out.m_PcieDataWidth;
    Props->ReorderBufSize = Out.m_ReorderBufSize;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_CdmacAllocateBufferAs -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// DtProxyCDMAC::AllocateBuffer. The answer must hold at least the command's fixed output
// structure; on Linux that is all there is, and the driver writes nothing into it.
//
DtapiResult DtPcieCmd_CdmacAllocateBufferAs(OsDrv* Drv, int Uuid, int PortIndex,
                                            int Direction, const OsDmaBuffer* Buf,
                                            bool BufferIsOutput)
{
    uint32_t Status = 0;

    if (Drv == NULL || Buf == NULL || Buf->Data == NULL || Buf->Size == 0 ||
        Buf->Size > (size_t)INT_MAX)
    {
        return DTAPI_E_INVALID_ARG;
    }
    if (Direction != DT_CDMAC_DIR_RX && Direction != DT_CDMAC_DIR_TX)
        return DTAPI_E_INVALID_ARG;

    DtIoctlCDmaCCmdAllocateBufferInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_CDMAC_CMD_ALLOCATE_BUFFER, Uuid, PortIndex);
    In.m_Direction = Direction;
    In.m_BufferSize = (Int)Buf->Size;
    DtIoctlCDmaCCmdAllocateBufferOutput Fixed;
    memset(&Fixed, 0, sizeof(Fixed));
    OsDmaHandOff HandOff;
    OsDmaBuffer_DescribeHandOffAs(BufferIsOutput, Buf, &Fixed, sizeof(Fixed), &HandOff);
    In.m_BufferAddr = HandOff.BufferAddr;

    size_t Returned = HandOff.OutSize;
    int Outcome = OsDrv_IoCtl(Drv, DT_IOCTL(DT_IOCTL_CDMAC_CMD), &In, sizeof(In),
                              HandOff.Out, &Returned, &Status);
    if (Outcome != OS_IOCTL_OK)
        return DtPcieStatus_OutcomeToResult(Outcome, Status);
    if (Returned < sizeof(Fixed))
        return DTAPI_E_DEV_DRIVER;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_CdmacAllocateBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_CdmacAllocateBuffer(OsDrv* Drv, int Uuid, int PortIndex,
                                          int Direction, const OsDmaBuffer* Buf)
{
#if defined(_WIN32) || defined(_WIN64)
    return DtPcieCmd_CdmacAllocateBufferAs(Drv, Uuid, PortIndex, Direction, Buf, true);
#else
    return DtPcieCmd_CdmacAllocateBufferAs(Drv, Uuid, PortIndex, Direction, Buf, false);
#endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_CdmacFreeBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_CdmacFreeBuffer(OsDrv* Drv, int Uuid, int PortIndex)
{
    return Plain(Drv, DT_IOCTL(DT_IOCTL_CDMAC_CMD), DT_CDMAC_CMD_FREE_BUFFER, Uuid,
                 PortIndex, NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_CdmacIssueChannelFlush -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_CdmacIssueChannelFlush(OsDrv* Drv, int Uuid, int PortIndex)
{
    return Plain(Drv, DT_IOCTL(DT_IOCTL_CDMAC_CMD), DT_CDMAC_CMD_ISSUE_CHANNEL_FLUSH,
                 Uuid, PortIndex, NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_CdmacSetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_CdmacSetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int OpMode)
{
    return SetOpMode(Drv, DT_IOCTL(DT_IOCTL_CDMAC_CMD), DT_CDMAC_CMD_SET_OPERATIONAL_MODE,
                     Uuid, PortIndex, OpMode);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_CdmacSetTestMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DtProxyCDMAC::SetTestMode refuses a mode it cannot convert; so does this.
//
DtapiResult DtPcieCmd_CdmacSetTestMode(OsDrv* Drv, int Uuid, int PortIndex, int TestMode)
{
    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;
    if (TestMode != DT_CDMAC_TESTMODE_NORMAL && TestMode != DT_CDMAC_TESTMODE_TEST_INT &&
        TestMode != DT_CDMAC_TESTMODE_TEST_EXT)
    {
        return DTAPI_E_INVALID_ARG;
    }

    DtIoctlCDmaCCmdSetTestModeInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_CDMAC_CMD_SET_TEST_MODE, Uuid, PortIndex);
    In.m_TestMode = TestMode;
    return DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_CDMAC_CMD), &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_CdmacGetTxReadOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_CdmacGetTxReadOffset(OsDrv* Drv, int Uuid, int PortIndex,
                                           uint32_t* Offset)
{
    if (Offset == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlCDmaCCmdGetTxRdOffsetOutput Out;
    DtapiResult Result =
        Plain(Drv, DT_IOCTL(DT_IOCTL_CDMAC_CMD), DT_CDMAC_CMD_GET_TX_READ_OFFSET, Uuid,
              PortIndex, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *Offset = Out.m_TxReadOffset;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_CdmacSetTxWriteOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_CdmacSetTxWriteOffset(OsDrv* Drv, int Uuid, int PortIndex,
                                            uint32_t Offset)
{
    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlCDmaCCmdSetTxWrOffsetInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_CDMAC_CMD_SET_TX_WRITE_OFFSET, Uuid, PortIndex);
    In.m_TxWriteOffset = Offset;
    return DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_CDMAC_CMD), &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_CdmacGetReorderBufStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_CdmacGetReorderBufStatus(OsDrv* Drv, int Uuid, int PortIndex,
                                               int* Load, int* MinMaxLoad)
{
    if (Load == NULL || MinMaxLoad == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlCDmaCCmdGetReorderBufStatusOutput Out;
    DtapiResult Result =
        Plain(Drv, DT_IOCTL(DT_IOCTL_CDMAC_CMD), DT_CDMAC_CMD_GET_REORDER_BUF_STATUS,
              Uuid, PortIndex, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *Load = Out.m_ReorderBufLoad;
    *MinMaxLoad = Out.m_ReorderBufMinMaxLoad;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_CdmacClearReorderBufMinMax -.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_CdmacClearReorderBufMinMax(OsDrv* Drv, int Uuid, int PortIndex)
{
    return Plain(Drv, DT_IOCTL(DT_IOCTL_CDMAC_CMD),
                 DT_CDMAC_CMD_CLEAR_REORDER_BUF_MIN_MAX, Uuid, PortIndex, NULL, 0);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= BURSTFIFO +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_BurstFifoGetProps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_BurstFifoGetProps(OsDrv* Drv, int Uuid, int PortIndex,
                                        DtBurstFifoProps* Props)
{
    if (Props != NULL)
        memset(Props, 0, sizeof(*Props));
    if (Props == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlBurstFifoCmdGetPropertiesOutput Out;
    DtapiResult Result =
        Plain(Drv, DT_IOCTL(DT_IOCTL_BURSTFIFO_CMD), DT_BURSTFIFO_CMD_GET_PROPERTIES,
              Uuid, PortIndex, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    Props->Caps = Out.m_Capabilities;
    Props->DataWidth = Out.m_DataWidth;
    Props->FifoSize = Out.m_BurstFifoSize;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_BurstFifoGetStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_BurstFifoGetStatus(OsDrv* Drv, int Uuid, int PortIndex,
                                         DtBurstFifoStatus* Status)
{
    if (Status != NULL)
        memset(Status, 0, sizeof(*Status));
    if (Status == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlBurstFifoCmdGetFifoStatusOutput Out;
    DtapiResult Result =
        Plain(Drv, DT_IOCTL(DT_IOCTL_BURSTFIFO_CMD), DT_BURSTFIFO_CMD_GET_FIFO_STATUS,
              Uuid, PortIndex, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    Status->CurFree = Out.m_CurFree;
    Status->CurLoad = Out.m_CurLoad;
    Status->MaxFree = Out.m_MaxFree;
    Status->MaxLoad = Out.m_MaxLoad;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_BurstFifoClearMax -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_BurstFifoClearMax(OsDrv* Drv, int Uuid, int PortIndex, bool MaxFree,
                                        bool MaxLoad)
{
    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlBurstFifoCmdClearFifoMaxInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_BURSTFIFO_CMD_CLEAR_FIFO_MAX, Uuid, PortIndex);
    In.m_ClearMaxFree = MaxFree ? 1 : 0;
    In.m_ClearMaxLoad = MaxLoad ? 1 : 0;
    return DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_BURSTFIFO_CMD), &In, sizeof(In), NULL,
                           0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_BurstFifoGetOvfUflCount -.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_BurstFifoGetOvfUflCount(OsDrv* Drv, int Uuid, int PortIndex,
                                              uint32_t* Count)
{
    if (Count == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlBurstFifoCmdGetOvfUflCountOutput Out;
    DtapiResult Result =
        Plain(Drv, DT_IOCTL(DT_IOCTL_BURSTFIFO_CMD), DT_BURSTFIFO_CMD_GET_OVFL_UFL_COUNT,
              Uuid, PortIndex, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *Count = (uint32_t)Out.m_OvfUflCount;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_BurstFifoSetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_BurstFifoSetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int OpMode)
{
    return SetOpMode(Drv, DT_IOCTL(DT_IOCTL_BURSTFIFO_CMD),
                     DT_BURSTFIFO_CMD_SET_OPERATIONAL_MODE, Uuid, PortIndex, OpMode);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= SDITXF +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_SdiTxFSetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_SdiTxFSetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int OpMode)
{
    return SetOpMode(Drv, DT_IOCTL(DT_IOCTL_SDITXF_CMD),
                     DT_SDITXF_CMD_SET_OPERATIONAL_MODE, Uuid, PortIndex, OpMode);
}

// .-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_SdiTxFSetFmtEventSetting -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_SdiTxFSetFmtEventSetting(OsDrv* Drv, int Uuid, int PortIndex,
                                               int NumLinesPerEvent,
                                               int NumSofsBetweenTod)
{
    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlSdiTxFCmdSetFmtEventSettingInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_SDITXF_CMD_SET_FMT_EVENT_SETTING, Uuid,
                         PortIndex);
    In.m_NumLinesPerEvent = NumLinesPerEvent;
    In.m_NumSofsBetweenTod = NumSofsBetweenTod;
    return DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_SDITXF_CMD), &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_SdiTxFGetStreamAlignment -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_SdiTxFGetStreamAlignment(OsDrv* Drv, int Uuid, int PortIndex,
                                               int* AlignmentBits)
{
    if (AlignmentBits == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlSdiTxFCmdGetStreamAlignmentOutput Out;
    DtapiResult Result =
        Plain(Drv, DT_IOCTL(DT_IOCTL_SDITXF_CMD), DT_SDITXF_CMD_GET_STREAM_ALIGNMENT,
              Uuid, PortIndex, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *AlignmentBits = Out.m_StreamAlignment;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_SdiTxFWaitForFmtEvent -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_SdiTxFWaitForFmtEvent(OsDrv* Drv, int Uuid, int PortIndex,
                                            int TimeoutMs, DtSdiTxFEvent* Event)
{
    if (Event != NULL)
        memset(Event, 0, sizeof(*Event));
    if (Drv == NULL || Event == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlSdiTxFCmdWaitForFmtEventInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_SDITXF_CMD_WAIT_FOR_FMT_EVENT, Uuid, PortIndex);
    In.m_Timeout = TimeoutMs;
    DtIoctlSdiTxFCmdWaitForFmtEventOutput Out;
    memset(&Out, 0, sizeof(Out));
    DtapiResult Result = DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_SDITXF_CMD), &In,
                                         sizeof(In), &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    Event->FrameId = Out.m_FrameId;
    Event->SeqNumber = Out.m_SeqNumber;
    Event->Underflow = Out.m_Underflow != 0;
    Event->SofTimeValid = Out.m_SofTimeValid != 0;
    Event->SofSeconds = Out.m_SofTime.m_Seconds;
    Event->SofNanoseconds = Out.m_SofTime.m_Nanoseconds;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= SWITCH and SDIDMX12G +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_SwitchSetPosition -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_SwitchSetPosition(OsDrv* Drv, int Uuid, int PortIndex,
                                        int InputIndex, int OutputIndex)
{
    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlSwitchCmdSetPositionInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_SWITCH_CMD_SET_POSITION, Uuid, PortIndex);
    In.m_InputIndex = InputIndex;
    In.m_OutputIndex = OutputIndex;
    return DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_SWITCH_CMD), &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_SwitchSetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_SwitchSetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int OpMode)
{
    return SetOpMode(Drv, DT_IOCTL(DT_IOCTL_SWITCH_CMD),
                     DT_SWITCH_CMD_SET_OPERATIONAL_MODE, Uuid, PortIndex, OpMode);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_SdiDmx12GSetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_SdiDmx12GSetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int OpMode)
{
    return SetOpMode(Drv, DT_IOCTL(DT_IOCTL_SDIDMX12G_CMD),
                     DT_SDIDMX12G_CMD_SET_OPERATIONAL_MODE, Uuid, PortIndex, OpMode);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= SDITXP +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_SdiTxPSetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_SdiTxPSetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int OpMode)
{
    return SetOpMode(Drv, DT_IOCTL(DT_IOCTL_SDITXP_CMD),
                     DT_SDITXP_CMD_SET_OPERATIONAL_MODE, Uuid, PortIndex, OpMode);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_SdiTxPSetGenerationMode -.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_SdiTxPSetGenerationMode(OsDrv* Drv, int Uuid, int PortIndex,
                                              bool Clamp, bool AncChecksum, bool LineCrc)
{
    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlSdiTxPCmdSetGenModeInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_SDITXP_CMD_SET_GENERATION_MODE, Uuid,
                         PortIndex);
    In.m_ClampEnable = Clamp ? 1 : 0;
    In.m_AdpChecksumEnable = AncChecksum ? 1 : 0;
    In.m_LineCrcEnable = LineCrc ? 1 : 0;
    return DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_SDITXP_CMD), &In, sizeof(In), NULL, 0);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= SDITXPHY +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_SdiTxPhySetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_SdiTxPhySetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int OpMode)
{
    return SetOpMode(Drv, DT_IOCTL(DT_IOCTL_SDITXPHY_CMD),
                     DT_SDITXPHY_CMD_SET_OPERATIONAL_MODE, Uuid, PortIndex, OpMode);
}

// .-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_SdiTxPhyGetUnderflowFlag -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_SdiTxPhyGetUnderflowFlag(OsDrv* Drv, int Uuid, int PortIndex,
                                               bool* Underflow)
{
    if (Underflow == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlSdiTxPhyCmdGetUnderflowFlagOutput Out;
    DtapiResult Result =
        Plain(Drv, DT_IOCTL(DT_IOCTL_SDITXPHY_CMD), DT_SDITXPHY_CMD_GET_UNDERFLOW_FLAG,
              Uuid, PortIndex, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *Underflow = Out.m_UflFlag != 0;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_SdiTxPhyClearUnderflowFlag -.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_SdiTxPhyClearUnderflowFlag(OsDrv* Drv, int Uuid, int PortIndex)
{
    return Plain(Drv, DT_IOCTL(DT_IOCTL_SDITXPHY_CMD),
                 DT_SDITXPHY_CMD_CLEAR_UNDERFLOW_FLAG, Uuid, PortIndex, NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_SdiTxPhySetStartOfFrameOffset -.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_SdiTxPhySetStartOfFrameOffset(OsDrv* Drv, int Uuid, int PortIndex,
                                                    int OffsetNs)
{
    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlSdiTxPhyCmdSetStartOfFrameOffsetInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_SDITXPHY_CMD_SET_START_OF_FRAME_OFFSET, Uuid,
                         PortIndex);
    In.m_StartOfFrameOffsetNs = OffsetNs;
    return DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_SDITXPHY_CMD), &In, sizeof(In), NULL,
                           0);
}
