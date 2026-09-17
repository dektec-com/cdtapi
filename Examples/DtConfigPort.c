// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtConfigPort.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Example: makes an SDI port an input or output, with a video standard
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Attaches to the device, then makes the port an input with --input or an output with
// --output, and with --vidstd sets its I/O standard to the one that carries that video
// standard. Prints one line per step with its result:
//
//     9217800001:1  IODIR INPUT  DTAPI_OK
//     9217800001:1  IOSTD HDSDI 1080I50  DTAPI_OK
//
// The configuration stays on the device after the program ends. Exits with 0 when every
// step succeeds, 1 when one fails or the command line is wrong, and 2 when no port suits.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <string.h>

// Example includes
#include "Common/ExampleCommon.h" // The API and what the examples share.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static const ExampleOption g_Options[] = {
    {"--serial", true,
     "The device's serial number; the first device with a port that suits"},
    {"--port", true, "The port number; the first SDI port that suits"},
    {"--input", false, "Make the port an input"},
    {"--output", false, "Make the port an output"},
    {"--vidstd", true, "Set the I/O standard for this video standard, such as 1080I50"},
    {"--linkstd", true,
     "How 4K is carried: 0 or 1 four 3G links, 2 6G, 3 12G; default -1"},
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsSdiInput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool IsSdiInput(const DtHwFuncDesc* Port)
{
    return Port->IsSdi && Port->IsInput;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsSdiOutput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool IsSdiOutput(const DtHwFuncDesc* Port)
{
    return Port->IsSdi && Port->IsOutput;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsSdi -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool IsSdi(const DtHwFuncDesc* Port)
{
    return Port->IsSdi != 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DtapiVidStd2IoStd turns a video standard into the value and sub-value of the IOSTD
// group; for a video standard, the sub-value is the standard's own code.
//
int main(int Argc, char** Argv)
{
    int64_t Serial = 0;
    int64_t PortNumber = 0;
    int64_t LinkStd = -1;
    bool Input = ExampleHasFlag(Argc, Argv, "--input");
    bool Output = ExampleHasFlag(Argc, Argv, "--output");
    const char* VidStdName = ExampleValue(Argc, Argv, "--vidstd");
    int VidStd = DTAPI_VIDSTD_UNKNOWN;
    int Value = -1;
    int SubValue = -1;
    int Exit = EXAMPLE_OK;

    if (!ExampleCheckArguments(Argc, Argv,
                               "Makes an SDI port an input or output, and sets its I/O "
                               "standard for a video standard.",
                               g_Options,
                               (int)(sizeof(g_Options) / sizeof(g_Options[0]))) ||
        !ExampleInt64(Argc, Argv, "--serial", &Serial) ||
        !ExampleInt64(Argc, Argv, "--port", &PortNumber) ||
        !ExampleInt64(Argc, Argv, "--linkstd", &LinkStd))
    {
        return EXAMPLE_FAILED;
    }
    if (Input && Output)
    {
        printf("Give --input or --output, not both\n");
        return EXAMPLE_FAILED;
    }
    if (!Input && !Output && VidStdName == NULL)
    {
        printf("Nothing to do: give --input, --output or --vidstd; --help lists them\n");
        return EXAMPLE_FAILED;
    }

    // Check the video standard before anything changes on the device.
    unsigned int Result;
    if (VidStdName != NULL)
    {
        if (!ExampleVidStdFromName(VidStdName, &VidStd))
        {
            printf("Unknown video standard: %s\n", VidStdName);
            return EXAMPLE_FAILED;
        }
        Result = DtapiVidStd2IoStd(VidStd, (int)LinkStd, &Value, &SubValue);
        if (Result != DTAPI_OK)
            return ExampleFailed("DtapiVidStd2IoStd", Result);
    }

    DtHwFuncDesc Port;
    Result = ExampleFindPort(Serial, (int)PortNumber,
                             Input ? IsSdiInput : (Output ? IsSdiOutput : IsSdi), &Port);
    if (Result == DTAPI_E_NOT_FOUND)
    {
        printf("No port that suits\n");
        return EXAMPLE_NOTHING;
    }
    if (Result != DTAPI_OK)
        return ExampleFailed("DtapiHwFuncScan", Result);

    DtDevice* Device = DtDevice_Alloc();
    if (Device == NULL)
        return ExampleFailed("DtDevice_Alloc", DTAPI_E_OUT_OF_MEM);

    Result = DtDevice_AttachToSerial(Device, Port.SerialNumber);
    if (!ExampleSucceeded(Result))
    {
        DtDevice_Free(Device);
        return ExampleFailed("DtDevice_AttachToSerial", Result);
    }

    if (Input || Output)
    {
        Result = Input ? DtDevice_SetToInput(Device, Port.Port)
                       : DtDevice_SetToOutput(Device, Port.Port);
        printf("%s  IODIR %s  %s\n", Port.DeviceName, Input ? "INPUT" : "OUTPUT",
               DtapiResult2Str(Result));
        if (!ExampleSucceeded(Result))
            Exit = EXAMPLE_FAILED;
    }

    if (VidStdName != NULL && Exit == EXAMPLE_OK)
    {
        Result = DtDevice_SetIoConfig(Device, Port.Port, DTAPI_IOCONFIG_IOSTD, Value,
                                      SubValue);
        printf("%s  IOSTD %s %s  %s\n", Port.DeviceName, ExampleIoStdName(Value),
               ExampleVidStdName(SubValue), DtapiResult2Str(Result));
        if (!ExampleSucceeded(Result))
            Exit = EXAMPLE_FAILED;
    }

    DtDevice_Detach(Device);
    DtDevice_Free(Device);
    return Exit;
}
