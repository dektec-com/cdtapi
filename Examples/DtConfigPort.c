// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtConfigPort.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Example: makes a port an input or output, with a video standard or ASI
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Attaches to the device, then makes the port an input with --input or an output with
// --output, and with --vidstd sets its I/O standard to the one that carries that video
// standard, or with --asi to DVB-ASI. Prints one line per step with its result:
//
//     9217800001:1  IODIR INPUT  DTAPI_OK
//     9217800001:1  IOSTD HDSDI 1080I50  DTAPI_OK
//     9217800001:5  IOSTD ASI  DTAPI_OK
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
    {"--port", true, "The port number; the first SDI or ASI port that suits"},
    {"--input", false, "Make the port an input"},
    {"--output", false, "Make the port an output"},
    {"--vidstd", true, "Set the I/O standard for this video standard, such as 1080I50"},
    {"--linkstd", true,
     "How 4K is carried: 0 or 1 four 3G links, 2 6G, 3 12G; default -1"},
    {"--asi", false, "Set the I/O standard to DVB-ASI"},
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsAsiInput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool IsAsiInput(const DtHwFuncDesc* Port)
{
    return Port->IsAsi && Port->IsInput;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsAsiOutput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool IsAsiOutput(const DtHwFuncDesc* Port)
{
    return Port->IsAsi && Port->IsOutput;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsAsi -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool IsAsi(const DtHwFuncDesc* Port)
{
    return Port->IsAsi != 0;
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
    bool Input = Example_HasFlag(Argc, Argv, "--input");
    bool Output = Example_HasFlag(Argc, Argv, "--output");
    bool Asi = Example_HasFlag(Argc, Argv, "--asi");
    const char* VidStdName = Example_Value(Argc, Argv, "--vidstd");
    int VidStd = DTAPI_VIDSTD_UNKNOWN;
    int Value = -1;
    int SubValue = -1;
    int Exit = EXAMPLE_OK;

    if (!Example_CheckArguments(Argc, Argv,
                                "Makes a port an input or output, and sets its I/O "
                                "standard for a video standard or to ASI.",
                                g_Options,
                                (int)(sizeof(g_Options) / sizeof(g_Options[0]))) ||
        !Example_Int64(Argc, Argv, "--serial", &Serial) ||
        !Example_Int64(Argc, Argv, "--port", &PortNumber) ||
        !Example_Int64(Argc, Argv, "--linkstd", &LinkStd))
    {
        return EXAMPLE_FAILED;
    }
    if (Input && Output)
    {
        printf("Give --input or --output, not both\n");
        return EXAMPLE_FAILED;
    }
    if (Asi && VidStdName != NULL)
    {
        printf("Give --vidstd or --asi, not both\n");
        return EXAMPLE_FAILED;
    }
    if (!Input && !Output && VidStdName == NULL && !Asi)
    {
        printf("Nothing to do: give --input, --output, --vidstd or --asi; --help lists "
               "them\n");
        return EXAMPLE_FAILED;
    }

    // Check the video standard before anything changes on the device.
    unsigned int Result;
    if (VidStdName != NULL)
    {
        if (!Example_VidStdFromName(VidStdName, &VidStd))
        {
            printf("Unknown video standard: %s\n", VidStdName);
            return EXAMPLE_FAILED;
        }
        Result = DtapiVidStd2IoStd(VidStd, (int)LinkStd, &Value, &SubValue);
        if (Result != DTAPI_OK)
            return Example_Failed("DtapiVidStd2IoStd", Result);
    }

    ExampleSuits Suits = Input ? IsSdiInput : (Output ? IsSdiOutput : IsSdi);
    if (Asi)
        Suits = Input ? IsAsiInput : (Output ? IsAsiOutput : IsAsi);
    DtHwFuncDesc Port;
    Result = Example_FindPort(Serial, (int)PortNumber, Suits, &Port);
    if (Result == DTAPI_E_NOT_FOUND)
    {
        printf("No port that suits\n");
        return EXAMPLE_NOTHING;
    }
    if (Result != DTAPI_OK)
        return Example_Failed("DtapiHwFuncScan", Result);

    DtDevice* Device = DtDevice_Alloc();
    if (Device == NULL)
        return Example_Failed("DtDevice_Alloc", DTAPI_E_OUT_OF_MEM);

    Result = DtDevice_AttachToSerial(Device, Port.SerialNumber);
    if (!Example_Succeeded(Result))
    {
        DtDevice_Free(Device);
        return Example_Failed("DtDevice_AttachToSerial", Result);
    }

    if (Input || Output)
    {
        Result = Input ? DtDevice_SetToInput(Device, Port.Port)
                       : DtDevice_SetToOutput(Device, Port.Port);
        printf("%s  IODIR %s  %s\n", Port.DeviceName, Input ? "INPUT" : "OUTPUT",
               DtapiResult2Str(Result));
        if (!Example_Succeeded(Result))
            Exit = EXAMPLE_FAILED;
    }

    if (VidStdName != NULL && Exit == EXAMPLE_OK)
    {
        DtIoConfig Config = {Port.Port, DTAPI_IOCONFIG_IOSTD, Value, SubValue, {-1, -1}};
        Result = DtDevice_SetIoConfig(Device, &Config, 1);
        printf("%s  IOSTD %s %s  %s\n", Port.DeviceName, Example_IoStdName(Value),
               Example_VidStdName(SubValue), DtapiResult2Str(Result));
        if (!Example_Succeeded(Result))
            Exit = EXAMPLE_FAILED;
    }

    if (Asi && Exit == EXAMPLE_OK)
    {
        DtIoConfig Config = {
            Port.Port, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_ASI, -1, {-1, -1}};
        Result = DtDevice_SetIoConfig(Device, &Config, 1);
        printf("%s  IOSTD ASI  %s\n", Port.DeviceName, DtapiResult2Str(Result));
        if (!Example_Succeeded(Result))
            Exit = EXAMPLE_FAILED;
    }

    DtDevice_Detach(Device);
    DtDevice_Free(Device);
    return Exit;
}
