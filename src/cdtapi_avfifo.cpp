// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* cdtapi_avfifo.cpp *#*#*#*#*#*#* (C) 2022-2024 DekTec
//
#include "cdtapi_avfifo.h"

#define _NO_USING_NAMESPACE_DTAPI
#include "dtapi/DTAPI.h"
#include "dtapi/DTAPI_AvFifo.h"

#include <cstring>

thread_local static std::string LastExceptionMessage;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_Frame_Internal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
struct AvFifo_Frame_Internal : AvFifo_Frame
{
    Dtapi::AvFifo::Frame* CppFrame;
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Local utility functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToC -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static AvFifo_RxFifo* ToC(Dtapi::AvFifo::RxFifo* Fifo)
{
    return (AvFifo_RxFifo*)Fifo;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToC -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static AvFifo_TxFifo* ToC(Dtapi::AvFifo::TxFifo* Fifo)
{
    return (AvFifo_TxFifo*)Fifo;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToCpp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static const Dtapi::DtDevice* ToCpp(const DtDevice* Device)
{
    return (const Dtapi::DtDevice*)Device;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToC -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static RxStatistics ToC(const Dtapi::AvFifo::RxStatistics& Statistics)
{
    RxStatistics CStatistics{};
    CStatistics.FramesOk = Statistics.FramesOk;
    CStatistics.FramesIncomplete = Statistics.FramesIncomplete;
    CStatistics.FramesSizeError = Statistics.FramesSizeError;
    CStatistics.Gaps = Statistics.Gaps;
    CStatistics.IpPacketErrors = Statistics.IpPacketErrors;
    CStatistics.DroppedFrames = Statistics.DroppedFrames;
    CStatistics.SyncErrors = Statistics.SyncErrors;
    return CStatistics;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToC -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static TxStatistics ToC(const Dtapi::AvFifo::TxStatistics& Statistics)
{
    TxStatistics CStatistics{};
    CStatistics.FramesOk = Statistics.FramesOk;
    return CStatistics;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToC -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static AvFifo_Frame* ToC(Dtapi::AvFifo::Frame* Frame)
{
    AvFifo_Frame_Internal* CFrame{new (std::nothrow) AvFifo_Frame_Internal};
    if (CFrame)
    {
        CFrame->RtpTime = Frame->RtpTime;
        CFrame->ToD.Seconds = Frame->ToD.m_Seconds;
        CFrame->ToD.Nanoseconds = Frame->ToD.m_Nanoseconds;
        CFrame->Data = Frame->Data();
        CFrame->Field = Frame->Field;
        CFrame->NumValidBytes = Frame->NumValidBytes;
        CFrame->Size = Frame->Size();
        CFrame->Is420 = Frame->Is420();
        CFrame->NumRows = Frame->NumRows();
        CFrame->CppFrame = Frame;
    }
    return CFrame;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToCpp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static Dtapi::AvFifo::Frame* ToCpp(const AvFifo_Frame* Frame)
{
    Dtapi::AvFifo::Frame* CppFrame{((const AvFifo_Frame_Internal*)Frame)->CppFrame};
    CppFrame->RtpTime = Frame->RtpTime;
    CppFrame->ToD.m_Seconds = Frame->ToD.Seconds;
    CppFrame->ToD.m_Nanoseconds = Frame->ToD.Nanoseconds;
    CppFrame->Field = Frame->Field;
    CppFrame->NumValidBytes = Frame->NumValidBytes;
    return CppFrame;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToCpp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// static const Dtapi::AvFifo::Frame* ToCpp(const AvFifo_Frame* Frame)
// {
//     return ((const AvFifo_Frame_Internal*)Frame)->CppFrame;
// }

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToCpp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static Dtapi::AvFifo::RxFifo* ToCpp(AvFifo_RxFifo* Fifo)
{
    return (Dtapi::AvFifo::RxFifo*)Fifo;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToCpp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static const Dtapi::AvFifo::RxFifo* ToCpp(const AvFifo_RxFifo* Fifo)
{
    return (const Dtapi::AvFifo::RxFifo*)Fifo;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToCpp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static Dtapi::AvFifo::TxFifo* ToCpp(AvFifo_TxFifo* Fifo)
{
    return (Dtapi::AvFifo::TxFifo*)Fifo;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToCpp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static const Dtapi::AvFifo::TxFifo* ToCpp(const AvFifo_TxFifo* Fifo)
{
    return (const Dtapi::AvFifo::TxFifo*)Fifo;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToCpp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static Dtapi::AvFifo::St2110::RxConfigAudio ToCpp(const St2110_RxConfigAudio* Config)
{
    Dtapi::AvFifo::St2110::RxConfigAudio CppConfig{};
    CppConfig.Format = (Dtapi::AvFifo::St2110::AudioFormat)Config->Format;
    CppConfig.SampleRate = Config->SampleRate;
    return CppConfig;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToCpp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static Dtapi::AvFifo::St2110::RxConfigVideo ToCpp(const St2110_RxConfigVideo* Config)
{
    Dtapi::AvFifo::St2110::RxConfigVideo CppConfig{};
    CppConfig.Format = (Dtapi::AvFifo::St2110::RxFrameFormat)Config->Format;
    return CppConfig;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToCpp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static Dtapi::AvFifo::St2110::TxConfigAudio ToCpp(const St2110_TxConfigAudio* Config)
{
    Dtapi::AvFifo::St2110::TxConfigAudio CppConfig{};
    CppConfig.Format = (Dtapi::AvFifo::St2110::AudioFormat)Config->Format;
    CppConfig.NumChannels = Config->NumChannels;
    CppConfig.NumSamplesPerIpPacket = Config->NumSamplesPerIpPacket;
    CppConfig.SampleRate = Config->SampleRate;
    return CppConfig;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToCpp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static Dtapi::AvFifo::St2110::TxConfigVideo ToCpp(const St2110_TxConfigVideo* Config)
{
    Dtapi::AvFifo::St2110::TxConfigVideo CppConfig{};
    CppConfig.Format = (Dtapi::AvFifo::St2110::TxFrameFormat)Config->Format;
    
    CppConfig.Packing.OneLinePerPacket = Config->Packing.OneLinePerPacket;
    CppConfig.Packing.PackingMode = (Dtapi::AvFifo::St2110::PackingMode)Config->Packing.PackingMode;
    CppConfig.Packing.PayloadSize = Config->Packing.PayloadSize;
    
    CppConfig.Resolution.Width = Config->Resolution.Width;
    CppConfig.Resolution.Height = Config->Resolution.Height;

    CppConfig.Timing.Rate.Numerator = Config->Timing.Rate.Numerator;
    CppConfig.Timing.Rate.Denominator = Config->Timing.Rate.Denominator;
    CppConfig.Timing.Scheduling = (Dtapi::AvFifo::St2110::Scheduling)Config->Timing.Scheduling;
    CppConfig.Timing.VideoScanning = (Dtapi::AvFifo::St2110::VideoScanning)Config->Timing.VideoScanning;
    return CppConfig;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToCpp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static Dtapi::AvFifo::IpPars ToCpp(const AvFifo_IpPars* IpPars)
{
    Dtapi::AvFifo::IpPars CppIpPars{};
    memcpy(CppIpPars.IpAddr.data(), IpPars->IpAddr, 16);
    CppIpPars.IpVersion = (Dtapi::AvFifo::IpProtocolVersion)IpPars->IpVersion;
    CppIpPars.Port = IpPars->Port;
    for (int i{0}; i < IpPars->NSrcFlt; i++)
    {
        IpSrcFlt* SrcFlt{&IpPars->SrcFlt[i]};
        Dtapi::AvFifo::IpSrcFlt CppSrcFlt{};
        memcpy(CppSrcFlt.IpAddr.data(), SrcFlt->IpAddr, 16);
        CppSrcFlt.Port = SrcFlt->Port;
        CppIpPars.SrcFlt.push_back(CppSrcFlt);
    }
    CppIpPars.DiffServ = IpPars->DiffServ;
    memcpy(CppIpPars.Gateway.data(), IpPars->Gateway, 16);
    CppIpPars.RtpPayloadType = IpPars->RtpPayloadType;
    CppIpPars.TimeToLive = IpPars->TimeToLive;
    CppIpPars.TransportProtocol =
                            (Dtapi::AvFifo::IpTransportProtocol)IpPars->TransportProtocol;
    CppIpPars.Vlan.Id = IpPars->Vlan.Id;
    CppIpPars.Vlan.Priority = IpPars->Vlan.Priority;
    return CppIpPars;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+ AvFifo_Frame implementation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// // -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_Frame_GetRtpTime -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
// //
// uint32_t AvFifo_Frame_GetRtpTime(const AvFifo_Frame* Frame)
// {
//     return ToCpp(Frame)->RtpTime;
// }

// // -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_Frame_GetToD -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
// //
// DtTimeOfDay AvFifo_Frame_GetToD(const AvFifo_Frame* Frame)
// {
//     const Dtapi::DtTimeOfDay& CppToD{ToCpp(Frame)->ToD};
//     return DtTimeOfDay{CppToD.m_Seconds, CppToD.m_Nanoseconds};
// }

// // .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_Frame_GetData -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
// //
// uint8_t* AvFifo_Frame_GetData(const AvFifo_Frame* Frame)
// {
//     return ToCpp(Frame)->Data();
// }

// // .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_Frame_GetField -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
// //
// int AvFifo_Frame_GetField(const AvFifo_Frame* Frame)
// {
//     return ToCpp(Frame)->Field;
// }

// // .-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_Frame_GetNumValidBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.
// //
// int AvFifo_Frame_GetNumValidBytes(const AvFifo_Frame* Frame)
// {
//     return ToCpp(Frame)->NumValidBytes;
// }

// // .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_Frame_GetSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
// //
// size_t AvFifo_Frame_GetSize(const AvFifo_Frame* Frame)
// {
//     return ToCpp(Frame)->Size();
// }

// // -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_Frame_Is420 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
// //
// int AvFifo_Frame_Is420(const AvFifo_Frame* Frame)
// {
//     return ToCpp(Frame)->Is420() ? 1 : 0;
// }

// // -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_Frame_GetNumRows -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
// //
// int AvFifo_Frame_GetNumRows(const AvFifo_Frame* Frame)
// {
//     return ToCpp(Frame)->NumRows();
// }

// // -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_Frame_SetRtpTime -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
// //
// void AvFifo_Frame_SetRtpTime(AvFifo_Frame* Frame, uint32_t RtpTime)
// {
//     ToCpp(Frame)->RtpTime = RtpTime;
// }

// // -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_Frame_SetToD -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
// //
// void AvFifo_Frame_SetToD(AvFifo_Frame* Frame, DtTimeOfDay ToD)
// {
//     ToCpp(Frame)->ToD.m_Seconds = ToD.Seconds;
//     ToCpp(Frame)->ToD.m_Nanoseconds = ToD.Nanoseconds;
// }

// // .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_Frame_SetField -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
// //
// void AvFifo_Frame_SetField(AvFifo_Frame* Frame, int Field)
// {
//     ToCpp(Frame)->Field = Field;
// }

// // .-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_Frame_SetNumValidBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.
// //
// void AvFifo_Frame_SetNumValidBytes(AvFifo_Frame* Frame, int NumValidBytes)
// {
//     ToCpp(Frame)->NumValidBytes = NumValidBytes;
// }

// =+=+=+=+=+=+=+=+=+=+=+=+=+=+ AvFifo_RxFifo implementation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
AvFifo_RxFifo* AvFifo_RxFifo_Alloc()
{
    return ToC(new (std::nothrow) Dtapi::AvFifo::RxFifo);
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void AvFifo_RxFifo_Free(AvFifo_RxFifo* Fifo)
{
    delete ToCpp(Fifo);
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void AvFifo_RxFifo_Freep(AvFifo_RxFifo** Fifo)
{
    if (Fifo)
    {
        delete ToCpp(*Fifo);
        *Fifo = nullptr;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_Attach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int AvFifo_RxFifo_Attach(AvFifo_RxFifo* Fifo, const DtDevice* Device, int Port)
{
    if (!Fifo || !Device)
        return DTAPI_E_INVALID_ARG;
    try {
        ToCpp(Fifo)->Attach(*ToCpp(Device), Port);
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        return DTAPI_E_EXCEPTION;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int AvFifo_RxFifo_Detach(AvFifo_RxFifo* Fifo)
{
    if (!Fifo)
        return DTAPI_E_INVALID_ARG;
    try {
        ToCpp(Fifo)->Detach();
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        return DTAPI_E_EXCEPTION;
    }
    return DTAPI_OK;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_Clear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int AvFifo_RxFifo_Clear(AvFifo_RxFifo* Fifo)
{
    if (!Fifo)
        return DTAPI_E_INVALID_ARG;
    try {
        ToCpp(Fifo)->Clear();
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        return DTAPI_E_EXCEPTION;
    }
    return DTAPI_OK;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_Start -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int AvFifo_RxFifo_Start(AvFifo_RxFifo* Fifo)
{
    if (!Fifo)
        return DTAPI_E_INVALID_ARG;
    try {
        ToCpp(Fifo)->Start();
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        return DTAPI_E_EXCEPTION;
    }
    return DTAPI_OK;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_Stop -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int AvFifo_RxFifo_Stop(AvFifo_RxFifo* Fifo)
{
    if (!Fifo)
        return DTAPI_E_INVALID_ARG;
    try {
        ToCpp(Fifo)->Stop();
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        return DTAPI_E_EXCEPTION;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_ConfigureAudio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int AvFifo_RxFifo_ConfigureAudio(AvFifo_RxFifo* Fifo,
                                                       const St2110_RxConfigAudio* Config)
{
    if (!Fifo || !Config)
        return DTAPI_E_INVALID_ARG;
    try {
        ToCpp(Fifo)->Configure(ToCpp(Config));
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        return DTAPI_E_EXCEPTION;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_ConfigureVideo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int AvFifo_RxFifo_ConfigureVideo(AvFifo_RxFifo* Fifo,
                                                       const St2110_RxConfigVideo* Config)
{
    if (!Fifo || !Config)
        return DTAPI_E_INVALID_ARG;
    try {
        ToCpp(Fifo)->Configure(ToCpp(Config));
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        return DTAPI_E_EXCEPTION;
    }
    return DTAPI_OK;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_SetIpPars -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int AvFifo_RxFifo_SetIpPars(AvFifo_RxFifo* Fifo, const AvFifo_IpPars* IpPars)
{
    if (!Fifo || !IpPars)
        return DTAPI_E_INVALID_ARG;
    try {
        Dtapi::AvFifo::IpPars CppIpPars{ToCpp(IpPars)};
        ToCpp(Fifo)->SetIpPars(CppIpPars);
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        return DTAPI_E_EXCEPTION;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_GetFifoLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int AvFifo_RxFifo_GetFifoLoad(const AvFifo_RxFifo* Fifo)
{
    if (!Fifo)
        return 0;
    try {
        return ToCpp(Fifo)->GetFifoLoad();
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        return DTAPI_E_EXCEPTION;
    }
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_Read -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
AvFifo_Frame* AvFifo_RxFifo_Read(AvFifo_RxFifo* Fifo)
{
    if (!Fifo)
        return nullptr;

    try {
        // Get frame from the FIFO. Then copy the frame to the heap in order to
        // return a pointer to the frame.
        Dtapi::AvFifo::Frame* Frame{ToCpp(Fifo)->Read()};
        return ToC(Frame);
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        return nullptr;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_ReturnToMemPool -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int AvFifo_RxFifo_ReturnToMemPool(AvFifo_RxFifo* Fifo, AvFifo_Frame* Frame)
{
    if (!Fifo || !Frame)
        return DTAPI_E_INVALID_ARG;

    try {
        // Return the frame to the memory pool and delete from heap.
        Dtapi::AvFifo::Frame* CppFrame{ToCpp(Frame)};
        ToCpp(Fifo)->ReturnToMemPool(CppFrame);
        delete (AvFifo_Frame_Internal*)Frame;
        return DTAPI_OK;
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        delete (AvFifo_Frame_Internal*)Frame;
        return DTAPI_E_EXCEPTION;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_GetMaxSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int AvFifo_RxFifo_GetMaxSize(const AvFifo_RxFifo* Fifo)
{
    if (!Fifo)
        return 0;
    return ToCpp(Fifo)->GetMaxSize();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_SetMaxSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void AvFifo_RxFifo_SetMaxSize(AvFifo_RxFifo* Fifo, int Size)
{
    if (Fifo)
        ToCpp(Fifo)->SetMaxSize(Size);
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_RxFifo_GetStatistics -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
RxStatistics AvFifo_RxFifo_GetStatistics(const AvFifo_RxFifo* Fifo)
{
    if (!Fifo)
        return {};
    return ToC(ToCpp(Fifo)->GetStatistics());
}

// =+=+=+=+=+=+=+=+=+=+=+=+=+=+ AvFifo_TxFifo implementation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
AvFifo_TxFifo* AvFifo_TxFifo_Alloc()
{
    return ToC(new (std::nothrow) Dtapi::AvFifo::TxFifo);
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void AvFifo_TxFifo_Free(AvFifo_TxFifo* Fifo)
{
    delete ToCpp(Fifo);
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void AvFifo_TxFifo_Freep(AvFifo_TxFifo** Fifo)
{
    if (Fifo)
    {
        delete ToCpp(*Fifo);
        *Fifo = nullptr;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_Attach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int AvFifo_TxFifo_Attach(AvFifo_TxFifo* Fifo, const DtDevice* Device, int Port)
{
    if (!Fifo || !Device)
        return DTAPI_E_INVALID_ARG;
    try {
        ToCpp(Fifo)->Attach(*ToCpp(Device), Port);
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        return DTAPI_E_EXCEPTION;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int AvFifo_TxFifo_Detach(AvFifo_TxFifo* Fifo)
{
    if (!Fifo)
        return DTAPI_E_INVALID_ARG;
    try {
        ToCpp(Fifo)->Detach();
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        return DTAPI_E_EXCEPTION;
    }
    return DTAPI_OK;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_Clear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int AvFifo_TxFifo_Clear(AvFifo_TxFifo* Fifo)
{
    if (!Fifo)
        return DTAPI_E_INVALID_ARG;
    try {
        ToCpp(Fifo)->Clear();
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        return DTAPI_E_EXCEPTION;
    }
    return DTAPI_OK;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_Start -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int AvFifo_TxFifo_Start(AvFifo_TxFifo* Fifo)
{
    if (!Fifo)
        return DTAPI_E_INVALID_ARG;
    try {
        ToCpp(Fifo)->Start();
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        return DTAPI_E_EXCEPTION;
    }
    return DTAPI_OK;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_Stop -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int AvFifo_TxFifo_Stop(AvFifo_TxFifo* Fifo)
{
    if (!Fifo)
        return DTAPI_E_INVALID_ARG;
    try {
        ToCpp(Fifo)->Stop();
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        return DTAPI_E_EXCEPTION;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_ConfigureAudio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int AvFifo_TxFifo_ConfigureAudio(AvFifo_TxFifo* Fifo,
                                                       const St2110_TxConfigAudio* Config)
{
    if (!Fifo || !Config)
        return DTAPI_E_INVALID_ARG;
    try {
        ToCpp(Fifo)->Configure(ToCpp(Config));
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        return DTAPI_E_EXCEPTION;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_ConfigureVideo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int AvFifo_TxFifo_ConfigureVideo(AvFifo_TxFifo* Fifo,
                                                       const St2110_TxConfigVideo* Config)
{
    if (!Fifo || !Config)
        return DTAPI_E_INVALID_ARG;
    try {
        ToCpp(Fifo)->Configure(ToCpp(Config));
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        return DTAPI_E_EXCEPTION;
    }
    return DTAPI_OK;
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_SetIpPars -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int AvFifo_TxFifo_SetIpPars(AvFifo_TxFifo* Fifo, const AvFifo_IpPars* IpPars)
{
    if (!Fifo || !IpPars)
        return DTAPI_E_INVALID_ARG;
    try {
        Dtapi::AvFifo::IpPars CppIpPars{ToCpp(IpPars)};
        ToCpp(Fifo)->SetIpPars(CppIpPars);
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        return DTAPI_E_EXCEPTION;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_GetFifoLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int AvFifo_TxFifo_GetFifoLoad(const AvFifo_TxFifo* Fifo)
{
    if (!Fifo)
        return 0;
    try {
        return ToCpp(Fifo)->GetFifoLoad();
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        return DTAPI_E_EXCEPTION;
    }
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_Write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int AvFifo_TxFifo_Write(AvFifo_TxFifo* Fifo, AvFifo_Frame* Frame)
{
    if (!Fifo || !Frame)
        return DTAPI_E_INVALID_ARG;

    try {
        Dtapi::AvFifo::Frame* CppFrame{ToCpp(Frame)};
        ToCpp(Fifo)->Write(CppFrame);
        delete (AvFifo_Frame_Internal*)Frame;
        return DTAPI_OK;
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        delete (AvFifo_Frame_Internal*)Frame;
        return DTAPI_E_EXCEPTION;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_GetFromMemPool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
AvFifo_Frame* AvFifo_TxFifo_GetFromMemPool(AvFifo_TxFifo* Fifo, int Size)
{
    if (!Fifo)
        return nullptr;

    try {
        Dtapi::AvFifo::Frame* Frame{ToCpp(Fifo)->GetFrameFromMemPool(Size)};
        return ToC(Frame);
    } catch(std::exception& e) {
        LastExceptionMessage = e.what();
        return nullptr;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_GetMaxSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int AvFifo_TxFifo_GetMaxSize(const AvFifo_TxFifo* Fifo)
{
    if (!Fifo)
        return 0;
    return ToCpp(Fifo)->GetMaxSize();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_SetMaxSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void AvFifo_TxFifo_SetMaxSize(AvFifo_TxFifo* Fifo, int Size)
{
    if (Fifo)
        ToCpp(Fifo)->SetMaxSize(Size);
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.- AvFifo_TxFifo_GetStatistics -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
TxStatistics AvFifo_TxFifo_GetStatistics(const AvFifo_TxFifo* Fifo)
{
    if (!Fifo)
        return {};
    return ToC(ToCpp(Fifo)->GetStatistics());
}

// =+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Global functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetLastException -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* GetLastException()
{
    return LastExceptionMessage.data();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ScanningMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
enum class ScanningMode
{
    Progressive,
    Interlaced,
    PsF
};

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeFrameProperties -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
constexpr FrameProperties MakeFrameProperties(int Width, int Height,
                                              ScanningMode ScanningMode,
                                              ChromaSubsampling Subsampling, int BitDepth)
{
    int Bpp{0};
    if (Subsampling == ChromaSubsampling_Key)
        Bpp = BitDepth;
    else if (Subsampling == ChromaSubsampling_420)
        Bpp = (8 * BitDepth + 4 * BitDepth) / 8;
    else if (Subsampling == ChromaSubsampling_422)
        Bpp = (8 * BitDepth + 8 * BitDepth) / 8;
    else if (Subsampling == ChromaSubsampling_444)
        Bpp = (8 * BitDepth + 16 * BitDepth) / 8;

    FrameProperties Properties{};
    Properties.Is420 = Subsampling == ChromaSubsampling_420;
    Properties.NLines = ScanningMode == ScanningMode::Progressive ? Height : Height / 2;
    Properties.BytesPerLine = (Width * Bpp) / 8;
    Properties.BytesPerFrame = Properties.BytesPerLine * Properties.NLines;
    Properties.Width = Width;
    Properties.Height = Height;
    Properties.IsInterlaced = ScanningMode == ScanningMode::Interlaced;
    Properties.Subsampling = Subsampling;
    Properties.BitDepth = BitDepth;
    return Properties;
}

const std::array<FrameProperties, 28> Database{
    // .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- 240-lines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

    MakeFrameProperties(720, 480, ScanningMode::Interlaced, ChromaSubsampling_422, 8),
    MakeFrameProperties(720, 480, ScanningMode::Interlaced, ChromaSubsampling_422, 10),
    MakeFrameProperties(720, 480, ScanningMode::Interlaced, ChromaSubsampling_422, 12),
    MakeFrameProperties(720, 480, ScanningMode::Interlaced, ChromaSubsampling_422, 16),

    // .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- 288-lines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

    MakeFrameProperties(720, 576, ScanningMode::Interlaced, ChromaSubsampling_422, 8),
    MakeFrameProperties(720, 576, ScanningMode::Interlaced, ChromaSubsampling_422, 10),
    MakeFrameProperties(720, 576, ScanningMode::Interlaced, ChromaSubsampling_422, 12),
    MakeFrameProperties(720, 576, ScanningMode::Interlaced, ChromaSubsampling_422, 16),

    // .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- 540-lines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

    MakeFrameProperties(1920, 1080, ScanningMode::Interlaced, ChromaSubsampling_422, 8),
    MakeFrameProperties(1920, 1080, ScanningMode::Interlaced, ChromaSubsampling_422, 10),
    MakeFrameProperties(1920, 1080, ScanningMode::Interlaced, ChromaSubsampling_422, 12),
    MakeFrameProperties(1920, 1080, ScanningMode::Interlaced, ChromaSubsampling_422, 16),

    // .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- 720-lines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

    MakeFrameProperties(1280, 720, ScanningMode::Progressive, ChromaSubsampling_422, 8),
    MakeFrameProperties(1280, 720, ScanningMode::Progressive, ChromaSubsampling_422, 10),
    MakeFrameProperties(1280, 720, ScanningMode::Progressive, ChromaSubsampling_422, 12),
    MakeFrameProperties(1280, 720, ScanningMode::Progressive, ChromaSubsampling_422, 16),

    // -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- 1080-lines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

    MakeFrameProperties(1920, 1080, ScanningMode::Progressive, ChromaSubsampling_422, 8),
    MakeFrameProperties(1920, 1080, ScanningMode::Progressive, ChromaSubsampling_422, 10),
    MakeFrameProperties(1920, 1080, ScanningMode::Progressive, ChromaSubsampling_422, 12),
    MakeFrameProperties(1920, 1080, ScanningMode::Progressive, ChromaSubsampling_422, 16),

    MakeFrameProperties(2048, 1080, ScanningMode::Progressive, ChromaSubsampling_422, 8),
    MakeFrameProperties(2048, 1080, ScanningMode::Progressive, ChromaSubsampling_422, 10),
    MakeFrameProperties(2048, 1080, ScanningMode::Progressive, ChromaSubsampling_422, 12),
    MakeFrameProperties(2048, 1080, ScanningMode::Progressive, ChromaSubsampling_422, 16),

    // -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- 2160-lines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

    MakeFrameProperties(3840, 2160, ScanningMode::Progressive, ChromaSubsampling_422, 8),
    MakeFrameProperties(3840, 2160, ScanningMode::Progressive, ChromaSubsampling_422, 10),
    MakeFrameProperties(3840, 2160, ScanningMode::Progressive, ChromaSubsampling_422, 12),
    MakeFrameProperties(3840, 2160, ScanningMode::Progressive, ChromaSubsampling_422, 16),
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetVidStd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int GetFrameProperties(const AvFifo_Frame* Frame, FrameProperties* Properties)
{
    const Dtapi::AvFifo::Frame* CppFrame{ToCpp(Frame)};
    for (const auto& Props : Database)
    {
        if (CppFrame->Is420() != (Props.Is420 == 1))
            continue;
        if (CppFrame->NumValidBytes != Props.BytesPerFrame)
            continue;
        if (CppFrame->NumRows() != Props.NLines)
            continue;
        *Properties = Props;
        return 1;
    }
    return -1;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ TIMING HELPER FUNCTIONS +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Tod2Grid_Audio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtTimeOfDay Tod2Grid_Audio(const DtTimeOfDay* ToD, int SampleRate)
{
    Dtapi::DtTimeOfDay CppToD{ToD->Seconds, ToD->Nanoseconds};
    CppToD = Dtapi::AvFifo::Tod2Grid_Audio(CppToD, SampleRate);
    return DtTimeOfDay{CppToD.m_Seconds, CppToD.m_Nanoseconds};
}

// -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Tod2Grid_Video -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtTimeOfDay Tod2Grid_Video(const DtTimeOfDay* ToD, const FrameRate* Rate)
{
    Dtapi::DtTimeOfDay CppToD{ToD->Seconds, ToD->Nanoseconds};
    Dtapi::AvFifo::FrameRate CppRate{Rate->Numerator, Rate->Denominator};
    CppToD = Dtapi::AvFifo::Tod2Grid_Video(CppToD, CppRate);
    return DtTimeOfDay{CppToD.m_Seconds, CppToD.m_Nanoseconds};
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Rtp2Tod_Audio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtTimeOfDay Rtp2Tod_Audio(uint32_t RtpTime, const DtTimeOfDay* ToD, int SampleRate)
{
    Dtapi::DtTimeOfDay CppToD{ToD->Seconds, ToD->Nanoseconds};
    CppToD = Dtapi::AvFifo::St2110::Rtp2Tod_Audio(RtpTime, CppToD, SampleRate);
    return DtTimeOfDay{CppToD.m_Seconds, CppToD.m_Nanoseconds};
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Rtp2Tod_Video -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtTimeOfDay Rtp2Tod_Video(uint32_t RtpTime, const DtTimeOfDay* ToD)
{
    Dtapi::DtTimeOfDay CppToD{ToD->Seconds, ToD->Nanoseconds};
    CppToD = Dtapi::AvFifo::St2110::Rtp2Tod_Video(RtpTime, CppToD);
    return DtTimeOfDay{CppToD.m_Seconds, CppToD.m_Nanoseconds};
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Tod2Rtp_Audio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint32_t Tod2Rtp_Audio(const DtTimeOfDay* ToD, int SampleRate)
{
    Dtapi::DtTimeOfDay CppToD{ToD->Seconds, ToD->Nanoseconds};
    return Dtapi::AvFifo::St2110::Tod2Rtp_Audio(CppToD, SampleRate);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Tod2Rtp_Video -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint32_t Tod2Rtp_Video(const DtTimeOfDay* ToD)
{
    Dtapi::DtTimeOfDay CppToD{ToD->Seconds, ToD->Nanoseconds};
    return Dtapi::AvFifo::St2110::Tod2Rtp_Video(CppToD);
}
