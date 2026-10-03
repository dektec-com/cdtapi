// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtListDevices.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Example: lists every port of every DekTec device
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Lists the ports of all DekTec cards, one line per port: its name (serial number and
// port number), its description, what it can do (SDI, ASI, AVFIFO, INPUT, OUTPUT), and
// for an IP port its MAC address and IPv4 address, and the state and lock of its PTP
// clock slave when DtapiService runs one. Then the number of ports.
//
//     9217800001:1  DTA-2178 port 1  SDI,ASI,INPUT,OUTPUT
//     ...
//     9211000001:1  DTA-2110 port 1  AVFIFO,INPUT,OUTPUT  00:14:F4:08:00:01 192.168.1.10
//         PTP SLAVE LOCKED
//     10 ports
//
// (The DTA-2110's line is one line.)
//
// Exits with 0 when ports are found, 2 when there are none, and 1 when the scan fails.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Example includes
#include "Common/ExampleCommon.h" // The API and what the examples share.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static const ExampleOption g_Options[] = {
    {"--serial", true, "List only the ports of the device with this serial number"},
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrintKind -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Prints Name if Is is true, after a comma when a name was printed before it.
//
static void PrintKind(int Is, const char* Name, int* Printed)
{
    if (!Is)
        return;
    printf("%s%s", *Printed > 0 ? "," : "", Name);
    (*Printed)++;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrintKinds -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Prints what a port can do, separated by commas, or "-" when it can do none of these.
//
static void PrintKinds(const DtHwFuncDesc* Port)
{
    int Printed = 0;

    PrintKind(Port->IsSdi, "SDI", &Printed);
    PrintKind(Port->IsAsi, "ASI", &Printed);
    PrintKind(Port->IsAvFifo, "AVFIFO", &Printed);
    PrintKind(Port->IsInput, "INPUT", &Printed);
    PrintKind(Port->IsOutput, "OUTPUT", &Printed);
    printf("%s", Printed > 0 ? "" : "-");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrintAddress -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Prints the MAC address and IPv4 address of an IP port, as the scan found them. A port
// without a MAC address is not an IP port, and nothing is printed for it.
//
static void PrintAddress(const DtHwFuncDesc* Port)
{
    static const uint8_t NoMac[6] = {0};
    if (memcmp(Port->MacAddr, NoMac, sizeof(NoMac)) == 0)
        return;
    const uint8_t* M = Port->MacAddr;
    printf("  %02X:%02X:%02X:%02X:%02X:%02X", M[0], M[1], M[2], M[3], M[4], M[5]);
    const uint8_t* Ip = Port->Ip;
    if (Ip[0] != 0 || Ip[1] != 0 || Ip[2] != 0 || Ip[3] != 0)
        printf(" %u.%u.%u.%u", Ip[0], Ip[1], Ip[2], Ip[3]);
    else
        printf(" no IPv4 address");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrintPtp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Prints the state and lock of the PTP clock slave of an IP port. The device is attached
// for it, and the slave read through DtapiService; when either fails, as on a computer
// without the service, nothing is printed.
//
static void PrintPtp(const DtHwFuncDesc* Port)
{
    if (!Port->IsAvFifo)
        return;
    DtDevice* Device = DtDevice_Alloc();
    if (Device == NULL)
        return;
    DtPtpStatus Status;
    if (DtDevice_AttachToSerial(Device, Port->SerialNumber) == DTAPI_OK &&
        DtDevice_GetPtpStatus(Device, Port->Port, &Status) == DTAPI_OK)
        printf("  PTP %s %s", Example_PtpStateName(Status.SlaveState),
               Example_PtpLockName(Status.LockStatus));
    DtDevice_Free(Device);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The first DtapiHwFuncScan, without a buffer, asks how many ports there are; it returns
// DTAPI_E_BUF_TOO_SMALL when there are any. The second call fills a buffer of that size.
//
int main(int Argc, char** Argv)
{
    int64_t Serial = 0;
    int Count = 0;
    int Listed = 0;

    if (!Example_CheckArguments(Argc, Argv, "Lists every port of every DekTec device.",
                                g_Options,
                                (int)(sizeof(g_Options) / sizeof(g_Options[0]))) ||
        !Example_Int64(Argc, Argv, "--serial", &Serial))
    {
        return EXAMPLE_FAILED;
    }

    unsigned int Result = DtapiHwFuncScan(0, &Count, NULL);
    if (Result != DTAPI_OK && Result != DTAPI_E_BUF_TOO_SMALL)
        return Example_Failed("DtapiHwFuncScan", Result);

    DtHwFuncDesc* Ports =
        (DtHwFuncDesc*)calloc(Count > 0 ? (size_t)Count : 1, sizeof(DtHwFuncDesc));
    if (Ports == NULL)
        return Example_Failed("calloc", DTAPI_E_OUT_OF_MEM);

    Result = DtapiHwFuncScan(Count, &Count, Ports);
    if (Result != DTAPI_OK)
    {
        free(Ports);
        return Example_Failed("DtapiHwFuncScan", Result);
    }

    for (int i = 0; i < Count; i++)
    {
        if (Serial != 0 && Ports[i].SerialNumber != Serial)
            continue;
        printf("%s  %s  ", Ports[i].DeviceName, Ports[i].Description);
        PrintKinds(&Ports[i]);
        PrintAddress(&Ports[i]);
        PrintPtp(&Ports[i]);
        printf("\n");
        Listed++;
    }
    printf("%d ports\n", Listed);

    free(Ports);
    return Listed > 0 ? EXAMPLE_OK : EXAMPLE_NOTHING;
}
