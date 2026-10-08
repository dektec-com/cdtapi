// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtTransmitSdi.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Example: builds SDI frames from an image and audio, and transmits them
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Sends --count SDI frames of the video standard --vidstd on an output port. The SDI
// builder puts each frame together from an image and audio. The image and the audio
// come from:
//   --in       the files <in>.yuv and <in>.wav, as DtReceiveSdi --out writes them;
//              after the last image the first comes again, and so does the audio
//   otherwise  a test pattern: grey bars with a white bar that moves each frame, and a
//              1 kHz tone at -20 dBFS on channels 1 and 2
// The program prints one line per frame:
//
//     9217800001:5  frame 0  1920x1080  audio 1920 samples
//
// The YUV file holds planar 4:2:2 images with 10 bits in 16-bit words (yuv422p10le), of
// the size --vidstd gives. The WAV file holds PCM at 48 kHz with 16 or 24 bits; its first
// 16 channels are sent.
//
// --checksums makes the builder fill in the line CRCs and the packet checksums, which
// the card otherwise fills in itself. --threads gives the channel and the builder a pool
// of that many threads, which 2160p needs. --flags prints the channel's latched flags at
// the end. The program waits until the card has sent every frame before it detaches.
// The port must be an output; DtConfigPort makes it one. Exits with 0 when every frame is
// written, 2 when there is no SDI output, and 1 when a call fails or the command line is
// wrong.

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
#include "Common/ExampleCommon.h" // The API and what the examples share.
#include "cdtapi_sdi.h"           // The SDI builder.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The audio channels the program can send: four groups of four.
#define NUM_CHANNELS 16

// The most channels a WAV file may have; the program sends the first NUM_CHANNELS.
#define MAX_WAV_CHANNELS 64

// Milliseconds to wait for room in the card's buffer for a frame.
#define WRITE_TIMEOUT_MS 2000

// One period of a 1 kHz sine at 48 kHz, at -20 dBFS, as 24-bit values.
static const int32_t g_Tone[48] = {
    0,       109493,  217113,  321018,  419430,  510666,  593164,  665513,
    726475,  775007,  810278,  831684,  838861,  831684,  810278,  775007,
    726475,  665513,  593164,  510666,  419430,  321018,  217113,  109493,
    0,       -109493, -217113, -321018, -419430, -510666, -593164, -665513,
    -726475, -775007, -810278, -831684, -838861, -831684, -810278, -775007,
    -726475, -665513, -593164, -510666, -419430, -321018, -217113, -109493};

static const ExampleOption g_Options[] = {
    {"--serial", true, "The device's serial number; the first device with an SDI output"},
    {"--port", true, "The port number; the first SDI output"},
    {"--vidstd", true, "The video standard to send, such as 1080I50; required"},
    {"--linkstd", true,
     "How 4K is carried: 0 or 1 four 3G links, 2 6G, 3 12G; default -1"},
    {"--in", true, "Send the images of <in>.yuv and the audio of <in>.wav"},
    {"--count", true, "The number of frames to transmit; 1 without it"},
    {"--checksums", false, "Make the builder fill in the line CRCs and packet checksums"},
    {"--flags", false, "Print the latched flags after the last frame"},
    {"--threads", true,
     "Build and transmit over a pool of this many threads of the library's own, two or "
     "more; one thread without it"},
};

// Where the images and the audio come from.
typedef struct Source
{
    int VidStd; // The video standard of the frames
    int Width;  // The image width in pixels
    int Height; // The image height in lines

    // The image of the next frame, planar 10-bit with the fields woven.
    DtSdiImage Image;

    // The input files, or NULL for the test pattern.
    FILE* Yuv;
    FILE* Wav;
    long WavData;       // Where the samples start in the WAV file
    int WavChannels;    // The channels in the WAV file
    int WavBytes;       // Bytes per sample: 2 or 3
    int64_t ToneSample; // The test tone's next sample
} Source;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsSdiOutput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns whether a port can be an SDI output, for Example_FindPort().
//
static bool IsSdiOutput(const DtHwFuncDesc* Port)
{
    return Port->IsSdi && Port->IsOutput;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetLe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the Bytes bytes at From as a number, least significant byte first.
//
static uint32_t GetLe(const uint8_t* From, int Bytes)
{
    uint32_t Value = 0;
    for (int i = Bytes - 1; i >= 0; i--)
        Value = Value << 8 | From[i];
    return Value;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OpenWav -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Opens the WAV file Path and finds its format and its samples. Accepts PCM at 48 kHz
// with 16 or 24 bits per sample and at most MAX_WAV_CHANNELS channels. Returns false,
// after printing why, when the file cannot be read or has another format.
//
static bool OpenWav(Source* Src, const char* Path)
{
    Src->Wav = fopen(Path, "rb");
    uint8_t Head[12];
    if (Src->Wav == NULL || fread(Head, 1, 12, Src->Wav) != 12 ||
        memcmp(Head, "RIFF", 4) != 0 || memcmp(Head + 8, "WAVE", 4) != 0)
    {
        printf("Cannot read %s as a WAV file\n", Path);
        return false;
    }

    // Walk the chunks until the samples, taking the format on the way.
    bool HasFormat = false;
    for (;;)
    {
        uint8_t Chunk[8];
        if (fread(Chunk, 1, 8, Src->Wav) != 8)
            break;
        const uint32_t Size = GetLe(Chunk + 4, 4);
        if (memcmp(Chunk, "fmt ", 4) == 0 && Size >= 16)
        {
            uint8_t Format[16];
            if (fread(Format, 1, 16, Src->Wav) != 16 ||
                fseek(Src->Wav, (long)(Size - 16 + (Size & 1)), SEEK_CUR) != 0)
                break;
            const uint32_t Bits = GetLe(Format + 14, 2);
            Src->WavChannels = (int)GetLe(Format + 2, 2);
            Src->WavBytes = (int)Bits / 8;
            HasFormat = GetLe(Format, 2) == 1 && GetLe(Format + 4, 4) == 48000 &&
                        (Bits == 16 || Bits == 24) && Src->WavChannels > 0 &&
                        Src->WavChannels <= MAX_WAV_CHANNELS;
        }
        else if (memcmp(Chunk, "data", 4) == 0)
        {
            Src->WavData = ftell(Src->Wav);
            if (HasFormat)
                return true;
            break;
        }
        else if (fseek(Src->Wav, (long)(Size + (Size & 1)), SEEK_CUR) != 0)
            break;
    }
    printf("%s is not PCM at 48 kHz with 16 or 24 bits and at most %d channels\n", Path,
           MAX_WAV_CHANNELS);
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OpenSource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets up the image for VidStd, and opens <In>.yuv and <In>.wav when In is not NULL.
// Returns false, after printing why, when that fails.
//
static bool OpenSource(Source* Src, int VidStd, const char* In)
{
    int Strides[3] = {0, 0, 0};
    Src->VidStd = VidStd;
    if (DtSdiImage_GetSize(VidStd, DT_SDI_PIXFMT_YUV422P_10B, &Src->Width, &Src->Height,
                           Strides) != DTAPI_OK)
    {
        printf("Cannot build frames of video standard %s\n", Example_VidStdName(VidStd));
        return false;
    }
    Src->Image.Format = DT_SDI_PIXFMT_YUV422P_10B;
    Src->Image.Fields = DT_SDI_FIELDS_WOVEN;
    for (int p = 0; p < 3; p++)
    {
        Src->Image.Strides[p] = Strides[p];
        Src->Image.Planes[p] = (uint8_t*)malloc((size_t)Strides[p] * (size_t)Src->Height);
        if (Src->Image.Planes[p] == NULL)
        {
            printf("Out of memory\n");
            return false;
        }
    }
    if (In == NULL)
        return true;

    char Path[1024];
    snprintf(Path, sizeof(Path), "%s.yuv", In);
    Src->Yuv = fopen(Path, "rb");
    if (Src->Yuv == NULL)
    {
        printf("Cannot read %s\n", Path);
        return false;
    }
    snprintf(Path, sizeof(Path), "%s.wav", In);
    return OpenWav(Src, Path);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadImage -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the next image from the YUV file, line by line into each plane, and starts the
// file again after the last image. Returns false when the file holds no whole image.
//
static bool ReadImage(Source* Src)
{
    const size_t Widths[3] = {(size_t)Src->Width * 2, (size_t)Src->Width,
                              (size_t)Src->Width};
    for (int Attempt = 0; Attempt < 2; Attempt++)
    {
        bool Whole = true;
        for (int p = 0; p < 3 && Whole; p++)
        {
            for (int y = 0; y < Src->Height && Whole; y++)
            {
                uint8_t* Line =
                    Src->Image.Planes[p] + (size_t)y * (size_t)Src->Image.Strides[p];
                Whole = fread(Line, 1, Widths[p], Src->Yuv) == Widths[p];
            }
        }
        if (Whole)
            return true;
        rewind(Src->Yuv);
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakePattern -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Draws the test pattern of frame Number: seven grey bars from black to white, and a
// white bar a sixteenth of the width wide that moves 16 pixels each frame. The colour
// samples are all 512, no colour.
//
static void MakePattern(Source* Src, int64_t Number)
{
    const int BarWidth = Src->Width / 16;
    const int BarX = (int)((Number * 16) % Src->Width);
    for (int y = 0; y < Src->Height; y++)
    {
        uint16_t* Y =
            (uint16_t*)(Src->Image.Planes[0] + (size_t)y * (size_t)Src->Image.Strides[0]);
        uint16_t* Cb =
            (uint16_t*)(Src->Image.Planes[1] + (size_t)y * (size_t)Src->Image.Strides[1]);
        uint16_t* Cr =
            (uint16_t*)(Src->Image.Planes[2] + (size_t)y * (size_t)Src->Image.Strides[2]);
        for (int x = 0; x < Src->Width; x++)
        {
            const bool OnBar = x >= BarX && x < BarX + BarWidth;
            Y[x] = (uint16_t)(OnBar ? 940 : 64 + (x * 7 / Src->Width) * 146);
        }
        for (int x = 0; x < Src->Width / 2; x++)
            Cb[x] = Cr[x] = 512;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadAudio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Fills the first Channels arrays of Samples with Count samples each from the WAV file,
// and starts the file again when it runs out. A channel the file does not have gets
// silence. Returns false when the file holds no samples.
//
static bool ReadAudio(Source* Src, int32_t** Samples, int Channels, int Count)
{
    uint8_t Frame[3 * MAX_WAV_CHANNELS];
    const int FrameBytes = Src->WavChannels * Src->WavBytes;
    for (int s = 0; s < Count; s++)
    {
        if (fread(Frame, 1, (size_t)FrameBytes, Src->Wav) != (size_t)FrameBytes)
        {
            if (fseek(Src->Wav, Src->WavData, SEEK_SET) != 0 ||
                fread(Frame, 1, (size_t)FrameBytes, Src->Wav) != (size_t)FrameBytes)
                return false;
        }
        for (int c = 0; c < Channels; c++)
        {
            uint32_t Value = 0;
            if (c < Src->WavChannels)
                Value = GetLe(Frame + c * Src->WavBytes, Src->WavBytes)
                        << (32 - 8 * Src->WavBytes);
            Samples[c][s] = (int32_t)Value;
        }
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeTone -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Fills Samples[0] and Samples[1] with the next Count samples of the 1 kHz test tone.
// The samples are int32_t with the 24 bits at the top.
//
static void MakeTone(Source* Src, int32_t** Samples, int Count)
{
    for (int s = 0; s < Count; s++)
    {
        const int32_t Value =
            (int32_t)((uint32_t)g_Tone[(Src->ToneSample + s) % 48] << 8);
        Samples[0][s] = Value;
        Samples[1][s] = Value;
    }
    Src->ToneSample += Count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BuildAndWrite -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Builds frame Number into Frame and writes it to the channel. The builder says how many
// audio samples per channel this frame takes, which varies at a 1001 frame rate.
// Returns the program's exit code.
//
static int BuildAndWrite(DtOutpChannel* Channel, DtSdiBuilder* Builder, DtSdiView* View,
                         Source* Src, int32_t** Samples, uint8_t* Frame, size_t FrameSize,
                         const DtHwFuncDesc* Port, int64_t Number)
{
    int NumSamples = 0;
    unsigned int Result =
        DtSdiBuilder_GetNumAudioSamples(Builder, Src->VidStd, 0, &NumSamples);
    if (Result != DTAPI_OK)
        return Example_Failed("DtSdiBuilder_GetNumAudioSamples", Result);

    // The image and the audio of this frame.
    const int Channels =
        Src->Wav != NULL
            ? (Src->WavChannels < NUM_CHANNELS ? Src->WavChannels : NUM_CHANNELS)
            : 2;
    if (Src->Yuv != NULL)
    {
        if (!ReadImage(Src) || !ReadAudio(Src, Samples, Channels, NumSamples))
        {
            printf("The input files hold no whole image or no audio\n");
            return EXAMPLE_FAILED;
        }
    }
    else
    {
        MakePattern(Src, Number);
        MakeTone(Src, Samples, NumSamples);
    }
    DtSdiAudio Audio;
    memset(&Audio, 0, sizeof(Audio));
    for (int c = 0; c < Channels; c++)
    {
        Audio.Formats[c / 2] = DT_SDI_AUDIO_PCM;
        Audio.Channels[c].Samples = Samples[c];
        Audio.Channels[c].Stride = 1;
        Audio.Channels[c].NumSamples = NumSamples;
    }

    // Build the frame in the program's buffer, then hand it to the channel.
    Result = DtSdiView_SetRawFrame(View, Frame, FrameSize, Src->VidStd, 10);
    if (Result == DTAPI_OK)
        Result = DtSdiBuilder_Build(Builder, View, &Src->Image, &Audio, NULL);
    if (Result != DTAPI_OK)
        return Example_Failed("DtSdiBuilder_Build", Result);
    Result = DtOutpChannel_WriteFrame(Channel, Frame, (int)FrameSize, WRITE_TIMEOUT_MS);
    printf("%s  ", Port->DeviceName);
    if (Result != DTAPI_OK)
        return Example_Failed("DtOutpChannel_WriteFrame", Result);
    printf("frame %lld  %dx%d  audio %d samples\n", (long long)Number, Src->Width,
           Src->Height, NumSamples);
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Transmit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the first frame while the channel holds, then starts sending and writes the
// other frames. Returns the program's exit code.
//
static int Transmit(DtOutpChannel* Channel, DtSdiBuilder* Builder, Source* Src,
                    const DtHwFuncDesc* Port, int64_t Count, bool Flags)
{
    size_t FrameSize = 0;
    DtSdiView* View = DtSdiView_Alloc();
    int32_t* Samples[NUM_CHANNELS] = {NULL};
    int MaxSamples = 0;
    unsigned int Result = DtSdiView_RawFrameSize(Src->VidStd, 10, &FrameSize);
    if (Result == DTAPI_OK)
        Result = DtSdiAudio_MaxSamples(Src->VidStd, &MaxSamples);
    uint8_t* Frame = Result == DTAPI_OK ? (uint8_t*)malloc(FrameSize) : NULL;
    bool Allocated = View != NULL && Frame != NULL;
    for (int c = 0; c < NUM_CHANNELS && Allocated; c++)
    {
        Samples[c] = (int32_t*)malloc((size_t)MaxSamples * sizeof(int32_t));
        Allocated = Samples[c] != NULL;
    }

    int Exit = EXAMPLE_OK;
    if (!Allocated)
        Exit = Example_Failed("Allocating", DTAPI_E_OUT_OF_MEM);
    else
    {
        Result = DtOutpChannel_SetTxControl(Channel, DTAPI_TXCTRL_HOLD);
        if (Result != DTAPI_OK)
        {
            printf("%s  ", Port->DeviceName);
            Exit = Example_Failed("DtOutpChannel_SetTxControl", Result);
        }
    }
    for (int64_t i = 0; i < Count && Exit == EXAMPLE_OK; i++)
    {
        Exit = BuildAndWrite(Channel, Builder, View, Src, Samples, Frame, FrameSize, Port,
                             i);
        if (Exit == EXAMPLE_OK && i == 0)
        {
            Result = DtOutpChannel_SetTxControl(Channel, DTAPI_TXCTRL_SEND);
            if (Result != DTAPI_OK)
            {
                printf("%s  ", Port->DeviceName);
                Exit = Example_Failed("DtOutpChannel_SetTxControl", Result);
            }
        }
    }

    if (Exit == EXAMPLE_OK && Flags)
    {
        int Status = 0;
        int Latched = 0;
        Result = DtOutpChannel_GetFlags(Channel, &Status, &Latched);
        printf("%s  ", Port->DeviceName);
        if (Result != DTAPI_OK)
            Exit = Example_Failed("DtOutpChannel_GetFlags", Result);
        else
            printf(
                "latched%s%s%s\n", (Latched & DTAPI_TX_FIFO_UFL) != 0 ? " FIFO_UFL" : "",
                (Latched & DTAPI_TX_DMA_UFL) != 0 ? " DMA_UFL" : "",
                (Latched & (DTAPI_TX_FIFO_UFL | DTAPI_TX_DMA_UFL)) == 0 ? " none" : "");
    }

    for (int c = 0; c < NUM_CHANNELS; c++)
        free(Samples[c]);
    free(Frame);
    DtSdiView_Free(View);
    return Exit;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GivePool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Gives the channel and the builder a pool of NumThreads threads. Each picks how many of
// them a frame needs (NumThreads 0): 4 for 2160p50 and 2160p60, 2 for 2160p24 to
// 2160p30, and none up to 3G-SDI. Both keep the pool, so the program releases its own
// reference at once.
//
static unsigned int GivePool(DtOutpChannel* Channel, DtSdiBuilder* Builder,
                             int NumThreads)
{
    DtWorkerPool* Pool = DtWorkerPool_Alloc();
    unsigned int Result =
        Pool == NULL ? DTAPI_E_OUT_OF_MEM : DtWorkerPool_StartThreads(Pool, NumThreads);

    if (Result == DTAPI_OK)
        Result = DtOutpChannel_SetWorkerPool(Channel, Pool, 0);
    if (Result == DTAPI_OK)
        Result = DtSdiBuilder_SetWorkerPool(Builder, Pool, 0);
    DtWorkerPool_Freep(&Pool);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AttachAndTransmit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Attaches Device and Channel to Port, gives the channel and the builder a pool when
// asked, sets the I/O standard and the 10-bit transmit mode, and sends. The transmit mode
// comes last because changing the standard between SDI and ASI resets it. Returns the
// program's exit code.
//
static int AttachAndTransmit(DtDevice* Device, DtOutpChannel* Channel,
                             DtSdiBuilder* Builder, const DtHwFuncDesc* Port, int LinkStd,
                             Source* Src, int64_t Count, bool Flags, int64_t Threads)
{
    unsigned int Result = DtDevice_AttachToSerial(Device, Port->SerialNumber);
    if (!Example_Succeeded(Result))
        return Example_Failed("DtDevice_AttachToSerial", Result);
    Result = DtOutpChannel_AttachToPort(Channel, Device, Port->Port);
    if (!Example_Succeeded(Result))
    {
        printf("%s  ", Port->DeviceName);
        return Example_Failed("DtOutpChannel_AttachToPort", Result);
    }

    const char* What = "SetWorkerPool";
    Result = Threads > 0 ? GivePool(Channel, Builder, (int)Threads) : DTAPI_OK;
    int Value = -1;
    int SubValue = -1;
    if (Result == DTAPI_OK)
    {
        What = "DtapiVidStd2IoStd";
        Result = DtapiVidStd2IoStd(Src->VidStd, LinkStd, &Value, &SubValue);
    }
    if (Result == DTAPI_OK)
    {
        What = "DtOutpChannel_SetIoConfig";
        Result = DtOutpChannel_SetIoConfig(Channel, DTAPI_IOCONFIG_IOSTD, Value, SubValue,
                                           -1, -1);
    }
    if (Result == DTAPI_OK)
    {
        What = "DtOutpChannel_SetTxMode";
        Result = DtOutpChannel_SetTxMode(Channel,
                                         DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B, 0);
    }
    if (Result != DTAPI_OK)
    {
        printf("%s  ", Port->DeviceName);
        Example_Failed(What, Result);
        DtOutpChannel_Detach(Channel, DTAPI_INSTANT_DETACH);
        return EXAMPLE_FAILED;
    }

    int Exit = Transmit(Channel, Builder, Src, Port, Count, Flags);
    Result = DtOutpChannel_Detach(Channel, Exit == EXAMPLE_OK ? DTAPI_WAIT_UNTIL_SENT
                                                              : DTAPI_INSTANT_DETACH);
    if (Exit == EXAMPLE_OK && Result != DTAPI_OK)
    {
        printf("%s  ", Port->DeviceName);
        Exit = Example_Failed("DtOutpChannel_Detach", Result);
    }
    return Exit;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CloseSource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Closes the input files and frees the image.
//
static void CloseSource(Source* Src)
{
    if (Src->Yuv != NULL)
        fclose(Src->Yuv);
    if (Src->Wav != NULL)
        fclose(Src->Wav);
    for (int p = 0; p < 3; p++)
        free(Src->Image.Planes[p]);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main(int Argc, char** Argv)
{
    int64_t Serial = 0;
    int64_t PortNumber = 0;
    int64_t Count = 1;
    int64_t LinkStd = -1;
    int64_t Threads = 0;
    if (!Example_CheckArguments(
            Argc, Argv,
            "Builds SDI frames from an image and audio, and transmits "
            "them on an SDI output.",
            g_Options, (int)(sizeof(g_Options) / sizeof(g_Options[0]))) ||
        !Example_Int64(Argc, Argv, "--serial", &Serial) ||
        !Example_Int64(Argc, Argv, "--port", &PortNumber) ||
        !Example_Int64(Argc, Argv, "--count", &Count) ||
        !Example_Int64(Argc, Argv, "--linkstd", &LinkStd) ||
        !Example_Int64(Argc, Argv, "--threads", &Threads))
    {
        return EXAMPLE_FAILED;
    }
    const char* VidStdName = Example_Value(Argc, Argv, "--vidstd");
    int VidStd = DTAPI_VIDSTD_UNKNOWN;
    if (VidStdName == NULL)
    {
        printf("Give --vidstd, such as 1080I50\n");
        return EXAMPLE_FAILED;
    }
    if (!Example_VidStdFromName(VidStdName, &VidStd))
    {
        printf("Unknown video standard: %s\n", VidStdName);
        return EXAMPLE_FAILED;
    }

    Source Src;
    memset(&Src, 0, sizeof(Src));
    if (!OpenSource(&Src, VidStd, Example_Value(Argc, Argv, "--in")))
    {
        CloseSource(&Src);
        return EXAMPLE_FAILED;
    }

    DtHwFuncDesc Port;
    int Exit = EXAMPLE_FAILED;
    unsigned int Result = Example_FindPort(Serial, (int)PortNumber, IsSdiOutput, &Port);
    DtDevice* Device = DtDevice_Alloc();
    DtOutpChannel* Channel = DtOutpChannel_Alloc();
    DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
    if (Result == DTAPI_E_NOT_FOUND)
    {
        printf("No SDI output that suits\n");
        Exit = EXAMPLE_NOTHING;
    }
    else if (Result != DTAPI_OK)
        Exit = Example_Failed("DtapiHwFuncScan", Result);
    else if (Device == NULL || Channel == NULL || Builder == NULL)
        Exit = Example_Failed("Allocating", DTAPI_E_OUT_OF_MEM);
    else
    {
        if (Example_HasFlag(Argc, Argv, "--checksums"))
            DtSdiBuilder_SetChecksums(Builder, true);
        Exit = AttachAndTransmit(Device, Channel, Builder, &Port, (int)LinkStd, &Src,
                                 Count, Example_HasFlag(Argc, Argv, "--flags"), Threads);
    }

    DtSdiBuilder_Free(Builder);
    DtOutpChannel_Free(Channel);
    DtDevice_Free(Device);
    CloseSource(&Src);
    return Exit;
}
