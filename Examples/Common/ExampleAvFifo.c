// #*#*#*#*#*#*#*#*#*#*#*#*#*# ExampleAvFifo.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - What the SMPTE ST 2110 examples share: the header, the stream and frames
//
// SPDX-License-Identifier: BSD-3-Clause

// MSVC deprecates sscanf in favour of sscanf_s, which other C libraries do not have.
// Asked for before any header.
#ifdef _MSC_VER
    #define _CRT_SECURE_NO_WARNINGS
#endif

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Example includes
#include "Common/ExampleAvFifo.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The stream +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadIp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The four numbers of an IPv4 address, each from 0 to 255. False for anything else.
//
static bool ReadIp(const char* Text, uint8_t* Ip)
{
    int Values[4] = {0, 0, 0, 0};
    char Rest = 0;
    if (sscanf(Text, "%d.%d.%d.%d%c", &Values[0], &Values[1], &Values[2], &Values[3],
               &Rest) != 4)
    {
        return false;
    }
    for (int i = 0; i < 4; i++)
    {
        if (Values[i] < 0 || Values[i] > 255)
            return false;
        Ip[i] = (uint8_t)Values[i];
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleAv_Config -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool ExampleAv_Config(int Argc, char** Argv, ExampleAvConfig* Config)
{
    static const uint8_t Group[4] = {239, 1, 2, 3};

    memset(Config, 0, sizeof(*Config));
    memcpy(Config->Ip, Group, sizeof(Group));
    Config->UdpPort = 5004;
    Config->Width = 1920;
    Config->Height = 1080;
    Config->Rate = 50;
    Config->SampleRate = 48000;
    Config->SamplesPerFrame = 480;
    Config->EightBit = Example_HasFlag(Argc, Argv, "--8bit");

    const char* Ip = Example_Value(Argc, Argv, "--ip");
    if (Ip != NULL && !ReadIp(Ip, Config->Ip))
    {
        printf("Not an IPv4 address: %s\n", Ip);
        return false;
    }
    int64_t Udp = Config->UdpPort;
    int64_t Width = Config->Width;
    int64_t Height = Config->Height;
    int64_t Rate = Config->Rate;
    int64_t Channels = 0;
    if (!Example_Int64(Argc, Argv, "--udp", &Udp) ||
        !Example_Int64(Argc, Argv, "--width", &Width) ||
        !Example_Int64(Argc, Argv, "--height", &Height) ||
        !Example_Int64(Argc, Argv, "--rate", &Rate) ||
        !Example_Int64(Argc, Argv, "--audio", &Channels))
    {
        return false;
    }
    if (Udp < 0 || Udp > 65535 || Width < 2 || Height < 1 || Rate < 1 || Channels < 0 ||
        Channels > 8)
    {
        printf("A value is out of range: --udp 0..65535, --width, --height, --rate above "
               "0, --audio 1..8\n");
        return false;
    }
    Config->UdpPort = (int)Udp;
    Config->Width = (int)Width & ~1;
    Config->Height = (int)Height;
    Config->Rate = (int)Rate;
    Config->Channels = (int)Channels;

    const char* Pipe = Example_Value(Argc, Argv, "--pipe");
    if (Pipe == NULL || strcmp(Pipe, "auto") == 0)
        Config->Pipe = HwOrSwPipe_Auto;
    else if (strcmp(Pipe, "hw") == 0)
        Config->Pipe = HwOrSwPipe_ForceHwPipe;
    else if (strcmp(Pipe, "sw") == 0)
        Config->Pipe = HwOrSwPipe_UseSwPipe;
    else if (strcmp(Pipe, "prefer") == 0)
        Config->Pipe = HwOrSwPipe_PreferHwPipe;
    else
    {
        printf("Unknown pipe: %s; auto, hw, sw or prefer\n", Pipe);
        return false;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleAv_IsIpPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool ExampleAv_IsIpPort(const DtHwFuncDesc* Port)
{
    return Port->IsAvFifo;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleAv_IpPars -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void ExampleAv_IpPars(const ExampleAvConfig* Config, AvFifo_IpPars* Pars)
{
    memset(Pars, 0, sizeof(*Pars));
    memcpy(Pars->IpAddr, Config->Ip, sizeof(Config->Ip));
    Pars->IpVersion = IpProtocolVersion_IPv4;
    Pars->Port = Config->UdpPort;
    Pars->RtpPayloadType = Config->Channels > 0 ? 97 : 96;
    Pars->TimeToLive = 32;
    Pars->DiffServ = 0x88;
    Pars->TransportProtocol = IpTransportProtocol_Rtp;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleAv_PrintStream -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void ExampleAv_PrintStream(const DtHwFuncDesc* Port, const char* Pipe,
                           const ExampleAvConfig* Config, const char* RxFormat)
{
    char What[64];
    if (Config->Channels > 0)
    {
        snprintf(What, sizeof(What), "%d channels L24 %d Hz", Config->Channels,
                 Config->SampleRate);
    }
    else if (RxFormat != NULL)
        snprintf(What, sizeof(What), "video as %s", RxFormat);
    else
    {
        snprintf(What, sizeof(What), "%dx%d %dHz %s", Config->Width, Config->Height,
                 Config->Rate, Config->EightBit ? "8-bit" : "10-bit");
    }
    printf("%lld:%d  %s  %u.%u.%u.%u:%d  %s\n", (long long)Port->SerialNumber, Port->Port,
           Pipe, Config->Ip[0], Config->Ip[1], Config->Ip[2], Config->Ip[3],
           Config->UdpPort, What);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleAv_Failed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int ExampleAv_Failed(const char* What, unsigned int Result)
{
    int Exit = Example_Failed(What, Result);
    const char* Text = GetLastException();

    if (Text != NULL && Text[0] != '\0')
        printf("  %s\n", Text);
    return Exit;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleAv_RowBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int ExampleAv_RowBytes(const ExampleAvConfig* Config)
{
    return Config->Width / 2 * (Config->EightBit ? 4 : 5);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleAv_FrameBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int ExampleAv_FrameBytes(const ExampleAvConfig* Config)
{
    if (Config->Channels > 0)
        return Config->SamplesPerFrame * Config->Channels * 3;
    return ExampleAv_RowBytes(Config) * Config->Height;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleAv_PeriodNs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int64_t ExampleAv_PeriodNs(const ExampleAvConfig* Config)
{
    if (Config->Channels > 0)
        return (int64_t)Config->SamplesPerFrame * 1000000000 / Config->SampleRate;
    return 1000000000 / Config->Rate;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleAv_ToNs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int64_t ExampleAv_ToNs(const DtTimeOfDay* ToD)
{
    return (int64_t)ToD->Seconds * 1000000000 + ToD->Nanoseconds;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleAv_FromNs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtTimeOfDay ExampleAv_FromNs(int64_t Ns)
{
    DtTimeOfDay ToD;

    ToD.Seconds = (uint32_t)(Ns / 1000000000);
    ToD.Nanoseconds = (uint32_t)(Ns % 1000000000);
    return ToD;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleAv_WritePgroup -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A pixel group is Cb, Y, Cr, Y. Ten-bit samples are a bit stream, most significant bit
// first, of the eight-bit values shifted up by two.
//
void ExampleAv_WritePgroup(const ExampleAvConfig* Config, uint8_t* Row, int Index,
                           int Blue, int Luma, int Red)
{
    if (Config->EightBit)
    {
        uint8_t* Group = Row + (size_t)Index * 4;

        Group[0] = (uint8_t)Blue;
        Group[1] = (uint8_t)Luma;
        Group[2] = (uint8_t)Red;
        Group[3] = (uint8_t)Luma;
        return;
    }
    uint8_t* Group = Row + (size_t)Index * 5;
    uint32_t Cb = (uint32_t)Blue << 2;
    uint32_t Y0 = (uint32_t)Luma << 2;
    uint32_t Cr = (uint32_t)Red << 2;
    Group[0] = (uint8_t)(Cb >> 2);
    Group[1] = (uint8_t)((Cb & 3) << 6 | Y0 >> 4);
    Group[2] = (uint8_t)((Y0 & 15) << 4 | Cr >> 6);
    Group[3] = (uint8_t)((Cr & 63) << 2 | Y0 >> 8);
    Group[4] = (uint8_t)Y0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Pipes +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleAv_AttachTx -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int ExampleAv_AttachTx(AvFifo_TxFifo* Fifo, const DtDevice* Device, int Port,
                                const ExampleAvConfig* Config)
{
    return AvFifo_TxFifo_Attach2(Fifo, Device, Port, Config->Pipe);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleAv_AttachRx -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int ExampleAv_AttachRx(AvFifo_RxFifo* Fifo, const DtDevice* Device, int Port,
                                const ExampleAvConfig* Config)
{
    return AvFifo_RxFifo_Attach2(Fifo, Device, Port, Config->Pipe);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleAv_TxPipeKind -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* ExampleAv_TxPipeKind(const AvFifo_TxFifo* Fifo)
{
    int UsesHwPipe = 0;

    if (AvFifo_TxFifo_UsesHwPipe(Fifo, &UsesHwPipe) != DTAPI_OK)
        return "pipe";
    return UsesHwPipe != 0 ? "hardware pipe" : "software pipe";
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleAv_RxPipeKind -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* ExampleAv_RxPipeKind(const AvFifo_RxFifo* Fifo)
{
    int UsesHwPipe = 0;

    if (AvFifo_RxFifo_UsesHwPipe(Fifo, &UsesHwPipe) != DTAPI_OK)
        return "pipe";
    return UsesHwPipe != 0 ? "hardware pipe" : "software pipe";
}
