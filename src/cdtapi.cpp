// *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* cdtapi.cpp *#*#*#*#*#*#*#*#* (C) 2022-2024 DekTec
//

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include "cdtapi.h"

#define _NO_USING_NAMESPACE_DTAPI
#include "dtapi/DTAPI.h"

#include <chrono>
#include <thread>
#include <cstring>
#include <string>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Local utility functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToC -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtHwFuncDesc* ToC(Dtapi::DtHwFuncDesc* Desc)
{
    return (DtHwFuncDesc*)Desc;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToC -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtDevice* ToC(Dtapi::DtDevice* Device)
{
    return (DtDevice*)Device;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToC -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtOutpChannel* ToC(Dtapi::DtOutpChannel* OutpChannel)
{
    return (DtOutpChannel*)OutpChannel;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToC -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtInpChannel* ToC(Dtapi::DtInpChannel* InpChannel)
{
    return (DtInpChannel*)InpChannel;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToCpp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const Dtapi::DtHwFuncDesc* ToCpp(const DtHwFuncDesc* Desc)
{
    return (Dtapi::DtHwFuncDesc*)Desc;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToCpp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
Dtapi::DtHwFuncDesc* ToCpp(DtHwFuncDesc* Desc)
{
    return (Dtapi::DtHwFuncDesc*)Desc;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToCpp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
Dtapi::DtDevice* ToCpp(DtDevice* Device)
{
    return (Dtapi::DtDevice*)Device;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToCpp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const Dtapi::DtDevice* ToCpp(const DtDevice* Device)
{
    return (const Dtapi::DtDevice*)Device;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToCpp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
Dtapi::DtOutpChannel* ToCpp(DtOutpChannel* OutpChannel)
{
    return (Dtapi::DtOutpChannel*)OutpChannel;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToCpp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
Dtapi::DtInpChannel* ToCpp(DtInpChannel* InpChannel)
{
    return (Dtapi::DtInpChannel*)InpChannel;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ CDTAPI impelementation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

extern "C" {

// =+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Utility functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtapiHwFuncScan -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtapiHwFuncScan(int NumEntries, int* NumEntriesResult, DtHwFuncDesc* HwFuncs)
{
    if (!NumEntriesResult)
        return DTAPI_E_INVALID_ARG;
    
    std::vector<Dtapi::DtHwFuncDesc> CppHwFuncs(NumEntries);
    unsigned int Result{Dtapi::DtapiHwFuncScan((int)CppHwFuncs.size(), *NumEntriesResult, CppHwFuncs.data(), false, 0)};
    if (Result != DTAPI_OK)
        return Result;
    
    for (int i{0}; i < (int)CppHwFuncs.size(); i++)
    {
        Dtapi::DtHwFuncDesc& CppHwFunc{CppHwFuncs[i]};
        DtHwFuncDesc& HwFunc{HwFuncs[i]};
        memset(&HwFunc, 0, sizeof(DtHwFuncDesc));

        snprintf(HwFunc.DeviceName, MAX_DEVICE_NAME_SIZE, "%lld:%d",
                                          CppHwFunc.m_DvcDesc.m_Serial, CppHwFunc.m_Port);
        Result = DtapiDtHwFuncDesc2String(&CppHwFunc, DTAPI_HWF2STR_TYPE_AND_PORT2, HwFunc.Description, MAX_DEVICE_NAME_SIZE);
        if (Result != DTAPI_OK)
            return Result;
        HwFunc.SerialNumber = CppHwFunc.m_DvcDesc.m_Serial;
        HwFunc.Port = CppHwFunc.m_Port;
        if ((CppHwFunc.m_Flags & DTAPI_CAP_12GSDI) != 0 ||
            (CppHwFunc.m_Flags & DTAPI_CAP_3GSDI) != 0 ||
            (CppHwFunc.m_Flags & DTAPI_CAP_6GSDI) != 0 ||
            (CppHwFunc.m_Flags & DTAPI_CAP_HDSDI) != 0 ||
            (CppHwFunc.m_Flags & DTAPI_CAP_SDI) != 0)
            HwFunc.IsSdi = 1;

#ifdef ENABLE_AVFIFO
        if ((CppHwFunc.m_Flags & DTAPI_CAP_AVFIFO) != 0)
            HwFunc.IsAvFifo = 1;
#endif

        if ((CppHwFunc.m_Flags & DTAPI_CAP_INPUT) != 0)
            HwFunc.IsInput = 1;
        if ((CppHwFunc.m_Flags & DTAPI_CAP_OUTPUT) != 0)
            HwFunc.IsOutput = 1;
    }

    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ DtDevice implementation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtDevice* DtDevice_Alloc()
{
    return ToC(new (std::nothrow) Dtapi::DtDevice);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtDevice_Free(DtDevice* Device)
{
    delete ToCpp(Device);
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtDevice_Freep(DtDevice** Device)
{
    if (Device)
    {
        delete ToCpp(*Device);
        *Device = nullptr;
    }
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_AttachToSerial -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtDevice_AttachToSerial(DtDevice* Device, int64_t SerialNumber)
{
    if (Device)
        return ToCpp(Device)->AttachToSerial(SerialNumber);
    return DTAPI_E_INVALID_ARG;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_SetIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtDevice_SetIoConfig(DtDevice* Device, int Port, int Group, int Value, int SubValue)
{
    if (Device)
        return ToCpp(Device)->SetIoConfig(Port, Group, Value, SubValue);
    return DTAPI_E_INVALID_ARG;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtDevice_Detach(DtDevice* Device)
{
    if (Device)
        return ToCpp(Device)->Detach();
    return DTAPI_E_INVALID_ARG;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_SetToOutput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtDevice_SetToOutput(DtDevice* Device, int Port)
{
    if (Device)
        return DtDevice_SetIoConfig(Device, Port, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_OUTPUT);
    return DTAPI_E_INVALID_ARG;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_SetToInput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtDevice_SetToInput(DtDevice* Device, int Port)
{
    if (Device)
        return DtDevice_SetIoConfig(Device, Port, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT, DTAPI_IOCONFIG_INPUT);
    return DTAPI_E_INVALID_ARG;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_WaitForSignal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtDetVidStd DtDevice_WaitForSignal(DtDevice* Device, int Port)
{
    Dtapi::DtAvInputStatus Status;
    Status.AttachToPort(ToCpp(Device), Port);

    Dtapi::DtDetVidStd Standard{};
    while (true) {
        using namespace std::chrono_literals;
        Dtapi::DTAPI_RESULT Result{Status.DetectVidStd(Standard)};
        if (Result != DTAPI_OK || Standard.m_VidStd == DTAPI_VIDSTD_UNKNOWN)
            std::this_thread::sleep_for(5ms);
        else
            break;
    }

    DtDetVidStd CStandard{};
    CStandard.VidStd = Standard.m_VidStd;
    CStandard.LinkStd = Standard.m_LinkStd;
    CStandard.LinkNr = Standard.m_LinkNr;
    CStandard.Vpid = Standard.m_Vpid;
    CStandard.Vpid2 = Standard.m_Vpid2;
    CStandard.AspectRatio = (DtAspectRatio)Standard.m_AspectRatio;
    CStandard.OriginalVidStd = Standard.m_OriginalVidStd;
    CStandard.OriginalLinkStd = Standard.m_OriginalLinkStd;
    return CStandard;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_DetectVidStd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtDevice_DetectVidStd(DtDevice* Device, int Port, int* VidStd)
{
    if (!Device || !VidStd)
        return DTAPI_E_INVALID_ARG;

    Dtapi::DtAvInputStatus Status;
    Status.AttachToPort(ToCpp(Device), Port);

    Dtapi::DtDetVidStd Standard{};
    Dtapi::DTAPI_RESULT Result{Status.DetectVidStd(Standard)};
    *VidStd = int(Standard.m_VidStd);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_GetTimeOfDay -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtDevice_GetTimeOfDay(const DtDevice* Device, DtTimeOfDay* TimeOfDay)
{
    if (!Device || !TimeOfDay)
        return DTAPI_E_INVALID_ARG;
    
    Dtapi::DtTimeOfDay Tod;
    unsigned int Result{ToCpp(Device)->GetTimeOfDay(Tod)};
    TimeOfDay->Seconds = Tod.m_Seconds;
    TimeOfDay->Nanoseconds = Tod.m_Nanoseconds;
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+ DtInpChannel implementation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtInpChannel* DtInpChannel_Alloc()
{
    return ToC(new (std::nothrow) Dtapi::DtInpChannel);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtInpChannel_Free(DtInpChannel* InpChannel)
{
    delete ToCpp(InpChannel);
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtInpChannel_Freep(DtInpChannel** InpChannel)
{
    if (InpChannel)
    {
        delete ToCpp(*InpChannel);
        *InpChannel = nullptr;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_AttachToPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtInpChannel_AttachToPort(DtInpChannel* InpChannel, DtDevice* Device, int Port)
{
    if (!InpChannel)
        return DTAPI_E_INVALID_ARG;
    return ToCpp(InpChannel)->AttachToPort(ToCpp(Device), Port);
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_ClearFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtInpChannel_ClearFifo(DtInpChannel* InpChannel)
{
    if (!InpChannel)
        return DTAPI_E_INVALID_ARG;
    return ToCpp(InpChannel)->ClearFifo();
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_ClearFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtInpChannel_ClearFlags(DtInpChannel* InpChannel, int Latched)
{
    if (!InpChannel)
        return DTAPI_E_INVALID_ARG;
    return ToCpp(InpChannel)->ClearFlags(Latched);
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtInpChannel_Detach(DtInpChannel* InpChannel, int DetachMode)
{
    if (!InpChannel)
        return DTAPI_E_INVALID_ARG;
    return ToCpp(InpChannel)->Detach(DetachMode);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_DetectIoStd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtInpChannel_DetectIoStd(DtInpChannel* InpChannel, int* Value, int* SubValue)
{
    if (!InpChannel || !Value || !SubValue)
        return DTAPI_E_INVALID_ARG;
    return ToCpp(InpChannel)->DetectIoStd(*Value, *SubValue);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_GetFifoLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtInpChannel_GetFifoLoad(DtInpChannel* InpChannel, int* FifoLoad)
{
    if (!InpChannel || !FifoLoad)
        return DTAPI_E_INVALID_ARG;
    return ToCpp(InpChannel)->GetFifoLoad(*FifoLoad);
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_GetMaxFifoSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtInpChannel_GetMaxFifoSize(DtInpChannel* InpChannel, int* MaxFifoSize)
{
    if (!InpChannel || !MaxFifoSize)
        return DTAPI_E_INVALID_ARG;
    return ToCpp(InpChannel)->GetMaxFifoSize(*MaxFifoSize);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_GetFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtInpChannel_GetFlags(DtInpChannel* InpChannel, int* Flags, int* Latched)
{
    if (!InpChannel || !Flags || !Latched)
        return DTAPI_E_INVALID_ARG;
    return ToCpp(InpChannel)->GetFlags(*Flags, *Latched);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_SetIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtInpChannel_SetIoConfig(DtInpChannel* InpChannel, int Group, int Value, int SubValue)
{
    if (!InpChannel)
        return DTAPI_E_INVALID_ARG;
    return ToCpp(InpChannel)->SetIoConfig(Group, Value, SubValue);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_SetRxControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtInpChannel_SetRxControl(DtInpChannel* InpChannel, int RxControl)
{
    if (!InpChannel)
        return DTAPI_E_INVALID_ARG;
    return ToCpp(InpChannel)->SetRxControl(RxControl);
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_SetRxMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtInpChannel_SetRxMode(DtInpChannel* InpChannel, int RxMode)
{
    if (!InpChannel)
        return DTAPI_E_INVALID_ARG;
    return ToCpp(InpChannel)->SetRxMode(RxMode);
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_ReadFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtInpChannel_ReadFrame(DtInpChannel* InpChannel, char* FrameBuffer,
                                                              int* FrameSize, int TimeOut)
{
    if (!InpChannel || !FrameSize)
        return DTAPI_E_INVALID_ARG;
    // if ((((intptr_t)FrameBuffer) % 32) != 0) // Check for 32-bit alginment
    //     return DTAPI_E_INVALID_BUF;
    return ToCpp(InpChannel)->ReadFrame((unsigned int*)FrameBuffer, *FrameSize, TimeOut);
}

// =+=+=+=+=+=+=+=+=+=+=+=+=+=+ DtOutpChannel implementation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtOutpChannel* DtOutpChannel_Alloc()
{
    return ToC(new (std::nothrow) Dtapi::DtOutpChannel);
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtOutpChannel_Free(DtOutpChannel* OutpChannel)
{
    delete ToCpp(OutpChannel);
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtOutpChannel_Freep(DtOutpChannel** OutpChannel)
{
    if (OutpChannel)
    {
        delete ToCpp(*OutpChannel);
        *OutpChannel = nullptr;
    }
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_AttachToPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtOutpChannel_AttachToPort(DtOutpChannel* OutpChannel, DtDevice* Device, int Port)
{
    if (OutpChannel)
        return ToCpp(OutpChannel)->AttachToPort(ToCpp(Device), Port);
    return DTAPI_E_INVALID_ARG;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_ClearFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtOutpChannel_ClearFifo(DtOutpChannel* OutpChannel)
{
    if (OutpChannel)
        return ToCpp(OutpChannel)->ClearFifo();
    return DTAPI_E_INVALID_ARG;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtOutpChannel_Detach(DtOutpChannel* OutpChannel, int DetachMode)
{
    if (OutpChannel)
        return ToCpp(OutpChannel)->Detach(DetachMode);
    return DTAPI_E_INVALID_ARG;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_GetFifoLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtOutpChannel_GetFifoLoad(DtOutpChannel* OutpChannel, int* FifoLoad)
{
    if (OutpChannel && FifoLoad)
        return ToCpp(OutpChannel)->GetFifoLoad(*FifoLoad);
    return DTAPI_E_INVALID_ARG;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_GetFifoSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtOutpChannel_GetFifoSize(DtOutpChannel* OutpChannel, int* FifoSize)
{
    if (OutpChannel && FifoSize)
        return ToCpp(OutpChannel)->GetFifoSize(*FifoSize);
    return DTAPI_E_INVALID_ARG;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_GetMaxFifoSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtOutpChannel_GetMaxFifoSize(DtOutpChannel* OutpChannel, int* MaxFifoSize)
{
    if (OutpChannel && MaxFifoSize)
        return ToCpp(OutpChannel)->GetMaxFifoSize(*MaxFifoSize);
    return DTAPI_E_INVALID_ARG;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_GetFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtOutpChannel_GetFlags(DtOutpChannel* OutpChannel, int* Status, int* Latched)
{
    if (OutpChannel && Status && Latched)
        return ToCpp(OutpChannel)->GetFlags(*Status, *Latched);
    return DTAPI_E_INVALID_ARG;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_SetIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtOutpChannel_SetIoConfig(DtOutpChannel* OutpChannel, int Group, int Value, int SubValue)
{
    if (OutpChannel)
        return ToCpp(OutpChannel)->SetIoConfig(Group, Value, SubValue);
    return DTAPI_E_INVALID_ARG;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_SetTxControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtOutpChannel_SetTxControl(DtOutpChannel* OutpChannel, int TxControl)
{
    if (OutpChannel)
        return ToCpp(OutpChannel)->SetTxControl(TxControl);
    return DTAPI_E_INVALID_ARG;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_SetTxMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtOutpChannel_SetTxMode(DtOutpChannel* OutpChannel, int TxMode, int StuffMode)
{
    if (OutpChannel)
        return ToCpp(OutpChannel)->SetTxMode(TxMode, StuffMode);
    return DTAPI_E_INVALID_ARG;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_Write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtOutpChannel_Write(DtOutpChannel* OutpChannel, char* Buffer, int NumBytesToWrite)
{
    if (OutpChannel)
        return ToCpp(OutpChannel)->Write(Buffer, NumBytesToWrite);
    return DTAPI_E_INVALID_ARG;
}

// =+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Global functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtapiVidStd2IoStd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtapiVidStd2IoStd(int VideoStandard, int LinkStandard, int* Value,
                                                                            int* SubValue)
{
    if (Value && SubValue)
        return Dtapi::DtapiVidStd2IoStd(VideoStandard, LinkStandard, *Value, *SubValue);
    return DTAPI_E_INVALID_ARG;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtapiResult2Str -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* DtapiResult2Str(unsigned int Result)
{
    return Dtapi::DtapiResult2Str(Result);
}

} // extern "C"
