// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestSimFunc.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Finding the objects of an API function against the emulator
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPI_SIM=1. Every case starts from the emulator's power-on
// state, and ends with no handle to it and no allocation left open.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"           // Live allocations and allocation failures.
#include "DtFunc.h"                 // Functions under test.
#include "DtPcieAbi.h"              // Types, UUID flags and driver statuses.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // Device handles.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The objects of the ASI/SDI receiver of every SDI port, as a DTA-2178 lists them.
static const struct
{
    const char* Name;
    const char* Role;
    bool IsDf;
    int Type;
} g_Objects[] = {
    {"BC_SWITCH#2", "SDI_MUX_IN", false, DT_BLOCK_TYPE_SWITCH},
    {"BC_ST425LR#1", "", false, DT_BLOCK_TYPE_ST425LR},
    {"BC_SDIMUX12G#1", "", false, DT_BLOCK_TYPE_SDIMUX12G},
    {"BC_SWITCH#3", "SDI_MUX_OUT", false, DT_BLOCK_TYPE_SWITCH},
    {"BC_SDIRXF#1", "", false, DT_BLOCK_TYPE_SDIRXF},
    {"DF_SDIRX#1", "", true, DT_FUNC_TYPE_SDIRX},
    {"DF_ASIRX#1", "", true, DT_FUNC_TYPE_ASIRX},
    {"DF_CHSDIRX#1", "", true, DT_FUNC_TYPE_CHSDIRX},
};

#define OBJECT_COUNT (sizeof(g_Objects) / sizeof(g_Objects[0]))

// Opens the emulated device in its power-on state and notes the live allocations. Returns
// NULL, having recorded a failure, when it is not the emulator.
static OsDrv* OpenSim(int* DtFailures, int* Live)
{
    OsDrv* Drv;

    SimDtPcie_Reset();
    DtAlloc_ResetCount();
    *Live = DtAlloc_Live();
    Drv = OsDrv_Open(SIM_DEVICE_INDEX);
    if (Drv == NULL || !OsDrv_IsEmulated(Drv))
    {
        printf("    FAIL: no emulated device at index 0; is CDTAPI_SIM=1 set?\n");
        (*DtFailures)++;
        OsDrv_Close(Drv);
        return NULL;
    }
    return Drv;
}

// Closes the device and checks that nothing is left open or allocated.
#define FINISH(Drv, Live)                                                                \
    do                                                                                   \
    {                                                                                    \
        OsDrv_Close(Drv);                                                                \
        DT_ASSERT_EQ(SimDtPcie_OpenHandles(), 0);                                        \
        DT_ASSERT_EQ(DtAlloc_Live(), Live);                                              \
    } while (0)

// The object at Index of an instance.
static const DtFuncObject* ObjectAt(const DtFuncInstance* Instance, size_t Index)
{
    return &DT_VEC_AT(&Instance->Objects, DtFuncObject, Index);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Find +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Every object comes with its name, role, kind, type and the UUID the card gives it, in
// the card's order.
DT_TEST(ObjectsOfTheReceiverFunction)
{
    int Live;
    OsDrv* Drv = OpenSim(DtFailures, &Live);

    if (Drv == NULL)
        return;

    DtFuncInstance Func;
    DT_ASSERT_OK(DtFunc_Find(Drv, 5, "AF_ASISDIRX", "", &Func));
    DT_ASSERT_EQ(Func.PortIndex, 5);
    DT_ASSERT_EQ(DtVec_Count(&Func.Objects), OBJECT_COUNT);
    for (size_t i = 0; i < OBJECT_COUNT && i < DtVec_Count(&Func.Objects); i++)
    {
        const DtFuncObject* Object = ObjectAt(&Func, i);
        int Uuid = 0;

        DT_ASSERT_STR(Object->Name, g_Objects[i].Name);
        DT_ASSERT_STR(Object->Role, g_Objects[i].Role);
        DT_ASSERT_EQ(Object->IsDf, g_Objects[i].IsDf);
        DT_ASSERT_EQ(Object->Type, g_Objects[i].Type);
        char Key[PROPERTY_NAME_MAX_SIZE];
        snprintf(Key, sizeof(Key), "%s_UUID", g_Objects[i].Name);
        DT_ASSERT_OK(DtPcieCmd_GetPropertyInt(Drv, Key, 5, &Uuid));
        DT_ASSERT_EQ(Object->Ref.Uuid, Uuid);
    }
    DtFunc_Release(&Func);
    DT_ASSERT_EQ(DtVec_Count(&Func.Objects), 0);

    FINISH(Drv, Live);
}

// The transmitter and the DMA of every SDI port have the objects a DTA-2178 listed for
// its port 5, in that order.
DT_TEST(ObjectsOfTheTransmitFunctions)
{
    static const struct
    {
        const char* Af;
        const char* Name;
        const char* Role;
        bool IsDf;
        int Type;
    } Expected[] = {
        {"AF_ASISDITX", "BC_ASITXG#1", "", false, DT_BLOCK_TYPE_ASITXG},
        {"AF_ASISDITX", "BC_SDITXF#1", "", false, DT_BLOCK_TYPE_SDITXF},
        {"AF_ASISDITX", "BC_SWITCH#6", "SDI_DEMUX_IN", false, DT_BLOCK_TYPE_SWITCH},
        {"AF_ASISDITX", "BC_SDIDMX12G#1", "", false, DT_BLOCK_TYPE_SDIDMX12G},
        {"AF_ASISDITX", "BC_SWITCH#7", "SDI_DEMUX_OUT", false, DT_BLOCK_TYPE_SWITCH},
        {"AF_ASISDITX", "BC_SDITXP#1", "", false, DT_BLOCK_TYPE_SDITXP},
        {"AF_ASISDITX", "DF_SDITXPHY#1", "", true, DT_FUNC_TYPE_SDITXPHY},
        {"AF_DMA", "BC_CDMAC#1", "", false, DT_BLOCK_TYPE_CDMAC},
        {"AF_DMA", "BC_BURSTFIFO#1", "", false, DT_BLOCK_TYPE_BURSTFIFO},
        {"AF_DMA", "BC_CONSTSOURCE#1", "", false, DT_BLOCK_TYPE_CONSTSOURCE},
        {"AF_DMA", "BC_CONSTSINK#1", "", false, DT_BLOCK_TYPE_CONSTSINK},
    };
    const size_t Count = sizeof(Expected) / sizeof(Expected[0]);
    int Live;
    OsDrv* Drv = OpenSim(DtFailures, &Live);

    if (Drv == NULL)
        return;

    for (int Port = 0; Port < SIM_SDI_PORT_COUNT; Port++)
    {
        DtFuncInstance Tx;

        DT_ASSERT_OK(DtFunc_Find(Drv, Port, "AF_ASISDITX", "", &Tx));
        DtFuncInstance Dma;
        DT_ASSERT_OK(DtFunc_Find(Drv, Port, "AF_DMA", "", &Dma));
        DT_ASSERT_EQ(DtVec_Count(&Tx.Objects) + DtVec_Count(&Dma.Objects), Count);
        for (size_t i = 0;
             i < Count && i < DtVec_Count(&Tx.Objects) + DtVec_Count(&Dma.Objects); i++)
        {
            bool InTx = strcmp(Expected[i].Af, "AF_ASISDITX") == 0;
            size_t At = InTx ? i : i - DtVec_Count(&Tx.Objects);
            const DtFuncObject* Object = ObjectAt(InTx ? &Tx : &Dma, At);

            DT_ASSERT_STR(Object->Name, Expected[i].Name);
            DT_ASSERT_STR(Object->Role, Expected[i].Role);
            DT_ASSERT_EQ(Object->IsDf, Expected[i].IsDf);
            DT_ASSERT_EQ(Object->Type, Expected[i].Type);
            DT_ASSERT_EQ(Object->Ref.Uuid & DT_UUID_FLAG_MASK,
                         Expected[i].IsDf ? DT_UUID_DF_FLAG : DT_UUID_BC_FLAG);
        }
        DtFunc_Release(&Tx);
        DtFunc_Release(&Dma);
    }
    DtFuncInstance None;
    DT_ASSERT_EQ(DtFunc_Find(Drv, SIM_SDI_PORT_COUNT, "AF_DMA", "", &None),
                 DTAPI_E_NOT_FOUND);
    DT_ASSERT_EQ(DtFunc_Find(Drv, SIM_SDI_PORT_COUNT, "AF_ASISDITX", "", &None),
                 DTAPI_E_NOT_FOUND);

    FINISH(Drv, Live);
}

// No two objects of the card share a UUID, across API functions and ports.
DT_TEST(UuidsAreUnique)
{
    static const char* const Functions[] = {"AF_ASISDIRX", "AF_ASISDITX", "AF_DMA"};
    int Uuids[SIM_SDI_PORT_COUNT * 32];
    int Count = 0;
    int Live;
    int Port;
    int j;
    size_t f;
    size_t i;
    OsDrv* Drv = OpenSim(DtFailures, &Live);

    if (Drv == NULL)
        return;

    for (Port = 0; Port < SIM_SDI_PORT_COUNT; Port++)
    {
        for (f = 0; f < sizeof(Functions) / sizeof(Functions[0]); f++)
        {
            DtFuncInstance Func;

            DT_ASSERT_OK(DtFunc_Find(Drv, Port, Functions[f], "", &Func));
            for (i = 0; i < DtVec_Count(&Func.Objects) &&
                        Count < (int)(sizeof(Uuids) / sizeof(Uuids[0]));
                 i++)
            {
                int Uuid = ObjectAt(&Func, i)->Ref.Uuid;

                for (j = 0; j < Count; j++)
                {
                    if (Uuids[j] == Uuid)
                        DT_FAIL("%s of port %d repeats UUID 0x%x",
                                ObjectAt(&Func, i)->Name, Port, Uuid);
                }
                Uuids[Count++] = Uuid;
            }
            DtFunc_Release(&Func);
        }
    }
    DT_ASSERT_EQ(Count, SIM_SDI_PORT_COUNT * 19);

    FINISH(Drv, Live);
}

// An API function, instance role or port without it is not found, and leaves the instance
// empty; a name too long for a property is refused.
DT_TEST(MissingFunctionIsNotFound)
{
    int Live;
    OsDrv* Drv = OpenSim(DtFailures, &Live);

    if (Drv == NULL)
        return;

    DtFuncInstance Func;
    DT_ASSERT_EQ(DtFunc_Find(Drv, 5, "AF_ASISDIMON", "", &Func), DTAPI_E_NOT_FOUND);
    DT_ASSERT_EQ(DtVec_Count(&Func.Objects), 0);
    DT_ASSERT_EQ(DtFunc_Find(Drv, 5, "AF_ASISDIRX", "OTHER", &Func), DTAPI_E_NOT_FOUND);
    DT_ASSERT_EQ(DtFunc_Find(Drv, SIM_SDI_PORT_COUNT, "AF_ASISDIRX", "", &Func),
                 DTAPI_E_NOT_FOUND);

    char TooLong[PROPERTY_NAME_MAX_SIZE];
    memset(TooLong, 'A', sizeof(TooLong) - 1);
    TooLong[sizeof(TooLong) - 1] = '\0';
    DT_ASSERT_EQ(DtFunc_Find(Drv, 5, TooLong, "", &Func), DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_EQ(DtVec_Count(&Func.Objects), 0);

    FINISH(Drv, Live);
}

// The first instance with the role is read; objects end at the first one not found.
DT_TEST(InstanceIsChosenByRole)
{
    int Live;
    OsDrv* Drv = OpenSim(DtFailures, &Live);

    if (Drv == NULL)
        return;

    SimDtPcie_OverrideString("AF_ASISDIRX#2", 1, true, "SECOND");
    SimDtPcie_OverrideString("AF_ASISDIRX#2.1", 1, true, "DF_ASIRX#1");
    SimDtPcie_OverrideString("AF_ASISDIRX#3", 1, true, "SECOND");
    SimDtPcie_OverrideString("AF_ASISDIRX#3.1", 1, true, "DF_SDIRX#1");

    DtFuncInstance Func;
    DT_ASSERT_OK(DtFunc_Find(Drv, 1, "AF_ASISDIRX", "SECOND", &Func));
    DT_ASSERT_EQ(DtVec_Count(&Func.Objects), 1);
    DT_ASSERT_STR(ObjectAt(&Func, 0)->Name, "DF_ASIRX#1");
    DtFunc_Release(&Func);

    SimDtPcie_OverrideString("AF_ASISDIRX#1.4", 1, false, NULL);
    DT_ASSERT_OK(DtFunc_Find(Drv, 1, "AF_ASISDIRX", "", &Func));
    DT_ASSERT_EQ(DtVec_Count(&Func.Objects), 3);
    DtFunc_Release(&Func);

    FINISH(Drv, Live);
}

// Failing to read an instance or an object's name is returned, with nothing kept;
// failing to read an object's role, type or UUID, or a name of another kind, leaves the
// object out.
DT_TEST(ReadFailures)
{
    int Live;
    OsDrv* Drv = OpenSim(DtFailures, &Live);

    if (Drv == NULL)
        return;

    SimDtPcie_FailProperty("AF_ASISDIRX#1", 2, true, DT_STATUS_TIMEOUT);
    DtFuncInstance Func;
    DT_ASSERT_EQ(DtFunc_Find(Drv, 2, "AF_ASISDIRX", "", &Func), DTAPI_E_TIMEOUT);

    SimDtPcie_FailProperty("AF_ASISDIRX#1.8", 3, true, DT_STATUS_BUSY);
    DT_ASSERT_EQ(DtFunc_Find(Drv, 3, "AF_ASISDIRX", "", &Func), DTAPI_E_BUSY);
    DT_ASSERT_EQ(DtVec_Count(&Func.Objects), 0);

    SimDtPcie_Reset();
    SimDtPcie_FailProperty("BC_SWITCH#2", 4, true, DT_STATUS_TIMEOUT);
    SimDtPcie_OverrideProperty("BC_ST425LR#1_TYPE", 4, false, 0);
    SimDtPcie_OverrideProperty("BC_SDIMUX12G#1_UUID", 4, false, 0);
    SimDtPcie_OverrideString("AF_ASISDIRX#1.4", 4, true, "XX_SWITCH#3");
    SimDtPcie_OverrideString("XX_SWITCH#3", 4, true, "");
    SimDtPcie_OverrideProperty("XX_SWITCH#3_TYPE", 4, true, DT_BLOCK_TYPE_SWITCH);
    SimDtPcie_OverrideProperty("XX_SWITCH#3_UUID", 4, true, DT_UUID_BC_FLAG | 1);
    DT_ASSERT_OK(DtFunc_Find(Drv, 4, "AF_ASISDIRX", "", &Func));
    DT_ASSERT_EQ(DtVec_Count(&Func.Objects), OBJECT_COUNT - 4);
    DT_ASSERT_STR(ObjectAt(&Func, 0)->Name, "BC_SDIRXF#1");
    DtFunc_Release(&Func);

    FINISH(Drv, Live);
}

// When memory runs out the objects found are freed and the failure returned.
DT_TEST(OutOfMemory)
{
    int Live;
    OsDrv* Drv = OpenSim(DtFailures, &Live);

    if (Drv == NULL)
        return;

    DtAlloc_FailAfter(0);
    DtFuncInstance Func;
    DT_ASSERT_EQ(DtFunc_Find(Drv, 0, "AF_ASISDIRX", "", &Func), DTAPI_E_OUT_OF_MEM);
    DtAlloc_FailAfter(-1);
    DT_ASSERT_EQ(DtVec_Count(&Func.Objects), 0);

    FINISH(Drv, Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Get +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// An object is got by kind, type and role; of objects alike the last.
DT_TEST(ObjectsAreGotByKindTypeAndRole)
{
    int Live;
    OsDrv* Drv = OpenSim(DtFailures, &Live);

    if (Drv == NULL)
        return;

    DtFuncInstance Func;
    DT_ASSERT_OK(DtFunc_Find(Drv, 0, "AF_ASISDIRX", "", &Func));

    const DtFuncObject* Object = DtFunc_Get(&Func, true, DT_FUNC_TYPE_SDIRX, "");
    DT_ASSERT(Object != NULL && strcmp(Object->Name, "DF_SDIRX#1") == 0);
    Object = DtFunc_Get(&Func, false, DT_BLOCK_TYPE_SWITCH, "SDI_MUX_OUT");
    DT_ASSERT(Object != NULL && strcmp(Object->Name, "BC_SWITCH#3") == 0);
    Object = DtFunc_Get(&Func, false, DT_BLOCK_TYPE_SWITCH, "SDI_MUX_IN");
    DT_ASSERT(Object != NULL && strcmp(Object->Name, "BC_SWITCH#2") == 0);

    DT_ASSERT(DtFunc_Get(&Func, false, DT_BLOCK_TYPE_SWITCH, "") == NULL);
    DT_ASSERT(DtFunc_Get(&Func, true, DT_FUNC_TYPE_SDIRX, "OTHER") == NULL);
    DT_ASSERT(DtFunc_Get(&Func, false, DT_FUNC_TYPE_SDIRX, "") == NULL);
    DT_ASSERT(DtFunc_Get(&Func, true, DT_FUNC_TYPE_SDITXPHY, "") == NULL);
    DtFunc_Release(&Func);

    SimDtPcie_OverrideString("BC_SWITCH#3", 0, true, "SDI_MUX_IN");
    DT_ASSERT_OK(DtFunc_Find(Drv, 0, "AF_ASISDIRX", "", &Func));
    Object = DtFunc_Get(&Func, false, DT_BLOCK_TYPE_SWITCH, "SDI_MUX_IN");
    DT_ASSERT(Object != NULL && strcmp(Object->Name, "BC_SWITCH#3") == 0);
    DtFunc_Release(&Func);

    FINISH(Drv, Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Driver versions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Each proxy's minimum, the build number included; a type the table lacks is an
// internal error.
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
        {true, DT_FUNC_TYPE_NW, {2, 0, 0, 1}, {2, 0, 0, 0}},
        {true, DT_FUNC_TYPE_SDITXPHY, {1, 5, 4, 143}, {1, 5, 4, 142}},
        {false, DT_BLOCK_TYPE_BURSTFIFO, {1, 0, 5, 50}, {1, 0, 5, 49}},
        {false, DT_BLOCK_TYPE_CDMAC, {1, 0, 4, 48}, {1, 0, 4, 47}},
        {false, DT_BLOCK_TYPE_SDIDMX12G, {1, 2, 1, 68}, {1, 2, 1, 67}},
        {false, DT_BLOCK_TYPE_SDITXF, {1, 0, 4, 48}, {1, 0, 3, 99}},
        {false, DT_BLOCK_TYPE_SDITXP, {1, 0, 4, 48}, {0, 9, 9, 999}},
        {false, DT_BLOCK_TYPE_SWITCH, {1, 0, 4, 48}, {1, 0, 4, 47}},
    };
    const DtDriverVersion Newest = {3, 6, 4, 398};

    for (size_t i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        bool IsDf = Cases[i].IsDf;

        if (DtFunc_CheckDriverVersion(&Cases[i].Enough, IsDf, Cases[i].Type) !=
                DTAPI_OK ||
            DtFunc_CheckDriverVersion(&Newest, IsDf, Cases[i].Type) != DTAPI_OK ||
            DtFunc_CheckDriverVersion(&Cases[i].TooOld, IsDf, Cases[i].Type) !=
                DTAPI_E_DRIVER_INCOMP)
        {
            DT_FAIL("type %d", Cases[i].Type);
        }
    }

    DT_ASSERT_EQ(DtFunc_CheckDriverVersion(&Newest, false, DT_FUNC_TYPE_SDIRX),
                 DTAPI_E_INTERNAL);
    DT_ASSERT_EQ(DtFunc_CheckDriverVersion(&Newest, true, DT_FUNC_TYPE_GENLOCKCTRL),
                 DTAPI_E_INTERNAL);
}

DT_TEST_MAIN("SimFunc", DT_RUN(ObjectsOfTheReceiverFunction),
             DT_RUN(ObjectsOfTheTransmitFunctions), DT_RUN(UuidsAreUnique),
             DT_RUN(MissingFunctionIsNotFound), DT_RUN(InstanceIsChosenByRole),
             DT_RUN(ReadFailures), DT_RUN(OutOfMemory),
             DT_RUN(ObjectsAreGotByKindTypeAndRole), DT_RUN(DriverVersionPerProxy))
