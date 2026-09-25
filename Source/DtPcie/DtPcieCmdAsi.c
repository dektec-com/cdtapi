// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtPcieCmdAsi.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - DtPcie driver commands: the ASI blocks and CDMAC's receive direction
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <string.h>

// CDTAPI includes
#include "DtPcieAbi.h"      // Vendored driver structures and IOCTL codes.
#include "DtPcieCmd.h"      // Interface being implemented.
#include "DtPcieCmdIssue.h" // Issuing commands.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Every setting of these blocks is the header and one Int, and every reading one Int.
// The requests are laid out once here; the assertions hold each command's structure to
// the same bytes.
//

typedef struct IntInput
{
    DtIoctlInputDataHdr m_CmdHdr;
    Int m_Value;
} IntInput;

#define SAME_AS_INT_INPUT(Type, Field)                                                   \
    _Static_assert(sizeof(IntInput) == sizeof(Type) &&                                   \
                       offsetof(IntInput, m_Value) == offsetof(Type, Field),             \
                   #Type " must be the header and one Int")
SAME_AS_INT_INPUT(DtIoctlAsiRxCmdSetOpModeInput, m_OpMode);
SAME_AS_INT_INPUT(DtIoctlAsiRxCmdSetPckModeInput, m_PckMode);
SAME_AS_INT_INPUT(DtIoctlAsiRxCmdSetPolarityCtrlInput, m_PolarityCtrl);
SAME_AS_INT_INPUT(DtIoctlAsiRxCmdSetSyncModeInput, m_SyncMode);
SAME_AS_INT_INPUT(DtIoctlAsiTxGCmdSetOpModeInput, m_OpMode);
SAME_AS_INT_INPUT(DtIoctlAsiTxGCmdSetAsiPolarityInput, m_AsiPolarity);
SAME_AS_INT_INPUT(DtIoctlAsiTxSerCmdSetOpModeInput, m_OpMode);
SAME_AS_INT_INPUT(DtIoctlCDmaCCmdSetRxRdOffsetInput, m_RxReadOffset);

#define ONE_INT_OUTPUT(Type)                                                             \
    _Static_assert(sizeof(Type) == sizeof(Int), #Type " must be one Int")
ONE_INT_OUTPUT(DtIoctlAsiRxCmdGetOpStatusOutput);
ONE_INT_OUTPUT(DtIoctlAsiRxCmdGetPckModeOutput);
ONE_INT_OUTPUT(DtIoctlAsiRxCmdGetPolarityCtrlOutput);
ONE_INT_OUTPUT(DtIoctlAsiRxCmdGetSyncModeOutput);
ONE_INT_OUTPUT(DtIoctlAsiRxCmdGetTsBitrateOutput);
ONE_INT_OUTPUT(DtIoctlAsiRxCmdGetViolCountOutput);
ONE_INT_OUTPUT(DtIoctlAsiTxGCmdGetOpModeOutput);
ONE_INT_OUTPUT(DtIoctlAsiTxGCmdGetAsiPolarityOutput);
ONE_INT_OUTPUT(DtIoctlAsiTxSerCmdGetOpModeOutput);
ONE_INT_OUTPUT(DtIoctlCDmaCCmdGetRxWrOffsetOutput);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetInt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult SetInt(OsDrv* Drv, uint32_t Code, int Cmd, DtDrvObject Object,
                          Int Value)
{
    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    IntInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, Cmd, Object);
    In.m_Value = Value;
    return DtPcieCmd_Issue(Drv, Code, &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetInt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads one Int into *Value, which is 0 after a failure.
//
static DtapiResult GetInt(OsDrv* Drv, uint32_t Code, int Cmd, DtDrvObject Object,
                          Int* Value)
{
    if (Value == NULL)
        return DTAPI_E_INVALID_ARG;
    *Value = 0;
    return DtPcieCmd_IssueHeaderOnly(Drv, Code, Cmd, Object, Value, sizeof(*Value));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetIntAmong -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads one Int that must be one of the Count values in Allowed; any other answer is
// DTAPI_E_DEV_DRIVER, and *Value is then 0.
//
static DtapiResult GetIntAmong(OsDrv* Drv, uint32_t Code, int Cmd, DtDrvObject Object,
                               int* Value, const int* Allowed, int Count)
{
    if (Value == NULL)
        return DTAPI_E_INVALID_ARG;

    Int Answer;
    DtapiResult Result = GetInt(Drv, Code, Cmd, Object, &Answer);
    *Value = 0;
    if (!DT_SUCCEEDED(Result))
        return Result;
    for (int i = 0; i < Count; i++)
    {
        if (Answer == Allowed[i])
        {
            *Value = Answer;
            return Result;
        }
    }
    return DTAPI_E_DEV_DRIVER;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsOneOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool IsOneOf(int Value, const int* Allowed, int Count)
{
    for (int i = 0; i < Count; i++)
    {
        if (Value == Allowed[i])
            return true;
    }
    return false;
}

#define COUNT(Array) ((int)(sizeof(Array) / sizeof((Array)[0])))

static const int AsiRxOpModes[] = {DT_FUNC_OPMODE_IDLE, DT_FUNC_OPMODE_RUN};
static const int AsiRxOpStatuses[] = {DT_FUNC_OPSTATUS_IDLE, DT_FUNC_OPSTATUS_RUN};
static const int PacketModes[] = {DT_ASIRX_PCKMODE_AUTO, DT_ASIRX_PCKMODE_RAW};
static const int PolarityCtrls[] = {DT_ASIRX_POLARITY_AUTO, DT_ASIRX_POLARITY_NORMAL,
                                    DT_ASIRX_POLARITY_INVERT};
static const int SyncModes[] = {DT_ASIRX_SYNCMODE_AUTO, DT_ASIRX_SYNCMODE_188,
                                DT_ASIRX_SYNCMODE_204};
static const int BlockOpModes[] = {DT_BLOCK_OPMODE_IDLE, DT_BLOCK_OPMODE_STANDBY,
                                   DT_BLOCK_OPMODE_RUN};
static const int AsiTxGPolarities[] = {DT_ASITXG_POL_NORMAL, DT_ASITXG_POL_INVERT};

#define ASIRX_IOCTL DT_IOCTL(DT_IOCTL_ASIRX_CMD)
#define ASITXG_IOCTL DT_IOCTL(DT_IOCTL_ASITXG_CMD)
#define ASITXSER_IOCTL DT_IOCTL(DT_IOCTL_ASITXSER_CMD)

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= ASIRX_IOCTL +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxSetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_AsiRxSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode)
{
    if (!IsOneOf(OpMode, AsiRxOpModes, COUNT(AsiRxOpModes)))
        return DTAPI_E_INVALID_ARG;
    return SetInt(Drv, ASIRX_IOCTL, DT_ASIRX_CMD_SET_OPERATIONAL_MODE, Object, OpMode);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxGetOpStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_AsiRxGetOpStatus(OsDrv* Drv, DtDrvObject Object, int* OpStatus)
{
    return GetIntAmong(Drv, ASIRX_IOCTL, DT_ASIRX_CMD_GET_OPERATIONAL_STATUS, Object,
                       OpStatus, AsiRxOpStatuses, COUNT(AsiRxOpStatuses));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxSetPacketMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_AsiRxSetPacketMode(OsDrv* Drv, DtDrvObject Object, int Mode)
{
    if (!IsOneOf(Mode, PacketModes, COUNT(PacketModes)))
        return DTAPI_E_INVALID_ARG;
    return SetInt(Drv, ASIRX_IOCTL, DT_ASIRX_CMD_SET_PACKET_MODE, Object, Mode);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxGetPacketMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_AsiRxGetPacketMode(OsDrv* Drv, DtDrvObject Object, int* Mode)
{
    return GetIntAmong(Drv, ASIRX_IOCTL, DT_ASIRX_CMD_GET_PACKET_MODE, Object, Mode,
                       PacketModes, COUNT(PacketModes));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxSetPolarityCtrl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_AsiRxSetPolarityCtrl(OsDrv* Drv, DtDrvObject Object, int Polarity)
{
    if (!IsOneOf(Polarity, PolarityCtrls, COUNT(PolarityCtrls)))
        return DTAPI_E_INVALID_ARG;
    return SetInt(Drv, ASIRX_IOCTL, DT_ASIRX_CMD_SET_POLARITY_CTRL, Object, Polarity);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxGetPolarityCtrl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_AsiRxGetPolarityCtrl(OsDrv* Drv, DtDrvObject Object, int* Polarity)
{
    return GetIntAmong(Drv, ASIRX_IOCTL, DT_ASIRX_CMD_GET_POLARITY_CTRL, Object, Polarity,
                       PolarityCtrls, COUNT(PolarityCtrls));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxSetSyncMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_AsiRxSetSyncMode(OsDrv* Drv, DtDrvObject Object, int Mode)
{
    if (!IsOneOf(Mode, SyncModes, COUNT(SyncModes)))
        return DTAPI_E_INVALID_ARG;
    return SetInt(Drv, ASIRX_IOCTL, DT_ASIRX_CMD_SET_SYNC_MODE, Object, Mode);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxGetSyncMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_AsiRxGetSyncMode(OsDrv* Drv, DtDrvObject Object, int* Mode)
{
    return GetIntAmong(Drv, ASIRX_IOCTL, DT_ASIRX_CMD_GET_SYNC_MODE, Object, Mode,
                       SyncModes, COUNT(SyncModes));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxGetStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A packet size or polarity that is none of the driver's own values is the driver's
// fault. The status is cleared after a failure.
//
DtapiResult DtPcieCmd_AsiRxGetStatus(OsDrv* Drv, DtDrvObject Object,
                                     DtAsiRxStatus* Status)
{
    if (Status == NULL)
        return DTAPI_E_INVALID_ARG;
    memset(Status, 0, sizeof(*Status));

    DtIoctlAsiRxCmdGetStatusOutput Out;
    DtapiResult Result = DtPcieCmd_IssueHeaderOnly(
        Drv, ASIRX_IOCTL, DT_ASIRX_CMD_GET_STATUS, Object, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    static const int Sizes[] = {DT_ASIRX_PCKSIZE_UNKNOWN, DT_ASIRX_PCKSIZE_188,
                                DT_ASIRX_PCKSIZE_204};
    static const int Polarities[] = {DT_ASIRX_POLARITY_UNKNOWN, DT_ASIRX_POLARITY_NORMAL,
                                     DT_ASIRX_POLARITY_INVERT};
    if (!IsOneOf(Out.m_PacketSize, Sizes, COUNT(Sizes)) ||
        !IsOneOf(Out.m_AsiPolarity, Polarities, COUNT(Polarities)))
    {
        return DTAPI_E_DEV_DRIVER;
    }

    Status->PacketSize = Out.m_PacketSize;
    Status->CarrierDetect = Out.m_CarrierDetect != 0;
    Status->AsiLock = Out.m_AsiLock != 0;
    Status->Polarity = Out.m_AsiPolarity;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxGetTsBitrate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_AsiRxGetTsBitrate(OsDrv* Drv, DtDrvObject Object, int* Bitrate)
{
    return GetInt(Drv, ASIRX_IOCTL, DT_ASIRX_CMD_GET_TS_BITRATE, Object, Bitrate);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxGetViolCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_AsiRxGetViolCount(OsDrv* Drv, DtDrvObject Object, int* Count)
{
    return GetInt(Drv, ASIRX_IOCTL, DT_ASIRX_CMD_GET_VIOL_COUNT, Object, Count);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+= ASITXG_IOCTL and ASITXSER_IOCTL +=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiTxGSetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_AsiTxGSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode)
{
    if (!IsOneOf(OpMode, BlockOpModes, COUNT(BlockOpModes)))
        return DTAPI_E_INVALID_ARG;
    return SetInt(Drv, ASITXG_IOCTL, DT_ASITXG_CMD_SET_OPERATIONAL_MODE, Object, OpMode);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiTxGGetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_AsiTxGGetOpMode(OsDrv* Drv, DtDrvObject Object, int* OpMode)
{
    return GetIntAmong(Drv, ASITXG_IOCTL, DT_ASITXG_CMD_GET_OPERATIONAL_MODE, Object,
                       OpMode, BlockOpModes, COUNT(BlockOpModes));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiTxGSetPolarity -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_AsiTxGSetPolarity(OsDrv* Drv, DtDrvObject Object, int Polarity)
{
    if (!IsOneOf(Polarity, AsiTxGPolarities, COUNT(AsiTxGPolarities)))
        return DTAPI_E_INVALID_ARG;
    return SetInt(Drv, ASITXG_IOCTL, DT_ASITXG_CMD_SET_ASI_POLARITY, Object, Polarity);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiTxGGetPolarity -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_AsiTxGGetPolarity(OsDrv* Drv, DtDrvObject Object, int* Polarity)
{
    return GetIntAmong(Drv, ASITXG_IOCTL, DT_ASITXG_CMD_GET_ASI_POLARITY, Object,
                       Polarity, AsiTxGPolarities, COUNT(AsiTxGPolarities));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiTxGClearInputState -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_AsiTxGClearInputState(OsDrv* Drv, DtDrvObject Object)
{
    return DtPcieCmd_IssueHeaderOnly(Drv, ASITXG_IOCTL, DT_ASITXG_CMD_CLEAR_INPUT_STATE,
                                     Object, NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiTxSerSetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_AsiTxSerSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode)
{
    if (!IsOneOf(OpMode, BlockOpModes, COUNT(BlockOpModes)))
        return DTAPI_E_INVALID_ARG;
    return SetInt(Drv, ASITXSER_IOCTL, DT_ASITXSER_CMD_SET_OPERATIONAL_MODE, Object,
                  OpMode);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiTxSerGetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_AsiTxSerGetOpMode(OsDrv* Drv, DtDrvObject Object, int* OpMode)
{
    return GetIntAmong(Drv, ASITXSER_IOCTL, DT_ASITXSER_CMD_GET_OPERATIONAL_MODE, Object,
                       OpMode, BlockOpModes, COUNT(BlockOpModes));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= CDMAC, receiving +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_CdmacGetRxWriteOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_CdmacGetRxWriteOffset(OsDrv* Drv, DtDrvObject Object,
                                            uint32_t* Offset)
{
    if (Offset == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlCDmaCCmdGetRxWrOffsetOutput Out;
    memset(&Out, 0, sizeof(Out));
    DtapiResult Result = DtPcieCmd_IssueHeaderOnly(Drv, DT_IOCTL(DT_IOCTL_CDMAC_CMD),
                                                   DT_CDMAC_CMD_GET_RX_WRITE_OFFSET,
                                                   Object, &Out, sizeof(Out));
    *Offset = DT_SUCCEEDED(Result) ? Out.m_RxWriteOffset : 0;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_CdmacSetRxReadOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_CdmacSetRxReadOffset(OsDrv* Drv, DtDrvObject Object,
                                           uint32_t Offset)
{
    return SetInt(Drv, DT_IOCTL(DT_IOCTL_CDMAC_CMD), DT_CDMAC_CMD_SET_RX_READ_OFFSET,
                  Object, (Int)Offset);
}
