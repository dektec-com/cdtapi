// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestSimFunc.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Finding the parts of an API function against the emulator
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPILITE_SIM=1. Every case starts from the emulator's power-on
// state, and ends with no handle to it and no allocation left open.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
#include "Core/DtAlloc.h"           // Live allocations and allocation failures.
#include "DtDrvAbi.h"               // Types, UUID flags and driver statuses.
#include "DtFunc.h"                 // Functions under test.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // Device handles.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The parts of the ASI/SDI receiver of every SDI port, as a DTA-2178 lists them.
static const struct
{
    const char* Name;
    const char* Role;
    bool IsDf;
    int Type;
} g_Parts[] = {
    {"BC_SWITCH#2", "SDI_MUX_IN", false, DT_BLOCK_TYPE_SWITCH},
    {"BC_ST425LR#1", "", false, DT_BLOCK_TYPE_ST425LR},
    {"BC_SDIMUX12G#1", "", false, DT_BLOCK_TYPE_SDIMUX12G},
    {"BC_SWITCH#3", "SDI_MUX_OUT", false, DT_BLOCK_TYPE_SWITCH},
    {"BC_SDIRXF#1", "", false, DT_BLOCK_TYPE_SDIRXF},
    {"DF_SDIRX#1", "", true, DT_FUNC_TYPE_SDIRX},
    {"DF_ASIRX#1", "", true, DT_FUNC_TYPE_ASIRX},
    {"DF_CHSDIRX#1", "", true, DT_FUNC_TYPE_CHSDIRX},
};

#define PART_COUNT (sizeof(g_Parts) / sizeof(g_Parts[0]))

// Opens the emulated device in its power-on state and notes the live allocations. Returns
// NULL, having recorded a failure, when it is not the emulator.
static OsDrv* OpenSim(int* DtFailures, long* Live)
{
    OsDrv* Drv;

    SimDtPcieReset();
    DtAllocResetCount();
    *Live = DtAllocLive();
    Drv = OsDrvOpen(SIM_DEVICE_INDEX);
    if (Drv == NULL || !OsDrvIsEmulated(Drv))
    {
        printf("    FAIL: no emulated device at index 0; is CDTAPILITE_SIM=1 set?\n");
        (*DtFailures)++;
        OsDrvClose(Drv);
        return NULL;
    }
    return Drv;
}

// Closes the device and checks that nothing is left open or allocated.
#define FINISH(Drv, Live)                                                                \
    do                                                                                   \
    {                                                                                    \
        OsDrvClose(Drv);                                                                 \
        DT_ASSERT_EQ(SimDtPcieOpenHandles(), 0);                                         \
        DT_ASSERT_EQ(DtAllocLive(), Live);                                               \
    } while (0)

// The part at Index of an instance.
static const DtFuncPart* PartAt(const DtFuncInstance* Instance, size_t Index)
{
    return &DT_VEC_AT(&Instance->Parts, DtFuncPart, Index);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Find +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Every part comes with its name, role, kind, type and the UUID the card gives it, in the
// card's order.
DT_TEST(PartsOfTheReceiverFunction)
{
    DtFuncInstance Func;
    long Live;
    size_t i;
    OsDrv* Drv = OpenSim(DtFailures, &Live);

    if (Drv == NULL)
        return;

    DT_ASSERT_OK(DtFuncFind(Drv, 5, "AF_ASISDIRX", "", &Func));
    DT_ASSERT_EQ(Func.PortIndex, 5);
    DT_ASSERT_EQ(DtVecCount(&Func.Parts), PART_COUNT);
    for (i = 0; i < PART_COUNT && i < DtVecCount(&Func.Parts); i++)
    {
        const DtFuncPart* Part = PartAt(&Func, i);
        char Key[PROPERTY_NAME_MAX_SIZE];
        int Uuid = 0;

        DT_ASSERT_STR(Part->Name, g_Parts[i].Name);
        DT_ASSERT_STR(Part->Role, g_Parts[i].Role);
        DT_ASSERT_EQ(Part->IsDf, g_Parts[i].IsDf);
        DT_ASSERT_EQ(Part->Type, g_Parts[i].Type);
        snprintf(Key, sizeof(Key), "%s_UUID", g_Parts[i].Name);
        DT_ASSERT_OK(DtDrvGetPropertyInt(Drv, Key, 5, &Uuid));
        DT_ASSERT_EQ(Part->Uuid, Uuid);
    }
    DtFuncRelease(&Func);
    DT_ASSERT_EQ(DtVecCount(&Func.Parts), 0);

    FINISH(Drv, Live);
}

// An API function, instance role or port without it is not found, and leaves the instance
// empty; a name too long for a property is refused.
DT_TEST(MissingFunctionIsNotFound)
{
    char TooLong[PROPERTY_NAME_MAX_SIZE];
    DtFuncInstance Func;
    long Live;
    OsDrv* Drv = OpenSim(DtFailures, &Live);

    if (Drv == NULL)
        return;

    DT_ASSERT_EQ(DtFuncFind(Drv, 5, "AF_ASISDITX", "", &Func), DTAPI_E_NOT_FOUND);
    DT_ASSERT_EQ(DtVecCount(&Func.Parts), 0);
    DT_ASSERT_EQ(DtFuncFind(Drv, 5, "AF_ASISDIRX", "OTHER", &Func), DTAPI_E_NOT_FOUND);
    DT_ASSERT_EQ(DtFuncFind(Drv, SIM_SDI_PORT_COUNT, "AF_ASISDIRX", "", &Func),
                 DTAPI_E_NOT_FOUND);

    memset(TooLong, 'A', sizeof(TooLong) - 1);
    TooLong[sizeof(TooLong) - 1] = '\0';
    DT_ASSERT_EQ(DtFuncFind(Drv, 5, TooLong, "", &Func), DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_EQ(DtVecCount(&Func.Parts), 0);

    FINISH(Drv, Live);
}

// The first instance with the role is read; parts end at the first one not found.
DT_TEST(InstanceIsChosenByRole)
{
    DtFuncInstance Func;
    long Live;
    OsDrv* Drv = OpenSim(DtFailures, &Live);

    if (Drv == NULL)
        return;

    SimDtPcieOverrideString("AF_ASISDIRX#2", 1, true, "SECOND");
    SimDtPcieOverrideString("AF_ASISDIRX#2.1", 1, true, "DF_ASIRX#1");
    SimDtPcieOverrideString("AF_ASISDIRX#3", 1, true, "SECOND");
    SimDtPcieOverrideString("AF_ASISDIRX#3.1", 1, true, "DF_SDIRX#1");

    DT_ASSERT_OK(DtFuncFind(Drv, 1, "AF_ASISDIRX", "SECOND", &Func));
    DT_ASSERT_EQ(DtVecCount(&Func.Parts), 1);
    DT_ASSERT_STR(PartAt(&Func, 0)->Name, "DF_ASIRX#1");
    DtFuncRelease(&Func);

    SimDtPcieOverrideString("AF_ASISDIRX#1.4", 1, false, NULL);
    DT_ASSERT_OK(DtFuncFind(Drv, 1, "AF_ASISDIRX", "", &Func));
    DT_ASSERT_EQ(DtVecCount(&Func.Parts), 3);
    DtFuncRelease(&Func);

    FINISH(Drv, Live);
}

// Failing to read an instance or a part's name is returned, with nothing kept; failing to
// read a part's role, type or UUID, or a name of another kind, leaves the part out.
DT_TEST(ReadFailures)
{
    DtFuncInstance Func;
    long Live;
    OsDrv* Drv = OpenSim(DtFailures, &Live);

    if (Drv == NULL)
        return;

    SimDtPcieFailProperty("AF_ASISDIRX#1", 2, true, DT_STATUS_TIMEOUT);
    DT_ASSERT_EQ(DtFuncFind(Drv, 2, "AF_ASISDIRX", "", &Func), DTAPI_E_TIMEOUT);

    SimDtPcieFailProperty("AF_ASISDIRX#1.8", 3, true, DT_STATUS_BUSY);
    DT_ASSERT_EQ(DtFuncFind(Drv, 3, "AF_ASISDIRX", "", &Func), DTAPI_E_BUSY);
    DT_ASSERT_EQ(DtVecCount(&Func.Parts), 0);

    SimDtPcieReset();
    SimDtPcieFailProperty("BC_SWITCH#2", 4, true, DT_STATUS_TIMEOUT);
    SimDtPcieOverrideProperty("BC_ST425LR#1_TYPE", 4, false, 0);
    SimDtPcieOverrideProperty("BC_SDIMUX12G#1_UUID", 4, false, 0);
    SimDtPcieOverrideString("AF_ASISDIRX#1.4", 4, true, "XX_SWITCH#3");
    SimDtPcieOverrideString("XX_SWITCH#3", 4, true, "");
    SimDtPcieOverrideProperty("XX_SWITCH#3_TYPE", 4, true, DT_BLOCK_TYPE_SWITCH);
    SimDtPcieOverrideProperty("XX_SWITCH#3_UUID", 4, true, DT_UUID_BC_FLAG | 1);
    DT_ASSERT_OK(DtFuncFind(Drv, 4, "AF_ASISDIRX", "", &Func));
    DT_ASSERT_EQ(DtVecCount(&Func.Parts), PART_COUNT - 4);
    DT_ASSERT_STR(PartAt(&Func, 0)->Name, "BC_SDIRXF#1");
    DtFuncRelease(&Func);

    FINISH(Drv, Live);
}

// When memory runs out the parts found are freed and the failure returned.
DT_TEST(OutOfMemory)
{
    DtFuncInstance Func;
    long Live;
    OsDrv* Drv = OpenSim(DtFailures, &Live);

    if (Drv == NULL)
        return;

    DtAllocFailAfter(0);
    DT_ASSERT_EQ(DtFuncFind(Drv, 0, "AF_ASISDIRX", "", &Func), DTAPI_E_OUT_OF_MEM);
    DtAllocFailAfter(-1);
    DT_ASSERT_EQ(DtVecCount(&Func.Parts), 0);

    FINISH(Drv, Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Get +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A part is got by kind, type and role; of parts alike the last.
DT_TEST(PartsAreGotByKindTypeAndRole)
{
    DtFuncInstance Func;
    const DtFuncPart* Part;
    long Live;
    OsDrv* Drv = OpenSim(DtFailures, &Live);

    if (Drv == NULL)
        return;

    DT_ASSERT_OK(DtFuncFind(Drv, 0, "AF_ASISDIRX", "", &Func));

    Part = DtFuncGet(&Func, true, DT_FUNC_TYPE_SDIRX, "");
    DT_ASSERT(Part != NULL && strcmp(Part->Name, "DF_SDIRX#1") == 0);
    Part = DtFuncGet(&Func, false, DT_BLOCK_TYPE_SWITCH, "SDI_MUX_OUT");
    DT_ASSERT(Part != NULL && strcmp(Part->Name, "BC_SWITCH#3") == 0);
    Part = DtFuncGet(&Func, false, DT_BLOCK_TYPE_SWITCH, "SDI_MUX_IN");
    DT_ASSERT(Part != NULL && strcmp(Part->Name, "BC_SWITCH#2") == 0);

    DT_ASSERT(DtFuncGet(&Func, false, DT_BLOCK_TYPE_SWITCH, "") == NULL);
    DT_ASSERT(DtFuncGet(&Func, true, DT_FUNC_TYPE_SDIRX, "OTHER") == NULL);
    DT_ASSERT(DtFuncGet(&Func, false, DT_FUNC_TYPE_SDIRX, "") == NULL);
    DT_ASSERT(DtFuncGet(&Func, true, DT_FUNC_TYPE_SDITXPHY, "") == NULL);
    DtFuncRelease(&Func);

    SimDtPcieOverrideString("BC_SWITCH#3", 0, true, "SDI_MUX_IN");
    DT_ASSERT_OK(DtFuncFind(Drv, 0, "AF_ASISDIRX", "", &Func));
    Part = DtFuncGet(&Func, false, DT_BLOCK_TYPE_SWITCH, "SDI_MUX_IN");
    DT_ASSERT(Part != NULL && strcmp(Part->Name, "BC_SWITCH#3") == 0);
    DtFuncRelease(&Func);

    FINISH(Drv, Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Driver versions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Each proxy's minimum, the build number included; a type the table lacks is an internal
// error, as in DTAPI.
DT_TEST(DriverVersionPerProxy)
{
    static const struct
    {
        bool IsDf;
        int Type;
        DtDriverVersion Enough;
        DtDriverVersion TooOld;
    } Cases[] = {
        {true, DT_FUNC_TYPE_SDIRX, {1, 4, 0, 111}, {1, 4, 0, 110}},
        {true, DT_FUNC_TYPE_CHSDIRX, {2, 0, 2, 328}, {2, 0, 2, 327}},
        {true, DT_FUNC_TYPE_ASIRX, {1, 0, 4, 48}, {1, 0, 4, 47}},
        {true, DT_FUNC_TYPE_SDITXPHY, {1, 5, 4, 143}, {1, 5, 4, 142}},
        {false, DT_BLOCK_TYPE_BURSTFIFO, {1, 0, 5, 50}, {1, 0, 5, 49}},
        {false, DT_BLOCK_TYPE_CDMAC, {1, 0, 4, 48}, {1, 0, 4, 47}},
        {false, DT_BLOCK_TYPE_SDIDMX12G, {1, 2, 1, 68}, {1, 2, 1, 67}},
        {false, DT_BLOCK_TYPE_SDITXF, {1, 0, 4, 48}, {1, 0, 3, 99}},
        {false, DT_BLOCK_TYPE_SDITXP, {1, 0, 4, 48}, {0, 9, 9, 999}},
        {false, DT_BLOCK_TYPE_SWITCH, {1, 0, 4, 48}, {1, 0, 4, 47}},
    };
    const DtDriverVersion Newest = {3, 6, 4, 398};
    size_t i;

    for (i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        bool IsDf = Cases[i].IsDf;

        if (DtFuncCheckDriverVersion(&Cases[i].Enough, IsDf, Cases[i].Type) != DTAPI_OK ||
            DtFuncCheckDriverVersion(&Newest, IsDf, Cases[i].Type) != DTAPI_OK ||
            DtFuncCheckDriverVersion(&Cases[i].TooOld, IsDf, Cases[i].Type) !=
                DTAPI_E_DRIVER_INCOMP)
        {
            DT_FAIL("type %d", Cases[i].Type);
        }
    }

    DT_ASSERT_EQ(DtFuncCheckDriverVersion(&Newest, false, DT_FUNC_TYPE_SDIRX),
                 DTAPI_E_INTERNAL);
    DT_ASSERT_EQ(DtFuncCheckDriverVersion(&Newest, true, DT_FUNC_TYPE_GENLOCKCTRL),
                 DTAPI_E_INTERNAL);
}

DT_TEST_MAIN("SimFunc", DT_RUN(PartsOfTheReceiverFunction),
             DT_RUN(MissingFunctionIsNotFound), DT_RUN(InstanceIsChosenByRole),
             DT_RUN(ReadFailures), DT_RUN(OutOfMemory),
             DT_RUN(PartsAreGotByKindTypeAndRole), DT_RUN(DriverVersionPerProxy))
