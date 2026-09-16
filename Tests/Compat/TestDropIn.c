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
// attach by serial number, configure, read the clock, detach.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdlib.h>
#include <string.h>

// The original C API, and the test framework.
#include "CDTAPI.h"
#include "DtlTest.h"

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DTL_TEST(ScanAttachConfigureDetach)
{
    DtHwFuncDesc* Funcs;
    DtDevice* Device;
    DtTimeOfDay Tod;
    int Count = 0, Found = 0;
    int Output = -1, Input = -1;
    int i;

    DTL_ASSERT_EQ(DtapiHwFuncScan(0, &Count, NULL), DTAPI_E_BUF_TOO_SMALL);
    DTL_ASSERT(Count > 0);
    if (Count <= 0)
        return;

    Funcs = (DtHwFuncDesc*)calloc((size_t)Count, sizeof(DtHwFuncDesc));
    DTL_ASSERT(Funcs != NULL);
    if (Funcs == NULL)
        return;

    DTL_ASSERT_EQ(DtapiHwFuncScan(Count, &Found, Funcs), DTAPI_OK);
    DTL_ASSERT_EQ(Found, Count);

    for (i = 0; i < Found; i++)
    {
        if (Funcs[i].IsSdi && Funcs[i].IsOutput && Output < 0)
            Output = i;
        else if (Funcs[i].IsSdi && Funcs[i].IsInput && Input < 0)
            Input = i;
    }
    DTL_ASSERT(Output >= 0 && Input >= 0);
    if (Output < 0 || Input < 0)
    {
        free(Funcs);
        return;
    }
    DTL_ASSERT(strstr(Funcs[Output].Description, "DTA-") == Funcs[Output].Description);

    Device = DtDevice_Alloc();
    DTL_ASSERT(Device != NULL);
    DTL_ASSERT_EQ(DtDevice_AttachToSerial(Device, Funcs[Output].SerialNumber), DTAPI_OK);
    DTL_ASSERT_EQ(DtDevice_SetToOutput(Device, Funcs[Output].Port), DTAPI_OK);
    DTL_ASSERT_EQ(DtDevice_SetToInput(Device, Funcs[Input].Port), DTAPI_OK);
    DTL_ASSERT_EQ(DtDevice_SetIoConfig(Device, Funcs[Output].Port, DTAPI_IOCONFIG_IODIR,
                                       DTAPI_IOCONFIG_OUTPUT, -1),
                  DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtDevice_GetTimeOfDay(Device, &Tod), DTAPI_OK);
    DTL_ASSERT(Tod.Nanoseconds < 1000000000U);
    DTL_ASSERT_EQ(DtDevice_Detach(Device), DTAPI_OK);
    DtDevice_Freep(&Device);
    DTL_ASSERT(Device == NULL);

    free(Funcs);
}

DTL_TEST(ResultNames)
{
    DTL_ASSERT_STR(DtapiResult2Str(DTAPI_OK), "DTAPI_OK");
    DTL_ASSERT_STR(DtapiResult2Str(DTAPI_E_NO_TS_INPUT), "DTAPI_E_NO_DT_INPUT");
    DTL_ASSERT_STR(DtapiResult2Str(DTAPI_E_EXCEPTION), "???");
}

DTL_TEST_MAIN("DropIn", DTL_RUN(ScanAttachConfigureDetach), DTL_RUN(ResultNames))
