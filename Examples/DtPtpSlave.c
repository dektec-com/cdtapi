// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtPtpSlave.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Example: shows and sets the PTP clock slave of an IP port
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The PTP clock slave of a DTA-2110 or DTA-2125 keeps the card's time of day on that of a
// PTP grandmaster. DtapiService runs it; this program talks to it through the service.
//
// Without settings it shows the slave: its configuration, and its state and the
// grandmaster it follows; with --masters also every master it has heard. With settings
// it attaches with exclusive access and sets only those given, and with --save keeps
// them when the service starts again:
//
//     9211000001:1  set  ENABLE,DOMAIN  DTAPI_OK
//     9211000001:1  config  enable yes  domain 0  delay auto  protocol IPv4  scope 5
//     9211000001:1  status  SLAVE  LOCKED  grandmaster 00-1B-19-FF-FE-00-00-01  domain 0
//         traceable  class 6  steps 1
//
// (The status is one line.) Exits with 0 when every step succeeds, 1 when one fails or
// the command line is wrong, and 2 when no IP port suits.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <string.h>

// CDTAPI includes
#include "Common/ExampleCommon.h" // The API and what the examples share.
#include "cdtapi_avfifo.h"        // GetLastException.
#include "cdtapi_service.h"       // The PTP clock slave.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static const ExampleOption g_Options[] = {
    {"--serial", true, "The device's serial number; the first device with an IP port"},
    {"--port", true, "The port number; the first IP port"},
    {"--enable", false, "Switch the slave on"},
    {"--disable", false, "Switch the slave off"},
    {"--domain", true, "Listen in this PTP domain, 0 to 127"},
    {"--delay", true, "Measure the path delay: auto, e2e or p2p"},
    {"--protocol", true, "Talk to the master over ipv4 or ipv6"},
    {"--scope", true,
     "The IPv6 scope, as a number: 2 link-local, 5 site-local, 14 global"},
    {"--peer", true, "The peer's address in peer-to-peer mode; empty for multicast"},
    {"--save", false, "Keep the settings when the service starts again"},
    {"--masters", false, "List every master the slave has heard"},
};

// The names of the delay mechanisms and protocols, in the order of their enumerations.
static const char* const g_DelayNames[] = {"auto", "e2e", "p2p"};
static const char* const g_ProtocolNames[] = {"IPv4", "IPv6"};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsIpPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether a port is an IP port, which may have a PTP slave, for Example_FindPort().
//
static bool IsIpPort(const DtHwFuncDesc* Port)
{
    return Port->IsAvFifo != 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NameOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The name of Value in Names, which has Count of them, or "?" for a value outside them.
//
static const char* NameOf(const char* const* Names, int Count, int Value)
{
    return Value >= 0 && Value < Count ? Names[Value] : "?";
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ValueOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Finds Name, ignoring case, in Names, which has Count of them, and sets *Value to its
// place. Returns false when it is not there.
//
static bool ValueOf(const char* const* Names, int Count, const char* Name, int* Value)
{
    for (int i = 0; i < Count; i++)
    {
        const char* A = Names[i];
        const char* B = Name;
        while (*A != '\0' && (*A | 0x20) == (*B | 0x20))
        {
            A++;
            B++;
        }
        if (*A == '\0' && *B == '\0')
        {
            *Value = i;
            return true;
        }
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrintIdentity -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Prints a clock identity as its EUI-64, the bytes from the most significant.
//
static void PrintIdentity(uint64_t Identity)
{
    for (int i = 0; i < 8; i++)
        printf("%s%02X", i > 0 ? "-" : "", (unsigned)(Identity >> (56 - 8 * i)) & 0xFFu);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadSettings -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Fills *Config with the settings the command line gives, and their bits in Fields.
// Prints the problem and returns false for one it cannot read.
//
static bool ReadSettings(int Argc, char** Argv, DtPtpConfig* Config)
{
    int64_t Number = 0;
    int Value = 0;
    memset(Config, 0, sizeof(*Config));
    if (Example_HasFlag(Argc, Argv, "--enable") ||
        Example_HasFlag(Argc, Argv, "--disable"))
    {
        Config->Fields |= DT_PTP_CONFIG_ENABLE;
        Config->Enable = Example_HasFlag(Argc, Argv, "--enable");
    }
    if (Example_Value(Argc, Argv, "--domain") != NULL)
    {
        if (!Example_Int64(Argc, Argv, "--domain", &Number))
            return false;
        Config->Fields |= DT_PTP_CONFIG_DOMAIN;
        Config->Domain = (int)Number;
    }
    const char* Delay = Example_Value(Argc, Argv, "--delay");
    if (Delay != NULL)
    {
        if (!ValueOf(g_DelayNames, 3, Delay, &Value))
        {
            printf("Unknown delay mechanism: %s\n", Delay);
            return false;
        }
        Config->Fields |= DT_PTP_CONFIG_DELAY_MECHANISM;
        Config->DelayMechanism = (DtPtpDelayMechanism)Value;
    }
    const char* Protocol = Example_Value(Argc, Argv, "--protocol");
    if (Protocol != NULL)
    {
        if (!ValueOf(g_ProtocolNames, 2, Protocol, &Value))
        {
            printf("Unknown protocol: %s\n", Protocol);
            return false;
        }
        Config->Fields |= DT_PTP_CONFIG_NETWORK_PROTOCOL;
        Config->NetworkProtocol = (DtPtpNetworkProtocol)Value;
    }
    if (Example_Value(Argc, Argv, "--scope") != NULL)
    {
        if (!Example_Int64(Argc, Argv, "--scope", &Number))
            return false;
        Config->Fields |= DT_PTP_CONFIG_IPV6_SCOPE;
        Config->IpV6Scope = (DtPtpIpV6Scope)Number;
    }
    const char* Peer = Example_Value(Argc, Argv, "--peer");
    if (Peer != NULL)
    {
        if (strlen(Peer) >= sizeof(Config->PeerAddress))
        {
            printf("Peer address too long: %s\n", Peer);
            return false;
        }
        Config->Fields |= DT_PTP_CONFIG_PEER_ADDRESS;
        snprintf(Config->PeerAddress, sizeof(Config->PeerAddress), "%s", Peer);
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrintFields -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Prints the names of the settings in Fields, separated by commas.
//
static void PrintFields(uint32_t Fields)
{
    static const char* const Names[] = {"ENABLE",   "DOMAIN", "DELAY",
                                        "PROTOCOL", "SCOPE",  "PEER"};
    int Printed = 0;
    for (int i = 0; i < 6; i++)
    {
        if ((Fields & (1u << i)) != 0)
            printf("%s%s", Printed++ > 0 ? "," : "", Names[i]);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Show -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Prints the slave's configuration and status, and with Masters every master it has
// heard. Returns the first failure, or DTAPI_OK.
//
static unsigned int Show(DtPtpSlave* Slave, const char* Name, bool Masters)
{
    DtPtpConfig Config;
    unsigned int Result = DtPtpSlave_GetConfig(Slave, &Config);
    if (Result != DTAPI_OK)
    {
        Example_Failed("DtPtpSlave_GetConfig", Result);
        return Result;
    }
    printf("%s  config  enable %s  domain %d  delay %s  protocol %s  scope %d  peer %s\n",
           Name, Config.Enable ? "yes" : "no", Config.Domain,
           NameOf(g_DelayNames, 3, Config.DelayMechanism),
           NameOf(g_ProtocolNames, 2, Config.NetworkProtocol), (int)Config.IpV6Scope,
           Config.PeerAddress[0] != '\0' ? Config.PeerAddress : "-");

    DtPtpStatus Status;
    Result = DtPtpSlave_GetStatus(Slave, &Status);
    if (Result != DTAPI_OK)
    {
        Example_Failed("DtPtpSlave_GetStatus", Result);
        return Result;
    }
    printf("%s  status  %s  %s", Name, Example_PtpStateName(Status.SlaveState),
           Example_PtpLockName(Status.LockStatus));
    if (Status.HasMaster)
    {
        printf("  grandmaster");
        for (int i = 0; i < 8; i++)
            printf("%s%02X", i > 0 ? "-" : " ", Status.GrandmasterId[i]);
        printf("  domain %d  %s  class %d  steps %d", Status.Domain,
               Status.IsTimeTraceable ? "traceable" : "not traceable", Status.ClockClass,
               Status.StepsRemoved);
    }
    else
        printf("  no master");
    printf("\n");
    if (!Masters)
        return DTAPI_OK;

    DtPtpMasterInfo* Heard = NULL;
    int NumHeard = 0;
    Result = DtPtpSlave_GetMasters(Slave, &Heard, &NumHeard);
    if (Result != DTAPI_OK)
    {
        Example_Failed("DtPtpSlave_GetMasters", Result);
        return Result;
    }
    for (int i = 0; i < NumHeard; i++)
    {
        printf("%s  master  ", Name);
        PrintIdentity(Heard[i].GrandmasterIdentity);
        printf("  domain %d  class %d  priority %d/%d  steps %d  %s  %s\n",
               Heard[i].DomainNumber, Heard[i].GrandmasterClockClass,
               Heard[i].GrandmasterPriority1, Heard[i].GrandmasterPriority2,
               Heard[i].StepsRemoved,
               Heard[i].IsTimeTraceable ? "traceable" : "not traceable",
               Heard[i].IpAddress[0] != '\0' ? Heard[i].IpAddress : "-");
    }
    printf("%s  %d masters\n", Name, NumHeard);
    DtPtp_FreeMasterInfo(Heard);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A slave that only shows is attached without exclusive access, so that it does not keep
// a program or tool that sets the slave out; one that sets needs exclusive access.
//
int main(int Argc, char** Argv)
{
    int64_t Serial = 0;
    int64_t PortNumber = 0;
    DtPtpConfig Settings;
    bool Save = Example_HasFlag(Argc, Argv, "--save");
    int Exit = EXAMPLE_OK;

    if (!Example_CheckArguments(
            Argc, Argv, "Shows the PTP clock slave of an IP port, and sets it.",
            g_Options, (int)(sizeof(g_Options) / sizeof(g_Options[0]))) ||
        !Example_Int64(Argc, Argv, "--serial", &Serial) ||
        !Example_Int64(Argc, Argv, "--port", &PortNumber) ||
        !ReadSettings(Argc, Argv, &Settings))
    {
        return EXAMPLE_FAILED;
    }
    if (Example_HasFlag(Argc, Argv, "--enable") &&
        Example_HasFlag(Argc, Argv, "--disable"))
    {
        printf("Give --enable or --disable, not both\n");
        return EXAMPLE_FAILED;
    }

    DtHwFuncDesc Port;
    unsigned int Result = Example_FindPort(Serial, (int)PortNumber, IsIpPort, &Port);
    if (Result == DTAPI_E_NOT_FOUND)
    {
        printf("No IP port that suits\n");
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

    bool Sets = Settings.Fields != 0 || Save;
    DtPtpSlave* Slave = NULL;
    Result = DtPtpSlave_Attach(Device, Port.Port, Sets, &Slave);
    if (Result != DTAPI_OK)
    {
        printf("%s  DtPtpSlave_Attach: %s\n", Port.DeviceName, DtapiResult2Str(Result));
        DtDevice_Free(Device);
        return EXAMPLE_FAILED;
    }

    if (Settings.Fields != 0)
    {
        Result = DtPtpSlave_SetConfig(Slave, &Settings);
        printf("%s  set  ", Port.DeviceName);
        PrintFields(Settings.Fields);
        printf("  %s\n", DtapiResult2Str(Result));
        if (Result != DTAPI_OK)
        {
            printf("%s  %s\n", Port.DeviceName, GetLastException());
            Exit = EXAMPLE_FAILED;
        }
    }
    if (Save && Exit == EXAMPLE_OK)
    {
        Result = DtPtpSlave_SaveSettings(Slave);
        printf("%s  saved  %s\n", Port.DeviceName, DtapiResult2Str(Result));
        if (Result != DTAPI_OK)
            Exit = EXAMPLE_FAILED;
    }
    if (Show(Slave, Port.DeviceName, Example_HasFlag(Argc, Argv, "--masters")) !=
        DTAPI_OK)
        Exit = EXAMPLE_FAILED;

    DtPtpSlave_Detach(Slave);
    DtDevice_Free(Device);
    return Exit;
}
