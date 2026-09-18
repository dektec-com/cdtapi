// #*#*#*#*#*#*#*#*#*#*#*#*#* DtDetectVidStd.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Example: detects the video standard on an SDI input
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Attaches to the device and detects the video standard on the port, once, or with
// --timeout until a standard is found or the time has passed. Prints the port and what
// was found:
//
//     9217800001:1  1080I50
//     9217800001:1  no video standard
//
// The port must be configured as an input; DtConfigPort does that. Exits with 0 when a
// standard is found, 2 when none is, and 1 when detection fails or the command line is
// wrong.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>

// Example includes
#include "Common/ExampleCommon.h" // The API and what the examples share.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Time between two detections while waiting.
#define POLL_MS 10

static const ExampleOption g_Options[] = {
    {"--serial", true, "The device's serial number; the first device with an SDI input"},
    {"--port", true, "The port number; the first SDI input"},
    {"--timeout", true, "Milliseconds to wait for a standard; without it, detect once"},
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsSdiInput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool IsSdiInput(const DtHwFuncDesc* Port)
{
    return Port->IsSdi && Port->IsInput;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DtDevice_DetectVidStd returns DTAPI_OK with DTAPI_VIDSTD_UNKNOWN while there is no
// signal, so waiting is detecting again after a pause. DtDevice_WaitForSignal would do
// that too, but without an end.
//
int main(int Argc, char** Argv)
{
    int64_t Serial = 0;
    int64_t PortNumber = 0;
    int64_t TimeoutMs = 0;
    int VidStd = DTAPI_VIDSTD_UNKNOWN;
    int64_t Waited = 0;

    if (!Example_CheckArguments(Argc, Argv, "Detects the video standard on an SDI input.",
                                g_Options,
                                (int)(sizeof(g_Options) / sizeof(g_Options[0]))) ||
        !Example_Int64(Argc, Argv, "--serial", &Serial) ||
        !Example_Int64(Argc, Argv, "--port", &PortNumber) ||
        !Example_Int64(Argc, Argv, "--timeout", &TimeoutMs))
    {
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
    if (Device == NULL)
        return Example_Failed("DtDevice_Alloc", DTAPI_E_OUT_OF_MEM);

    Result = DtDevice_AttachToSerial(Device, Port.SerialNumber);
    if (!Example_Succeeded(Result))
    {
        DtDevice_Free(Device);
        return Example_Failed("DtDevice_AttachToSerial", Result);
    }

    for (;;)
    {
        Result = DtDevice_DetectVidStd(Device, Port.Port, &VidStd);
        if (Result != DTAPI_OK || VidStd != DTAPI_VIDSTD_UNKNOWN || Waited >= TimeoutMs)
            break;
        Example_SleepMs(POLL_MS);
        Waited += POLL_MS;
    }

    DtDevice_Detach(Device);
    DtDevice_Free(Device);

    if (Result != DTAPI_OK)
    {
        printf("%s  ", Port.DeviceName);
        return Example_Failed("DtDevice_DetectVidStd", Result);
    }
    if (VidStd == DTAPI_VIDSTD_UNKNOWN)
    {
        printf("%s  no video standard\n", Port.DeviceName);
        return EXAMPLE_NOTHING;
    }
    printf("%s  %s\n", Port.DeviceName, Example_VidStdName(VidStd));
    return EXAMPLE_OK;
}
