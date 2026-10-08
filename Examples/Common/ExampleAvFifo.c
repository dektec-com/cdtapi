// #*#*#*#*#*#*#*#*#*#*#*#*#*# ExampleAvFifo.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - What the SMPTE ST 2110 examples share: the stream, frames and pipes
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
// Reads an IPv4 address, four numbers from 0 to 255, from Text into Ip. Returns false
// for anything else.
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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test pattern +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PackArea -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Packs Area of the pattern's image into Data, a frame of video in the sample size of
// Config. A pixel group is two pixels: Cb, Y, Cr and Y. In 10 bits the samples follow one
// another, most significant bit first, so that a group takes five bytes; in 8 bits each
// sample is the top 8 bits of the 10-bit one. The area is widened to whole groups.
//
static void PackArea(const ExampleAvConfig* Config, const ExamplePattern* Pattern,
                     const ExamplePatternArea* Area, uint8_t* Data)
{
    const int First = Area->X / 2;
    const int End = (Area->X + Area->Width + 1) / 2;
    const size_t RowBytes = (size_t)ExampleAv_RowBytes(Config);

    for (int y = Area->Y; y < Area->Y + Area->Lines; y++)
    {
        const uint16_t* Luma = Pattern->Y + (size_t)y * (size_t)Pattern->Width;
        const uint16_t* Blue = Pattern->Cb + (size_t)y * (size_t)(Pattern->Width / 2);
        const uint16_t* Red = Pattern->Cr + (size_t)y * (size_t)(Pattern->Width / 2);
        uint8_t* Row = Data + (size_t)y * RowBytes;
        for (int p = First; p < End; p++)
        {
            const uint32_t Cb = Blue[p];
            const uint32_t Y0 = Luma[2 * p];
            const uint32_t Cr = Red[p];
            const uint32_t Y1 = Luma[2 * p + 1];
            if (Config->EightBit)
            {
                uint8_t* Group = Row + (size_t)p * 4;
                Group[0] = (uint8_t)(Cb >> 2);
                Group[1] = (uint8_t)(Y0 >> 2);
                Group[2] = (uint8_t)(Cr >> 2);
                Group[3] = (uint8_t)(Y1 >> 2);
                continue;
            }
            uint8_t* Group = Row + (size_t)p * 5;
            Group[0] = (uint8_t)(Cb >> 2);
            Group[1] = (uint8_t)((Cb & 3) << 6 | Y0 >> 4);
            Group[2] = (uint8_t)((Y0 & 15) << 4 | Cr >> 6);
            Group[3] = (uint8_t)((Cr & 63) << 2 | Y1 >> 8);
            Group[4] = (uint8_t)Y1;
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleAv_SourceInit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// For video, the pattern's first image is packed once here; for audio, only room for
// one frame's samples is needed.
//
bool ExampleAv_SourceInit(ExampleAvSource* Src, const ExampleAvConfig* Config)
{
    memset(Src, 0, sizeof(*Src));
    Src->Config = Config;
    if (Config->Channels > 0)
    {
        Src->Samples =
            (int32_t*)malloc((size_t)Config->SamplesPerFrame * sizeof(int32_t));
        if (Src->Samples == NULL)
        {
            printf("Out of memory\n");
            return false;
        }
        return true;
    }

    Src->Background = (uint8_t*)malloc((size_t)ExampleAv_FrameBytes(Config));
    if (Src->Background == NULL)
    {
        printf("Out of memory\n");
        return false;
    }
    if (!ExamplePattern_Init(&Src->Pattern, Config->Width, Config->Height))
    {
        printf("No test pattern for an image of %dx%d; it needs at least 320x240\n",
               Config->Width, Config->Height);
        ExampleAv_SourceFree(Src);
        return false;
    }
    const ExamplePatternArea All = {0, 0, Config->Width, Config->Height};
    PackArea(Config, &Src->Pattern, &All, Src->Background);
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleAv_SourceFree -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void ExampleAv_SourceFree(ExampleAvSource* Src)
{
    ExamplePattern_Free(&Src->Pattern);
    free(Src->Background);
    free(Src->Samples);
    memset(Src, 0, sizeof(*Src));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleAv_SourceFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A video frame costs one copy of the packed first image, and packing the bar and the
// box: little enough for any image size and rate.
//
void ExampleAv_SourceFrame(ExampleAvSource* Src, int64_t Number, uint8_t* Data)
{
    const ExampleAvConfig* Config = Src->Config;

    if (Config->Channels > 0)
    {
        ExampleTone_Next(&Src->Tone, Src->Samples, Config->SamplesPerFrame);
        uint8_t* Out = Data;
        for (int s = 0; s < Config->SamplesPerFrame; s++)
        {
            const uint32_t Value = (uint32_t)Src->Samples[s] >> 8;
            for (int c = 0; c < Config->Channels; c++)
            {
                *Out++ = (uint8_t)(Value >> 16);
                *Out++ = (uint8_t)(Value >> 8);
                *Out++ = (uint8_t)Value;
            }
        }
        return;
    }

    memcpy(Data, Src->Background, (size_t)ExampleAv_FrameBytes(Config));
    ExamplePattern_Draw(&Src->Pattern, Number);
    ExamplePatternArea Areas[2];
    const int Count = ExamplePattern_Changes(&Src->Pattern, Areas);
    for (int a = 0; a < Count; a++)
        PackArea(Config, &Src->Pattern, &Areas[a], Data);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleAv_ReadNumber -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Takes the luma of the line that carries the code, as 10-bit values, from the frame's
// format: in 10-bit pixel groups Y0 is the bits after Cb and Y1 the last ten; in 8-bit
// groups and in planar 8-bit each luma byte is shifted up by two.
//
bool ExampleAv_ReadNumber(const AvFifo_Frame* Frame, St2110_RxFrameFormat Format,
                          uint32_t* Number)
{
    uint16_t Luma[8192];
    const int Rows = Frame->NumRows;
    const int Line = ExamplePattern_CodeLine(Rows);

    *Number = 0;
    if (Frame->Is420 || Rows <= 0 || Line >= Rows || Frame->NumValidBytes <= 0)
        return false;
    const size_t RowBytes = (size_t)Frame->NumValidBytes / (size_t)Rows;
    int Width = 0;
    if (Format == St2110_RxFrameFormat_Uyvy422_10b)
    {
        Width = (int)(RowBytes / 5 * 2);
        const uint8_t* Row = Frame->Data + (size_t)Line * RowBytes;
        for (int p = 0; p < Width / 2 && Width <= 8192; p++)
        {
            const uint8_t* Group = Row + 5 * (size_t)p;
            Luma[2 * p] = (uint16_t)((Group[1] & 0x3F) << 4 | Group[2] >> 4);
            Luma[2 * p + 1] = (uint16_t)((Group[3] & 3) << 8 | Group[4]);
        }
    }
    else if (Format == St2110_RxFrameFormat_Uyvy422_8b ||
             Format == St2110_RxFrameFormat_Uyvy422_10b_to_8b)
    {
        Width = (int)(RowBytes / 2);
        const uint8_t* Row = Frame->Data + (size_t)Line * RowBytes;
        for (int x = 0; x < Width && Width <= 8192; x++)
            Luma[x] = (uint16_t)(Row[2 * (size_t)x + 1] << 2);
    }
    else if (Format == St2110_RxFrameFormat_Yuv422p_8b)
    {
        // The Y plane comes first, one byte per pixel; Cb and Cr together are as large.
        Width = (int)(RowBytes / 2);
        const uint8_t* Row = Frame->Data + (size_t)Line * (size_t)Width;
        for (int x = 0; x < Width && Width <= 8192; x++)
            Luma[x] = (uint16_t)(Row[x] << 2);
    }
    if (Width <= 0 || Width > 8192)
        return false;
    return ExamplePattern_ReadNumber(Luma, Width, Number);
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
    bool UsesHwPipe = false;

    if (AvFifo_TxFifo_UsesHwPipe(Fifo, &UsesHwPipe) != DTAPI_OK)
        return "pipe";
    return UsesHwPipe ? "hardware pipe" : "software pipe";
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleAv_RxPipeKind -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* ExampleAv_RxPipeKind(const AvFifo_RxFifo* Fifo)
{
    bool UsesHwPipe = false;

    if (AvFifo_RxFifo_UsesHwPipe(Fifo, &UsesHwPipe) != DTAPI_OK)
        return "pipe";
    return UsesHwPipe ? "hardware pipe" : "software pipe";
}
