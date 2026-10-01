// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtListDevices.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Example: lists every port of every DekTec device
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Prints one line per port: its device name, which is the serial number and the port
// number; its description; what it is: SDI, ASI, AVFIFO, INPUT, OUTPUT; and for a
// network port its MAC address and IPv4 address. Then the number of ports found.
//
//     9217800001:1  DTA-2178 port 1  SDI,ASI,INPUT,OUTPUT
//     ...
//     9211000001:1  DTA-2110 port 1  AVFIFO,INPUT,OUTPUT  00:14:F4:08:00:01 192.168.1.10
//     10 ports
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
// Prints Name when the port Is it, after a comma when an earlier kind was printed.
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
// What a port is, as a comma-separated list, or "-" when it is none of these.
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
// The MAC address and the IPv4 address of a network port, as the scan found them; a
// port without a MAC address is no network port, and prints nothing.
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DtapiHwFuncScan with no room reports how many ports there are, with
// DTAPI_E_BUF_TOO_SMALL; the second call fills a buffer of that size.
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
        printf("\n");
        Listed++;
    }
    printf("%d ports\n", Listed);

    free(Ports);
    return Listed > 0 ? EXAMPLE_OK : EXAMPLE_NOTHING;
}
