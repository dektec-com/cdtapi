// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtListDevices.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Example: lists every port of every DekTec device
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Prints one line per port: the device name the scan gives it, serial number and port
// number; its description; and what it is: SDI, AVFIFO, INPUT, OUTPUT. Then the number
// of ports found.
//
//     9217800001:1  DTA-2178 port 1  SDI,INPUT,OUTPUT
//     ...
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
    PrintKind(Port->IsAvFifo, "AVFIFO", &Printed);
    PrintKind(Port->IsInput, "INPUT", &Printed);
    PrintKind(Port->IsOutput, "OUTPUT", &Printed);
    printf("%s\n", Printed > 0 ? "" : "-");
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
        Listed++;
    }
    printf("%d ports\n", Listed);

    free(Ports);
    return Listed > 0 ? EXAMPLE_OK : EXAMPLE_NOTHING;
}
