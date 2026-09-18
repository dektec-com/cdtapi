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
static DtapiResult SetOffset(OsDrv* Drv, int Cmd, int PipeUuid, int PortIndex,
                             uint32_t Offset)
{
    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    OffsetInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, Cmd, PipeUuid, PortIndex);
    In.m_Offset = Offset;
    return DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_PIPE_CMD), &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtapiResult GetOffset(OsDrv* Drv, int Cmd, int PipeUuid, int PortIndex,
                             uint32_t* Offset)
{
    if (Offset == NULL)
        return DTAPI_E_INVALID_ARG;

    UInt Out = 0;
    DtapiResult Result = DtPcieCmd_IssuePlain(Drv, DT_IOCTL(DT_IOCTL_PIPE_CMD), Cmd,
                                              PipeUuid, PortIndex, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *Offset = Out;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= EMAC +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_NwGetMacAddress -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_NwGetMacAddress(OsDrv* Drv, int Uuid, int PortIndex, uint8_t* Mac)
{
    if (Mac == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlEMACCmdGetMacAddressOutput Out;
    DtapiResult Result =
        DtPcieCmd_IssuePlain(Drv, DT_IOCTL(DT_IOCTL_EMAC_CMD), DT_EMAC_CMD_GET_MACADDRESS,
                             Uuid, PortIndex, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    memcpy(Mac, Out.m_Address, sizeof(Out.m_Address));
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_NwGetPhySpeed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_NwGetPhySpeed(OsDrv* Drv, int Uuid, int PortIndex, int* Speed)
{
    if (Speed == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlEMACCmdGetPhySpeedOutput Out;
    DtapiResult Result =
        DtPcieCmd_IssuePlain(Drv, DT_IOCTL(DT_IOCTL_EMAC_CMD), DT_EMAC_CMD_GET_PHY_SPEED,
                             Uuid, PortIndex, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *Speed = (int)Out.m_Speed;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= NW +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_NwOpenPipe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_NwOpenPipe(OsDrv* Drv, int Uuid, int PortIndex, int Type,
                                 int Fallback, int* PipeUuid)
{
    if (PipeUuid != NULL)
        *PipeUuid = 0;
    if (Drv == NULL || PipeUuid == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlNwCmdPipeOpenInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_NW_CMD_PIPE_OPEN, Uuid, PortIndex);
    In.m_PipeType = Type;
    In.m_PipeTypeFallback = Fallback;
    DtIoctlNwCmdPipeOpenOutput Out;
    memset(&Out, 0, sizeof(Out));
    DtapiResult Result = DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_NW_CMD), &In, sizeof(In),
                                         &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *PipeUuid = (int)Out.m_PipeUuid;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_NwClosePipe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// DtProxyNw::ClosePipe addresses the command to the pipe rather than to the function.
//
DtapiResult DtPcieCmd_NwClosePipe(OsDrv* Drv, int PipeUuid, int PortIndex)
{
    return DtPcieCmd_IssuePlain(Drv, DT_IOCTL(DT_IOCTL_NW_CMD), DT_NW_CMD_PIPE_CLOSE,
                                PipeUuid, PortIndex, NULL, 0);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= PIPE +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeGetProps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_PipeGetProps(OsDrv* Drv, int PipeUuid, int PortIndex,
                                   DtPipeProps* Props)
{
    if (Props != NULL)
        memset(Props, 0, sizeof(*Props));
    if (Props == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlPipeCmdGetPropertiesOutput Out;
    DtapiResult Result =
        DtPcieCmd_IssuePlain(Drv, DT_IOCTL(DT_IOCTL_PIPE_CMD), DT_PIPE_CMD_GET_PROPERTIES,
                             PipeUuid, PortIndex, &Out, sizeof(Out));
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
DtapiResult DtPcieCmd_PipeGetStatus(OsDrv* Drv, int PipeUuid, int PortIndex,
                                    DtPipeStatus* Status)
{
    if (Status != NULL)
        memset(Status, 0, sizeof(*Status));
    if (Status == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlPipeCmdGetStatusOutput Out;
    DtapiResult Result =
        DtPcieCmd_IssuePlain(Drv, DT_IOCTL(DT_IOCTL_PIPE_CMD), DT_PIPE_CMD_GET_STATUS,
                             PipeUuid, PortIndex, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    Status->OpStatus = Out.m_OpStatus;
    Status->StatusFlags = Out.m_StatusFlags;
    Status->ErrorFlags = Out.m_ErrorFlags;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeSetSharedBufferAs -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// DtPalPipe_Nw::AllocateSharedBuffer. The answer must hold at least the command's fixed
// output structure, as for CDMAC's buffer.
//
DtapiResult DtPcieCmd_PipeSetSharedBufferAs(OsDrv* Drv, int PipeUuid, int PortIndex,
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
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_PIPE_CMD_SET_SHARED_BUFFER, PipeUuid,
                         PortIndex);
    In.m_BufferSize = (Int)Buf->Size;
    DtIoctlPipeCmdSetSharedBufferOutput Fixed;
    memset(&Fixed, 0, sizeof(Fixed));
    OsDmaHandOff HandOff;
    OsDmaBuffer_DescribeHandOffAs(BufferIsOutput, Buf, &Fixed, sizeof(Fixed), &HandOff);
    In.m_BufferAddr = HandOff.BufferAddr;

    size_t Returned = HandOff.OutSize;
    int Outcome = OsDrv_IoCtl(Drv, DT_IOCTL(DT_IOCTL_PIPE_CMD), &In, sizeof(In),
                              HandOff.Out, &Returned, &Status);
    if (Outcome != OS_IOCTL_OK)
        return DtPcieStatus_OutcomeToResult(Outcome, Status);
    if (Returned < sizeof(Fixed))
        return DTAPI_E_DEV_DRIVER;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeSetSharedBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_PipeSetSharedBuffer(OsDrv* Drv, int PipeUuid, int PortIndex,
                                          const OsDmaBuffer* Buf)
{
#if defined(_WIN32) || defined(_WIN64)
    return DtPcieCmd_PipeSetSharedBufferAs(Drv, PipeUuid, PortIndex, Buf, true);
#else
    return DtPcieCmd_PipeSetSharedBufferAs(Drv, PipeUuid, PortIndex, Buf, false);
#endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeReleaseSharedBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_PipeReleaseSharedBuffer(OsDrv* Drv, int PipeUuid, int PortIndex)
{
    return DtPcieCmd_IssuePlain(Drv, DT_IOCTL(DT_IOCTL_PIPE_CMD),
                                DT_PIPE_CMD_RELEASE_SHARED_BUFFER, PipeUuid, PortIndex,
                                NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeFlush -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_PipeFlush(OsDrv* Drv, int PipeUuid, int PortIndex)
{
    return DtPcieCmd_IssuePlain(Drv, DT_IOCTL(DT_IOCTL_PIPE_CMD),
                                DT_PIPE_CMD_ISSUE_PIPE_FLUSH, PipeUuid, PortIndex, NULL,
                                0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeSetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// DtPalPipe_Nw::SetOperationalMode refuses a mode it cannot convert; so does this.
//
DtapiResult DtPcieCmd_PipeSetOpMode(OsDrv* Drv, int PipeUuid, int PortIndex, int OpMode)
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
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_PIPE_CMD_SET_OPERATIONAL_MODE, PipeUuid,
                         PortIndex);
    In.m_OpMode = OpMode;
    return DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_PIPE_CMD), &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeSetRxReadOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_PipeSetRxReadOffset(OsDrv* Drv, int PipeUuid, int PortIndex,
                                          uint32_t Offset)
{
    return SetOffset(Drv, DT_PIPE_CMD_SET_RX_READ_OFFSET, PipeUuid, PortIndex, Offset);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeGetRxWriteOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_PipeGetRxWriteOffset(OsDrv* Drv, int PipeUuid, int PortIndex,
                                           uint32_t* Offset)
{
    return GetOffset(Drv, DT_PIPE_CMD_GET_RX_WRITE_OFFSET, PipeUuid, PortIndex, Offset);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeSetTxWriteOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_PipeSetTxWriteOffset(OsDrv* Drv, int PipeUuid, int PortIndex,
                                           uint32_t Offset)
{
    return SetOffset(Drv, DT_PIPE_CMD_SET_TX_WRITE_OFFSET, PipeUuid, PortIndex, Offset);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeGetTxReadOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_PipeGetTxReadOffset(OsDrv* Drv, int PipeUuid, int PortIndex,
                                          uint32_t* Offset)
{
    return GetOffset(Drv, DT_PIPE_CMD_GET_TX_READ_OFFSET, PipeUuid, PortIndex, Offset);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_PipeSetIpFilter -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_PipeSetIpFilter(OsDrv* Drv, int PipeUuid, int PortIndex,
                                      const DtIpFilter* Filter)
{
    if (Drv == NULL || Filter == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlPipeCmdSetIpFilterInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_PIPE_CMD_SET_IPFILTER, PipeUuid, PortIndex);
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
