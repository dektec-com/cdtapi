// #*#*#*#*#*#*#*#*#*#*#*#*#* DtReceiveFrames.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Example: receives raw SDI frames from an input channel
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Attaches an input channel to the port, receives --count frames in the receive mode's
// symbol size, and prints one line per frame: its number, its size and a 64-bit FNV-1a
// hash of its bytes. With --out each frame is also written to <out><number>.raw. With
// --detect the channel first detects the I/O standard of the input. With --threads a
// pool of that many threads of the library's own converts the frames, which a 2160p
// input needs on a slow core:
//
//     9217800001:1  io standard HDSDI 1080I50
//
//     9217800001:1  frame 0  7425000 bytes  hash 3C0F2E6D89A1B437
//     9217800001:1  no frame within 1000 ms
//
// The port must be configured as an input for the standard it receives; DtConfigPort
// does that. Exits with 0 when every frame is received, 2 when a frame does not arrive in
// time, and 1 when a call fails or the command line is wrong.

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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Room for the largest frame a channel delivers: 2160p24 over one 6G link with 16-bit
// symbols, 49 500 000 bytes.
#define FRAME_BUFFER_SIZE (48 * 1024 * 1024)

static const ExampleOption g_Options[] = {
    {"--serial", true, "The device's serial number; the first device with an SDI input"},
    {"--port", true, "The port number; the first SDI input"},
    {"--count", true, "The number of frames to receive; 1 without it"},
    {"--rxmode", true, "8B, 10B or 16B symbols; 10B without it"},
    {"--timeout", true, "Milliseconds to wait for each frame; 1000 without it"},
    {"--out", true, "Write each frame to <out><number>.raw"},
    {"--detect", false, "First detect the input's I/O standard through the channel"},
    {"--threads", true,
     "Convert over a pool of this many threads of the library's own, two or more; one "
     "thread without it"},
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsSdiInput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool IsSdiInput(const DtHwFuncDesc* Port)
{
    return Port->IsSdi && Port->IsInput;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Fnv1a64 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static uint64_t Fnv1a64(const char* Data, int Size)
{
    uint64_t Hash = 0xCBF29CE484222325ull;

    for (int i = 0; i < Size; i++)
    {
        Hash ^= (uint8_t)Data[i];
        Hash *= 0x100000001B3ull;
    }
    return Hash;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RxModeFrom -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The receive mode for a symbol size named on the command line. False for another name.
//
static bool RxModeFrom(const char* Name, int* RxMode)
{
    if (Name == NULL || strcmp(Name, "10B") == 0)
        *RxMode = DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B;
    else if (strcmp(Name, "8B") == 0)
        *RxMode = DTAPI_RXMODE_SDI_FULL;
    else if (strcmp(Name, "16B") == 0)
        *RxMode = DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_16B;
    else
        return false;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool WriteFrame(const char* Prefix, int64_t Number, const char* Frame, int Size)
{
    char Path[1024];

    snprintf(Path, sizeof(Path), "%s%lld.raw", Prefix, (long long)Number);
    FILE* File = fopen(Path, "wb");
    if (File == NULL)
        return false;
    bool Written = fwrite(Frame, 1, (size_t)Size, File) == (size_t)Size;
    return fclose(File) == 0 && Written;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Receive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sets the channel up and receives the frames. Returns the exit code.
//
static int Receive(DtInpChannel* Channel, const DtHwFuncDesc* Port, int RxMode,
                   int64_t Count, int64_t TimeoutMs, const char* Out, char* Frame)
{
    unsigned int Result = DtInpChannel_SetRxMode(Channel, RxMode);

    if (Result != DTAPI_OK)
        return Example_Failed("DtInpChannel_SetRxMode", Result);
    Result = DtInpChannel_SetRxControl(Channel, DTAPI_RXCTRL_RCV);
    if (Result != DTAPI_OK)
        return Example_Failed("DtInpChannel_SetRxControl", Result);

    for (int64_t i = 0; i < Count; i++)
    {
        int Size = FRAME_BUFFER_SIZE;

        Result = DtInpChannel_ReadFrame(Channel, Frame, &Size, (int)TimeoutMs);
        if (Result == DTAPI_E_TIMEOUT)
        {
            printf("%s  no frame within %lld ms\n", Port->DeviceName,
                   (long long)TimeoutMs);
            return EXAMPLE_NOTHING;
        }
        if (Result != DTAPI_OK)
        {
            printf("%s  ", Port->DeviceName);
            return Example_Failed("DtInpChannel_ReadFrame", Result);
        }

        printf("%s  frame %lld  %d bytes  hash %016llX\n", Port->DeviceName, (long long)i,
               Size, (unsigned long long)Fnv1a64(Frame, Size));
        if (Out != NULL && !WriteFrame(Out, i, Frame, Size))
        {
            printf("Cannot write frame %lld to %s\n", (long long)i, Out);
            return EXAMPLE_FAILED;
        }
    }
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GivePool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Gives the channel a pool of NumThreads threads of the library's own, over which it
// converts each frame's lines. With 0 as its number of pieces the channel takes what the
// standard calls for: 4 for 2160p50 and 2160p60, 2 for 2160p24 to 2160p30, and one piece
// up to 3G, where the pool goes unused. The channel holds the pool, so the program lets
// go of its own hold at once.
//
static unsigned int GivePool(DtInpChannel* Channel, int NumThreads)
{
    DtWorkPool* Pool = DtWorkPool_Alloc();
    unsigned int Result =
        Pool == NULL ? DTAPI_E_OUT_OF_MEM : DtWorkPool_StartThreads(Pool, NumThreads);

    if (Result == DTAPI_OK)
        Result = DtInpChannel_SetWorkPool(Channel, Pool, 0);
    DtWorkPool_Freep(&Pool);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AttachAndReceive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Attaches the device and the channel to Port, gives it a pool of Threads threads when
// asked, detects the I/O standard when asked, and receives. Returns the exit code.
//
static int AttachAndReceive(DtDevice* Device, DtInpChannel* Channel, char* Frame,
                            const DtHwFuncDesc* Port, int Argc, char** Argv, int RxMode,
                            int64_t Count, int64_t TimeoutMs, int64_t Threads)
{
    unsigned int Result = DtDevice_AttachToSerial(Device, Port->SerialNumber);
    if (!Example_Succeeded(Result))
        return Example_Failed("DtDevice_AttachToSerial", Result);
    Result = DtInpChannel_AttachToPort(Channel, Device, Port->Port);
    if (!Example_Succeeded(Result))
    {
        printf("%s  ", Port->DeviceName);
        return Example_Failed("DtInpChannel_AttachToPort", Result);
    }
    Result = Threads > 0 ? GivePool(Channel, (int)Threads) : DTAPI_OK;
    if (Result != DTAPI_OK)
    {
        printf("%s  ", Port->DeviceName);
        Example_Failed("DtInpChannel_SetWorkPool", Result);
        DtInpChannel_Detach(Channel, DTAPI_INSTANT_DETACH);
        return EXAMPLE_FAILED;
    }

    if (Example_HasFlag(Argc, Argv, "--detect"))
    {
        int Value = -1;
        int SubValue = -1;

        Result = DtInpChannel_DetectIoStd(Channel, &Value, &SubValue);
        printf("%s  ", Port->DeviceName);
        if (Result != DTAPI_OK)
            Example_Failed("DtInpChannel_DetectIoStd", Result);
        else
        {
            const char* Name = Example_VidStdName(SubValue);
            printf("io standard %s %s\n", Example_IoStdName(Value),
                   Name != NULL ? Name : "?");
        }
    }

    int Exit = Receive(Channel, Port, RxMode, Count, TimeoutMs,
                       Example_Value(Argc, Argv, "--out"), Frame);
    DtInpChannel_Detach(Channel, DTAPI_INSTANT_DETACH);
    return Exit;
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
    if (!Example_CheckArguments(Argc, Argv, "Receives raw SDI frames from an SDI input.",
                                g_Options,
                                (int)(sizeof(g_Options) / sizeof(g_Options[0]))) ||
        !Example_Int64(Argc, Argv, "--serial", &Serial) ||
        !Example_Int64(Argc, Argv, "--port", &PortNumber) ||
        !Example_Int64(Argc, Argv, "--count", &Count) ||
        !Example_Int64(Argc, Argv, "--timeout", &TimeoutMs) ||
        !Example_Int64(Argc, Argv, "--threads", &Threads))
    {
        return EXAMPLE_FAILED;
    }
    int RxMode = 0;
    if (!RxModeFrom(Example_Value(Argc, Argv, "--rxmode"), &RxMode))
    {
        printf("Unknown receive mode: %s; 8B, 10B or 16B\n",
               Example_Value(Argc, Argv, "--rxmode"));
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

    DtDevice* Device = DtDevice_Alloc();
    DtInpChannel* Channel = DtInpChannel_Alloc();
    char* Frame = (char*)malloc(FRAME_BUFFER_SIZE);
    int Exit;
    if (Device == NULL || Channel == NULL || Frame == NULL)
        Exit = Example_Failed("Allocating", DTAPI_E_OUT_OF_MEM);
    else
        Exit = AttachAndReceive(Device, Channel, Frame, &Port, Argc, Argv, RxMode, Count,
                                TimeoutMs, Threads);

    DtInpChannel_Free(Channel);
    DtDevice_Free(Device);
    free(Frame);
    return Exit;
}
