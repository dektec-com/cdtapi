// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtAvPort.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - What the receive and transmit FIFOs share: the port, its network and pipes
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Device/DtFunc.h" // The network function.
#include "DtAvError.h"     // Failure texts.
#include "DtAvPort.h"      // Interface being implemented.
#include "DtPcieAbi.h"     // Pipe types, PHY speeds and firmware statuses.
#include "Net/DtNet.h"     // The operating system's network.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Port +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPort_Attach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The device must be attached, and the port's network function is found through AF_NW.
// No exclusive access is taken: a receive and a transmit FIFO share the port.
//
DtapiResult DtAvPort_Attach(DtAvPort* Port, const DtDevice* Device, int PortIndex,
                            HwOrSwPipe Preference, const char* Where)
{
    memset(Port, 0, sizeof(*Port));
    if (Device == NULL || Device->Drv == NULL)
        return DtAvError_Set(DTAPI_E_DEVICE, Where, "Device object must be attached");
    if (PortIndex < 0 || PortIndex >= Device->NumPublicPorts)
        return DtAvError_Set(DTAPI_E_NO_SUCH_PORT, Where, "The device has no such port");
    if ((Device->PortCaps[PortIndex] & DT_CAP_AVFIFO) == 0)
        return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where, "The port has no A/V FIFO");
    if ((int)Preference < (int)HwOrSwPipe_Auto ||
        (int)Preference > (int)HwOrSwPipe_PreferHwPipe)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "Unknown pipe preference");

    DtapiResult Result = DtDevice_AttachToIndex(&Port->Device, Device->DriverIndex, true,
                                                Device->Info.Serial);
    if (Result != DTAPI_OK)
        return DtAvError_Set(Result, Where, "Opening the device failed");

    DtFuncInstance NwFunction;
    DtVec_Init(&NwFunction.Objects, sizeof(DtFuncObject));
    Result = DtFunc_Find(Port->Device.Drv, PortIndex, "AF_NW", "", &NwFunction);
    const DtFuncObject* Object =
        Result == DTAPI_OK ? DtFunc_FindObject(&NwFunction, true, DT_FUNC_TYPE_NW, "")
                           : NULL;
    if (Result == DTAPI_OK && Object == NULL)
        Result = DTAPI_E_NOT_FOUND;
    if (Result == DTAPI_OK)
        Port->Nw = Object->Object;
    DtFunc_Release(&NwFunction);
    if (Result != DTAPI_OK)
    {
        DtDevice_Release(&Port->Device);
        return DtAvError_Set(Result, Where, "Network subsystem error");
    }
    Port->PortIndex = PortIndex;
    Port->PipePreference = Preference;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPort_Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtAvPort_Detach(DtAvPort* Port)
{
    if (Port->Device.Drv != NULL)
        DtDevice_Release(&Port->Device);
    memset(Port, 0, sizeof(*Port));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPort_CheckNetwork -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The link, the MAC address and the operating system's interface, with a failure text
// for each result.
//
DtapiResult DtAvPort_CheckNetwork(DtAvPort* Port, const AvFifo_IpPars* Pars,
                                  const char* Where)
{
    int Speed = 0;
    OsDrv* Drv = Port->Device.Drv;
    DtapiResult Result = DtPcieCmd_NwGetPhySpeed(Drv, Port->Nw, &Speed);
    if (Result != DTAPI_OK)
        return DtAvError_Set(Result, Where,
                             "Failed to get the network operational status");
    if (Speed == DT_PHY_SPEED_NO_LINK)
        return DtAvError_Set(DTAPI_E_NO_LINK, Where, "Network cable is disconnected");
    Result = DtPcieCmd_NwGetMacAddress(Drv, Port->Nw, Port->Mac);
    if (Result != DTAPI_OK)
        return DtAvError_Set(Result, Where, "Retrieving the MAC address failed");

    bool IpV6 = Pars->IpVersion == IpProtocolVersion_IPv6;
    Result = DtNet_CheckOperational(Port->Mac, Pars->Vlan.Id, !IpV6, IpV6);
    switch (Result)
    {
    case DTAPI_OK:
        return DTAPI_OK;
    case DTAPI_E_VLAN_NOT_FOUND:
        return DtAvError_Set(Result, Where,
                             "Network card for the selected VLAN not operational");
    case DTAPI_E_NW_DRIVER:
        return DtAvError_Set(Result, Where, "Network card not operational");
    case DTAPI_E_NO_ADAPTER_IP_ADDR:
        return DtAvError_Set(Result, Where, "Network card does not have an IP address");
    case DTAPI_E_BIND:
        return DtAvError_Set(Result, Where, "Network card is not yet ready to operate");
    case DTAPI_E_DISABLED:
        return DtAvError_Set(Result, Where, "Network link is down");
    default:
        return DtAvError_Set(Result, Where, "Network card not operational");
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPort_OpenPipe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtAvPort_OpenPipe(DtAvPort* Port, DtAvPipe* Pipe, bool IsRx,
                              bool HardwareIfAuto, const char* Where)
{
    int HwPipeType = IsRx ? DT_PIPE_RX_RT_HWP : DT_PIPE_TX_RT_HWP;
    int SwPipeType = IsRx ? DT_PIPE_RX_RT_SWP : DT_PIPE_TX_RT_SWP;
    int Type = SwPipeType;
    int Fallback = -1;

    switch (Port->PipePreference)
    {
    case HwOrSwPipe_ForceHwPipe:
        Type = HwPipeType;
        break;
    case HwOrSwPipe_PreferHwPipe:
        Type = HwPipeType;
        Fallback = SwPipeType;
        break;
    case HwOrSwPipe_Auto:
        if (HardwareIfAuto)
        {
            Type = HwPipeType;
            Fallback = SwPipeType;
        }
        break;
    default:
        break;
    }
    DtapiResult Result = DtAvPipe_Open(Pipe, Port->Device.Drv, Port->Nw, Type, Fallback);
    if (Result == DTAPI_E_IN_USE && Fallback == -1 && Type == HwPipeType)
        return DtAvError_Set(DTAPI_E_OUT_OF_RESOURCES, Where,
                             "The requested hardware pipe is not available");
    if (Result != DTAPI_OK)
        return DtAvError_Set(Result, Where, "Attaching to a pipe failed");
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPort_UsesHwPipe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtAvPort_UsesHwPipe(const DtAvPort* Port, bool Started, const DtAvPipe* Pipe,
                                int* UsesHwPipe, const char* Where)
{
    if (Started)
        *UsesHwPipe = DtAvPipe_IsHardware(Pipe) ? 1 : 0;
    else if (Port->PipePreference == HwOrSwPipe_ForceHwPipe)
        *UsesHwPipe = 1;
    else if (Port->PipePreference == HwOrSwPipe_UseSwPipe)
        *UsesHwPipe = 0;
    else
        return DtAvError_Set(DTAPI_E_NOT_STARTED, Where,
                             "The FIFO must be started before the pipe type is known");
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= IP parameters +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvIpPars_Copy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The checks the IP parameters must pass before a FIFO keeps them.
//
DtapiResult DtAvIpPars_Copy(DtAvIpPars* Copy, const AvFifo_IpPars* Pars,
                            const char* Where)
{
    if (Pars->IpVersion != IpProtocolVersion_IPv4 &&
        Pars->IpVersion != IpProtocolVersion_IPv6)
    {
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "Invalid IpVersion");
    }
    if (Pars->Port < 0 || Pars->Port > 65535)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "Invalid Port. Range: 0..65535");
    if (Pars->NSrcFlt < 0 || Pars->NSrcFlt > 3 ||
        (Pars->NSrcFlt > 0 && Pars->SrcFlt == NULL))
    {
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where,
                             "The maximum number of source filters is 3");
    }
    size_t Length = Pars->IpVersion == IpProtocolVersion_IPv6 ? 16 : 4;
    for (int i = 1; i < Pars->NSrcFlt; i++)
    {
        if (memcmp(Pars->SrcFlt[i].IpAddr, Pars->SrcFlt[0].IpAddr, Length) != 0)
            return DtAvError_Set(DTAPI_E_INVALID_ARG, Where,
                                 "All source filter addresses must be identical");
    }

    memset(Copy, 0, sizeof(*Copy));
    Copy->Pars = *Pars;
    for (int i = 0; i < Pars->NSrcFlt; i++)
        Copy->Sources[i] = Pars->SrcFlt[i];
    Copy->Pars.SrcFlt = Pars->NSrcFlt > 0 ? Copy->Sources : NULL;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvIpPars_IsIpV6 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtAvIpPars_IsIpV6(const DtAvIpPars* Ip)
{
    return Ip->Pars.IpVersion == IpProtocolVersion_IPv6;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvIpPars_CopySources -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtAvIpPars_CopySources(const DtAvIpPars* Ip, uint8_t Sources[3 * 16])
{
    memset(Sources, 0, 3 * 16);
    for (int i = 0; i < Ip->Pars.NSrcFlt; i++)
        memcpy(Sources + 16 * i, Ip->Sources[i].IpAddr, 16);
    return Ip->Pars.NSrcFlt;
}
