// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtTransmitTs.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Example: transmits a transport stream on a DVB-ASI output
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Sends a transport stream on a DVB-ASI output, at --rate bits per second. It sets the
// port to ASI first when it is not. What it sends:
//   --file      a file, once, or over and over with --loop
//   --stream numbered
//               packets that carry their number, for DtReceiveTs --check
//   --stream service
//               an MPEG-2 test picture that a player shows
// It sends --count packets, or without --count the file once or the stream until the
// program is stopped. Once a second it prints the packets written, how full the channel's
// FIFO is, and the problems that occurred; at the end, what was sent:
//
//     9217800001:5  io standard ASI
//     9217800001:5  26596 packets  load 2097152  flags -
//     9217800001:5  sent 26596 packets
//
// --generate writes the stream to a file instead, e.g. for DekTec's DtPlay; without
// --count, ten seconds of it:
//
//     wrote 66489 packets to numbered.ts
//
// The port must be an output; DtConfigPort --output makes it one. Exits with 0 when
// everything is sent, 2 when no port suits, and 1 when a call fails or the command line
// is wrong.

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
#include "Common/ExampleCommon.h"   // The API and what the examples share.
#include "Common/ExampleTsStream.h" // The streams the program makes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The source +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// How many bytes one Write passes: a whole number of both 188- and 204-byte packets.
#define CHUNK_SIZE (188 * 204)

// What the program sends.
typedef enum SourceKind
{
    SOURCE_NUMBERED,
    SOURCE_SERVICE,
    SOURCE_FILE
} SourceKind;

// What the program sends, and how far it is.
typedef struct Source
{
    SourceKind Kind;
    int PacketSize;          // Of the stream, for counting packets
    uint64_t Number;         // The number of the next numbered packet
    ExampleTsStream Service; // The test service
    FILE* File;              // The file to send
    bool Loop;               // Start the file again at its end
} Source;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Fill -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Puts up to Max bytes of the stream in Buffer, a multiple of 4 as Write takes them, and
// returns how many: 0 at the end of the file. The last bytes of a file whose size is not
// a multiple of 4 are not sent.
//
static int Fill(Source* Src, uint8_t* Buffer, int Max)
{
    int Size = 0;

    switch (Src->Kind)
    {
    case SOURCE_NUMBERED:
        for (; Size + Src->PacketSize <= Max; Size += Src->PacketSize)
            ExampleTs_Numbered(Src->Number++, Src->PacketSize, Buffer + Size);
        break;
    case SOURCE_SERVICE:
        for (; Size + 188 <= Max; Size += 188)
            ExampleTsStream_Next(&Src->Service, Buffer + Size);
        break;
    case SOURCE_FILE:
        while (Size < Max)
        {
            size_t Got = fread(Buffer + Size, 1, (size_t)(Max - Size), Src->File);
            Size += (int)Got;
            if (Got == 0)
            {
                if (!Src->Loop || ftell(Src->File) == 0)
                    break;
                rewind(Src->File);
            }
        }
        Size -= Size % 4;
        break;
    }
    return Size;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static const ExampleOption g_Options[] = {
    {"--serial", true, "The device's serial number; the first device with an ASI output"},
    {"--port", true, "The port number; the first ASI output"},
    {"--txmode", true, "188, 204, ADD16 (188 plus 16), MIN16 (204 less 16) or RAW; 188"},
    {"--rate", true, "Bits a second of 188-byte packets; 10000000 without it"},
    {"--stuff", false, "Fill the output with null packets when the stream falls behind"},
    {"--file", true, "Transmit this file"},
    {"--loop", false, "Transmit the file over and over"},
    {"--stream", true,
     "Transmit a stream the program makes: numbered or service; numbered without --file"},
    {"--count", true,
     "Packets to send or write; without it the file once, or the stream until stopped"},
    {"--generate", true,
     "Write the stream the program makes to this file, and stop; 10 s without --count"},
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsAsiOutput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns whether a port can be an ASI output, for Example_FindPort().
//
static bool IsAsiOutput(const DtHwFuncDesc* Port)
{
    return Port->IsAsi && Port->IsOutput;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TxModeFrom -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets *TxMode to the transmit mode --txmode names ("188", also when not given, "204",
// "MIN16", "ADD16" or "RAW"), and *PacketSize to the size of the packets the program
// writes in it. Returns false for another name.
//
static bool TxModeFrom(const char* Name, int* TxMode, int* PacketSize)
{
    *PacketSize = 188;
    if (Name == NULL || strcmp(Name, "188") == 0)
        *TxMode = DTAPI_TXMODE_188;
    else if (strcmp(Name, "204") == 0 || strcmp(Name, "MIN16") == 0)
    {
        *TxMode = Name[0] == '2' ? DTAPI_TXMODE_204 : DTAPI_TXMODE_MIN16;
        *PacketSize = 204;
    }
    else if (strcmp(Name, "ADD16") == 0)
        *TxMode = DTAPI_TXMODE_ADD16;
    else if (strcmp(Name, "RAW") == 0)
        *TxMode = DTAPI_TXMODE_RAW;
    else
        return false;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrintStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Prints a line with what the channel reports. It clears the latched flags, so that
// each line shows the problems since the line before.
//
static void PrintStatus(DtOutpChannel* Channel, const DtHwFuncDesc* Port, int64_t Packets)
{
    int Load = 0;
    int Status = 0, Latched = 0;

    DtOutpChannel_GetFifoLoad(Channel, &Load);
    DtOutpChannel_GetFlags(Channel, &Status, &Latched);
    DtOutpChannel_ClearFlags(Channel, Latched);

    printf("%s  %lld packets  load %d  flags %s%s%s\n", Port->DeviceName,
           (long long)Packets, Load,
           (Latched & (DTAPI_TX_FIFO_UFL | DTAPI_TX_SYNC_ERR)) == 0 ? "-" : "",
           (Latched & DTAPI_TX_FIFO_UFL) != 0 ? "underflow " : "",
           (Latched & DTAPI_TX_SYNC_ERR) != 0 ? "sync-error" : "");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Transmit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets the transmit mode and rate, and sends. The channel first holds while the first
// tenth of a second of the stream is written (or all of it, if shorter), so that it has
// data when it starts sending. Returns the program's exit code.
//
static int Transmit(DtOutpChannel* Channel, const DtHwFuncDesc* Port, int TxMode,
                    bool Stuff, int64_t Rate, int64_t Count, Source* Src, uint8_t* Buffer)
{
    unsigned int Result = DtOutpChannel_SetTxMode(Channel, TxMode, Stuff ? 1 : 0);
    if (Result != DTAPI_OK)
        return Example_Failed("DtOutpChannel_SetTxMode", Result);
    Result = DtOutpChannel_SetTsRateBps(Channel, (int)Rate);
    if (Result != DTAPI_OK)
        return Example_Failed("DtOutpChannel_SetTsRateBps", Result);
    Result = DtOutpChannel_SetTxControl(Channel, DTAPI_TXCTRL_HOLD);
    if (Result != DTAPI_OK)
        return Example_Failed("DtOutpChannel_SetTxControl", Result);

    int64_t Bytes = 0;
    const int64_t Limit = Count * Src->PacketSize;
    const int64_t Prefill = Rate / 8 / 10;
    bool Sending = false;
    int64_t NextStatus = Example_NowMs() + 1000;
    for (;;)
    {
        int Max = CHUNK_SIZE;
        if (Count != 0 && Limit - Bytes < Max)
            Max = (int)(Limit - Bytes);
        int Size = Max > 0 ? Fill(Src, Buffer, Max) : 0;
        if (Size > 0)
        {
            Result = DtOutpChannel_Write(Channel, Buffer, Size);
            if (Result != DTAPI_OK)
                return Example_Failed("DtOutpChannel_Write", Result);
            Bytes += Size;
        }

        if (!Sending && Bytes > 0 && (Bytes >= Prefill || Size == 0))
        {
            Result = DtOutpChannel_SetTxControl(Channel, DTAPI_TXCTRL_SEND);
            if (Result != DTAPI_OK)
                return Example_Failed("DtOutpChannel_SetTxControl", Result);
            Sending = true;
        }
        if (Size == 0)
            break;
        if (Example_NowMs() >= NextStatus)
        {
            PrintStatus(Channel, Port, Bytes / Src->PacketSize);
            NextStatus += 1000;
        }
    }
    if (!Sending)
    {
        printf("Nothing to send\n");
        return EXAMPLE_FAILED;
    }

    // What was written is sent before the channel lets go.
    Result = DtOutpChannel_Detach(Channel, DTAPI_WAIT_UNTIL_SENT);
    if (Result != DTAPI_OK)
        return Example_Failed("DtOutpChannel_Detach", Result);
    printf("%s  sent %lld packets\n", Port->DeviceName,
           (long long)(Bytes / Src->PacketSize));
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AttachAndTransmit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Attaches Device and Channel to Port, sets the port to ASI, and sends. Returns the
// program's exit code.
//
static int AttachAndTransmit(DtDevice* Device, DtOutpChannel* Channel, uint8_t* Buffer,
                             const DtHwFuncDesc* Port, int TxMode, bool Stuff,
                             int64_t Rate, int64_t Count, Source* Src)
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

    // The channel follows the port's I/O standard, from SDI to ASI too.
    int Value = -1;
    Result = DtOutpChannel_GetIoConfig(Channel, DTAPI_IOCONFIG_IOSTD, &Value, NULL, NULL,
                                       NULL);
    if (Result == DTAPI_OK && Value != DTAPI_IOCONFIG_ASI)
        Result = DtOutpChannel_SetIoConfig(Channel, DTAPI_IOCONFIG_IOSTD,
                                           DTAPI_IOCONFIG_ASI, -1, -1, -1);
    if (Result != DTAPI_OK)
    {
        printf("%s  ", Port->DeviceName);
        return Example_Failed("DtOutpChannel_SetIoConfig", Result);
    }
    printf("%s  io standard ASI\n", Port->DeviceName);

    // The output sends K28.5 from the attach on, and the receiver at the other end of the
    // cable needs a moment to lock to it; a stream sent at once loses its first tens of
    // milliseconds.
    Example_SleepMs(200);

    int Exit = Transmit(Channel, Port, TxMode, Stuff, Rate, Count, Src, Buffer);
    if (Exit != EXAMPLE_OK)
        DtOutpChannel_Detach(Channel, DTAPI_INSTANT_DETACH);
    return Exit;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Generate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes Count packets of the stream to the file Name. Returns the program's exit code.
//
static int Generate(const char* Name, Source* Src, int64_t Count, uint8_t* Buffer)
{
    FILE* File = fopen(Name, "wb");
    if (File == NULL)
    {
        printf("Cannot write %s\n", Name);
        return EXAMPLE_FAILED;
    }
    bool Written = true;
    for (int64_t Done = 0; Done < Count && Written;)
    {
        int64_t Packets = Count - Done;
        if (Packets > CHUNK_SIZE / Src->PacketSize)
            Packets = CHUNK_SIZE / Src->PacketSize;
        int Size = Fill(Src, Buffer, (int)Packets * Src->PacketSize);
        Written = fwrite(Buffer, 1, (size_t)Size, File) == (size_t)Size;
        Done += Packets;
    }
    if (fclose(File) != 0 || !Written)
    {
        printf("Cannot write %s\n", Name);
        return EXAMPLE_FAILED;
    }
    printf("wrote %lld packets to %s\n", (long long)Count, Name);
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OpenSource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets up *Src from the command line: opens the file, or prepares the stream. Returns
// false, after printing why, when that is not possible.
//
static bool OpenSource(int Argc, char** Argv, int PacketSize, int64_t Rate, Source* Src)
{
    const char* FileName = Example_Value(Argc, Argv, "--file");
    const char* Stream = Example_Value(Argc, Argv, "--stream");

    memset(Src, 0, sizeof(*Src));
    Src->PacketSize = PacketSize;
    Src->Loop = Example_HasFlag(Argc, Argv, "--loop");
    if (FileName != NULL && (Stream != NULL || Example_Value(Argc, Argv, "--generate")))
    {
        printf("Give --file, or --stream or --generate, not both\n");
        return false;
    }
    if (FileName != NULL)
    {
        Src->Kind = SOURCE_FILE;
        Src->File = fopen(FileName, "rb");
        if (Src->File == NULL)
            printf("Cannot read %s\n", FileName);
        return Src->File != NULL;
    }
    if (Stream == NULL || strcmp(Stream, "numbered") == 0)
    {
        Src->Kind = SOURCE_NUMBERED;
        return true;
    }
    if (strcmp(Stream, "service") != 0)
    {
        printf("Unknown stream: %s; numbered or service\n", Stream);
        return false;
    }
    if (PacketSize != 188)
    {
        printf("The test service has 188-byte packets; --txmode 188, ADD16 or RAW\n");
        return false;
    }
    Src->Kind = SOURCE_SERVICE;
    if (!ExampleTsStream_Init(&Src->Service, Rate))
    {
        printf("The test service needs a rate from %d to %d\n", EXAMPLE_TS_MIN_RATE,
               EXAMPLE_TS_MAX_RATE);
        return false;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CloseSource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Closes the file of *Src, or frees its stream.
//
static void CloseSource(Source* Src)
{
    if (Src->Kind == SOURCE_SERVICE)
        ExampleTsStream_Close(&Src->Service);
    if (Src->File != NULL)
        fclose(Src->File);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main(int Argc, char** Argv)
{
    int64_t Serial = 0;
    int64_t PortNumber = 0;
    int64_t Rate = 10000000;
    int64_t Count = 0;
    if (!Example_CheckArguments(Argc, Argv,
                                "Transmits a transport stream on a DVB-ASI output, or "
                                "writes one to a file.",
                                g_Options,
                                (int)(sizeof(g_Options) / sizeof(g_Options[0]))) ||
        !Example_Int64(Argc, Argv, "--serial", &Serial) ||
        !Example_Int64(Argc, Argv, "--port", &PortNumber) ||
        !Example_Int64(Argc, Argv, "--rate", &Rate) ||
        !Example_Int64(Argc, Argv, "--count", &Count))
    {
        return EXAMPLE_FAILED;
    }
    int TxMode = 0;
    int PacketSize = 0;
    if (!TxModeFrom(Example_Value(Argc, Argv, "--txmode"), &TxMode, &PacketSize))
    {
        printf("Unknown transmit mode: %s; 188, 204, ADD16, MIN16 or RAW\n",
               Example_Value(Argc, Argv, "--txmode"));
        return EXAMPLE_FAILED;
    }
    if (Rate <= 0 || Rate > 0x7FFFFFFF || Count < 0)
    {
        printf("Option --rate needs a positive number, and --count one from 0\n");
        return EXAMPLE_FAILED;
    }

    Source Src;
    if (!OpenSource(Argc, Argv, PacketSize, Rate, &Src))
        return EXAMPLE_FAILED;
    uint8_t* Buffer = (uint8_t*)malloc(CHUNK_SIZE);
    int Exit = EXAMPLE_FAILED;

    const char* GenerateName = Example_Value(Argc, Argv, "--generate");
    if (Buffer == NULL)
        Exit = Example_Failed("Allocating", DTAPI_E_OUT_OF_MEM);
    else if (GenerateName != NULL)
    {
        // Ten seconds of the stream, unless told otherwise.
        if (Count == 0)
            Count = Rate * 10 / (8 * Src.PacketSize);
        Exit = Generate(GenerateName, &Src, Count, Buffer);
    }
    else
    {
        DtHwFuncDesc Port;
        unsigned int Result =
            Example_FindPort(Serial, (int)PortNumber, IsAsiOutput, &Port);
        DtDevice* Device = DtDevice_Alloc();
        DtOutpChannel* Channel = DtOutpChannel_Alloc();
        if (Result == DTAPI_E_NOT_FOUND)
        {
            printf("No ASI output that suits\n");
            Exit = EXAMPLE_NOTHING;
        }
        else if (Result != DTAPI_OK)
            Exit = Example_Failed("DtapiHwFuncScan", Result);
        else if (Device == NULL || Channel == NULL)
            Exit = Example_Failed("Allocating", DTAPI_E_OUT_OF_MEM);
        else
            Exit = AttachAndTransmit(Device, Channel, Buffer, &Port, TxMode,
                                     Example_HasFlag(Argc, Argv, "--stuff"), Rate, Count,
                                     &Src);
        DtOutpChannel_Free(Channel);
        DtDevice_Free(Device);
    }

    CloseSource(&Src);
    free(Buffer);
    return Exit;
}
