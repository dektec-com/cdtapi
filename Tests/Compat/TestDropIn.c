// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestDropIn.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - An application written against the original CDTAPI.h, linked to CDtapiLite
//
// SPDX-License-Identifier: BSD-3-Clause
//
// This file includes CDTAPI.h and nothing of CDtapiLite. That it compiles, links against
// CDtapiLite and runs against the emulator is what "drop-in replacement" means. CTest
// builds it only where the original header is found, and runs it with CDTAPILITE_SIM=1.
//
// It uses the device part of the API the way an application does: scan, pick a port,
// attach by serial number, configure, read the clock, detect a video standard, detach.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdlib.h>
#include <string.h>

// The original C API, and the test framework.
#include "CDTAPI.h"
#include "DtTest.h"

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(ScanAttachConfigureDetach)
{
    DtHwFuncDesc* Funcs;
    DtDevice* Device;
    DtTimeOfDay Tod;
    int Count = 0, Found = 0;
    int Output = -1, Input = -1;
    int i;

    DT_ASSERT_EQ(DtapiHwFuncScan(0, &Count, NULL), DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT(Count > 0);
    if (Count <= 0)
        return;

    Funcs = (DtHwFuncDesc*)calloc((size_t)Count, sizeof(DtHwFuncDesc));
    DT_ASSERT(Funcs != NULL);
    if (Funcs == NULL)
        return;

    DT_ASSERT_EQ(DtapiHwFuncScan(Count, &Found, Funcs), DTAPI_OK);
    DT_ASSERT_EQ(Found, Count);

    for (i = 0; i < Found; i++)
    {
        if (Funcs[i].IsSdi && Funcs[i].IsOutput && Output < 0)
            Output = i;
        else if (Funcs[i].IsSdi && Funcs[i].IsInput && Input < 0)
            Input = i;
    }
    DT_ASSERT(Output >= 0 && Input >= 0);
    if (Output < 0 || Input < 0)
    {
        free(Funcs);
        return;
    }
    DT_ASSERT(strstr(Funcs[Output].Description, "DTA-") == Funcs[Output].Description);

    Device = DtDevice_Alloc();
    DT_ASSERT(Device != NULL);
    DT_ASSERT_EQ(DtDevice_AttachToSerial(Device, Funcs[Output].SerialNumber), DTAPI_OK);
    DT_ASSERT_EQ(DtDevice_SetToOutput(Device, Funcs[Output].Port), DTAPI_OK);
    DT_ASSERT_EQ(DtDevice_SetToInput(Device, Funcs[Input].Port), DTAPI_OK);
    DT_ASSERT_EQ(DtDevice_SetIoConfig(Device, Funcs[Output].Port, DTAPI_IOCONFIG_IODIR,
                                      DTAPI_IOCONFIG_OUTPUT, -1),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDevice_GetTimeOfDay(Device, &Tod), DTAPI_OK);
    DT_ASSERT(Tod.Nanoseconds < 1000000000U);
    DT_ASSERT_EQ(DtDevice_Detach(Device), DTAPI_OK);
    DtDevice_Freep(&Device);
    DT_ASSERT(Device == NULL);

    free(Funcs);
}

// Detection on the ports the scan describes: an input without a signal has no standard,
// an output is in the wrong mode for it, a port that is no input is refused, and so is a
// port the device does not have. Waiting on a port that cannot be attached returns at
// once, with every field unknown.
DT_TEST(DetectVideoStandard)
{
    DtHwFuncDesc Funcs[64];
    DtDevice* Device;
    DtDetVidStd Info;
    int Found = 0;
    int Output = -1, Input = -1, NoInput = -1;
    int VidStd;
    int i;

    DT_ASSERT_EQ(DtapiHwFuncScan(64, &Found, Funcs), DTAPI_OK);
    for (i = 0; i < Found; i++)
    {
        if (!Funcs[i].IsInput && NoInput < 0)
            NoInput = i;
        else if (Funcs[i].IsSdi && Funcs[i].IsInput && Funcs[i].IsOutput)
        {
            if (Input < 0)
                Input = i;
            else if (Output < 0)
                Output = i;
        }
    }
    DT_ASSERT(Input >= 0 && Output >= 0 && NoInput >= 0);
    if (Input < 0 || Output < 0 || NoInput < 0)
        return;

    Device = DtDevice_Alloc();
    DT_ASSERT_EQ(DtDevice_AttachToSerial(Device, Funcs[Input].SerialNumber), DTAPI_OK);
    DT_ASSERT_EQ(DtDevice_SetToInput(Device, Funcs[Input].Port), DTAPI_OK);
    DT_ASSERT_EQ(DtDevice_SetToOutput(Device, Funcs[Output].Port), DTAPI_OK);

    VidStd = 12345;
    DT_ASSERT_EQ(DtDevice_DetectVidStd(Device, Funcs[Input].Port, &VidStd), DTAPI_OK);
    DT_ASSERT_EQ(VidStd, DTAPI_VIDSTD_UNKNOWN);

    VidStd = 12345;
    DT_ASSERT_EQ(DtDevice_DetectVidStd(Device, Funcs[Output].Port, &VidStd),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(VidStd, 12345);
    DT_ASSERT_EQ(DtDevice_DetectVidStd(Device, Funcs[NoInput].Port, &VidStd),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtDevice_DetectVidStd(Device, Found + 1, &VidStd), DTAPI_E_NO_SUCH_PORT);

    Info = DtDevice_WaitForSignal(Device, Funcs[NoInput].Port);
    DT_ASSERT_EQ(Info.VidStd, DTAPI_VIDSTD_UNKNOWN);
    DT_ASSERT_EQ(Info.LinkStd, -1);
    DT_ASSERT_EQ(Info.LinkNr, -1);
    DT_ASSERT_EQ(Info.AspectRatio, DT_AR_UNKNOWN);

    DtDevice_Free(Device);
}

DT_TEST(ResultNames)
{
    DT_ASSERT_STR(DtapiResult2Str(DTAPI_OK), "DTAPI_OK");
    DT_ASSERT_STR(DtapiResult2Str(DTAPI_E_NO_TS_INPUT), "DTAPI_E_NO_DT_INPUT");
    DT_ASSERT_STR(DtapiResult2Str(DTAPI_E_EXCEPTION), "???");
}

DT_TEST_MAIN("DropIn", DT_RUN(ScanAttachConfigureDetach), DT_RUN(DetectVideoStandard),
             DT_RUN(ResultNames))
