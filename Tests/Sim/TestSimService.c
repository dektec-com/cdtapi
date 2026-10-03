// #*#*#*#*#*#*#*#*#*#*#*#*#* TestSimService.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The proxy to DtapiService and the PTP clock slave, against the emulator
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPI_SIM=1, so that a proxy talks to the emulated DtapiService
// in the program. Every case starts from the emulator's power-on state with the DTA-2110
// attached, whose slave is off, and ends with no allocation left.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"       // Live allocations.
#include "DtTest.h"             // Test framework.
#include "OAL/Sim/SimDtPcie.h"  // The emulator's reset.
#include "OAL/Sim/SimDta2110.h" // The DTA-2110's serial number.
#include "OAL/Sim/SimService.h" // The emulated grandmaster, and whether it saved.
#include "cdtapi_avfifo.h"      // GetLastException.
#include "cdtapi_service.h"     // Interface under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The DTA-2110's driver index, and its port.
#define INDEX 1
#define PORT 1

typedef struct Fixture
{
    int Live; // The allocations before the case
    DtDevice* Device;
    DtServiceProxy* Proxy;
    DtServiceProxy* Other;
} Fixture;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FreeAll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void FreeAll(Fixture* Fix)
{
    DtServiceProxy_Detach(Fix->Other);
    DtServiceProxy_Detach(Fix->Proxy);
    Fix->Other = NULL;
    Fix->Proxy = NULL;
    DtDevice_Freep(&Fix->Device);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Cleanup -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Frees what the case made when an assertion fails before FINISH does.
//
static void Cleanup(void* Context)
{
    FreeAll((Fixture*)Context);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Open -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Resets the emulator and attaches the DTA-2110.
//
static bool Open(Fixture* Fix, int* DtFailures)
{
    SimDtPcie_Reset();
    memset(Fix, 0, sizeof(*Fix));
    Fix->Live = DtAlloc_NumLive();
    DtTest_SetCleanup(Cleanup, Fix);
    SimDtPcie_SetDta2110Index(INDEX);
    Fix->Device = DtDevice_Alloc();
    if (Fix->Device == NULL ||
        DtDevice_AttachToSerial(Fix->Device, (int64_t)SIM_DTA2110_SERIAL) != DTAPI_OK)
    {
        printf("    FAIL: no emulated DTA-2110; is CDTAPI_SIM=1 set?\n");
        (*DtFailures)++;
        DtTest_Cleanup();
        return false;
    }
    return true;
}

// Frees the proxies and the device and checks that nothing is left.
#define FINISH(Fix)                                                                      \
    do                                                                                   \
    {                                                                                    \
        DtTest_SetCleanup(NULL, NULL);                                                   \
        FreeAll(&(Fix));                                                                 \
        SimDtPcie_Reset();                                                               \
        DT_ASSERT_EQ(DtAlloc_NumLive(), (Fix).Live);                                     \
    } while (0)

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ParId -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The ID of the slave's parameter Name, through the proxy's descriptions; -1 when it
// cannot be found.
//
static int ParId(DtServiceProxy* Proxy, const char* Name)
{
    DtServiceParDesc* Descs = NULL;
    int NumDescs = 0;
    if (DtServiceProxy_GetParDescs(Proxy, "General", &Descs, &NumDescs) != DTAPI_OK)
        return -1;
    int Id = DtServiceProxy_FindParId(Descs, NumDescs, Name);
    DtServiceProxy_FreeParDescs(Descs, NumDescs);
    return Id;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IntValue -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtVariant IntValue(int Value)
{
    DtVariant Variant;
    memset(&Variant, 0, sizeof(Variant));
    Variant.Type = DT_VARIANT_INT;
    Variant.Int = Value;
    return Variant;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BoolValue -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtVariant BoolValue(bool Value)
{
    DtVariant Variant;
    memset(&Variant, 0, sizeof(Variant));
    Variant.Type = DT_VARIANT_BOOL;
    Variant.Bool = Value;
    return Variant;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reading +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(SlaveStartsOff)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;
    DtPtpStatus Status;
    DT_ASSERT_OK(DtDevice_GetPtpStatus(Fix.Device, PORT, &Status));
    DT_ASSERT_EQ(Status.SlaveState, DT_PTP_SLAVE_DISABLED);
    DT_ASSERT_EQ(Status.LockStatus, DT_PTP_LOCK_NOT_IN_USE);
    DT_ASSERT(!Status.HasMaster);
    FINISH(Fix);
}

DT_TEST(DescriptionsHaveTypesEnumsAndDefaults)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtServiceProxy_Attach(Fix.Device, PORT, DT_SERVICE_PTP_CLOCK_SLAVE,
                                       false, &Fix.Proxy));
    char** Groups = NULL;
    int NumGroups = 0;
    DT_ASSERT_OK(DtServiceProxy_GetParGroups(Fix.Proxy, &Groups, &NumGroups));
    DT_ASSERT_EQ(NumGroups, 1);
    DT_ASSERT_STR(Groups[0], "General");
    DtServiceProxy_FreeParGroups(Groups, NumGroups);

    DtServiceParDesc* Descs = NULL;
    int NumDescs = 0;
    DT_ASSERT_OK(DtServiceProxy_GetParDescs(Fix.Proxy, "General", &Descs, &NumDescs));
    DT_ASSERT_EQ(NumDescs, 19);
    int Writable = 0;
    const DtServiceParDesc* Delay = NULL;
    const DtServiceParDesc* Domain = NULL;
    const DtServiceParDesc* Peer = NULL;
    const DtServiceParDesc* Sync = NULL;
    for (int i = 0; i < NumDescs; i++)
    {
        Writable += Descs[i].IsWritable;
        if (strcmp(Descs[i].Name, "DelayMechanism") == 0)
            Delay = &Descs[i];
        if (strcmp(Descs[i].Name, "DomainNumber") == 0)
            Domain = &Descs[i];
        if (strcmp(Descs[i].Name, "PeerUnicastAddress") == 0)
            Peer = &Descs[i];
        if (strcmp(Descs[i].Name, "MasterSyncInterval") == 0)
            Sync = &Descs[i];
    }
    bool Ok = Writable == 6 && Delay != NULL && Delay->NumEnumVals == 3 &&
              Delay->EnumVals[1].Value == DT_PTP_DELAY_END_TO_END &&
              strcmp(Delay->EnumVals[1].Name, "EndToEnd") == 0 && Domain != NULL &&
              Domain->Type == DT_VARIANT_INT && Domain->Max.Int == 127 &&
              Domain->Default.Int == 127 && Domain->Description[0] != '\0' &&
              Peer != NULL && Peer->Type == DT_VARIANT_STRING &&
              strcmp(Peer->Default.String, "0.0.0.0") == 0 && Sync != NULL &&
              Sync->Type == DT_VARIANT_DOUBLE && Sync->Min.Double == -5.0;
    DtServiceProxy_FreeParDescs(Descs, NumDescs);
    DT_ASSERT(Ok);
    DT_ASSERT_EQ(DtServiceProxy_GetParDescs(Fix.Proxy, "Other", &Descs, &NumDescs),
                 DTAPI_E_NOT_FOUND);
    DT_ASSERT_EQ(DtServiceProxy_LastException(Fix.Proxy),
                 DT_SERVICE_EXC_UNKNOWN_GROUP_NAME);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Writing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(SlaveSwitchedOnLocks)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtServiceProxy_Attach(Fix.Device, PORT, DT_SERVICE_PTP_CLOCK_SLAVE, true,
                                       &Fix.Proxy));
    DtServiceParVal Settings[2];
    Settings[0].ParId = ParId(Fix.Proxy, "DomainNumber");
    Settings[0].Value = IntValue(0);
    Settings[1].ParId = ParId(Fix.Proxy, "Enable");
    Settings[1].Value = BoolValue(true);
    DT_ASSERT_OK(DtServiceProxy_SetParVals(Fix.Proxy, Settings, 2));
    DT_ASSERT(!SimService_WasSaved());
    DT_ASSERT_OK(DtServiceProxy_SaveSettings(Fix.Proxy));
    DT_ASSERT(SimService_WasSaved());

    // A double, which the service writes with a decimal point.
    DtVariant Sync;
    DT_ASSERT_OK(DtServiceProxy_GetParVal(Fix.Proxy,
                                          ParId(Fix.Proxy, "MasterSyncInterval"), &Sync));
    DT_ASSERT_EQ(Sync.Type, DT_VARIANT_DOUBLE);
    DT_ASSERT(Sync.Double == -3.0);
    DtServiceProxy_Detach(Fix.Proxy);
    Fix.Proxy = NULL;

    static const uint8_t Eui64[8] = {0x00, 0x1B, 0x19, 0xFF, 0xFE, 0x00, 0x00, 0x01};
    DtPtpStatus Status;
    DT_ASSERT_OK(DtDevice_GetPtpStatus(Fix.Device, PORT, &Status));
    DT_ASSERT_EQ(Status.SlaveState, DT_PTP_SLAVE_SLAVE);
    DT_ASSERT_EQ(Status.LockStatus, DT_PTP_LOCK_LOCKED);
    DT_ASSERT(Status.HasMaster);
    DT_ASSERT_MEM(Status.GrandmasterId, Eui64, 8);
    DT_ASSERT_EQ(Status.Domain, 0);
    DT_ASSERT(Status.IsTimeTraceable);
    DT_ASSERT_EQ(Status.ClockClass, 6);
    DT_ASSERT_EQ(Status.StepsRemoved, 1);
    FINISH(Fix);
}

DT_TEST(MasterListHasTheGrandmaster)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtServiceProxy_Attach(Fix.Device, PORT, DT_SERVICE_PTP_CLOCK_SLAVE, true,
                                       &Fix.Proxy));
    DtVariant On = BoolValue(true);
    DT_ASSERT_OK(DtServiceProxy_SetParVal(Fix.Proxy, ParId(Fix.Proxy, "Enable"), &On));
    DtServiceParVal Vals[2];
    memset(Vals, 0, sizeof(Vals));
    Vals[0].ParId = ParId(Fix.Proxy, "MasterInfo");
    Vals[1].ParId = ParId(Fix.Proxy, "MasterIdentity");
    DT_ASSERT_OK(DtServiceProxy_GetParVals(Fix.Proxy, Vals, 2));
    DT_ASSERT(Vals[1].Value.UInt64 == SIM_SERVICE_GRANDMASTER);
    DtPtpMasterInfo* Masters = NULL;
    int NumMasters = 0;
    DtapiResult Result =
        DtPtp_ParseMasterInfo(Vals[0].Value.String, &Masters, &NumMasters);
    DtVariant_Clear(&Vals[0].Value);
    DtVariant_Clear(&Vals[1].Value);
    DT_ASSERT_OK(Result);
    bool Ok = NumMasters == 1 &&
              Masters[0].GrandmasterIdentity == SIM_SERVICE_GRANDMASTER &&
              Masters[0].DomainNumber == 127 && Masters[0].IsTimeTraceable;
    DtPtp_FreeMasterInfo(Masters);
    DT_ASSERT(Ok);
    FINISH(Fix);
}

DT_TEST(PeerAddressIsAString)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtServiceProxy_Attach(Fix.Device, PORT, DT_SERVICE_PTP_CLOCK_SLAVE, true,
                                       &Fix.Proxy));
    int Peer = ParId(Fix.Proxy, "PeerUnicastAddress");
    DtVariant Address;
    memset(&Address, 0, sizeof(Address));
    Address.Type = DT_VARIANT_STRING;
    Address.String = "192.168.39.254 & <more>";
    DT_ASSERT_OK(DtServiceProxy_SetParVal(Fix.Proxy, Peer, &Address));
    DtVariant Read;
    DT_ASSERT_OK(DtServiceProxy_GetParVal(Fix.Proxy, Peer, &Read));
    DT_ASSERT_STR(Read.String, "192.168.39.254 & <more>");
    DtVariant_Clear(&Read);
    DT_ASSERT(Read.String == NULL);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Refusals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(SettingNeedsExclusiveAccess)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtServiceProxy_Attach(Fix.Device, PORT, DT_SERVICE_PTP_CLOCK_SLAVE,
                                       false, &Fix.Proxy));
    DtVariant On = BoolValue(true);
    DT_ASSERT_EQ(DtServiceProxy_SetParVal(Fix.Proxy, ParId(Fix.Proxy, "Enable"), &On),
                 DTAPI_E_IN_USE);
    DT_ASSERT_EQ(DtServiceProxy_LastException(Fix.Proxy),
                 DT_SERVICE_EXC_NOT_EXCLUSIVE_ACCESS);
    DT_ASSERT(strstr(GetLastException(), "NotExclusiveAccess") != NULL);
    DT_ASSERT_EQ(DtServiceProxy_SaveSettings(Fix.Proxy), DTAPI_E_IN_USE);

    // A call that succeeds clears the last exception.
    DtVariant Value;
    DT_ASSERT_OK(DtServiceProxy_GetParVal(Fix.Proxy, ParId(Fix.Proxy, "Enable"), &Value));
    DT_ASSERT_EQ(DtServiceProxy_LastException(Fix.Proxy), DT_SERVICE_EXC_NONE);
    DT_ASSERT(!Value.Bool);
    FINISH(Fix);
}

DT_TEST(ExclusiveAccessKeepsOthersOut)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtServiceProxy_Attach(Fix.Device, PORT, DT_SERVICE_PTP_CLOCK_SLAVE, true,
                                       &Fix.Proxy));
    DT_ASSERT_EQ(DtServiceProxy_Attach(Fix.Device, PORT, DT_SERVICE_PTP_CLOCK_SLAVE,
                                       false, &Fix.Other),
                 DTAPI_E_IN_USE);
    DT_ASSERT(Fix.Other == NULL);
    DtPtpStatus Status;
    DT_ASSERT_EQ(DtDevice_GetPtpStatus(Fix.Device, PORT, &Status), DTAPI_E_IN_USE);

    // Once it detaches, others may attach again.
    DtServiceProxy_Detach(Fix.Proxy);
    Fix.Proxy = NULL;
    DT_ASSERT_OK(DtServiceProxy_Attach(Fix.Device, PORT, DT_SERVICE_PTP_CLOCK_SLAVE,
                                       false, &Fix.Other));
    FINISH(Fix);
}

DT_TEST(BadValuesAreRefused)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtServiceProxy_Attach(Fix.Device, PORT, DT_SERVICE_PTP_CLOCK_SLAVE, true,
                                       &Fix.Proxy));
    static const struct
    {
        const char* Name;
        DtVariantType Type;
        int Int;
        DtServiceExc Exception;
    } Cases[] = {
        {"DomainNumber", DT_VARIANT_INT, 128, DT_SERVICE_EXC_PAR_OUT_OF_RANGE},
        {"DelayMechanism", DT_VARIANT_INT, 3, DT_SERVICE_EXC_PAR_OUT_OF_RANGE},
        {"Enable", DT_VARIANT_INT, 1, DT_SERVICE_EXC_INVALID_PAR_TYPE},
        {"SlaveState", DT_VARIANT_INT, 5, DT_SERVICE_EXC_READ_ONLY_PAR},
    };
    for (size_t i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        DtVariant Value = IntValue(Cases[i].Int);
        Value.Type = Cases[i].Type;
        DT_ASSERT_EQ(
            DtServiceProxy_SetParVal(Fix.Proxy, ParId(Fix.Proxy, Cases[i].Name), &Value),
            DTAPI_E_INVALID_ARG);
        DT_ASSERT_EQ(DtServiceProxy_LastException(Fix.Proxy), Cases[i].Exception);
    }
    DtVariant Value = IntValue(0);
    DT_ASSERT_EQ(DtServiceProxy_SetParVal(Fix.Proxy, 9999, &Value), DTAPI_E_NOT_FOUND);
    DT_ASSERT_EQ(DtServiceProxy_LastException(Fix.Proxy), DT_SERVICE_EXC_INVALID_PAR_ID);
    DT_ASSERT_EQ(DtServiceProxy_GetParVal(Fix.Proxy, 9999, &Value), DTAPI_E_NOT_FOUND);

    // An empty variant, or a string without one, CDTAPI refuses itself.
    memset(&Value, 0, sizeof(Value));
    DT_ASSERT_EQ(DtServiceProxy_SetParVal(Fix.Proxy, 0, &Value), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtServiceProxy_LastException(Fix.Proxy), DT_SERVICE_EXC_NONE);
    Value.Type = DT_VARIANT_STRING;
    DT_ASSERT_EQ(DtServiceProxy_SetParVal(Fix.Proxy, 0, &Value), DTAPI_E_INVALID_ARG);
    FINISH(Fix);
}

DT_TEST(PortsWithoutTheSlaveAreRefused)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;
    DtServiceProxy* Proxy = NULL;
    DT_ASSERT_EQ(
        DtServiceProxy_Attach(Fix.Device, 2, DT_SERVICE_PTP_CLOCK_SLAVE, false, &Proxy),
        DTAPI_E_NO_SUCH_PORT);
    DT_ASSERT_EQ(DtServiceProxy_Attach(Fix.Device, PORT, DT_SERVICE_NONE, false, &Proxy),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(
        DtServiceProxy_Attach(NULL, PORT, DT_SERVICE_PTP_CLOCK_SLAVE, false, &Proxy),
        DTAPI_E_DEVICE);
    DtPtpStatus Status;
    DT_ASSERT_EQ(DtDevice_GetPtpStatus(Fix.Device, 0, &Status), DTAPI_E_NO_SUCH_PORT);

    // The DTA-2178 has no PTP.
    DtDevice* Sdi = DtDevice_Alloc();
    DT_ASSERT(Sdi != NULL);
    DtapiResult Attached = DtDevice_AttachToSerial(Sdi, (int64_t)SIM_SERIAL);
    DtapiResult Result = DtDevice_GetPtpStatus(Sdi, 1, &Status);
    DtDevice_Free(Sdi);
    DT_ASSERT_OK(Attached);
    DT_ASSERT_EQ(Result, DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT(Proxy == NULL);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= PTP slave +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct SlaveFixture
{
    Fixture Base;
    DtPtpSlave* Slave;
} SlaveFixture;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FreeSlave -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Frees what a slave's case made when an assertion fails before SLAVE_FINISH does.
//
static void FreeSlave(void* Context)
{
    SlaveFixture* Fix = (SlaveFixture*)Context;
    DtPtpSlave_Detach(Fix->Slave);
    Fix->Slave = NULL;
    FreeAll(&Fix->Base);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OpenSlave -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Opens the DTA-2110 and attaches to its slave, with exclusive access when Exclusive.
//
static bool OpenSlave(SlaveFixture* Fix, bool Exclusive, int* DtFailures)
{
    Fix->Slave = NULL;
    if (!Open(&Fix->Base, DtFailures))
        return false;
    DtTest_SetCleanup(FreeSlave, Fix);
    if (DtPtpSlave_Attach(Fix->Base.Device, PORT, Exclusive, &Fix->Slave) != DTAPI_OK)
    {
        printf("    FAIL: cannot attach to the emulated slave\n");
        (*DtFailures)++;
        DtTest_Cleanup();
        return false;
    }
    return true;
}

// Detaches the slave, frees the device and checks that nothing is left.
#define SLAVE_FINISH(Fix)                                                                \
    do                                                                                   \
    {                                                                                    \
        DtPtpSlave_Detach((Fix).Slave);                                                  \
        (Fix).Slave = NULL;                                                              \
        FINISH((Fix).Base);                                                              \
    } while (0)

DT_TEST(SlaveConfigStartsAtTheDefaults)
{
    SlaveFixture Fix;
    if (!OpenSlave(&Fix, false, DtFailures))
        return;
    DtPtpConfig Config;
    DT_ASSERT_OK(DtPtpSlave_GetConfig(Fix.Slave, &Config));
    DT_ASSERT_EQ(Config.Fields, DT_PTP_CONFIG_ALL);
    DT_ASSERT(!Config.Enable);
    DT_ASSERT_EQ(Config.Domain, 127);
    DT_ASSERT_EQ(Config.DelayMechanism, DT_PTP_DELAY_AUTO);
    DT_ASSERT_EQ(Config.NetworkProtocol, DT_PTP_PROTOCOL_IPV4);
    DT_ASSERT_EQ(Config.IpV6Scope, DT_PTP_IPV6_SCOPE_SITE_LOCAL);
    DT_ASSERT_STR(Config.PeerAddress, "0.0.0.0");
    DT_ASSERT(DtPtpSlave_Proxy(Fix.Slave) != NULL);
    DT_ASSERT(DtPtpSlave_Proxy(NULL) == NULL);

    // Without exclusive access it reads, and does not set or save.
    DT_ASSERT_EQ(DtPtpSlave_SetConfig(Fix.Slave, &Config), DTAPI_E_IN_USE);
    DT_ASSERT_EQ(DtPtpSlave_SaveSettings(Fix.Slave), DTAPI_E_IN_USE);
    SLAVE_FINISH(Fix);
}

DT_TEST(SlaveSetsOnlyTheFieldsNamed)
{
    SlaveFixture Fix;
    if (!OpenSlave(&Fix, true, DtFailures))
        return;

    // A struct of zeros but for the domain sets the domain alone.
    DtPtpConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Fields = DT_PTP_CONFIG_DOMAIN;
    Config.Domain = 5;
    DT_ASSERT_OK(DtPtpSlave_SetConfig(Fix.Slave, &Config));
    DT_ASSERT_OK(DtPtpSlave_GetConfig(Fix.Slave, &Config));
    DT_ASSERT_EQ(Config.Domain, 5);
    DT_ASSERT_EQ(Config.IpV6Scope, DT_PTP_IPV6_SCOPE_SITE_LOCAL);
    DT_ASSERT_STR(Config.PeerAddress, "0.0.0.0");

    // Read, change and write back.
    Config.Enable = true;
    Config.DelayMechanism = DT_PTP_DELAY_PEER_TO_PEER;
    Config.NetworkProtocol = DT_PTP_PROTOCOL_IPV6;
    Config.IpV6Scope = DT_PTP_IPV6_SCOPE_GLOBAL;
    snprintf(Config.PeerAddress, sizeof(Config.PeerAddress), "%s", "fe80::1");
    DT_ASSERT_OK(DtPtpSlave_SetConfig(Fix.Slave, &Config));
    DtPtpConfig Read;
    DT_ASSERT_OK(DtPtpSlave_GetConfig(Fix.Slave, &Read));
    DT_ASSERT_MEM(&Read, &Config, sizeof(Config));
    DT_ASSERT_OK(DtPtpSlave_SaveSettings(Fix.Slave));
    DT_ASSERT(SimService_WasSaved());

    // Switched on, the slave follows the emulated grandmaster in its domain, 5.
    DtPtpStatus Status;
    DT_ASSERT_OK(DtPtpSlave_GetStatus(Fix.Slave, &Status));
    DT_ASSERT_EQ(Status.LockStatus, DT_PTP_LOCK_LOCKED);
    DT_ASSERT_EQ(Status.Domain, 5);
    DtPtpMasterInfo* Masters = NULL;
    int NumMasters = 0;
    DT_ASSERT_OK(DtPtpSlave_GetMasters(Fix.Slave, &Masters, &NumMasters));
    bool Ok = NumMasters == 1 &&
              Masters[0].GrandmasterIdentity == SIM_SERVICE_GRANDMASTER &&
              Masters[0].DomainNumber == 5;
    DtPtp_FreeMasterInfo(Masters);
    DT_ASSERT(Ok);
    SLAVE_FINISH(Fix);
}

DT_TEST(SlaveRefusesBadConfigsItself)
{
    SlaveFixture Fix;
    if (!OpenSlave(&Fix, true, DtFailures))
        return;
    DtPtpConfig Good;
    DT_ASSERT_OK(DtPtpSlave_GetConfig(Fix.Slave, &Good));

    DtPtpConfig Config = Good;
    Config.Fields = 0;
    DT_ASSERT_EQ(DtPtpSlave_SetConfig(Fix.Slave, &Config), DTAPI_E_INVALID_ARG);
    Config.Fields = DT_PTP_CONFIG_ALL | 0x40u;
    DT_ASSERT_EQ(DtPtpSlave_SetConfig(Fix.Slave, &Config), DTAPI_E_INVALID_ARG);

    // Out of range, not a value of the enumeration, and an address without its end; the
    // service is not asked, so its last exception stays none.
    Config = Good;
    Config.Domain = 128;
    DT_ASSERT_EQ(DtPtpSlave_SetConfig(Fix.Slave, &Config), DTAPI_E_INVALID_ARG);
    DT_ASSERT(strstr(GetLastException(), "DomainNumber 128") != NULL);
    Config = Good;
    Config.DelayMechanism = (DtPtpDelayMechanism)7;
    DT_ASSERT_EQ(DtPtpSlave_SetConfig(Fix.Slave, &Config), DTAPI_E_INVALID_ARG);
    Config = Good;
    Config.IpV6Scope = (DtPtpIpV6Scope)0;
    DT_ASSERT_EQ(DtPtpSlave_SetConfig(Fix.Slave, &Config), DTAPI_E_INVALID_ARG);
    Config = Good;
    memset(Config.PeerAddress, 'x', sizeof(Config.PeerAddress));
    DT_ASSERT_EQ(DtPtpSlave_SetConfig(Fix.Slave, &Config), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtServiceProxy_LastException(DtPtpSlave_Proxy(Fix.Slave)),
                 DT_SERVICE_EXC_NONE);

    // Nothing was set.
    DtPtpConfig Read;
    DT_ASSERT_OK(DtPtpSlave_GetConfig(Fix.Slave, &Read));
    DT_ASSERT_MEM(&Read, &Good, sizeof(Good));
    DT_ASSERT_EQ(DtPtpSlave_SetConfig(NULL, &Good), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPtpSlave_GetConfig(NULL, &Read), DTAPI_E_INVALID_ARG);
    SLAVE_FINISH(Fix);
}

DT_TEST(SlaveAttachIsChecked)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;
    DtPtpSlave* Slave = NULL;
    DT_ASSERT_EQ(DtPtpSlave_Attach(Fix.Device, 2, false, &Slave), DTAPI_E_NO_SUCH_PORT);
    DT_ASSERT_EQ(DtPtpSlave_Attach(NULL, PORT, false, &Slave), DTAPI_E_DEVICE);
    DT_ASSERT_EQ(DtPtpSlave_Attach(Fix.Device, PORT, false, NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT(Slave == NULL);

    // A slave with exclusive access keeps DtDevice_GetPtpStatus out, as a proxy does.
    DT_ASSERT_OK(DtPtpSlave_Attach(Fix.Device, PORT, true, &Slave));
    DtPtpStatus Status;
    DtapiResult Result = DtDevice_GetPtpStatus(Fix.Device, PORT, &Status);
    DtPtpSlave_Detach(Slave);
    DtPtpSlave_Detach(NULL);
    DT_ASSERT_EQ(Result, DTAPI_E_IN_USE);
    FINISH(Fix);
}

DT_TEST_MAIN("SimService", DT_RUN(SlaveStartsOff),
             DT_RUN(DescriptionsHaveTypesEnumsAndDefaults), DT_RUN(SlaveSwitchedOnLocks),
             DT_RUN(MasterListHasTheGrandmaster), DT_RUN(PeerAddressIsAString),
             DT_RUN(SettingNeedsExclusiveAccess), DT_RUN(ExclusiveAccessKeepsOthersOut),
             DT_RUN(BadValuesAreRefused), DT_RUN(PortsWithoutTheSlaveAreRefused),
             DT_RUN(SlaveConfigStartsAtTheDefaults), DT_RUN(SlaveSetsOnlyTheFieldsNamed),
             DT_RUN(SlaveRefusesBadConfigsItself), DT_RUN(SlaveAttachIsChecked))
