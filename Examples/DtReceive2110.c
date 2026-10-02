// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtReceive2110.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Example: receives SMPTE ST 2110 video or audio on an IP port
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Receives --count frames of SMPTE ST 2110 video or audio from a multicast group on an
// IP port. Prints a line per frame: its number, its size, its rows, its time of day, its
// RTP timestamp and a 64-bit FNV-1a hash of its bytes. At the end it prints what the
// FIFO counted.
//
//     9211000001:1  hardware pipe  239.1.2.3:5004  video as 10b
//     9211000001:1  frame 0  5184000 bytes  1080 rows  tod 1800000000.100000000  rtp
//     2296742400  hash 3C0F2E6D89A1B437
//     9211000001:1  ok 3  incomplete 0  gaps 0  packet errors 0  dropped 0  sync 0
//
// --format sets the format the FIFO converts the video to. --sdp, in a library built
// with the NMOS bridge, takes the stream from an SDP file instead: its first video or
// audio flow, which the bridge converts into the FIFO's configuration and IP parameters;
// --ip, --udp and --audio are then not used. Exits with 0 when every frame
// arrives, 2 when a frame does not arrive in time or there is no IP port, and 1 when a
// call fails or the command line is wrong.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// MSVC deprecates fopen in favour of fopen_s, which other C libraries do not have. Asked
// for before any header.
#ifdef _MSC_VER
    #define _CRT_SECURE_NO_WARNINGS
#endif

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Example includes
#include "Common/ExampleAvFifo.h" // The AV FIFO header and what the 2110 examples share.
#include "Common/ExampleCommon.h" // The API and what the examples share.

// The NMOS bridge, when the library has it; the build sets CDTAPI_EXAMPLE_WITH_NMOS
// then.
#if CDTAPI_EXAMPLE_WITH_NMOS
    #include "cdtapi_nmos.h"
#endif

// The largest SDP file the program reads, in bytes.
#define MAX_SDP_SIZE 65536

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static const ExampleOption g_Options[] = {
    {"--serial", true, "The device's serial number; the first device with an IP port"},
    {"--port", true, "The port number; the first IP port"},
    {"--ip", true, "The group or address to receive; 239.1.2.3 without it"},
    {"--udp", true, "The UDP port to receive; 5004 without it"},
    {"--count", true, "The number of frames to receive; 3 without it"},
    {"--timeout", true, "Milliseconds to wait for each frame; 1000 without it"},
    {"--format", true, "raw, 8b, 10b, 10bto8b or planar; 10b without it"},
    {"--audio", true, "Receive audio of this many channels instead of video"},
    {"--pipe", true, EXAMPLE_PIPE_HELP},
    {"--sdp", true, "An SDP file whose first video or audio flow to receive; with NMOS"},
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Fnv1a64 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the 64-bit FNV-1a hash of Size bytes of Data, to compare frames by.
//
static uint64_t Fnv1a64(const uint8_t* Data, int Size)
{
    uint64_t Hash = 0xCBF29CE484222325ull;

    for (int i = 0; i < Size; i++)
    {
        Hash ^= Data[i];
        Hash *= 0x100000001B3ull;
    }
    return Hash;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FormatFrom -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets *Format to the frame format --format names ("raw", "8b", "10b", also when
// not given, "10bto8b" or "planar"). Returns false for another name.
//
static bool FormatFrom(const char* Name, St2110_RxFrameFormat* Format)
{
    if (Name == NULL || strcmp(Name, "10b") == 0)
        *Format = St2110_RxFrameFormat_Uyvy422_10b;
    else if (strcmp(Name, "raw") == 0)
        *Format = St2110_RxFrameFormat_Raw;
    else if (strcmp(Name, "8b") == 0)
        *Format = St2110_RxFrameFormat_Uyvy422_8b;
    else if (strcmp(Name, "10bto8b") == 0)
        *Format = St2110_RxFrameFormat_Uyvy422_10b_to_8b;
    else if (strcmp(Name, "planar") == 0)
        *Format = St2110_RxFrameFormat_Yuv422p_8b;
    else
        return false;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReceiveFrames -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads Count frames, waiting up to TimeoutMs for each, prints a line per frame and
// then the statistics. Returns the program's exit code.
//
static int ReceiveFrames(AvFifo_RxFifo* Fifo, const DtHwFuncDesc* Port, int Count,
                         int TimeoutMs)
{
    int Exit = EXAMPLE_OK;
    for (int Number = 0; Number < Count; Number++)
    {
        AvFifo_Frame* Frame = NULL;
        for (int Waited = 0; Frame == NULL && Waited <= TimeoutMs; Waited += 2)
        {
            Frame = AvFifo_RxFifo_Read(Fifo);
            if (Frame == NULL)
                Example_SleepMs(2);
        }
        if (Frame == NULL)
        {
            printf("%lld:%d  no frame within %d ms\n", (long long)Port->SerialNumber,
                   Port->Port, TimeoutMs);
            Exit = EXAMPLE_NOTHING;
            break;
        }
        printf(
            "%lld:%d  frame %d  %d bytes  %d rows  tod %u.%09u  rtp %u  hash %016llX\n",
            (long long)Port->SerialNumber, Port->Port, Number, Frame->NumValidBytes,
            Frame->NumRows, Frame->ToD.Seconds, Frame->ToD.Nanoseconds, Frame->RtpTime,
            (unsigned long long)Fnv1a64(Frame->Data, Frame->NumValidBytes));
        unsigned int Result = AvFifo_RxFifo_ReturnToMemPool(Fifo, Frame);
        if (Result != DTAPI_OK)
            return ExampleAv_Failed("AvFifo_RxFifo_ReturnToMemPool", Result);
    }

    RxStatistics Stats = AvFifo_RxFifo_GetStatistics(Fifo);
    printf("%lld:%d  ok %d  incomplete %d  gaps %d  packet errors %d  dropped %d  "
           "sync %d\n",
           (long long)Port->SerialNumber, Port->Port, Stats.FramesOk,
           Stats.FramesIncomplete, Stats.Gaps, Stats.IpPacketErrors, Stats.DroppedFrames,
           Stats.SyncErrors);
    return Exit;
}

#if CDTAPI_EXAMPLE_WITH_NMOS

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadSdp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the file Path into Text, of Size bytes, with a null at its end. Returns false,
// after printing why, when the file cannot be read or is too large.
//
static bool ReadSdp(const char* Path, char* Text, size_t Size)
{
    FILE* File = fopen(Path, "rb");
    if (File == NULL)
    {
        printf("Cannot open %s\n", Path);
        return false;
    }
    size_t Length = fread(Text, 1, Size - 1, File);
    bool TooLarge = Length == Size - 1 && fgetc(File) != EOF;
    fclose(File);
    if (TooLarge)
    {
        printf("%s is larger than %d bytes\n", Path, (int)Size - 1);
        return false;
    }
    Text[Length] = '\0';
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConfigureFromSdp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Configures Fifo for the first video or audio flow of the SDP file Path, with frame
// format Format for video, through the NMOS bridge. Sets the fields of Config the
// program prints. Returns EXAMPLE_OK, or the exit code of the failure.
//
static int ConfigureFromSdp(AvFifo_RxFifo* Fifo, const char* Path,
                            St2110_RxFrameFormat Format, ExampleAvConfig* Config)
{
    static char Text[MAX_SDP_SIZE];
    if (!ReadSdp(Path, Text, sizeof(Text)))
        return EXAMPLE_FAILED;
    DtNmosSdp* Sdp = NULL;
    if (DtNmosSdp_Parse(Text, strlen(Text), &Sdp) != DTNMOS_OK)
    {
        printf("%s: %s\n", Path, DtNmos_GetLastError());
        return EXAMPLE_FAILED;
    }
    const DtNmosFlow* Flow = NULL;
    for (size_t i = 0; i < DtNmosSdp_FlowCount(Sdp) && Flow == NULL; i++)
    {
        const DtNmosFlow* Candidate = DtNmosSdp_Flow(Sdp, i);
        if (Candidate->Media == DTNMOS_MEDIA_VIDEO ||
            Candidate->Media == DTNMOS_MEDIA_AUDIO)
            Flow = Candidate;
    }
    if (Flow == NULL)
    {
        printf("%s has no video or audio flow\n", Path);
        DtNmosSdp_Free(Sdp);
        return EXAMPLE_FAILED;
    }

    St2110_RxConfigVideo Video;
    St2110_RxConfigAudio Audio;
    AvFifo_IpPars Pars;
    int Exit = EXAMPLE_OK;
    unsigned int Result =
        DtNmosAvFifo_RxConfigFromFlow(Flow, Format, &Video, &Audio, &Pars);
    if (Result != DTAPI_OK)
        Exit = ExampleAv_Failed("DtNmosAvFifo_RxConfigFromFlow", Result);
    else if (Flow->Media == DTNMOS_MEDIA_AUDIO &&
             (Result = AvFifo_RxFifo_ConfigureAudio(Fifo, &Audio)) != DTAPI_OK)
        Exit = ExampleAv_Failed("AvFifo_RxFifo_ConfigureAudio", Result);
    else if (Flow->Media == DTNMOS_MEDIA_VIDEO &&
             (Result = AvFifo_RxFifo_ConfigureVideo(Fifo, &Video)) != DTAPI_OK)
        Exit = ExampleAv_Failed("AvFifo_RxFifo_ConfigureVideo", Result);
    else if ((Result = AvFifo_RxFifo_SetIpPars(Fifo, &Pars)) != DTAPI_OK)
        Exit = ExampleAv_Failed("AvFifo_RxFifo_SetIpPars", Result);
    else
    {
        // What is printed of the stream; an IPv6 group prints as its first four bytes.
        memcpy(Config->Ip, Pars.IpAddr, sizeof(Config->Ip));
        Config->UdpPort = Pars.Port;
        Config->Channels =
            Flow->Media == DTNMOS_MEDIA_AUDIO ? (int)Flow->Format.Audio.Channels : 0;
        Config->SampleRate = Audio.SampleRate;
    }
    DtNmosSdp_Free(Sdp);
    return Exit;
}

#else

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConfigureFromSdp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Without the NMOS bridge the program cannot read an SDP file: prints so and fails.
//
static int ConfigureFromSdp(AvFifo_RxFifo* Fifo, const char* Path,
                            St2110_RxFrameFormat Format, ExampleAvConfig* Config)
{
    (void)Fifo;
    (void)Path;
    (void)Format;
    (void)Config;
    printf("--sdp needs a library built with the NMOS bridge, CDTAPI_WITH_NMOS\n");
    return EXAMPLE_FAILED;
}

#endif

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConfigureFromCommandLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Configures Fifo for the stream Config describes, with frame format Format for video.
// Returns EXAMPLE_OK, or the exit code of the failure.
//
static int ConfigureFromCommandLine(AvFifo_RxFifo* Fifo, const ExampleAvConfig* Config,
                                    St2110_RxFrameFormat Format)
{
    unsigned int Result = DTAPI_OK;
    if (Config->Channels > 0)
    {
        St2110_RxConfigAudio Audio;
        memset(&Audio, 0, sizeof(Audio));
        Audio.Format = St2110_AudioFormat_L24BE;
        Audio.SampleRate = Config->SampleRate;
        Result = AvFifo_RxFifo_ConfigureAudio(Fifo, &Audio);
        if (Result != DTAPI_OK)
            return ExampleAv_Failed("AvFifo_RxFifo_ConfigureAudio", Result);
    }
    else
    {
        St2110_RxConfigVideo Video;
        memset(&Video, 0, sizeof(Video));
        Video.Format = Format;
        Result = AvFifo_RxFifo_ConfigureVideo(Fifo, &Video);
        if (Result != DTAPI_OK)
            return ExampleAv_Failed("AvFifo_RxFifo_ConfigureVideo", Result);
    }

    AvFifo_IpPars Pars;
    ExampleAv_IpPars(Config, &Pars);
    Result = AvFifo_RxFifo_SetIpPars(Fifo, &Pars);
    if (Result != DTAPI_OK)
        return ExampleAv_Failed("AvFifo_RxFifo_SetIpPars", Result);
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AttachAndReceive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Receives the stream of the SDP file SdpPath, or of the command line when it is NULL.
//
static int AttachAndReceive(DtDevice* Device, AvFifo_RxFifo* Fifo,
                            const DtHwFuncDesc* Port, ExampleAvConfig* Config,
                            const char* SdpPath, St2110_RxFrameFormat Format,
                            const char* FormatName, int Count, int TimeoutMs)
{
    unsigned int Result = DtDevice_AttachToSerial(Device, Port->SerialNumber);
    if (Result != DTAPI_OK)
        return Example_Failed("DtDevice_AttachToSerial", Result);

    Result = ExampleAv_AttachRx(Fifo, Device, Port->Port, Config);
    if (Result != DTAPI_OK)
        return ExampleAv_Failed("AvFifo_RxFifo_Attach", Result);

    int Exit = SdpPath != NULL ? ConfigureFromSdp(Fifo, SdpPath, Format, Config)
                               : ConfigureFromCommandLine(Fifo, Config, Format);
    if (Exit != EXAMPLE_OK)
        return Exit;

    Result = AvFifo_RxFifo_Start(Fifo);
    if (Result != DTAPI_OK)
        return ExampleAv_Failed("AvFifo_RxFifo_Start", Result);

    ExampleAv_PrintStream(Port, ExampleAv_RxPipeKind(Fifo), Config, FormatName);
    Exit = ReceiveFrames(Fifo, Port, Count, TimeoutMs);
    AvFifo_RxFifo_Stop(Fifo);
    return Exit;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main(int Argc, char** Argv)
{
    int64_t Serial = 0;
    int64_t PortNumber = 0;
    int64_t Count = 3;
    int64_t Timeout = 1000;
    ExampleAvConfig Config;
    if (!Example_CheckArguments(
            Argc, Argv, "Receives SMPTE ST 2110 video or audio on an IP port.", g_Options,
            (int)(sizeof(g_Options) / sizeof(g_Options[0]))) ||
        !Example_Int64(Argc, Argv, "--serial", &Serial) ||
        !Example_Int64(Argc, Argv, "--port", &PortNumber) ||
        !Example_Int64(Argc, Argv, "--count", &Count) ||
        !Example_Int64(Argc, Argv, "--timeout", &Timeout) ||
        !ExampleAv_Config(Argc, Argv, &Config))
    {
        return EXAMPLE_FAILED;
    }
    St2110_RxFrameFormat Format = St2110_RxFrameFormat_Uyvy422_10b;
    const char* FormatName = Example_Value(Argc, Argv, "--format");
    if (!FormatFrom(FormatName, &Format))
    {
        printf("Unknown receive format: %s; raw, 8b, 10b, 10bto8b or planar\n",
               FormatName);
        return EXAMPLE_FAILED;
    }
    if (FormatName == NULL)
        FormatName = "10b";

    DtHwFuncDesc Port;
    int Exit = EXAMPLE_FAILED;
    unsigned int Result =
        Example_FindPort(Serial, (int)PortNumber, ExampleAv_IsIpPort, &Port);
    DtDevice* Device = DtDevice_Alloc();
    AvFifo_RxFifo* Fifo = AvFifo_RxFifo_Alloc();
    if (Result == DTAPI_E_NOT_FOUND)
    {
        printf("No IP port that suits\n");
        Exit = EXAMPLE_NOTHING;
    }
    else if (Result != DTAPI_OK)
        Exit = Example_Failed("DtapiHwFuncScan", Result);
    else if (Device == NULL || Fifo == NULL)
        Exit = Example_Failed("Allocating", DTAPI_E_OUT_OF_MEM);
    else
        Exit = AttachAndReceive(Device, Fifo, &Port, &Config,
                                Example_Value(Argc, Argv, "--sdp"), Format, FormatName,
                                (int)Count, (int)Timeout);

    AvFifo_RxFifo_Free(Fifo);
    DtDevice_Free(Device);
    return Exit;
}
