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
static DtapiResult SetInt(OsDrv* Drv, uint32_t Code, int Cmd, int Uuid, int PortIndex,
                          Int Value)
{
    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    IntInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, Cmd, Uuid, PortIndex);
    In.m_Value = Value;
    return DtPcieCmd_Issue(Drv, Code, &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetInt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads one Int into *Value, which is 0 after a failure.
//
static DtapiResult GetInt(OsDrv* Drv, uint32_t Code, int Cmd, int Uuid, int PortIndex,
                          Int* Value)
{
    if (Value == NULL)
        return DTAPI_E_INVALID_ARG;
    *Value = 0;
    return DtPcieCmd_IssuePlain(Drv, Code, Cmd, Uuid, PortIndex, Value, sizeof(*Value));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetOneOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads one Int that must be one of the Count values in Allowed, as DTAPI's proxy
// converts it; any other answer is DTAPI_E_DEV_DRIVER, and *Value is then 0.
//
static DtapiResult GetOneOf(OsDrv* Drv, uint32_t Code, int Cmd, int Uuid, int PortIndex,
                            int* Value, const int* Allowed, int Count)
{
    if (Value == NULL)
        return DTAPI_E_INVALID_ARG;

    Int Got;
    DtapiResult Result = GetInt(Drv, Code, Cmd, Uuid, PortIndex, &Got);
    *Value = 0;
    if (!DT_SUCCEEDED(Result))
        return Result;
    for (int i = 0; i < Count; i++)
    {
        if (Got == Allowed[i])
        {
            *Value = Got;
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

static const int AsiRxModes[] = {DT_FUNC_OPMODE_IDLE, DT_FUNC_OPMODE_RUN};
static const int AsiRxOpStatuses[] = {DT_FUNC_OPSTATUS_IDLE, DT_FUNC_OPSTATUS_RUN};
static const int PacketModes[] = {DT_ASIRX_PCKMODE_AUTO, DT_ASIRX_PCKMODE_RAW};
static const int PolarityCtrls[] = {DT_ASIRX_POLARITY_AUTO, DT_ASIRX_POLARITY_NORMAL,
                                    DT_ASIRX_POLARITY_INVERT};
static const int SyncModes[] = {DT_ASIRX_SYNCMODE_AUTO, DT_ASIRX_SYNCMODE_188,
                                DT_ASIRX_SYNCMODE_204};
static const int BlockModes[] = {DT_BLOCK_OPMODE_IDLE, DT_BLOCK_OPMODE_STANDBY,
                                 DT_BLOCK_OPMODE_RUN};
static const int TxPolarities[] = {DT_ASITXG_POL_NORMAL, DT_ASITXG_POL_INVERT};

#define ASIRX DT_IOCTL(DT_IOCTL_ASIRX_CMD)
#define ASITXG DT_IOCTL(DT_IOCTL_ASITXG_CMD)
#define ASITXSER DT_IOCTL(DT_IOCTL_ASITXSER_CMD)

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= ASIRX +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxSetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_AsiRxSetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int OpMode)
{
    if (!IsOneOf(OpMode, AsiRxModes, COUNT(AsiRxModes)))
        return DTAPI_E_INVALID_ARG;
    return SetInt(Drv, ASIRX, DT_ASIRX_CMD_SET_OPERATIONAL_MODE, Uuid, PortIndex, OpMode);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxGetOpStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_AsiRxGetOpStatus(OsDrv* Drv, int Uuid, int PortIndex, int* OpStatus)
{
    return GetOneOf(Drv, ASIRX, DT_ASIRX_CMD_GET_OPERATIONAL_STATUS, Uuid, PortIndex,
                    OpStatus, AsiRxOpStatuses, COUNT(AsiRxOpStatuses));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxSetPacketMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_AsiRxSetPacketMode(OsDrv* Drv, int Uuid, int PortIndex, int Mode)
{
    if (!IsOneOf(Mode, PacketModes, COUNT(PacketModes)))
        return DTAPI_E_INVALID_ARG;
    return SetInt(Drv, ASIRX, DT_ASIRX_CMD_SET_PACKET_MODE, Uuid, PortIndex, Mode);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxGetPacketMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_AsiRxGetPacketMode(OsDrv* Drv, int Uuid, int PortIndex, int* Mode)
{
    return GetOneOf(Drv, ASIRX, DT_ASIRX_CMD_GET_PACKET_MODE, Uuid, PortIndex, Mode,
                    PacketModes, COUNT(PacketModes));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxSetPolarityCtrl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_AsiRxSetPolarityCtrl(OsDrv* Drv, int Uuid, int PortIndex,
                                           int Polarity)
{
    if (!IsOneOf(Polarity, PolarityCtrls, COUNT(PolarityCtrls)))
        return DTAPI_E_INVALID_ARG;
    return SetInt(Drv, ASIRX, DT_ASIRX_CMD_SET_POLARITY_CTRL, Uuid, PortIndex, Polarity);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxGetPolarityCtrl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_AsiRxGetPolarityCtrl(OsDrv* Drv, int Uuid, int PortIndex,
                                           int* Polarity)
{
    return GetOneOf(Drv, ASIRX, DT_ASIRX_CMD_GET_POLARITY_CTRL, Uuid, PortIndex, Polarity,
                    PolarityCtrls, COUNT(PolarityCtrls));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxSetSyncMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_AsiRxSetSyncMode(OsDrv* Drv, int Uuid, int PortIndex, int Mode)
{
    if (!IsOneOf(Mode, SyncModes, COUNT(SyncModes)))
        return DTAPI_E_INVALID_ARG;
    return SetInt(Drv, ASIRX, DT_ASIRX_CMD_SET_SYNC_MODE, Uuid, PortIndex, Mode);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxGetSyncMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_AsiRxGetSyncMode(OsDrv* Drv, int Uuid, int PortIndex, int* Mode)
{
    return GetOneOf(Drv, ASIRX, DT_ASIRX_CMD_GET_SYNC_MODE, Uuid, PortIndex, Mode,
                    SyncModes, COUNT(SyncModes));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxGetStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DtProxyASIRX::GetStatus: a packet size or polarity the proxy does not know is the
// driver's fault. The status is cleared after a failure.
//
DtapiResult DtPcieCmd_AsiRxGetStatus(OsDrv* Drv, int Uuid, int PortIndex,
                                     DtAsiRxStatus* Status)
{
    if (Status == NULL)
        return DTAPI_E_INVALID_ARG;
    memset(Status, 0, sizeof(*Status));

    DtIoctlAsiRxCmdGetStatusOutput Out;
    DtapiResult Result = DtPcieCmd_IssuePlain(Drv, ASIRX, DT_ASIRX_CMD_GET_STATUS, Uuid,
                                              PortIndex, &Out, sizeof(Out));
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
DtapiResult DtPcieCmd_AsiRxGetTsBitrate(OsDrv* Drv, int Uuid, int PortIndex, int* Bitrate)
{
    return GetInt(Drv, ASIRX, DT_ASIRX_CMD_GET_TS_BITRATE, Uuid, PortIndex, Bitrate);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiRxGetViolCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_AsiRxGetViolCount(OsDrv* Drv, int Uuid, int PortIndex, int* Count)
{
    return GetInt(Drv, ASIRX, DT_ASIRX_CMD_GET_VIOL_COUNT, Uuid, PortIndex, Count);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= ASITXG and ASITXSER +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiTxGSetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_AsiTxGSetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int OpMode)
{
    if (!IsOneOf(OpMode, BlockModes, COUNT(BlockModes)))
        return DTAPI_E_INVALID_ARG;
    return SetInt(Drv, ASITXG, DT_ASITXG_CMD_SET_OPERATIONAL_MODE, Uuid, PortIndex,
                  OpMode);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiTxGGetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_AsiTxGGetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int* OpMode)
{
    return GetOneOf(Drv, ASITXG, DT_ASITXG_CMD_GET_OPERATIONAL_MODE, Uuid, PortIndex,
                    OpMode, BlockModes, COUNT(BlockModes));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiTxGSetPolarity -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_AsiTxGSetPolarity(OsDrv* Drv, int Uuid, int PortIndex, int Polarity)
{
    if (!IsOneOf(Polarity, TxPolarities, COUNT(TxPolarities)))
        return DTAPI_E_INVALID_ARG;
    return SetInt(Drv, ASITXG, DT_ASITXG_CMD_SET_ASI_POLARITY, Uuid, PortIndex, Polarity);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiTxGGetPolarity -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_AsiTxGGetPolarity(OsDrv* Drv, int Uuid, int PortIndex,
                                        int* Polarity)
{
    return GetOneOf(Drv, ASITXG, DT_ASITXG_CMD_GET_ASI_POLARITY, Uuid, PortIndex,
                    Polarity, TxPolarities, COUNT(TxPolarities));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiTxGClearInputState -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_AsiTxGClearInputState(OsDrv* Drv, int Uuid, int PortIndex)
{
    return DtPcieCmd_IssuePlain(Drv, ASITXG, DT_ASITXG_CMD_CLEAR_INPUT_STATE, Uuid,
                                PortIndex, NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiTxSerSetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_AsiTxSerSetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int OpMode)
{
    if (!IsOneOf(OpMode, BlockModes, COUNT(BlockModes)))
        return DTAPI_E_INVALID_ARG;
    return SetInt(Drv, ASITXSER, DT_ASITXSER_CMD_SET_OPERATIONAL_MODE, Uuid, PortIndex,
                  OpMode);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_AsiTxSerGetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_AsiTxSerGetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int* OpMode)
{
    return GetOneOf(Drv, ASITXSER, DT_ASITXSER_CMD_GET_OPERATIONAL_MODE, Uuid, PortIndex,
                    OpMode, BlockModes, COUNT(BlockModes));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= CDMAC, receiving +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_CdmacGetRxWriteOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_CdmacGetRxWriteOffset(OsDrv* Drv, int Uuid, int PortIndex,
                                            uint32_t* Offset)
{
    if (Offset == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlCDmaCCmdGetRxWrOffsetOutput Out;
    memset(&Out, 0, sizeof(Out));
    DtapiResult Result = DtPcieCmd_IssuePlain(Drv, DT_IOCTL(DT_IOCTL_CDMAC_CMD),
                                              DT_CDMAC_CMD_GET_RX_WRITE_OFFSET, Uuid,
                                              PortIndex, &Out, sizeof(Out));
    *Offset = DT_SUCCEEDED(Result) ? Out.m_RxWriteOffset : 0;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_CdmacSetRxReadOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_CdmacSetRxReadOffset(OsDrv* Drv, int Uuid, int PortIndex,
                                           uint32_t Offset)
{
    return SetInt(Drv, DT_IOCTL(DT_IOCTL_CDMAC_CMD), DT_CDMAC_CMD_SET_RX_READ_OFFSET,
                  Uuid, PortIndex, (Int)Offset);
}
