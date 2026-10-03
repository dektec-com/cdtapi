// #*#*#*#*#*#*#*#*#*#*#*#*#*#* SimService.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The emulated DtapiService, with the PTP clock slave of the emulated DTA-2110
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"      // Allocation seam.
#include "Service/DtService.h" // The messages.
#include "Service/DtXml.h"     // Their XML.
#include "SimDtPcie.h"         // The emulator's lock.
#include "SimDta2110.h"        // The emulated DTA-2110's serial number and ports.
#include "SimService.h"        // Interface being implemented.
#include "cdtapi_service.h"    // Variants and the service's exceptions.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The ID of the first parameter. The real service counts from 0; these IDs differ from
// its, so that a client must look the parameters up by name.
#define SIM_FIRST_PAR_ID 100

// The service type of the PTP clock slave.
#define SIM_PTP_CLOCK_SLAVE 0

// The size of a string setting, with its terminating zero.
#define SIM_TEXT_SIZE 64

// The parameters, in alphabetical order; a parameter's ID is SIM_FIRST_PAR_ID plus its
// place here.
enum
{
    PAR_ANNOUNCE_RECEIPT_TIMEOUT,
    PAR_CLOCK_OFFSET,
    PAR_CUR_DELAY_MECHANISM,
    PAR_DELAY_MECHANISM,
    PAR_DOMAIN_NUMBER,
    PAR_ENABLE,
    PAR_IPV6_SCOPE,
    PAR_MASTER_IDENTITY,
    PAR_MASTER_INFO,
    PAR_MASTER_LOCKING_STATUS,
    PAR_MASTER_SYNC_INTERVAL,
    PAR_MIN_DELAY_REQ_INTERVAL,
    PAR_MIN_PDELAY_REQ_INTERVAL,
    PAR_NETWORK_DELAY,
    PAR_NETWORK_PROTOCOL,
    PAR_PARENT_PORT_IDENTITY,
    PAR_PARENT_PORT_NUM,
    PAR_PEER_UNICAST_ADDRESS,
    PAR_SLAVE_STATE,
    NUM_PARS
};

// A parameter as the service describes it. The values of an enumeration are consecutive,
// from FirstEnum.
typedef struct SimPar
{
    const char* Name;
    DtVariantType Type;
    bool IsWritable;
    double Min;
    double Max;
    double Default;          // For a number or a bool
    const char* DefaultText; // For a string
    const char* const* Enums;
    int NumEnums;
    int FirstEnum;
    const char* Description;
} SimPar;

static const char* const CurDelayNames[] = {"EndToEnd", "PeerToPeer"};
static const char* const DelayNames[] = {"Auto", "EndToEnd", "PeerToPeer"};
static const char* const LockNames[] = {"NotInUse", "FreeRun", "ColdLocking",
                                        "WarmLocking", "Locked"};
static const char* const ProtocolNames[] = {"IpV4", "IpV6"};
static const char* const StateNames[] = {"Disabled", "Initializing", "Listening",
                                         "Faulty",   "Uncalibrated", "Slave"};
#define ENUMS(Names, First) Names, (int)(sizeof(Names) / sizeof(Names[0])), First

static const SimPar Pars[NUM_PARS] = {
    {"AnnounceReceiptTimeout", DT_VARIANT_INT, false, 2, 10, 2, NULL, NULL, 0, 0,
     "Specifies the number of announce intervals that have to pass without receipt of an "
     "Announce message. It is specified in seconds as a power of two."},
    {"ClockOffset", DT_VARIANT_INT, false, 0, 0, 0, NULL, NULL, 0, 0,
     "Indicates the clock offset between the grandmaster and the DekTec card in "
     "nanoseconds."},
    {"CurDelayMechanism", DT_VARIANT_INT, false, 0, 0, 1, NULL, ENUMS(CurDelayNames, 1),
     "The current delay mechanism used."},
    {"DelayMechanism", DT_VARIANT_INT, true, 0, 0, 0, NULL, ENUMS(DelayNames, 0),
     "Specifies the propagation delay measuring method to be used by the port in "
     "computing "
     "<meanPathDelay>."},
    {"DomainNumber", DT_VARIANT_INT, true, 0, 127, 127, NULL, NULL, 0, 0,
     "Specifies the domain (scope) of PTP message communication, state, operations, data "
     "sets, and timescale."},
    {"Enable", DT_VARIANT_BOOL, true, 0, 1, 0, NULL, NULL, 0, 0,
     "Enables/disables the PTP clock slave."},
    {"IpV6Scope", DT_VARIANT_INT, true, 1, 14, 5, NULL, NULL, 0, 0,
     "Specifies the topological area within which the IPv6 address can be used as a "
     "unique "
     "identifier."},
    {"MasterIdentity", DT_VARIANT_UINT64, false, 0, 0, 0, NULL, NULL, 0, 0,
     "Specifies the identity (a unique identifier) of the grandmaster."},
    {"MasterInfo", DT_VARIANT_STRING, false, 0, 0, 0, "<V Cnt=\"0\"/>\n", NULL, 0, 0,
     "Lists all PTP masters detected in the network."},
    {"MasterLockingStatus", DT_VARIANT_INT, false, 0, 0, 0, NULL, ENUMS(LockNames, 0),
     "Indicates the status of the selected grandmaster."},
    {"MasterSyncInterval", DT_VARIANT_DOUBLE, false, -5, 16, 0, NULL, NULL, 0, 0,
     "Indicates the mean interval time between successive Sync messages sent by the PTP "
     "master, as a power of two in seconds."},
    {"MinDelayReqInterval", DT_VARIANT_DOUBLE, false, -5, 16, 0, NULL, NULL, 0, 0,
     "Specifies the minimum permitted time interval between delay request messages, as a "
     "power of two in seconds."},
    {"MinPDelayReqInterval", DT_VARIANT_DOUBLE, false, -5, 16, 0, NULL, NULL, 0, 0,
     "Specifies the minimum permitted time interval between peer delay request messages, "
     "as a power of two in seconds."},
    {"NetworkDelay", DT_VARIANT_INT, false, 0, 0, 0, NULL, NULL, 0, 0,
     "Indicates the path delay between the DekTec card and the grandmaster, in "
     "nanoseconds."},
    {"NetworkProtocol", DT_VARIANT_INT, true, 0, 0, 0, NULL, ENUMS(ProtocolNames, 0),
     "Specifies the IP network protocol to be used for the network communication with "
     "the "
     "PTP master."},
    {"ParentPortIdentity", DT_VARIANT_UINT64, false, 0, 0, 0, NULL, NULL, 0, 0,
     "Indicates the parent port identity of the selected grandmaster."},
    {"ParentPortNum", DT_VARIANT_INT, false, 0, 0, 0, NULL, NULL, 0, 0,
     "Indicates the parent port number of the selected grandmaster."},
    {"PeerUnicastAddress", DT_VARIANT_STRING, true, 0, 0, 0, "0.0.0.0", NULL, 0, 0,
     "Specifies the Unicast address of the peer node (peer-to-peer mode only). If empty, "
     "multicast is used."},
    {"SlaveState", DT_VARIANT_INT, false, 0, 0, 0, NULL, ENUMS(StateNames, 0),
     "Indicates the current state of the PTP clock slave."},
};

// The slave's settings.
typedef struct SimSettings
{
    bool Enable;
    int Domain;
    int DelayMechanism;
    int NetworkProtocol;
    int IpV6Scope;
    char PeerAddress[SIM_TEXT_SIZE];
} SimSettings;

// The emulated service, shared by every connection and guarded by the emulator's
// lock.
static struct
{
    bool Started; // Settings holds the slave's settings
    SimSettings Settings;
    const SimService* Exclusive; // The connection with exclusive access, or NULL
    int NextId;                  // The ID the next attach gets
    bool Saved;
} Sim;

struct SimService
{
    bool Attached;
    int Id;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StartSim -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Gives the slave its settings at the start, the first time. The lock is held.
//
static void StartSim(void)
{
    if (Sim.Started)
        return;
    memset(&Sim.Settings, 0, sizeof(Sim.Settings));
    Sim.Settings.Domain = (int)Pars[PAR_DOMAIN_NUMBER].Default;
    Sim.Settings.IpV6Scope = (int)Pars[PAR_IPV6_SCOPE].Default;
    snprintf(Sim.Settings.PeerAddress, SIM_TEXT_SIZE, "%s",
             Pars[PAR_PEER_UNICAST_ADDRESS].DefaultText);
    Sim.Started = true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MasterInfo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the value of MasterInfo into Text: the emulated grandmaster once the slave
// runs, with the attributes the real service writes, and no master before.
//
static void MasterInfo(char* Text, size_t Size)
{
    if (!Sim.Settings.Enable)
    {
        snprintf(Text, Size, "%s", Pars[PAR_MASTER_INFO].DefaultText);
        return;
    }
    snprintf(Text, Size,
             "<V Cnt=\"1\">\n<MI AM=\"false\" FT=\"true\" L59=\"false\" L61=\"false\" "
             "TT=\"true\" CUOV=\"true\" ID=\"%" PRId64 "\" PID=\"%" PRId64 "\" PPN=\"1\" "
             "AI=\"-2\" CA=\"33\" CC=\"6\" CUO=\"37\" DN=\"%d\" MiId=\"0\" MaId=\"0\" "
             "OSLV=\"20061\" OT=\"0\" P1=\"128\" P2=\"128\" SR=\"1\" TS=\"32\" "
             "IP=\"192.168.0.1\" AMC=\"1\"/>\n</V>\n",
             (int64_t)SIM_SERVICE_GRANDMASTER, (int64_t)SIM_SERVICE_GRANDMASTER,
             Sim.Settings.Domain);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ValueOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The value of parameter Par into *Value, a string into Text. A slave that runs is at
// once locked to the emulated grandmaster.
//
static void ValueOf(int Par, DtVariant* Value, char* Text, size_t Size)
{
    const SimSettings* Set = &Sim.Settings;
    bool Locked = Set->Enable;
    memset(Value, 0, sizeof(*Value));
    Value->Type = Pars[Par].Type;
    switch (Par)
    {
    case PAR_ANNOUNCE_RECEIPT_TIMEOUT:
        Value->Int = (int)Pars[Par].Default;
        break;
    case PAR_CLOCK_OFFSET:
        Value->Int = Locked ? 12 : 0;
        break;
    case PAR_CUR_DELAY_MECHANISM:
        Value->Int = Set->DelayMechanism == DT_PTP_DELAY_PEER_TO_PEER ? 2 : 1;
        break;
    case PAR_DELAY_MECHANISM:
        Value->Int = Set->DelayMechanism;
        break;
    case PAR_DOMAIN_NUMBER:
        Value->Int = Set->Domain;
        break;
    case PAR_ENABLE:
        Value->Bool = Set->Enable;
        break;
    case PAR_IPV6_SCOPE:
        Value->Int = Set->IpV6Scope;
        break;
    case PAR_MASTER_IDENTITY:
    case PAR_PARENT_PORT_IDENTITY:
        Value->UInt64 = Locked ? SIM_SERVICE_GRANDMASTER : 0;
        break;
    case PAR_MASTER_INFO:
        MasterInfo(Text, Size);
        Value->String = Text;
        break;
    case PAR_MASTER_LOCKING_STATUS:
        Value->Int = Locked ? DT_PTP_LOCK_LOCKED : DT_PTP_LOCK_NOT_IN_USE;
        break;
    case PAR_MASTER_SYNC_INTERVAL:
        Value->Double = Locked ? -3 : 0;
        break;
    case PAR_NETWORK_DELAY:
        Value->Int = Locked ? 2500 : 0;
        break;
    case PAR_NETWORK_PROTOCOL:
        Value->Int = Set->NetworkProtocol;
        break;
    case PAR_PARENT_PORT_NUM:
        Value->Int = Locked ? 1 : 0;
        break;
    case PAR_PEER_UNICAST_ADDRESS:
        snprintf(Text, Size, "%s", Set->PeerAddress);
        Value->String = Text;
        break;
    case PAR_SLAVE_STATE:
        Value->Int = Locked ? DT_PTP_SLAVE_SLAVE : DT_PTP_SLAVE_DISABLED;
        break;
    default:
        break;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindPar -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The place of the parameter with ID ParId, or -1.
//
static int FindPar(int64_t ParId)
{
    int64_t Par = ParId - SIM_FIRST_PAR_ID;
    return Par >= 0 && Par < NUM_PARS ? (int)Par : -1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetPar -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets parameter ParId to *Value, refusing as the real service does, in its order.
// Returns the exception, or DT_SERVICE_EXC_NONE.
//
static DtServiceExc SetPar(int64_t ParId, const DtVariant* Value)
{
    int Par = FindPar(ParId);
    if (Par < 0)
        return DT_SERVICE_EXC_INVALID_PAR_ID;
    const SimPar* Desc = &Pars[Par];
    if (Value->Type != Desc->Type)
        return DT_SERVICE_EXC_INVALID_PAR_TYPE;
    if (!Desc->IsWritable)
        return DT_SERVICE_EXC_READ_ONLY_PAR;
    if (Desc->Type == DT_VARIANT_INT && Desc->NumEnums > 0 &&
        (Value->Int < Desc->FirstEnum || Value->Int >= Desc->FirstEnum + Desc->NumEnums))
        return DT_SERVICE_EXC_PAR_OUT_OF_RANGE;
    if (Desc->Type == DT_VARIANT_INT && Desc->NumEnums == 0 &&
        (Value->Int < Desc->Min || Value->Int > Desc->Max))
        return DT_SERVICE_EXC_PAR_OUT_OF_RANGE;
    if (Desc->Type == DT_VARIANT_STRING && strlen(Value->String) >= SIM_TEXT_SIZE)
        return DT_SERVICE_EXC_PAR_OUT_OF_RANGE;

    SimSettings* Set = &Sim.Settings;
    switch (Par)
    {
    case PAR_DELAY_MECHANISM:
        Set->DelayMechanism = Value->Int;
        break;
    case PAR_DOMAIN_NUMBER:
        Set->Domain = Value->Int;
        break;
    case PAR_ENABLE:
        Set->Enable = Value->Bool;
        break;
    case PAR_IPV6_SCOPE:
        Set->IpV6Scope = Value->Int;
        break;
    case PAR_NETWORK_PROTOCOL:
        Set->NetworkProtocol = Value->Int;
        break;
    case PAR_PEER_UNICAST_ADDRESS:
        snprintf(Set->PeerAddress, SIM_TEXT_SIZE, "%s", Value->String);
        break;
    default:
        break;
    }
    return DT_SERVICE_EXC_NONE;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteDescs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The descriptions of the group General, as the real service writes them.
//
static void WriteDescs(DtXmlOut* Out)
{
    DtXmlOut_Open(Out, "ParDescs");
    DtXmlOut_AttrInt(Out, "Cnt", NUM_PARS);
    for (int Par = 0; Par < NUM_PARS; Par++)
    {
        const SimPar* Desc = &Pars[Par];
        DtVariant Min;
        DtVariant Max;
        DtVariant Default;
        memset(&Min, 0, sizeof(Min));
        Min.Type = Desc->Type;
        Min.Double = Desc->Min;
        Min.Int = (int)Desc->Min;
        Min.UInt64 = (uint64_t)Desc->Min;
        Min.Bool = Desc->Min != 0;
        Min.String = "";
        Max = Min;
        Max.Double = Desc->Max;
        Max.Int = (int)Desc->Max;
        Max.UInt64 = (uint64_t)Desc->Max;
        Max.Bool = Desc->Max != 0;
        Default = Min;
        Default.Double = Desc->Default;
        Default.Int = (int)Desc->Default;
        Default.UInt64 = (uint64_t)Desc->Default;
        Default.Bool = Desc->Default != 0;
        Default.String = Desc->DefaultText != NULL ? Desc->DefaultText : "";

        DtXmlOut_Open(Out, "ParDesc");
        DtXmlOut_AttrInt(Out, "ParId", SIM_FIRST_PAR_ID + Par);
        DtXmlOut_Attr(Out, "Name", Desc->Name);
        DtXmlOut_Attr(Out, "Desc", Desc->Description);
        DtXmlOut_AttrInt(Out, "VT", Desc->Type);
        DtXmlOut_AttrBool(Out, "W", Desc->IsWritable);
        DtXmlOut_AttrInt(Out, "ECount", Desc->NumEnums);
        DtService_WriteVariant(Out, "Min", &Min);
        DtService_WriteVariant(Out, "Max", &Max);
        DtService_WriteVariant(Out, "Def", &Default);
        for (int i = 0; i < Desc->NumEnums; i++)
        {
            DtXmlOut_Open(Out, "EValDesc");
            DtXmlOut_AttrInt(Out, "IntVal", Desc->FirstEnum + i);
            DtXmlOut_Attr(Out, "Idf", Desc->Enums[i]);
            DtXmlOut_Close(Out, "EValDesc");
        }
        DtXmlOut_Close(Out, "ParDesc");
    }
    DtXmlOut_Close(Out, "ParDescs");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteParVal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the value of the parameter at Par, in the element Name with the ID when WithId.
//
static void WriteParVal(DtXmlOut* Out, const char* Name, int Par, bool WithId)
{
    char Text[1024];
    DtVariant Value;
    ValueOf(Par, &Value, Text, sizeof(Text));
    DtXmlOut_Open(Out, Name);
    if (WithId)
        DtXmlOut_AttrInt(Out, "ParId", SIM_FIRST_PAR_ID + Par);
    DtService_WriteVariant(Out, "", &Value);
    DtXmlOut_Close(Out, Name);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Attach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The slave is that of the emulated DTA-2110's one port.
//
static DtServiceExc Attach(SimService* Connection, const DtXmlElem* Request,
                           DtXmlOut* Out)
{
    int64_t Serial = 0;
    int64_t Port = 0;
    int64_t Type = 0;
    bool Exclusive = false;
    if (Connection->Attached)
        return DT_SERVICE_EXC_DEVICE_ALREADY_ATTACHED;
    if (Request->Name == NULL || strcmp(Request->Name, "Attach") != 0 ||
        !DtXml_AttrInt(Request, "Serial", &Serial) ||
        !DtXml_AttrInt(Request, "Port", &Port) ||
        !DtXml_AttrInt(Request, "ServiceType", &Type) ||
        !DtXml_AttrBool(Request, "Exclusive", &Exclusive))
        return DT_SERVICE_EXC_INVALID_XML_STRING;
    if (Type != SIM_PTP_CLOCK_SLAVE)
        return DT_SERVICE_EXC_UNKNOWN_SERVICE;
    if (Serial != (int64_t)SIM_DTA2110_SERIAL)
        return DT_SERVICE_EXC_DEVICE_NOT_FOUND;
    if (Port < 0 || Port >= SIM_DTA2110_PORT_COUNT)
        return DT_SERVICE_EXC_INVALID_PORT_NO;
    if (Sim.Exclusive != NULL)
        return DT_SERVICE_EXC_EXCLUSIVE_IN_USE;

    Connection->Attached = true;
    Connection->Id = Sim.NextId++;
    if (Exclusive)
        Sim.Exclusive = Connection;
    DtXmlOut_Open(Out, "Id");
    DtXmlOut_AttrInt(Out, "Val", Connection->Id);
    DtXmlOut_Close(Out, "Id");
    return DT_SERVICE_EXC_NONE;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Detach(SimService* Connection)
{
    if (Sim.Exclusive == Connection)
        Sim.Exclusive = NULL;
    Connection->Attached = false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasId -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether Elem's attribute Name is the connection's attach ID.
//
static bool HasId(const SimService* Connection, const DtXmlElem* Elem, const char* Name)
{
    int64_t Id = -1;
    return DtXml_AttrInt(Elem, Name, &Id) && Id == Connection->Id;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetParVals -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets the values of the elements ParIdVal in List, in order, until one is refused, as
// the real service does.
//
static DtServiceExc SetParVals(const DtXmlElem* List)
{
    for (int i = 0; i < List->NumChildren; i++)
    {
        const DtXmlElem* Item = &List->Children[i];
        int64_t ParId = -1;
        DtVariant Value;
        if (strcmp(Item->Name, "ParIdVal") != 0 ||
            !DtXml_AttrInt(Item, "ParId", &ParId) ||
            !DtService_ReadVariant(Item, "", &Value))
            return DT_SERVICE_EXC_INVALID_XML_STRING;
        DtServiceExc Exception = SetPar(ParId, &Value);
        if (Exception != DT_SERVICE_EXC_NONE)
            return Exception;
    }
    return DT_SERVICE_EXC_NONE;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Handle -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Carries out command Cmd with Request, writes its answer into Out, and returns its
// exception. The lock is held.
//
static DtServiceExc Handle(SimService* Connection, uint32_t Cmd, const DtXmlElem* Request,
                           DtXmlOut* Out)
{
    int64_t Number = -1;
    DtVariant Value;
    if (Cmd == DT_SERVICE_CMD_VERSION)
    {
        DtXmlOut_Open(Out, "Version");
        DtXmlOut_AttrInt(Out, "Major", 5);
        DtXmlOut_AttrInt(Out, "Minor", 2);
        DtXmlOut_AttrInt(Out, "BugFix", 8);
        DtXmlOut_AttrInt(Out, "Build", 0);
        DtXmlOut_Close(Out, "Version");
        return DT_SERVICE_EXC_NONE;
    }
    if (Cmd == DT_SERVICE_CMD_ATTACH)
        return Attach(Connection, Request, Out);
    if (Cmd > DT_SERVICE_CMD_SAVE_SETTINGS)
        return DT_SERVICE_EXC_INVALID_CMD;
    if (!Connection->Attached)
        return DT_SERVICE_EXC_DEVICE_NOT_ATTACHED;

    bool IsExclusive = Sim.Exclusive == Connection;
    switch (Cmd)
    {
    case DT_SERVICE_CMD_DETACH:
        if (!HasId(Connection, Request, "Val"))
            return DT_SERVICE_EXC_DEVICE_NOT_ATTACHED;
        Detach(Connection);
        return DT_SERVICE_EXC_NONE;
    case DT_SERVICE_CMD_GET_PARGROUPS:
        DtXmlOut_Open(Out, "ParGroups");
        DtXmlOut_AttrInt(Out, "Cnt", 1);
        DtXmlOut_Open(Out, "S");
        DtXmlOut_Attr(Out, "Val", "General");
        DtXmlOut_Close(Out, "S");
        DtXmlOut_Close(Out, "ParGroups");
        return DT_SERVICE_EXC_NONE;
    case DT_SERVICE_CMD_GET_PARDESCS:
        if (DtXml_Attr(Request, "Val") == NULL)
            return DT_SERVICE_EXC_INVALID_XML_STRING;
        if (strcmp(DtXml_Attr(Request, "Val"), "General") != 0)
            return DT_SERVICE_EXC_UNKNOWN_GROUP_NAME;
        WriteDescs(Out);
        return DT_SERVICE_EXC_NONE;
    case DT_SERVICE_CMD_GET_PARVAL:
        if (!DtXml_AttrInt(Request, "Val", &Number))
            return DT_SERVICE_EXC_INVALID_XML_STRING;
        if (FindPar(Number) < 0)
            return DT_SERVICE_EXC_INVALID_PAR_ID;
        WriteParVal(Out, "ParVal", FindPar(Number), false);
        return DT_SERVICE_EXC_NONE;
    case DT_SERVICE_CMD_GET_PARVALS:
        for (int i = 0; i < Request->NumChildren; i++)
        {
            if (!DtXml_AttrInt(&Request->Children[i], "Val", &Number) ||
                FindPar(Number) < 0)
                return DT_SERVICE_EXC_INVALID_PAR_ID;
        }
        DtXmlOut_Open(Out, "ParIdVals");
        DtXmlOut_AttrInt(Out, "Cnt", Request->NumChildren);
        for (int i = 0; i < Request->NumChildren; i++)
        {
            DtXml_AttrInt(&Request->Children[i], "Val", &Number);
            WriteParVal(Out, "ParIdVal", FindPar(Number), true);
        }
        DtXmlOut_Close(Out, "ParIdVals");
        return DT_SERVICE_EXC_NONE;
    case DT_SERVICE_CMD_SET_PARVAL:
        if (!HasId(Connection, Request, "Id"))
            return DT_SERVICE_EXC_DEVICE_NOT_ATTACHED;
        if (!IsExclusive)
            return DT_SERVICE_EXC_NOT_EXCLUSIVE_ACCESS;
        if (!DtXml_AttrInt(Request, "ParId", &Number) ||
            !DtService_ReadVariant(Request, "", &Value))
            return DT_SERVICE_EXC_INVALID_XML_STRING;
        return SetPar(Number, &Value);
    case DT_SERVICE_CMD_SET_PARVALS:
        if (!HasId(Connection, Request, "Id"))
            return DT_SERVICE_EXC_DEVICE_NOT_ATTACHED;
        if (!IsExclusive)
            return DT_SERVICE_EXC_NOT_EXCLUSIVE_ACCESS;
        if (DtXml_Child(Request, "ParIdVals") == NULL)
            return DT_SERVICE_EXC_INVALID_XML_STRING;
        return SetParVals(DtXml_Child(Request, "ParIdVals"));
    case DT_SERVICE_CMD_SAVE_SETTINGS:
        if (!HasId(Connection, Request, "Val"))
            return DT_SERVICE_EXC_DEVICE_NOT_ATTACHED;
        if (!IsExclusive)
            return DT_SERVICE_EXC_NOT_EXCLUSIVE_ACCESS;
        Sim.Saved = true;
        return DT_SERVICE_EXC_NONE;
    default:
        return DT_SERVICE_EXC_INVALID_CMD;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeAnswer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The answer to Cmd: its header, and without an exception the text Xml.
//
static bool MakeAnswer(uint32_t Cmd, DtServiceExc Exception, const char* Xml,
                       uint8_t** Answer, size_t* AnswerSize)
{
    uint8_t* Wire = NULL;
    size_t WireSize = 0;
    if (Exception == DT_SERVICE_EXC_NONE &&
        DtService_TextToWire(Xml, DT_SERVICE_CHAR_SIZE, &Wire, &WireSize) != DTAPI_OK)
        return false;
    uint8_t* Bytes = (uint8_t*)DtAlloc_Malloc(8 + WireSize);
    if (Bytes == NULL)
    {
        DtAlloc_Free(Wire);
        return false;
    }
    uint32_t Reason = (uint32_t)(int32_t)Exception;
    for (int i = 0; i < 4; i++)
    {
        Bytes[i] = (uint8_t)(Cmd >> (8 * i));
        Bytes[4 + i] = (uint8_t)(Reason >> (8 * i));
    }
    if (WireSize > 0)
        memcpy(Bytes + 8, Wire, WireSize);
    DtAlloc_Free(Wire);
    *Answer = Bytes;
    *AnswerSize = 8 + WireSize;
    return true;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Service +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimService_Close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimService_Close(SimService* Connection)
{
    if (Connection == NULL)
        return;
    SimDtPcie_Lock();
    Detach(Connection);
    SimDtPcie_Unlock();
    DtAlloc_Free(Connection);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimService_Connect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
SimService* SimService_Connect(void)
{
    SimService* Connection = (SimService*)DtAlloc_Malloc(sizeof(SimService));
    if (Connection != NULL)
    {
        Connection->Attached = false;
        Connection->Id = -1;
    }
    return Connection;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimService_Reset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimService_Reset(void)
{
    SimDtPcie_Lock();
    Sim.Started = false;
    Sim.Exclusive = NULL;
    Sim.Saved = false;
    StartSim();
    SimDtPcie_Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimService_Transfer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool SimService_Transfer(SimService* Connection, const uint8_t* Msg, size_t Size,
                         uint8_t** Answer, size_t* AnswerSize)
{
    *Answer = NULL;
    *AnswerSize = 0;
    uint32_t Cmd = Size >= 4 ? (uint32_t)Msg[0] | (uint32_t)Msg[1] << 8 |
                                   (uint32_t)Msg[2] << 16 | (uint32_t)Msg[3] << 24
                             : 0;
    if (Size >= 4 && Cmd == DT_SERVICE_CMD_CLEANUP_CONNECTION)
    {
        SimDtPcie_Lock();
        Detach(Connection);
        SimDtPcie_Unlock();
        return true;
    }

    char* Text = NULL;
    DtXmlElem Request;
    memset(&Request, 0, sizeof(Request));
    DtServiceExc Exception = DT_SERVICE_EXC_NONE;
    if (Size < 4)
        Exception = DT_SERVICE_EXC_INVALID_SIZE;
    else
    {
        DtapiResult Result =
            DtService_TextFromWire(Msg + 4, Size - 4, DT_SERVICE_CHAR_SIZE, &Text);
        if (Result == DTAPI_OK && Text[0] != '\0')
            Result = DtXml_Parse(Text, &Request);
        if (Result == DTAPI_E_OUT_OF_MEM)
        {
            DtAlloc_Free(Text);
            return false;
        }
        if (Result != DTAPI_OK)
            Exception = DT_SERVICE_EXC_INVALID_XML_STRING;
    }
    DtAlloc_Free(Text);

    DtXmlOut Out;
    DtXmlOut_Init(&Out);
    if (Exception == DT_SERVICE_EXC_NONE)
    {
        SimDtPcie_Lock();
        StartSim();
        Exception = Handle(Connection, Cmd, &Request, &Out);
        SimDtPcie_Unlock();
    }
    DtXml_Free(&Request);
    char* Xml = NULL;
    if (DtXmlOut_Finish(&Out, &Xml) != DTAPI_OK)
        return false;
    bool Made = MakeAnswer(Cmd, Exception, Xml, Answer, AnswerSize);
    DtAlloc_Free(Xml);
    return Made;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimService_WasSaved -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool SimService_WasSaved(void)
{
    SimDtPcie_Lock();
    bool Saved = Sim.Saved;
    SimDtPcie_Unlock();
    return Saved;
}
