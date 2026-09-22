// #*#*#*#*#*#*#*#*#*#*#*#*#* DtTransmit2110.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Example: transmits SMPTE ST 2110 video or audio on an IP port
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Attaches a transmit FIFO to an IP port, sends --count frames of a moving test pattern,
// or of a tone with --audio, to a multicast group, and prints one line per frame: its
// number, its size, its time of day and its RTP timestamp.
//
//     9211000001:1  hardware pipe  239.1.2.3:5004
//     9211000001:1  frame 0  5184000 bytes  tod 1800000000.100000000  rtp 2296742400
//     9211000001:1  sent 3 frames
//
// Each frame is given a time of day a little after the card's clock, which is what the
// card's scheduler sends it at; correct ST 2110 timing needs the card's clock locked to
// a PTP grandmaster. Exits with 0 when every frame is written, 2 when there is no IP
// port, and 1 when a call fails or the command line is wrong.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Example includes
#include "Common/ExampleAvFifo.h" // The AV FIFO header and what the 2110 examples share.
#include "Common/ExampleCommon.h" // The API and what the examples share.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static const ExampleOption g_Options[] = {
    {"--serial", true, "The device's serial number; the first device with an IP port"},
    {"--port", true, "The port number; the first IP port"},
    {"--ip", true, "The destination address; 239.1.2.3 without it"},
    {"--udp", true, "The destination UDP port; 5004 without it"},
    {"--count", true, "The number of frames to send; 3 without it"},
    {"--width", true, "Pixels a row; 1920 without it"},
    {"--height", true, "Rows a frame; 1080 without it"},
    {"--rate", true, "Frames a second; 50 without it"},
    {"--8bit", false, "Send 8-bit video; 10-bit without it"},
    {"--audio", true, "Send audio of this many channels instead of video"},
    {"--pipe", true, EXAMPLE_PIPE_HELP},
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Pattern -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A frame of vertical bars that move a bar a frame, in the packing the configuration
// asks for, or a square wave of 1 kHz on every channel for audio: 48 samples a period at
// 48 kHz, as 24-bit two's complement samples, most significant byte first.
//
static void Pattern(const ExampleAvConfig* Config, int Number, uint8_t* Data, int Size)
{
    if (Config->Channels > 0)
    {
        int Bytes = 3 * Config->Channels;

        for (int i = 0; i + Bytes <= Size; i += Bytes)
        {
            int Sample = (i / Bytes + Number * Config->SamplesPerFrame) % 48;
            uint32_t Value = Sample < 24 ? 0x200000u : 0xE00000u;

            for (int c = 0; c < Config->Channels; c++)
            {
                Data[i + 3 * c] = (uint8_t)(Value >> 16);
                Data[i + 3 * c + 1] = (uint8_t)(Value >> 8);
                Data[i + 3 * c + 2] = (uint8_t)Value;
            }
        }
        return;
    }
    for (int Row = 0; Row < Config->Height; Row++)
    {
        uint8_t* Line = Data + (size_t)Row * (size_t)ExampleAv_RowBytes(Config);

        for (int Pixel = 0; Pixel < Config->Width; Pixel += 2)
        {
            int Bar = ((Pixel + Number * 16) / 128) % 8;
            int Luma = 16 + Bar * 28;
            int Blue = Bar % 2 == 0 ? 128 : 200;
            int Red = Bar < 4 ? 128 : 80;

            ExampleAv_WritePgroup(Config, Line, Pixel / 2, Blue, Luma, Red);
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SendFrames -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes Count frames, one frame period apart in time of day, from a little after the
// card's clock, as fast as the FIFO takes them, and waits until the FIFO is empty.
//
static int SendFrames(AvFifo_TxFifo* Fifo, DtDevice* Device, const DtHwFuncDesc* Port,
                      const ExampleAvConfig* Config, int Count)
{
    DtTimeOfDay Now;
    unsigned int Result = DtDevice_GetTimeOfDay(Device, &Now);
    if (Result != DTAPI_OK)
        return Example_Failed("DtDevice_GetTimeOfDay", Result);

    int64_t PeriodNs = ExampleAv_PeriodNs(Config);
    int64_t StartNs = ExampleAv_ToNs(&Now) + 100 * 1000 * 1000;
    int WaitMs = (int)(PeriodNs / 4000000) > 1 ? (int)(PeriodNs / 4000000) : 1;
    int Size = ExampleAv_FrameBytes(Config);
    for (int Number = 0; Number < Count; Number++)
    {
        AvFifo_Frame* Frame = AvFifo_TxFifo_GetFromMemPool(Fifo, Size);
        if (Frame == NULL)
            return ExampleAv_Failed("AvFifo_TxFifo_GetFromMemPool", DTAPI_E_OUT_OF_MEM);

        Pattern(Config, Number, Frame->Data, Size);
        Frame->NumValidBytes = Size;
        Frame->NumRows = Config->Height;
        Frame->ToD = ExampleAv_FromNs(StartNs + (int64_t)Number * PeriodNs);
        Frame->RtpTime = Config->Channels > 0
                             ? Tod2Rtp_Audio(&Frame->ToD, Config->SampleRate)
                             : Tod2Rtp_Video(&Frame->ToD);
        // The card's scheduler sends each frame at its time of day, so the program only
        // keeps the FIFO full: a frame the full FIFO refuses stays the program's, to
        // write again once the thread has taken one out.
        Result = AvFifo_TxFifo_Write(Fifo, Frame);
        while (Result == DTAPI_E_FIFO_FULL)
        {
            Example_SleepMs(WaitMs);
            Result = AvFifo_TxFifo_Write(Fifo, Frame);
        }
        if (Result != DTAPI_OK)
            return ExampleAv_Failed("AvFifo_TxFifo_Write", Result);

        printf("%lld:%d  frame %d  %d bytes  tod %u.%09u  rtp %u\n",
               (long long)Port->SerialNumber, Port->Port, Number, Size,
               Frame->ToD.Seconds, Frame->ToD.Nanoseconds, Frame->RtpTime);
    }

    // The thread packetizes what is left, and the card's scheduler sends it at its time.
    while (AvFifo_TxFifo_GetFifoLoad(Fifo) > 0)
        Example_SleepMs(WaitMs);
    Example_SleepMs(200);
    printf("%lld:%d  sent %d frames\n", (long long)Port->SerialNumber, Port->Port,
           AvFifo_TxFifo_GetStatistics(Fifo).FramesOk);
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AttachAndTransmit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static int AttachAndTransmit(DtDevice* Device, AvFifo_TxFifo* Fifo,
                             const DtHwFuncDesc* Port, const ExampleAvConfig* Config,
                             int Count)
{
    unsigned int Result = DtDevice_AttachToSerial(Device, Port->SerialNumber);
    if (Result != DTAPI_OK)
        return Example_Failed("DtDevice_AttachToSerial", Result);

    // An AV FIFO counts ports from 0, where a hardware function's Port counts from 1.
    Result = ExampleAv_AttachTx(Fifo, Device, Port->Port - 1, Config);
    if (Result != DTAPI_OK)
        return ExampleAv_Failed("AvFifo_TxFifo_Attach", Result);

    if (Config->Channels > 0)
    {
        St2110_TxConfigAudio Audio;
        memset(&Audio, 0, sizeof(Audio));
        Audio.Format = St2110_AudioFormat_L24BE;
        Audio.NumChannels = Config->Channels;
        Audio.NumSamplesPerIpPacket = 48;
        Audio.SampleRate = Config->SampleRate;
        Result = AvFifo_TxFifo_ConfigureAudio(Fifo, &Audio);
        if (Result != DTAPI_OK)
            return ExampleAv_Failed("AvFifo_TxFifo_ConfigureAudio", Result);
    }
    else
    {
        St2110_TxConfigVideo Video;
        memset(&Video, 0, sizeof(Video));
        Video.Format = Config->EightBit ? St2110_TxFrameFormat_Uyvy422_8b
                                        : St2110_TxFrameFormat_Uyvy422_10b;
        Video.Packing.PayloadSize = -1;
        Video.Resolution.Width = Config->Width;
        Video.Resolution.Height = Config->Height;
        Video.Timing.Rate.Numerator = Config->Rate;
        Video.Timing.Rate.Denominator = 1;
        Result = AvFifo_TxFifo_ConfigureVideo(Fifo, &Video);
        if (Result != DTAPI_OK)
            return ExampleAv_Failed("AvFifo_TxFifo_ConfigureVideo", Result);
    }

    AvFifo_IpPars Pars;
    ExampleAv_IpPars(Config, &Pars);
    Result = AvFifo_TxFifo_SetIpPars(Fifo, &Pars);
    if (Result != DTAPI_OK)
        return ExampleAv_Failed("AvFifo_TxFifo_SetIpPars", Result);

    Result = AvFifo_TxFifo_Start(Fifo);
    if (Result != DTAPI_OK)
        return ExampleAv_Failed("AvFifo_TxFifo_Start", Result);

    ExampleAv_PrintStream(Port, ExampleAv_TxPipeKind(Fifo), Config);
    int Exit = SendFrames(Fifo, Device, Port, Config, Count);
    AvFifo_TxFifo_Stop(Fifo);
    return Exit;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main(int Argc, char** Argv)
{
    int64_t Serial = 0;
    int64_t PortNumber = 0;
    int64_t Count = 3;
    ExampleAvConfig Config;
    if (!Example_CheckArguments(
            Argc, Argv, "Transmits SMPTE ST 2110 video or audio on an IP port.",
            g_Options, (int)(sizeof(g_Options) / sizeof(g_Options[0]))) ||
        !Example_Int64(Argc, Argv, "--serial", &Serial) ||
        !Example_Int64(Argc, Argv, "--port", &PortNumber) ||
        !Example_Int64(Argc, Argv, "--count", &Count) ||
        !ExampleAv_Config(Argc, Argv, &Config))
    {
        return EXAMPLE_FAILED;
    }

    DtHwFuncDesc Port;
    int Exit = EXAMPLE_FAILED;
    unsigned int Result =
        Example_FindPort(Serial, (int)PortNumber, ExampleAv_IsIpPort, &Port);
    DtDevice* Device = DtDevice_Alloc();
    AvFifo_TxFifo* Fifo = AvFifo_TxFifo_Alloc();
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
        Exit = AttachAndTransmit(Device, Fifo, &Port, &Config, (int)Count);

    AvFifo_TxFifo_Free(Fifo);
    DtDevice_Free(Device);
    return Exit;
}
