// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtAvInput.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Device layer: video standard detection on an input port - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <string.h>

// CDtapiLite includes
#include "DtAvInput.h"        // Interface being implemented.
#include "DtDrvAbi.h"         // DT_FWSTATUS_ values and DT_FUNC_TYPE_SDIRX.
#include "DtFunc.h"           // The port's ASI/SDI receiver API function.
#include "Video/DtSmpte352.h" // Link number and aspect ratio from the VPID.
#include "Video/DtVidStd.h"   // Deducing the standard.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Attach +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvInputAttach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtAvInputAttach(DtAvInput* Input, DtDevice* Device, int Port)
{
    DtFuncInstance Func;
    const DtFuncPart* SdiRx;
    uint32_t Caps;
    unsigned int Result;

    memset(Input, 0, sizeof(*Input));

    if (Device == NULL || Device->Drv == NULL)
        return DTAPI_E_DEVICE;

    if (Device->Info.FirmwareStatus == DT_FWSTATUS_OBSOLETE)
        return DTAPI_E_OBSOLETE_FW;
    if (Device->Info.FirmwareStatus == DT_FWSTATUS_TAINTED)
        return DTAPI_E_TAINTED_FW;

    if (Port < 1 || Port > Device->NumPorts)
        return DTAPI_E_NO_SUCH_PORT;

    Caps = Device->PortCaps[Port - 1];
    if ((Caps & (DT_CAP_INPUT | DT_CAP_INTINPUT)) == 0)
        return DTAPI_E_NOT_SUPPORTED;
    if ((Caps & (DT_CAP_MATRIX2 | DT_CAP_SDIRX | DT_CAP_HDMI)) == 0)
        return DTAPI_E_NOT_SUPPORTED;

    // A DtPcie card goes through the Matrix API's function, and only with its capability.
    if ((Caps & DT_CAP_MATRIX2) == 0)
        return DTAPI_E_NOT_SUPPORTED;

    // AvInputStatusProxy::Init makes the proxies of the ASI/SDI receiver function with
    // the empty role. DtPalSDIRX then takes the SDI receiver with the empty role from
    // them, which DTAPI only does when it detects.
    Result = DtFuncFind(Device->Drv, Port - 1, "AF_ASISDIRX", "", &Func);
    if (Result != DTAPI_OK)
        return Result;
    SdiRx = DtFuncGet(&Func, true, DT_FUNC_TYPE_SDIRX, "");
    if (SdiRx != NULL)
    {
        Input->Device = Device;
        Input->PortIndex = Port - 1;
        Input->Caps = Caps;
        Input->SdiRxUuid = SdiRx->Uuid;
    }
    DtFuncRelease(&Func);
    return SdiRx != NULL ? DTAPI_OK : DTAPI_E_NOT_FOUND;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Detect +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvInputSetUnknown -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtAvInputSetUnknown(DtDetVidStd* Info)
{
    memset(Info, 0, sizeof(*Info));
    Info->VidStd = DTAPI_VIDSTD_UNKNOWN;
    Info->LinkStd = -1;
    Info->LinkNr = -1;
    Info->Vpid = 0;
    Info->Vpid2 = 0;
    Info->AspectRatio = DT_AR_UNKNOWN;
    Info->OriginalVidStd = DTAPI_VIDSTD_UNKNOWN;
    Info->OriginalLinkStd = -1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvInputDetectVidStd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Deduction gets no second VPID, so Vpid2 stays 0, and the link number and aspect ratio
// come from the VPID when there is one. A 4K standard on one link, 6G or 12G, that the
// port scales to 3G is reported as the 1080p standard that link carries.
//
unsigned int DtAvInputDetectVidStd(const DtAvInput* Input, DtDetVidStd* Info)
{
    OsDrv* Drv = Input->Device->Drv;
    DtSdiRxStatus Status;
    DtVidStdProps Props;
    bool Scale = false;
    unsigned int Result;

    DtAvInputSetUnknown(Info);

    if ((Input->Caps & DT_CAP_SCALE_12GTO3G) != 0)
    {
        DtIoConfig Config;

        memset(&Config, 0, sizeof(Config));
        Config.Port = Input->PortIndex + 1;
        Config.Group = DTAPI_IOCONFIG_IODOWNSCALE;
        Result = DtDrvGetIoConfig(Drv, &Config);
        if (Result != DTAPI_OK)
            return Result;
        Scale = Config.Value == DTAPI_IOCONFIG_SCALE_12GTO3G;
    }

    Result =
        DtFuncCheckDriverVersion(&Input->Device->DriverVersion, true, DT_FUNC_TYPE_SDIRX);
    if (Result != DTAPI_OK)
        return Result;

    Result = DtDrvSdiRxGetStatus(Drv, Input->SdiRxUuid, Input->PortIndex, &Status);
    if (Result != DTAPI_OK)
        return Result;

    if (!Status.Valid || !Status.SdiLock)
        return DTAPI_OK;

    DtVidStdPropsDeduce(&Props, Status.NumLinesF1, Status.NumLinesF2, Status.NumSymsHanc,
                        Status.NumSymsVidVanc, Status.FrameRate, Status.IsLevelB,
                        Status.PayloadId, Status.SdiRate);
    if (Props.VidStd == DTAPI_VIDSTD_UNKNOWN)
        return DTAPI_OK;

    Info->VidStd = Props.VidStd;
    Info->OriginalVidStd = Props.VidStd;
    Info->LinkStd = Props.LinkStd;
    Info->OriginalLinkStd = Props.LinkStd;
    Info->Vpid = Status.PayloadId;

    if (Info->Vpid != 0)
    {
        Info->LinkNr = DtSmpte352LinkNumber(Info->Vpid) + 1;
        Info->AspectRatio = DtSmpte352Is16x9(Info->Vpid) ? DT_AR_16_9 : DT_AR_4_3;
    }

    if (Scale && DtVidStdIs4k(Props.VidStd) &&
        DtVidStdNumPhysicalLinks(Props.LinkStd) == 1)
    {
        Info->VidStd = Props.Frame.VidStd;
        Info->LinkStd = -1;
    }
    return DTAPI_OK;
}
