// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtPcieCmdPipe.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - DtPcie driver commands: the network port, its MAC and its pipes
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <limits.h>
#include <stddef.h>
#include <string.h>

// CDTAPI includes
#include "DtPcieAbi.h"      // Vendored driver structures and IOCTL codes.
#include "DtPcieCmd.h"      // Interface being implemented.
#include "DtPcieCmdIssue.h" // Issuing commands.
#include "DtPcieStatus.h"   // Driver status to result.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The offset commands take the header and one UInt. They are laid out once here; the
// assertions hold each command's structure to the same bytes.
//

typedef struct OffsetInput
{
    DtIoctlInputDataHdr m_CmdHdr;
    UInt m_Offset;
} OffsetInput;

_Static_assert(sizeof(OffsetInput) == sizeof(DtIoctlPipeCmdSetRxReadOffsetInput) &&
                   offsetof(OffsetInput, m_Offset) ==
                       offsetof(DtIoctlPipeCmdSetRxReadOffsetInput, m_RxReadOffset),
               "SET_RX_READ_OFFSET must have the common layout");
_Static_assert(sizeof(OffsetInput) == sizeof(DtIoctlPipeCmdSetTxWriteOffsetInput) &&
                   offsetof(OffsetInput, m_Offset) ==
                       offsetof(DtIoctlPipeCmdSetTxWriteOffsetInput, m_TxWriteOffset),
               "SET_TX_WRITE_OFFSET must have the common layout");
_Static_assert(sizeof(UInt) == sizeof(DtIoctlPipeCmdGetRxWriteOffsetOutput) &&
                   sizeof(UInt) == sizeof(DtIoctlPipeCmdGetTxReadOffsetOutput),
               "The offset answers must be one UInt");

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtapiResult SetOffset(OsDrv* Drv, int Cmd, DtDrvObject Pipe, uint32_t Offset)
{
    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    OffsetInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, Cmd, Pipe);
    In.m_Offset = Offset;
    return DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_PIPE_CMD), &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtapiResult GetOffset(OsDrv* Drv, int Cmd, DtDrvObject Pipe, uint32_t* Offset)
{
    if (Offset == NULL)
        return DTAPI_E_INVALID_ARG;

    UInt Out = 0;
    DtapiResult Result = DtPcieCmd_IssueHeaderOnly(Drv, DT_IOCTL(DT_IOCTL_PIPE_CMD), Cmd,
                                                   Pipe, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *Offset = Out;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= EMAC +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_NwGetMacAddress -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_NwGetMacAddress(OsDrv* Drv, DtDrvObject Object, uint8_t* Mac)
{
    if (Mac == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlEMACCmdGetMacAddressOutput Out;
    DtapiResult Result =
        DtPcieCmd_IssueHeaderOnly(Drv, DT_IOCTL(DT_IOCTL_EMAC_CMD),
                                  DT_EMAC_CMD_GET_MACADDRESS, Object, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    memcpy(Mac, Out.m_Address, sizeof(Out.m_Address));
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_NwGetPhySpeed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_NwGetPhySpeed(OsDrv* Drv, DtDrvObject Object, int* Speed)
{
    if (Speed == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlEMACCmdGetPhySpeedOutput Out;
    DtapiResult Result =
        DtPcieCmd_IssueHeaderOnly(Drv, DT_IOCTL(DT_IOCTL_EMAC_CMD),
                                  DT_EMAC_CMD_GET_PHY_SPEED, Object, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *Speed = (int)Out.m_Speed;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= NW +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_NwOpenPipe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_NwOpenPipe(OsDrv* Drv, DtDrvObject Object, int Type,
                                 int TypeFallback, DtDrvObject* Pipe)
{
    if (Pipe != NULL)
    {
        Pipe->Uuid = 0;
        Pipe->PortIndex = Object.PortIndex;
    }
    if (Drv == NULL || Pipe == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlNwCmdPipeOpenInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_NW_CMD_PIPE_OPEN, Object);
    In.m_PipeType = Type;
    In.m_PipeTypeFallback = TypeFallback;
    DtIoctlNwCmdPipeOpenOutput Out;
    memset(&Out, 0, sizeof(Out));
    DtapiResult Result = DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_NW_CMD), &In, sizeof(In),
                                         &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    Pipe->Uuid = (int)Out.m_PipeUuid;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_NwClosePipe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The command is addressed to the pipe rather than to the network function.
//
DtapiResult DtPcieCmd_NwClosePipe(OsDrv* Drv, DtDrvObject Pipe)
{
    return DtPcieCmd_IssueHeaderOnly(Drv, DT_IOCTL(DT_IOCTL_NW_CMD), DT_NW_CMD_PIPE_CLOSE,
                                     Pipe, NULL, 0);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= PIPE +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeGetProps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_PipeGetProps(OsDrv* Drv, DtDrvObject Pipe, DtPipeProps* Props)
{
    if (Props != NULL)
        memset(Props, 0, sizeof(*Props));
    if (Props == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlPipeCmdGetPropertiesOutput Out;
    DtapiResult Result =
        DtPcieCmd_IssueHeaderOnly(Drv, DT_IOCTL(DT_IOCTL_PIPE_CMD),
                                  DT_PIPE_CMD_GET_PROPERTIES, Pipe, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    Props->Caps = Out.m_Capabilities;
    Props->PrefetchSize = Out.m_PrefetchSize;
    Props->DataWidth = Out.m_PipeDataWidth;
    Props->Type = Out.m_PipeType;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeGetStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_PipeGetStatus(OsDrv* Drv, DtDrvObject Pipe, DtPipeStatus* Status)
{
    if (Status != NULL)
        memset(Status, 0, sizeof(*Status));
    if (Status == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlPipeCmdGetStatusOutput Out;
    DtapiResult Result =
        DtPcieCmd_IssueHeaderOnly(Drv, DT_IOCTL(DT_IOCTL_PIPE_CMD),
                                  DT_PIPE_CMD_GET_STATUS, Pipe, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    Status->OpStatus = Out.m_OpStatus;
    Status->StatusFlags = Out.m_StatusFlags;
    Status->ErrorFlags = Out.m_ErrorFlags;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeSetSharedBufferAs -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The answer must hold at least the command's fixed output structure, as for CDMAC's
// buffer.
//
DtapiResult DtPcieCmd_PipeSetSharedBufferAs(OsDrv* Drv, DtDrvObject Pipe,
                                            const OsDmaBuffer* Buf, bool BufferIsOutput)
{
    uint32_t Status = 0;

    if (Drv == NULL || Buf == NULL || Buf->Data == NULL || Buf->Size == 0 ||
        Buf->Size > (size_t)INT_MAX)
    {
        return DTAPI_E_INVALID_ARG;
    }

    DtIoctlPipeCmdSetSharedBufferInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_PIPE_CMD_SET_SHARED_BUFFER, Pipe);
    In.m_BufferSize = (Int)Buf->Size;
    DtIoctlPipeCmdSetSharedBufferOutput FixedOut;
    memset(&FixedOut, 0, sizeof(FixedOut));
    OsDmaHandOff HandOff;
    OsDmaBuffer_DescribeHandOffAs(BufferIsOutput, Buf, &FixedOut, sizeof(FixedOut),
                                  &HandOff);
    In.m_BufferAddr = HandOff.BufferAddr;

    size_t BytesReturned = HandOff.OutSize;
    int Outcome = OsDrv_Ioctl(Drv, DT_IOCTL(DT_IOCTL_PIPE_CMD), &In, sizeof(In),
                              HandOff.Out, &BytesReturned, &Status);
    if (Outcome != OS_IOCTL_OK)
        return DtPcieStatus_OutcomeToResult(Outcome, Status);
    if (BytesReturned < sizeof(FixedOut))
        return DTAPI_E_DEV_DRIVER;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeSetSharedBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_PipeSetSharedBuffer(OsDrv* Drv, DtDrvObject Pipe,
                                          const OsDmaBuffer* Buf)
{
#if defined(_WIN32) || defined(_WIN64)
    return DtPcieCmd_PipeSetSharedBufferAs(Drv, Pipe, Buf, true);
#else
    return DtPcieCmd_PipeSetSharedBufferAs(Drv, Pipe, Buf, false);
#endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeReleaseSharedBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_PipeReleaseSharedBuffer(OsDrv* Drv, DtDrvObject Pipe)
{
    return DtPcieCmd_IssueHeaderOnly(Drv, DT_IOCTL(DT_IOCTL_PIPE_CMD),
                                     DT_PIPE_CMD_RELEASE_SHARED_BUFFER, Pipe, NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeFlush -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_PipeFlush(OsDrv* Drv, DtDrvObject Pipe)
{
    return DtPcieCmd_IssueHeaderOnly(Drv, DT_IOCTL(DT_IOCTL_PIPE_CMD),
                                     DT_PIPE_CMD_ISSUE_PIPE_FLUSH, Pipe, NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeSetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// An operational mode that is none of the driver's own values is refused without a
// command.
//
DtapiResult DtPcieCmd_PipeSetOpMode(OsDrv* Drv, DtDrvObject Pipe, int OpMode)
{
    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;
    if (OpMode != DT_PIPE_OPMODE_IDLE && OpMode != DT_PIPE_OPMODE_STANDBY &&
        OpMode != DT_PIPE_OPMODE_RUN)
    {
        return DTAPI_E_INVALID_ARG;
    }

    DtIoctlPipeCmdSetOpModeInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_PIPE_CMD_SET_OPERATIONAL_MODE, Pipe);
    In.m_OpMode = OpMode;
    return DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_PIPE_CMD), &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeSetRxReadOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_PipeSetRxReadOffset(OsDrv* Drv, DtDrvObject Pipe, uint32_t Offset)
{
    return SetOffset(Drv, DT_PIPE_CMD_SET_RX_READ_OFFSET, Pipe, Offset);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeGetRxWriteOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_PipeGetRxWriteOffset(OsDrv* Drv, DtDrvObject Pipe, uint32_t* Offset)
{
    return GetOffset(Drv, DT_PIPE_CMD_GET_RX_WRITE_OFFSET, Pipe, Offset);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeSetTxWriteOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_PipeSetTxWriteOffset(OsDrv* Drv, DtDrvObject Pipe, uint32_t Offset)
{
    return SetOffset(Drv, DT_PIPE_CMD_SET_TX_WRITE_OFFSET, Pipe, Offset);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeGetTxReadOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_PipeGetTxReadOffset(OsDrv* Drv, DtDrvObject Pipe, uint32_t* Offset)
{
    return GetOffset(Drv, DT_PIPE_CMD_GET_TX_READ_OFFSET, Pipe, Offset);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeSetIpFilter -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_PipeSetIpFilter(OsDrv* Drv, DtDrvObject Pipe,
                                      const DtIpFilter* Filter)
{
    if (Drv == NULL || Filter == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlPipeCmdSetIpFilterInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_PIPE_CMD_SET_IPFILTER, Pipe);
    memcpy(In.m_DstIp, Filter->DstIp, sizeof(In.m_DstIp));
    memcpy(In.m_SrcIp, Filter->SrcIp, sizeof(In.m_SrcIp));
    for (int i = 0; i < 3; i++)
    {
        In.m_DstPort[i] = Filter->DstPort[i];
        In.m_SrcPort[i] = Filter->SrcPort[i];
    }
    In.m_VlanId[0] = Filter->VlanId[0];
    In.m_VlanId[1] = Filter->VlanId[1];
    In.m_Flags = (Int)Filter->Flags;
    return DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_PIPE_CMD), &In, sizeof(In), NULL, 0);
}
