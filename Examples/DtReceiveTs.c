// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtReceiveTs.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Example: receives a transport stream from a DVB-ASI input
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Attaches an input channel to the port, sets the port's I/O standard to ASI when it is
// another, and receives --count packets in the receive mode, or until the program is
// stopped. With --out the stream is written to a file; with --check each packet must be
// the next of DtTransmitTs's numbered packets. Once a second, and at the end, it prints
// the packets received, the rate the card measures, the packet size it finds, whether
// it is locked, and the flags that were raised:
//
//     9217800001:1  io standard ASI
//     9217800001:1  26596 packets  rate 40000000  packets of 188  lock  flags -
//     9217800001:1  checked 26596 packets  first 0  gaps 0  bad 0
//
// The port must be an input; DtConfigPort --input makes it one. Exits with 0 when every
// packet is received, and checks when asked, 2 when the stream stops arriving or no port
// suits, and 1 when a call fails, a check fails or the command line is wrong.

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
#include "Common/ExampleTsStream.h" // Numbered packets.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// What one Read asks for: a whole number of 188- and of 204-byte packets, and a tenth of
// a second at about 3 Mbit/s.
#define CHUNK_SIZE (188 * 204)

static const ExampleOption g_Options[] = {
    {"--serial", true, "The device's serial number; the first device with an ASI input"},
    {"--port", true, "The port number; the first ASI input"},
    {"--rxmode", true, "188, 204, MP2 (204 to 188) or RAW; 188 without it"},
    {"--count", true, "The number of packets to receive; until stopped without it"},
    {"--timeout", true,
     "Milliseconds to wait for each part of the stream; 1000 without it"},
    {"--out", true, "Write the stream to this file"},
    {"--check", false, "Check that the packets are DtTransmitTs's numbered packets"},
};

// What was received, and what the check found.
typedef struct Tally
{
    int64_t Packets;
    bool Check;
    uint64_t Expected; // The number the next packet should have
    int64_t Gaps;      // Packets whose number is not the one expected
    int64_t Bad;       // Packets that are not numbered packets
    uint64_t First;
} Tally;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsAsiInput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool IsAsiInput(const DtHwFuncDesc* Port)
{
    return Port->IsAsi && Port->IsInput;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RxModeFrom -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The receive mode for a name on the command line, and the size of the packets it
// delivers. False for another name.
//
static bool RxModeFrom(const char* Name, int* RxMode, int* PacketSize)
{
    *PacketSize = 188;
    if (Name == NULL || strcmp(Name, "188") == 0)
        *RxMode = DTAPI_RXMODE_ST188;
    else if (strcmp(Name, "204") == 0)
    {
        *RxMode = DTAPI_RXMODE_ST204;
        *PacketSize = 204;
    }
    else if (strcmp(Name, "MP2") == 0)
        *RxMode = DTAPI_RXMODE_STMP2;
    else if (strcmp(Name, "RAW") == 0)
        *RxMode = DTAPI_RXMODE_STRAW;
    else
        return false;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrintStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// One line of what the channel reports. The latched flags are cleared, so that each
// line shows what happened since the one before.
//
static void PrintStatus(DtInpChannel* Channel, const DtHwFuncDesc* Port,
                        const Tally* Counts)
{
    int Rate = 0;
    int PacketSize = DTAPI_PCKSIZE_INV;
    int NumInv = 0, ClkDet = 0, AsiLock = 0, RateOk = 0, AsiInv = 0;
    int Flags = 0, Latched = 0;

    DtInpChannel_GetTsRateBps(Channel, &Rate);
    DtInpChannel_GetStatus(Channel, &PacketSize, &NumInv, &ClkDet, &AsiLock, &RateOk,
                           &AsiInv);
    DtInpChannel_GetFlags(Channel, &Flags, &Latched);
    DtInpChannel_ClearFlags(Channel, Latched);

    printf("%s  %lld packets  rate %d  packets of %s  %s  flags %s%s%s\n",
           Port->DeviceName, (long long)Counts->Packets, Rate,
           PacketSize == DTAPI_PCKSIZE_188   ? "188"
           : PacketSize == DTAPI_PCKSIZE_204 ? "204"
                                             : "?",
           AsiLock == DTAPI_ASI_INLOCK ? "lock" : "no lock",
           (Latched & (DTAPI_RX_FIFO_OVF | DTAPI_RX_SYNC_ERR)) == 0 ? "-" : "",
           (Latched & DTAPI_RX_FIFO_OVF) != 0 ? "overflow " : "",
           (Latched & DTAPI_RX_SYNC_ERR) != 0 ? "sync-error" : "");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Check -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Checks each packet of Data against the numbered packets. A packet with another number
// than the one expected counts as a gap, and the count goes on from its number.
//
static void Check(Tally* Counts, const uint8_t* Data, int Size, int PacketSize)
{
    for (int i = 0; i + PacketSize <= Size; i += PacketSize)
    {
        uint64_t Number = 0;
        if (!ExampleTs_IsNumbered(Data + i, PacketSize, &Number))
        {
            Counts->Bad++;
            Counts->Expected++;
            continue;
        }
        if (Counts->Packets == 0 && i == 0)
            Counts->First = Number;
        else if (Number != Counts->Expected)
            Counts->Gaps++;
        Counts->Expected = Number + 1;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Receive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sets the channel up and receives the stream. Returns the exit code.
//
static int Receive(DtInpChannel* Channel, const DtHwFuncDesc* Port, int RxMode,
                   int PacketSize, int64_t Count, int64_t TimeoutMs, FILE* Out,
                   Tally* Counts, uint8_t* Buffer)
{
    unsigned int Result = DtInpChannel_SetRxMode(Channel, RxMode);
    if (Result != DTAPI_OK)
        return Example_Failed("DtInpChannel_SetRxMode", Result);
    Result = DtInpChannel_SetRxControl(Channel, DTAPI_RXCTRL_RCV);
    if (Result != DTAPI_OK)
        return Example_Failed("DtInpChannel_SetRxControl", Result);

    int64_t NextStatus = Example_NowMs() + 1000;
    while (Count == 0 || Counts->Packets < Count)
    {
        int Size = CHUNK_SIZE;
        if (Count != 0 && (Count - Counts->Packets) * PacketSize < Size)
            Size = (int)((Count - Counts->Packets) * PacketSize);

        Result = DtInpChannel_Read(Channel, Buffer, Size, (int)TimeoutMs);
        if (Result == DTAPI_E_TIMEOUT)
        {
            printf("%s  no data within %lld ms\n", Port->DeviceName,
                   (long long)TimeoutMs);
            PrintStatus(Channel, Port, Counts);
            return EXAMPLE_NOTHING;
        }
        if (Result != DTAPI_OK)
        {
            printf("%s  ", Port->DeviceName);
            return Example_Failed("DtInpChannel_Read", Result);
        }

        if (Out != NULL && fwrite(Buffer, 1, (size_t)Size, Out) != (size_t)Size)
        {
            printf("Cannot write to the output file\n");
            return EXAMPLE_FAILED;
        }
        if (Counts->Check)
            Check(Counts, Buffer, Size, PacketSize);
        Counts->Packets += Size / PacketSize;

        if (Example_NowMs() >= NextStatus)
        {
            PrintStatus(Channel, Port, Counts);
            NextStatus += 1000;
        }
    }
    PrintStatus(Channel, Port, Counts);
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AttachAndReceive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Attaches the device and the channel to Port, makes the port ASI, and receives.
// Returns the exit code.
//
static int AttachAndReceive(DtDevice* Device, DtInpChannel* Channel, uint8_t* Buffer,
                            const DtHwFuncDesc* Port, int RxMode, int PacketSize,
                            int64_t Count, int64_t TimeoutMs, FILE* Out, Tally* Counts)
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

    // The channel follows the port's I/O standard, from SDI to ASI too.
    int Value = -1;
    Result =
        DtInpChannel_GetIoConfig(Channel, DTAPI_IOCONFIG_IOSTD, &Value, NULL, NULL, NULL);
    if (Result == DTAPI_OK && Value != DTAPI_IOCONFIG_ASI)
        Result = DtInpChannel_SetIoConfig(Channel, DTAPI_IOCONFIG_IOSTD,
                                          DTAPI_IOCONFIG_ASI, -1, -1, -1);
    if (Result != DTAPI_OK)
    {
        printf("%s  ", Port->DeviceName);
        return Example_Failed("DtInpChannel_SetIoConfig", Result);
    }
    printf("%s  io standard ASI\n", Port->DeviceName);

    int Exit =
        Receive(Channel, Port, RxMode, PacketSize, Count, TimeoutMs, Out, Counts, Buffer);
    DtInpChannel_Detach(Channel, DTAPI_INSTANT_DETACH);
    return Exit;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main(int Argc, char** Argv)
{
    int64_t Serial = 0;
    int64_t PortNumber = 0;
    int64_t Count = 0;
    int64_t TimeoutMs = 1000;
    if (!Example_CheckArguments(
            Argc, Argv, "Receives a transport stream from a DVB-ASI input.", g_Options,
            (int)(sizeof(g_Options) / sizeof(g_Options[0]))) ||
        !Example_Int64(Argc, Argv, "--serial", &Serial) ||
        !Example_Int64(Argc, Argv, "--port", &PortNumber) ||
        !Example_Int64(Argc, Argv, "--count", &Count) ||
        !Example_Int64(Argc, Argv, "--timeout", &TimeoutMs))
    {
        return EXAMPLE_FAILED;
    }
    int RxMode = 0;
    int PacketSize = 0;
    if (!RxModeFrom(Example_Value(Argc, Argv, "--rxmode"), &RxMode, &PacketSize))
    {
        printf("Unknown receive mode: %s; 188, 204, MP2 or RAW\n",
               Example_Value(Argc, Argv, "--rxmode"));
        return EXAMPLE_FAILED;
    }
    if (Count < 0)
    {
        printf("Option --count needs a number from 0\n");
        return EXAMPLE_FAILED;
    }

    DtHwFuncDesc Port;
    unsigned int Result = Example_FindPort(Serial, (int)PortNumber, IsAsiInput, &Port);
    if (Result == DTAPI_E_NOT_FOUND)
    {
        printf("No ASI input that suits\n");
        return EXAMPLE_NOTHING;
    }
    if (Result != DTAPI_OK)
        return Example_Failed("DtapiHwFuncScan", Result);

    const char* OutName = Example_Value(Argc, Argv, "--out");
    FILE* Out = NULL;
    if (OutName != NULL && (Out = fopen(OutName, "wb")) == NULL)
    {
        printf("Cannot write %s\n", OutName);
        return EXAMPLE_FAILED;
    }

    Tally Counts;
    memset(&Counts, 0, sizeof(Counts));
    Counts.Check = Example_HasFlag(Argc, Argv, "--check");
    DtDevice* Device = DtDevice_Alloc();
    DtInpChannel* Channel = DtInpChannel_Alloc();
    uint8_t* Buffer = (uint8_t*)malloc(CHUNK_SIZE);
    int Exit;
    if (Device == NULL || Channel == NULL || Buffer == NULL)
        Exit = Example_Failed("Allocating", DTAPI_E_OUT_OF_MEM);
    else
        Exit = AttachAndReceive(Device, Channel, Buffer, &Port, RxMode, PacketSize, Count,
                                TimeoutMs, Out, &Counts);

    if (Counts.Check && Counts.Packets > 0)
    {
        printf("%s  checked %lld packets  first %llu  gaps %lld  bad %lld\n",
               Port.DeviceName, (long long)Counts.Packets,
               (unsigned long long)Counts.First, (long long)Counts.Gaps,
               (long long)Counts.Bad);
        if (Exit == EXAMPLE_OK && (Counts.Gaps != 0 || Counts.Bad != 0))
            Exit = EXAMPLE_FAILED;
    }
    if (Out != NULL && fclose(Out) != 0 && Exit == EXAMPLE_OK)
    {
        printf("Cannot write %s\n", OutName);
        Exit = EXAMPLE_FAILED;
    }
    DtInpChannel_Free(Channel);
    DtDevice_Free(Device);
    free(Buffer);
    return Exit;
}
