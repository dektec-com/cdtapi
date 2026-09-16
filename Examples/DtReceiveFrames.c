// #*#*#*#*#*#*#*#*#*#*#*#*#* DtReceiveFrames.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Example: receives raw SDI frames from an input channel
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Attaches an input channel to the port, receives --count frames in the receive mode's
// symbol size, and prints one line per frame: its number, its size and a 64-bit FNV-1a
// hash of its bytes. With --out each frame is also written to <out><number>.raw.
//
//     9217800001:1  frame 0  6187504 bytes  hash 3C0F2E6D89A1B437
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

// Room for the largest frame a channel delivers: 1080-line 3G with 16-bit symbols.
#define FRAME_BUFFER_SIZE (16 * 1024 * 1024)

static const ExampleOption g_Options[] = {
    {"--serial", true, "The device's serial number; the first device with an SDI input"},
    {"--port", true, "The port number; the first SDI input"},
    {"--count", true, "The number of frames to receive; 1 without it"},
    {"--rxmode", true, "8B, 10B or 16B symbols; 10B without it"},
    {"--timeout", true, "Milliseconds to wait for each frame; 1000 without it"},
    {"--out", true, "Write each frame to <out><number>.raw"},
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
    int i;

    for (i = 0; i < Size; i++)
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
    FILE* File;
    bool Written;

    snprintf(Path, sizeof(Path), "%s%lld.raw", Prefix, (long long)Number);
    File = fopen(Path, "wb");
    if (File == NULL)
        return false;
    Written = fwrite(Frame, 1, (size_t)Size, File) == (size_t)Size;
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
    int64_t i;

    if (Result != DTAPI_OK)
        return ExampleFailed("DtInpChannel_SetRxMode", Result);
    Result = DtInpChannel_SetRxControl(Channel, DTAPI_RXCTRL_RCV);
    if (Result != DTAPI_OK)
        return ExampleFailed("DtInpChannel_SetRxControl", Result);

    for (i = 0; i < Count; i++)
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
            return ExampleFailed("DtInpChannel_ReadFrame", Result);
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main(int Argc, char** Argv)
{
    DtHwFuncDesc Port;
    DtDevice* Device = NULL;
    DtInpChannel* Channel = NULL;
    char* Frame = NULL;
    int64_t Serial = 0, PortNumber = 0, Count = 1, TimeoutMs = 1000;
    int RxMode = 0;
    int Exit = EXAMPLE_FAILED;
    unsigned int Result;

    if (!ExampleCheckArguments(Argc, Argv, "Receives raw SDI frames from an SDI input.",
                               g_Options,
                               (int)(sizeof(g_Options) / sizeof(g_Options[0]))) ||
        !ExampleInt64(Argc, Argv, "--serial", &Serial) ||
        !ExampleInt64(Argc, Argv, "--port", &PortNumber) ||
        !ExampleInt64(Argc, Argv, "--count", &Count) ||
        !ExampleInt64(Argc, Argv, "--timeout", &TimeoutMs))
    {
        return EXAMPLE_FAILED;
    }
    if (!RxModeFrom(ExampleValue(Argc, Argv, "--rxmode"), &RxMode))
    {
        printf("Unknown receive mode: %s; 8B, 10B or 16B\n",
               ExampleValue(Argc, Argv, "--rxmode"));
        return EXAMPLE_FAILED;
    }

    Result = ExampleFindPort(Serial, (int)PortNumber, IsSdiInput, &Port);
    if (Result == DTAPI_E_NOT_FOUND)
    {
        printf("No SDI input that suits\n");
        return EXAMPLE_NOTHING;
    }
    if (Result != DTAPI_OK)
        return ExampleFailed("DtapiHwFuncScan", Result);

    Device = DtDevice_Alloc();
    Channel = DtInpChannel_Alloc();
    Frame = (char*)malloc(FRAME_BUFFER_SIZE);
    if (Device == NULL || Channel == NULL || Frame == NULL)
    {
        Exit = ExampleFailed("Allocating", DTAPI_E_OUT_OF_MEM);
        goto Cleanup;
    }

    Result = DtDevice_AttachToSerial(Device, Port.SerialNumber);
    if (!ExampleSucceeded(Result))
    {
        Exit = ExampleFailed("DtDevice_AttachToSerial", Result);
        goto Cleanup;
    }
    Result = DtInpChannel_AttachToPort(Channel, Device, Port.Port);
    if (!ExampleSucceeded(Result))
    {
        printf("%s  ", Port.DeviceName);
        Exit = ExampleFailed("DtInpChannel_AttachToPort", Result);
        goto Cleanup;
    }

    Exit = Receive(Channel, &Port, RxMode, Count, TimeoutMs,
                   ExampleValue(Argc, Argv, "--out"), Frame);
    DtInpChannel_Detach(Channel, 1);

Cleanup:
    if (Channel != NULL)
        DtInpChannel_Free(Channel);
    if (Device != NULL)
        DtDevice_Free(Device);
    free(Frame);
    return Exit;
}
