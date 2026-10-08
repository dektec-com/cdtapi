// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtReceiveSdi.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Example: receives SDI and takes the image and the audio out of each frame
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Receives --count SDI frames from an input port and takes each one apart with the SDI
// parser. The frames are not copied: DtInpChannel_AcquireFrame() lends each frame where
// the card wrote it, the parser reads it there, and DtInpChannel_ReleaseFrame() gives it
// back. The program prints one line per frame:
//
//     9217800001:1  frame 0  1920x1080  audio 1920 samples  channels 1-2  packets 0
//
// The line gives the image size, the number of audio samples per channel, the channels
// that carry audio, and the number of other ancillary packets. At a 1001 frame rate it
// also gives the frame's place in the audio cadence, as "cadence 3". A frame of the
// examples' test pattern ends the line with its frame number, as "number 1234", and with
// "gap of 3", "repeat" or "back" when the number does not count up by one (see
// ExamplePattern.h).
//
// --out also writes the image and the audio to two files:
//   - <out>.yuv  the images, planar 4:2:2 with 10 bits in 16-bit words, which FFmpeg
//                calls yuv422p10le;
//   - <out>.wav  the audio of the first --channels channels, 24-bit PCM at 48 kHz.
// FFmpeg plays them, for example for 1080i50:
//
//     ffplay -f rawvideo -pixel_format yuv422p10le -video_size 1920x1080 -i frame.yuv
//     ffplay frame.wav
//
// --threads gives the channel and the parser a pool of that many threads, which 2160p
// needs. The port must be an input set to the signal's standard: --vidstd sets it, or
// DtConfigPort does it beforehand.
//
// Exits with 0 when every frame is received, 2 when a frame does not arrive in time, and
// 1 when a call fails or the command line is wrong.

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
#include "Common/ExampleCommon.h"  // The API and what the examples share.
#include "Common/ExamplePattern.h" // Reading the test pattern's frame number.
#include "cdtapi_sdi.h"            // The SDI parser.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The most ancillary packets, and their words, the program lists per frame.
#define MAX_PACKETS 4096
#define MAX_WORDS (MAX_PACKETS * 64)

// The most audio channels the program writes to the WAV file.
#define MAX_WAV_CHANNELS 16

static const ExampleOption g_Options[] = {
    {"--serial", true, "The device's serial number; the first device with an SDI input"},
    {"--port", true, "The port number; the first SDI input"},
    {"--vidstd", true,
     "Set the input to this video standard first, such as 1080I50; without it the input "
     "keeps the standard it has"},
    {"--linkstd", true,
     "How 4K is carried: 0 or 1 four 3G links, 2 6G, 3 12G; default -1"},
    {"--count", true, "The number of frames to receive; 1 without it"},
    {"--timeout", true, "Milliseconds to wait for each frame; 1000 without it"},
    {"--out", true, "Write the images to <out>.yuv and the audio to <out>.wav"},
    {"--channels", true,
     "The number of audio channels in <out>.wav, 1 to 16; 2 without it"},
    {"--threads", true,
     "Receive and parse over a pool of this many threads of the library's own, two or "
     "more; one thread without it"},
};

// What the program needs for receiving and taking frames apart.
typedef struct Receiver
{
    DtInpChannel* Channel;
    DtSdiParser* Parser;
    DtSdiView* View;

    // Set up for the video standard of the first frame.
    int VidStd;       // The standard of the frames; DTAPI_VIDSTD_UNKNOWN before the first
    int Width;        // The image width in pixels
    int Height;       // The image height in lines
    DtSdiImage Image; // The image of a frame, planar 10-bit
    DtSdiAudio Audio; // The audio of a frame, PCM
    int32_t* Samples[DT_SDI_AUDIO_MAX_CHANNELS]; // Room for each channel's samples
    DtSdiAncPacket Packets[MAX_PACKETS];         // The other ancillary packets
    uint16_t Words[MAX_WORDS];                   // Their user data words

    // The frame numbers of the examples' test pattern.
    ExampleNumberCheck Numbers;

    // The output files; NULL without --out.
    FILE* Yuv;
    FILE* Wav;
    int WavChannels;    // The channels written to the WAV file
    uint32_t WavFrames; // The sample frames written to the WAV file so far
} Receiver;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsSdiInput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns whether a port can be an SDI input, for Example_FindPort().
//
static bool IsSdiInput(const DtHwFuncDesc* Port)
{
    return Port->IsSdi && Port->IsInput;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutLe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Stores the lowest Bytes bytes of Value at To, least significant first.
//
static void PutLe(uint8_t* To, uint32_t Value, int Bytes)
{
    for (int i = 0; i < Bytes; i++)
        To[i] = (uint8_t)(Value >> (8 * i));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteWavHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the 44-byte header of a WAV file with Channels channels of 24-bit PCM at
// 48 kHz, holding Frames sample frames. The program writes it once with 0 frames at the
// start, and again with the real number at the end. Returns false when writing fails.
//
static bool WriteWavHeader(FILE* File, int Channels, uint32_t Frames)
{
    const uint32_t BlockAlign = (uint32_t)Channels * 3;
    const uint32_t DataBytes = Frames * BlockAlign;
    uint8_t Header[44];

    memcpy(Header, "RIFF", 4);
    PutLe(Header + 4, 36 + DataBytes, 4);
    memcpy(Header + 8, "WAVEfmt ", 8);
    PutLe(Header + 16, 16, 4);                 // The size of the format chunk
    PutLe(Header + 20, 1, 2);                  // PCM
    PutLe(Header + 22, (uint32_t)Channels, 2); // Channels
    PutLe(Header + 24, 48000, 4);              // Sample rate
    PutLe(Header + 28, 48000 * BlockAlign, 4); // Bytes per second
    PutLe(Header + 32, BlockAlign, 2);         // Bytes per sample frame
    PutLe(Header + 34, 24, 2);                 // Bits per sample
    memcpy(Header + 36, "data", 4);
    PutLe(Header + 40, DataBytes, 4);
    return fseek(File, 0, SEEK_SET) == 0 && fwrite(Header, 1, 44, File) == 44;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OpenFiles -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Opens <Out>.yuv and <Out>.wav, and writes a first WAV header. Returns false, after
// printing why, when a file cannot be opened.
//
static bool OpenFiles(Receiver* R, const char* Out)
{
    char Path[1024];

    snprintf(Path, sizeof(Path), "%s.yuv", Out);
    R->Yuv = fopen(Path, "wb");
    if (R->Yuv == NULL)
    {
        printf("Cannot write %s\n", Path);
        return false;
    }
    snprintf(Path, sizeof(Path), "%s.wav", Out);
    R->Wav = fopen(Path, "wb");
    if (R->Wav == NULL || !WriteWavHeader(R->Wav, R->WavChannels, 0))
    {
        printf("Cannot write %s\n", Path);
        return false;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CloseFiles -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the final WAV header and closes both files. Returns false when that fails.
//
static bool CloseFiles(Receiver* R)
{
    bool Ok = true;

    if (R->Wav != NULL)
    {
        Ok = WriteWavHeader(R->Wav, R->WavChannels, R->WavFrames) && Ok;
        Ok = fclose(R->Wav) == 0 && Ok;
    }
    if (R->Yuv != NULL)
        Ok = fclose(R->Yuv) == 0 && Ok;
    R->Wav = NULL;
    R->Yuv = NULL;
    return Ok;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetUp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Allocates the image and the audio buffers for the video standard of the frame that
// R->View describes. The image is planar 4:2:2 10-bit with the fields woven; the audio
// is PCM on every channel. Returns false when the standard is not known or there is no
// memory.
//
static bool SetUp(Receiver* R)
{
    int Strides[3] = {0, 0, 0};
    int MaxSamples = 0;
    if (DtSdiView_GetFormat(R->View, &R->VidStd, NULL) != DTAPI_OK ||
        DtSdiImage_GetSize(R->VidStd, DT_SDI_PIXFMT_YUV422P_10B, &R->Width, &R->Height,
                           Strides) != DTAPI_OK ||
        DtSdiAudio_MaxSamples(R->VidStd, &MaxSamples) != DTAPI_OK)
    {
        return false;
    }

    R->Image.Format = DT_SDI_PIXFMT_YUV422P_10B;
    R->Image.Fields = DT_SDI_FIELDS_WOVEN;
    for (int p = 0; p < 3; p++)
    {
        R->Image.Strides[p] = Strides[p];
        R->Image.Planes[p] = (uint8_t*)malloc((size_t)Strides[p] * (size_t)R->Height);
        if (R->Image.Planes[p] == NULL)
            return false;
    }

    // Channels 17 to 32 are reserved; the parser leaves them empty.
    for (int p = 0; p < 8; p++)
        R->Audio.Formats[p] = DT_SDI_AUDIO_PCM;
    for (int c = 0; c < 16; c++)
    {
        R->Samples[c] = (int32_t*)malloc((size_t)MaxSamples * sizeof(int32_t));
        if (R->Samples[c] == NULL)
            return false;
        R->Audio.Channels[c].Samples = R->Samples[c];
        R->Audio.Channels[c].Stride = 1;
        R->Audio.Channels[c].MaxSamples = MaxSamples;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteFrameData -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Appends the image and the first R->WavChannels audio channels of the frame just parsed
// to the output files. A channel the frame does not carry is written as silence.
// Returns false when writing fails.
//
static bool WriteFrameData(Receiver* R)
{
    // The image: each plane line by line, without the padding at the end of a line.
    const size_t Widths[3] = {(size_t)R->Width * 2, (size_t)R->Width, (size_t)R->Width};
    for (int p = 0; p < 3; p++)
    {
        for (int y = 0; y < R->Height; y++)
        {
            const uint8_t* Line =
                R->Image.Planes[p] + (size_t)y * (size_t)R->Image.Strides[p];
            if (fwrite(Line, 1, Widths[p], R->Yuv) != Widths[p])
                return false;
        }
    }

    // The audio: one sample of each channel in turn, 24 bits each. The samples are
    // int32_t with the 24 bits at the top, so the lowest byte is dropped.
    const int Count = R->Audio.Channels[0].NumSamples;
    for (int s = 0; s < Count; s++)
    {
        uint8_t Frame[3 * MAX_WAV_CHANNELS];
        for (int c = 0; c < R->WavChannels; c++)
        {
            const DtSdiAudioChannel* Ch = &R->Audio.Channels[c];
            const int32_t Sample = s < Ch->NumSamples ? R->Samples[c][s] : 0;
            PutLe(Frame + 3 * c, (uint32_t)Sample >> 8, 3);
        }
        if (fwrite(Frame, 3, (size_t)R->WavChannels, R->Wav) != (size_t)R->WavChannels)
            return false;
    }
    R->WavFrames += (uint32_t)Count;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrintFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Prints the line for frame Number, just parsed, as the file's header describes it.
//
static void PrintFrame(Receiver* R, const DtHwFuncDesc* Port, int64_t Number,
                       const DtSdiAncData* Anc)
{
    // The test pattern's frame number, from the luma of the line that carries its code.
    const int Line = ExamplePattern_CodeLine(R->Height);
    const uint16_t* Luma = (const uint16_t*)(R->Image.Planes[0] +
                                             (size_t)Line * (size_t)R->Image.Strides[0]);
    uint32_t Code = 0;
    const bool Found =
        Line < R->Height && ExamplePattern_ReadNumber(Luma, R->Width, &Code);
    char Text[64];
    ExampleNumberCheck_Describe(&R->Numbers, Found, Code, Text, sizeof(Text));

    printf("%s  frame %lld  %dx%d  audio %d samples", Port->DeviceName, (long long)Number,
           R->Width, R->Height, R->Audio.Channels[0].NumSamples);

    // The channels that carry audio, as ranges such as "1-4 9-12".
    printf("  channels");
    int First = -1;
    bool Any = false;
    for (int c = 0; c <= 16; c++)
    {
        const bool Present = c < 16 && R->Audio.Channels[c].Present;
        if (Present && First < 0)
            First = c;
        if (!Present && First >= 0)
        {
            printf(First == c - 1 ? " %d" : " %d-%d", First + 1, c);
            First = -1;
            Any = true;
        }
    }
    if (!Any)
        printf(" none");
    if (R->Audio.FrameNumber > 0)
        printf("  cadence %d", R->Audio.FrameNumber);
    printf("  packets %d%s\n", Anc->NumPackets, Text);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Receive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sets the 10-bit receive mode, starts receiving, and takes Count frames apart, waiting
// up to TimeoutMs for each. Returns the program's exit code.
//
static int Receive(Receiver* R, const DtHwFuncDesc* Port, int64_t Count,
                   int64_t TimeoutMs)
{
    // Lending frames needs 10-bit symbols.
    unsigned int Result =
        DtInpChannel_SetRxMode(R->Channel, DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B);
    if (Result != DTAPI_OK)
        return Example_Failed("DtInpChannel_SetRxMode", Result);
    Result = DtInpChannel_SetRxControl(R->Channel, DTAPI_RXCTRL_RCV);
    if (Result != DTAPI_OK)
        return Example_Failed("DtInpChannel_SetRxControl", Result);

    for (int64_t i = 0; i < Count; i++)
    {
        Result = DtInpChannel_AcquireFrame(R->Channel, R->View, (int)TimeoutMs, NULL);
        if (Result == DTAPI_E_TIMEOUT)
        {
            printf("%s  no frame within %lld ms\n", Port->DeviceName,
                   (long long)TimeoutMs);
            return EXAMPLE_NOTHING;
        }
        if (Result != DTAPI_OK)
        {
            printf("%s  ", Port->DeviceName);
            return Example_Failed("DtInpChannel_AcquireFrame", Result);
        }
        if (R->VidStd == DTAPI_VIDSTD_UNKNOWN && !SetUp(R))
        {
            DtInpChannel_ReleaseFrame(R->Channel, R->View);
            return Example_Failed("Setting up for the frame's standard",
                                  DTAPI_E_OUT_OF_MEM);
        }

        // Every ancillary packet but the audio and the payload ID, which the parser
        // lists by default, without a filter.
        DtSdiAncData Anc;
        memset(&Anc, 0, sizeof(Anc));
        Anc.Packets = R->Packets;
        Anc.MaxPackets = MAX_PACKETS;
        Anc.Words = R->Words;
        Anc.MaxWords = MAX_WORDS;
        Result = DtSdiParser_Parse(R->Parser, R->View, &R->Image, &R->Audio, &Anc);

        // The frame goes back to the card as soon as the parser is done with it.
        DtInpChannel_ReleaseFrame(R->Channel, R->View);
        if (Result != DTAPI_OK && Result != DTAPI_E_BUF_TOO_SMALL)
        {
            printf("%s  ", Port->DeviceName);
            return Example_Failed("DtSdiParser_Parse", Result);
        }

        PrintFrame(R, Port, i, &Anc);
        if (R->Yuv != NULL && !WriteFrameData(R))
        {
            printf("Cannot write frame %lld\n", (long long)i);
            return EXAMPLE_FAILED;
        }
    }
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GivePool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Gives the channel and the parser a pool of NumThreads threads. Each picks how many of
// them a frame needs (NumThreads 0): 4 for 2160p50 and 2160p60, 2 for 2160p24 to
// 2160p30, and none up to 3G-SDI. Both keep the pool, so the program releases its own
// reference at once.
//
static unsigned int GivePool(Receiver* R, int NumThreads)
{
    DtWorkerPool* Pool = DtWorkerPool_Alloc();
    unsigned int Result =
        Pool == NULL ? DTAPI_E_OUT_OF_MEM : DtWorkerPool_StartThreads(Pool, NumThreads);

    if (Result == DTAPI_OK)
        Result = DtInpChannel_SetWorkerPool(R->Channel, Pool, 0);
    if (Result == DTAPI_OK)
        Result = DtSdiParser_SetWorkerPool(R->Parser, Pool, 0);
    DtWorkerPool_Freep(&Pool);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AttachAndReceive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Attaches Device and the channel to Port, gives them a pool of Threads threads when
// asked, sets the input to VidStd unless it is DTAPI_VIDSTD_UNKNOWN, and receives the
// frames. Returns the program's exit code.
//
static int AttachAndReceive(DtDevice* Device, Receiver* R, const DtHwFuncDesc* Port,
                            int VidStd, int LinkStd, int64_t Count, int64_t TimeoutMs,
                            int64_t Threads)
{
    unsigned int Result = DtDevice_AttachToSerial(Device, Port->SerialNumber);
    if (!Example_Succeeded(Result))
        return Example_Failed("DtDevice_AttachToSerial", Result);
    Result = DtInpChannel_AttachToPort(R->Channel, Device, Port->Port);
    if (!Example_Succeeded(Result))
    {
        printf("%s  ", Port->DeviceName);
        return Example_Failed("DtInpChannel_AttachToPort", Result);
    }
    const char* What = "SetWorkerPool";
    Result = Threads > 0 ? GivePool(R, (int)Threads) : DTAPI_OK;
    if (Result == DTAPI_OK && VidStd != DTAPI_VIDSTD_UNKNOWN)
    {
        int Value = -1;
        int SubValue = -1;
        What = "DtapiVidStd2IoStd";
        Result = DtapiVidStd2IoStd(VidStd, LinkStd, &Value, &SubValue);
        if (Result == DTAPI_OK)
        {
            What = "DtInpChannel_SetIoConfig";
            Result = DtInpChannel_SetIoConfig(R->Channel, DTAPI_IOCONFIG_IOSTD, Value,
                                              SubValue, -1, -1);
        }
    }
    if (Result != DTAPI_OK)
    {
        printf("%s  ", Port->DeviceName);
        Example_Failed(What, Result);
        DtInpChannel_Detach(R->Channel, DTAPI_INSTANT_DETACH);
        return EXAMPLE_FAILED;
    }

    int Exit = Receive(R, Port, Count, TimeoutMs);
    DtInpChannel_Detach(R->Channel, DTAPI_INSTANT_DETACH);
    return Exit;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FreeReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Frees everything in R and R itself. NULL does nothing.
//
static void FreeReceiver(Receiver* R)
{
    if (R == NULL)
        return;
    for (int p = 0; p < 3; p++)
        free(R->Image.Planes[p]);
    for (int c = 0; c < DT_SDI_AUDIO_MAX_CHANNELS; c++)
        free(R->Samples[c]);
    DtSdiView_Free(R->View);
    DtSdiParser_Free(R->Parser);
    DtInpChannel_Free(R->Channel);
    free(R);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main(int Argc, char** Argv)
{
    int64_t Serial = 0;
    int64_t PortNumber = 0;
    int64_t Count = 1;
    int64_t TimeoutMs = 1000;
    int64_t Threads = 0;
    int64_t Channels = 2;
    int64_t LinkStd = -1;
    if (!Example_CheckArguments(Argc, Argv,
                                "Receives SDI, and takes the image and the audio out of "
                                "each frame.",
                                g_Options,
                                (int)(sizeof(g_Options) / sizeof(g_Options[0]))) ||
        !Example_Int64(Argc, Argv, "--serial", &Serial) ||
        !Example_Int64(Argc, Argv, "--port", &PortNumber) ||
        !Example_Int64(Argc, Argv, "--count", &Count) ||
        !Example_Int64(Argc, Argv, "--timeout", &TimeoutMs) ||
        !Example_Int64(Argc, Argv, "--threads", &Threads) ||
        !Example_Int64(Argc, Argv, "--channels", &Channels) ||
        !Example_Int64(Argc, Argv, "--linkstd", &LinkStd))
    {
        return EXAMPLE_FAILED;
    }
    int VidStd = DTAPI_VIDSTD_UNKNOWN;
    const char* VidStdName = Example_Value(Argc, Argv, "--vidstd");
    if (VidStdName != NULL && !Example_VidStdFromName(VidStdName, &VidStd))
    {
        printf("Unknown video standard: %s\n", VidStdName);
        return EXAMPLE_FAILED;
    }
    if (Channels < 1 || Channels > MAX_WAV_CHANNELS)
    {
        printf("Give --channels from 1 to %d\n", MAX_WAV_CHANNELS);
        return EXAMPLE_FAILED;
    }

    DtHwFuncDesc Port;
    unsigned int Result = Example_FindPort(Serial, (int)PortNumber, IsSdiInput, &Port);
    if (Result == DTAPI_E_NOT_FOUND)
    {
        printf("No SDI input that suits\n");
        return EXAMPLE_NOTHING;
    }
    if (Result != DTAPI_OK)
        return Example_Failed("DtapiHwFuncScan", Result);

    // The receiver is large, for the packet list, so it lives on the heap.
    DtDevice* Device = DtDevice_Alloc();
    Receiver* R = (Receiver*)calloc(1, sizeof(Receiver));
    int Exit = EXAMPLE_FAILED;
    if (R != NULL)
    {
        R->Channel = DtInpChannel_Alloc();
        R->Parser = DtSdiParser_Alloc();
        R->View = DtSdiView_Alloc();
        R->VidStd = DTAPI_VIDSTD_UNKNOWN;
        R->WavChannels = (int)Channels;
    }
    const char* Out = Example_Value(Argc, Argv, "--out");
    if (Device == NULL || R == NULL || R->Channel == NULL || R->Parser == NULL ||
        R->View == NULL)
    {
        Exit = Example_Failed("Allocating", DTAPI_E_OUT_OF_MEM);
    }
    else if (Out == NULL || OpenFiles(R, Out))
    {
        Exit = AttachAndReceive(Device, R, &Port, VidStd, (int)LinkStd, Count, TimeoutMs,
                                Threads);
        if (!CloseFiles(R) && Exit == EXAMPLE_OK)
        {
            printf("Cannot finish the files %s.yuv and %s.wav\n", Out, Out);
            Exit = EXAMPLE_FAILED;
        }
    }
    else
        CloseFiles(R);

    FreeReceiver(R);
    DtDevice_Free(Device);
    return Exit;
}
