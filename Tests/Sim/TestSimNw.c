// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# TestSimNw.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The network port's commands and pipes against the emulated DTA-2110
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPI_SIM=1. Every case starts from the emulator's power-on
// state with a DTA-2110 added and its time of day set by the test, and ends with no
// handle to it and no allocation left open.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"           // Live allocations.
#include "DtFunc.h"                 // Finding the network function.
#include "DtPcie/DtEthIp.h"         // The packets in a pipe's buffer.
#include "DtPcieAbi.h"              // Types, commands and flags.
#include "DtPcieCmd.h"              // Commands under test.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // Device handles.
#include "OAL/OsDmaBuffer.h"        // Buffers to give a pipe.
#include "OAL/OsThread.h"           // Waiting for the host's clock.
#include "OAL/Sim/SimDtPcie.h"      // The emulated devices and their test controls.
#include "OAL/Sim/SimDta2110.h"     // What the emulated DTA-2110 is.
#include "OAL/Sim/SimNw.h"          // The network function's controls.
#include "cdtapi.h"                 // Results and the device scan.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The DTA-2110's driver index, and its IP port.
#define INDEX 1
#define PORT 0

// A time of day on an interval, and a millisecond.
#define T0 (1800000000ull * 1000000000ull)
#define MS 1000000ull

typedef struct Fixture
{
    OsDrv* Drv;
    DtPartRef Nw; // The network function
    int Live;
} Fixture;

// Adds the DTA-2110, stops its clock at T0, opens it and finds its network function.
// Returns false, having recorded a failure, when that is not possible.
static bool Open(Fixture* Fix, int* DtFailures)
{
    SimDtPcie_Reset();
    Fix->Live = DtAlloc_Live();
    SimDtPcie_SetDta2110Index(INDEX);
    SimDtPcie_SetNwTime(T0);
    Fix->Drv = OsDrv_Open(INDEX);
    memset(&Fix->Nw, 0, sizeof(Fix->Nw));

    DtFuncInstance Af;
    DtVec_Init(&Af.Parts, sizeof(DtFuncPart));
    if (Fix->Drv == NULL || !OsDrv_IsEmulated(Fix->Drv) ||
        DtFunc_Find(Fix->Drv, PORT, "AF_NW", "", &Af) != DTAPI_OK)
    {
        printf("    FAIL: no emulated DTA-2110; is CDTAPI_SIM=1 set?\n");
        (*DtFailures)++;
        OsDrv_Close(Fix->Drv);
        return false;
    }
    const DtFuncPart* Part = DtFunc_Get(&Af, true, DT_FUNC_TYPE_NW, "");
    if (Part != NULL)
        Fix->Nw = Part->Ref;
    DtFunc_Release(&Af);
    return true;
}

// Closes the device and checks that nothing is left open or allocated.
#define FINISH(Fix)                                                                      \
    do                                                                                   \
    {                                                                                    \
        OsDrv_Close((Fix).Drv);                                                          \
        DT_ASSERT_EQ(SimDtPcie_OpenHandles(), 0);                                        \
        SimDtPcie_Reset();                                                               \
        DT_ASSERT_EQ(DtAlloc_Live(), (Fix).Live);                                        \
    } while (0)

// The number in a pipe's UUID.
static int IdOf(DtPartRef Pipe)
{
    return (int)((uint32_t)Pipe.Uuid >> 20);
}

// Whether the last command was Cmd of FunctionCode for Part, with an input of Size bytes,
// which are copied to Input when it is not NULL.
static bool LastWas(int FunctionCode, DtPartRef Part, int Cmd, size_t Size, void* Input)
{
    DtIoctlInputDataHdr Hdr;
    int Code = -1;
    uint8_t In[SIM_MAX_RECORDED_INPUT];
    size_t Got = SimDtPcie_LastInput(&Code, In, sizeof(In));

    memcpy(&Hdr, In, sizeof(Hdr));
    if (Input != NULL)
        memcpy(Input, In, Size);
    return Got == Size && Code == FunctionCode && Hdr.m_Uuid == Part.Uuid &&
           Hdr.m_PortIndex == Part.PortIndex && Hdr.m_Cmd == Cmd &&
           Hdr.m_CmdEx == DT_IOCTL_CMD_NOP;
}

// Builds an Ethernet frame of an IPv4 UDP datagram from 192.168.1.10:SrcPort to
// 239.1.2.3:DstPort with Payload bytes counting up from Seed. Returns its size.
static size_t MakeFrame(uint8_t* Frame, uint16_t SrcPort, uint16_t DstPort, int Payload,
                        uint8_t Seed)
{
    static const uint8_t Head[34] = {
        0x01, 0x00, 0x5E, 0x01, 0x02, 0x03, 0x00, 0x14, 0xF4, 0x08, 0x00, 0x01,
        0x08, 0x00, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11,
        0x00, 0x00, 192,  168,  1,    10,   239,  1,    2,    3,
    };
    int UdpLength = 8 + Payload;

    memcpy(Frame, Head, sizeof(Head));
    Frame[16] = (uint8_t)((20 + UdpLength) >> 8);
    Frame[17] = (uint8_t)(20 + UdpLength);
    Frame[34] = (uint8_t)(SrcPort >> 8);
    Frame[35] = (uint8_t)SrcPort;
    Frame[36] = (uint8_t)(DstPort >> 8);
    Frame[37] = (uint8_t)DstPort;
    Frame[38] = (uint8_t)(UdpLength >> 8);
    Frame[39] = (uint8_t)UdpLength;
    Frame[40] = 0;
    Frame[41] = 0;
    for (int i = 0; i < Payload; i++)
        Frame[42 + i] = (uint8_t)(Seed + i);
    return (size_t)(42 + Payload);
}

// Writes a packet of Frame, to be sent at TodNs, into a transmit buffer at Offset, as
// DTAPI builds one. Returns the packet's size.
static size_t PutPacket(OsDmaBuffer* Buf, size_t Offset, const uint8_t* Frame,
                        size_t Size, uint64_t TodNs)
{
    DtEthIpFields Header;

    memset(&Header, 0, sizeof(Header));
    Header.NumWords = DtEthIp_NumWords((int)Size, 8);
    Header.FrameSize = (int)Size;
    Header.IpAddressOffset = DT_ETHIP_HEADER_SIZE + 26;
    Header.PortOffset = DT_ETHIP_HEADER_SIZE + 34;
    Header.Protocol = DT_ETHIP_PROTO_UDP;
    Header.PacketType = DT_ETHIP_TYPE_IPV4;
    Header.TimestampValid = true;
    Header.Seconds = (uint32_t)(TodNs / 1000000000u);
    Header.Nanoseconds = (uint32_t)(TodNs % 1000000000u);

    size_t PacketSize = (size_t)Header.NumWords * 8;
    uint8_t Packet[DT_ETHIP_MAX_FRAME_V1 + DT_ETHIP_HEADER_SIZE];
    memset(Packet, 0, sizeof(Packet));
    DtEthIp_Write(&Header, Packet);
    memcpy(Packet + DT_ETHIP_HEADER_SIZE, Frame, Size);
    for (size_t i = 0; i < PacketSize; i++)
        Buf->Data[(Offset + i) % Buf->Size] = Packet[i];
    return PacketSize;
}

// Opens a pipe of Type, gives it a buffer of Size bytes as this platform's driver takes
// it, flushes it and sets it running. Returns the pipe, with UUID 0 after a failure.
static DtPartRef StartPipe(Fixture* Fix, int Type, size_t Size, OsDmaBuffer* Buf)
{
    DtPartRef Uuid = {0, PORT};
    const DtPartRef None = {0, PORT};

    if (DtPcieCmd_NwOpenPipe(Fix->Drv, Fix->Nw, Type, -1, &Uuid) != DTAPI_OK ||
        OsDmaBuffer_Alloc(Size, Buf) != 0)
    {
        return None;
    }
    if (DtPcieCmd_PipeSetSharedBuffer(Fix->Drv, Uuid, Buf) != DTAPI_OK ||
        DtPcieCmd_PipeFlush(Fix->Drv, Uuid) != DTAPI_OK ||
        DtPcieCmd_PipeSetOpMode(Fix->Drv, Uuid, DT_PIPE_OPMODE_RUN) != DTAPI_OK)
    {
        return None;
    }
    return Uuid;
}

// A filter on 239.1.2.3 and destination port DstPort.
static DtIpFilter FilterOn(uint16_t DstPort)
{
    DtIpFilter Filter;
    static const uint8_t Group[4] = {239, 1, 2, 3};

    memset(&Filter, 0, sizeof(Filter));
    memcpy(Filter.DstIp, Group, sizeof(Group));
    Filter.DstPort[0] = DstPort;
    Filter.Flags = DT_PIPE_IPFLT_FLAG_EN_FILT | DT_PIPE_IPFLT_FLAG_EN_DSTIP_IPV4 |
                   DT_PIPE_IPFLT_FLAG_EN_DSTPORT0;
    return Filter;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The device +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Only a test adds the DTA-2110, beside the DTA-2178, and a reset takes it away.
DT_TEST(Dta2110IsThereWhenAdded)
{
    SimDtPcie_Reset();
    DT_ASSERT(OsDrv_Open(INDEX) == NULL);
    int Count = -1;
    DT_ASSERT_EQ(DtapiDeviceScan(0, &Count, NULL), DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_EQ(Count, 1);

    SimDtPcie_SetDta2110Index(INDEX);
    DtDeviceDesc Descs[2];
    DT_ASSERT_OK(DtapiDeviceScan(2, &Count, Descs));
    DT_ASSERT_EQ(Count, 2);
    DT_ASSERT_EQ(Descs[0].TypeNumber, SIM_TYPE_NUMBER);
    DT_ASSERT_EQ(Descs[1].TypeNumber, SIM_DTA2110_TYPE_NUMBER);
    DT_ASSERT_EQ(Descs[1].Serial, SIM_DTA2110_SERIAL);
    DT_ASSERT_EQ(Descs[1].DeviceId, SIM_DTA2110_DEVICE_ID);
    DT_ASSERT_EQ(Descs[1].SubsystemId, SIM_DTA2110_SUBSYSTEM_ID);
    DT_ASSERT_EQ(Descs[1].FirmwareVersion, SIM_DTA2110_FIRMWARE_VERSION);
    DT_ASSERT_EQ(Descs[1].NumPorts, SIM_DTA2110_PORT_COUNT);

    SimDtPcie_Reset();
    DT_ASSERT(OsDrv_Open(INDEX) == NULL);
    DT_ASSERT_EQ(SimDtPcie_OpenHandles(), 0);
}

// The port's capabilities, its one function, and no I/O configuration.
DT_TEST(PortAndFunction)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;

    bool Has = false;
    DT_ASSERT_OK(DtPcieCmd_GetPropertyBool(Fix.Drv, "CAP_AVFIFO", PORT, &Has));
    DT_ASSERT(Has);
    DT_ASSERT_OK(DtPcieCmd_GetPropertyBool(Fix.Drv, "CAP_ST2110", PORT, &Has));
    DT_ASSERT(Has);
    DT_ASSERT_OK(DtPcieCmd_GetPropertyBool(Fix.Drv, "CAP_SDI", PORT, &Has));
    DT_ASSERT(!Has);
    int Ports = 0;
    DT_ASSERT_OK(
        DtPcieCmd_GetPropertyInt(Fix.Drv, "PORT_COUNT", DT_PROPERTY_DEVICE, &Ports));
    DT_ASSERT_EQ(Ports, 1);
    DT_ASSERT_EQ(Fix.Nw.Uuid, DT_UUID_DF_FLAG | 1);

    DtIoConfig Config = {1, DTAPI_IOCONFIG_IODIR, -1, -1, {-1, -1}};
    DT_ASSERT_EQ(DtPcieCmd_GetIoConfig(Fix.Drv, &Config), DTAPI_E_NOT_SUPPORTED);

    DtDriverVersion Version;
    DT_ASSERT_OK(DtPcieCmd_GetDriverVersion(Fix.Drv, &Version));
    DT_ASSERT_OK(DtFunc_CheckDriverVersion(&Version, true, DT_FUNC_TYPE_NW));

    uint32_t Seconds = 0;
    uint32_t Nanoseconds = 1;
    DT_ASSERT_OK(DtPcieCmd_GetTimeOfDay(Fix.Drv, &Seconds, &Nanoseconds));
    DT_ASSERT_EQ(Seconds, T0 / 1000000000u);
    DT_ASSERT_EQ(Nanoseconds, 0);

    DT_ASSERT_OK(DtPcieCmd_ExclAccess(Fix.Drv, Fix.Nw, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT_OK(DtPcieCmd_ExclAccess(Fix.Drv, Fix.Nw, DT_EXCLUSIVE_ACCESS_CMD_CHECK));
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= EMAC +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(MacAddressAndPhySpeed)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;

    static const uint8_t Expected[6] = SIM_DTA2110_MAC_ADDRESS;
    uint8_t Mac[6];
    DT_ASSERT_OK(DtPcieCmd_NwGetMacAddress(Fix.Drv, Fix.Nw, Mac));
    DT_ASSERT_MEM(Mac, Expected, 6);
    DT_ASSERT(LastWas(DT_FUNC_CODE_EMAC_CMD, Fix.Nw, DT_EMAC_CMD_GET_MACADDRESS,
                      sizeof(DtIoctlInputDataHdr), NULL));

    int Speed = -1;
    DT_ASSERT_OK(DtPcieCmd_NwGetPhySpeed(Fix.Drv, Fix.Nw, &Speed));
    DT_ASSERT_EQ(Speed, DT_PHY_SPEED_10000);
    DT_ASSERT(LastWas(DT_FUNC_CODE_EMAC_CMD, Fix.Nw, DT_EMAC_CMD_GET_PHY_SPEED,
                      sizeof(DtIoctlInputDataHdr), NULL));
    SimDtPcie_SetNwLink(false);
    DT_ASSERT_OK(DtPcieCmd_NwGetPhySpeed(Fix.Drv, Fix.Nw, &Speed));
    DT_ASSERT_EQ(Speed, DT_PHY_SPEED_NO_LINK);

    DT_ASSERT_EQ(DtPcieCmd_NwGetMacAddress(Fix.Drv, Fix.Nw, NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_NwGetPhySpeed(NULL, Fix.Nw, &Speed), DTAPI_E_INVALID_ARG);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Opening pipes +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Hardware pipes first free first, then IN_USE or the fallback; the driver's own queues
// are in use; a type the driver does not know is not supported.
DT_TEST(OpenPipesByType)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;

    DtPartRef Uuid = {0, PORT};
    for (int i = 0; i < SIM_DTA2110_HW_PIPES; i++)
    {
        DT_ASSERT_OK(DtPcieCmd_NwOpenPipe(Fix.Drv, Fix.Nw, DT_PIPE_TX_RT_HWP, -1, &Uuid));
        DT_ASSERT_EQ(Uuid.Uuid, Fix.Nw.Uuid | (SIM_NW_FIRST_TX_HWP + i) << 20);
    }
    DtIoctlNwCmdPipeOpenInput In;
    DT_ASSERT(LastWas(DT_FUNC_CODE_NW_CMD, Fix.Nw, DT_NW_CMD_PIPE_OPEN, sizeof(In), &In));
    DT_ASSERT_EQ(In.m_PipeType, DT_PIPE_TX_RT_HWP);
    DT_ASSERT_EQ(In.m_PipeTypeFallback, -1);

    DT_ASSERT_EQ(DtPcieCmd_NwOpenPipe(Fix.Drv, Fix.Nw, DT_PIPE_TX_RT_HWP, -1, &Uuid),
                 DTAPI_E_IN_USE);
    DT_ASSERT_EQ(Uuid.Uuid, 0);
    DT_ASSERT_OK(DtPcieCmd_NwOpenPipe(Fix.Drv, Fix.Nw, DT_PIPE_TX_RT_HWP,
                                      DT_PIPE_TX_RT_SWP, &Uuid));
    DT_ASSERT_EQ(IdOf(Uuid), SIM_NW_FIRST_SWP);
    DT_ASSERT(LastWas(DT_FUNC_CODE_NW_CMD, Fix.Nw, DT_NW_CMD_PIPE_OPEN, sizeof(In), &In));
    DT_ASSERT_EQ(In.m_PipeTypeFallback, DT_PIPE_TX_RT_SWP);

    DT_ASSERT_OK(DtPcieCmd_NwOpenPipe(Fix.Drv, Fix.Nw, DT_PIPE_RX_RT_HWP, -1, &Uuid));
    DT_ASSERT_EQ(IdOf(Uuid), SIM_NW_FIRST_RX_HWP);
    DT_ASSERT_OK(DtPcieCmd_NwOpenPipe(Fix.Drv, Fix.Nw, DT_PIPE_RX_RT_SWP,
                                      DT_PIPE_TX_RT_SWP, &Uuid));
    DT_ASSERT_EQ(IdOf(Uuid), SIM_NW_FIRST_SWP + 1);

    DT_ASSERT_EQ(DtPcieCmd_NwOpenPipe(Fix.Drv, Fix.Nw, DT_PIPE_RX_HWQ, -1, &Uuid),
                 DTAPI_E_IN_USE);
    DT_ASSERT_EQ(DtPcieCmd_NwOpenPipe(Fix.Drv, Fix.Nw, DT_PIPE_TX_NRT, -1, &Uuid),
                 DTAPI_E_IN_USE);
    DT_ASSERT_EQ(DtPcieCmd_NwOpenPipe(Fix.Drv, Fix.Nw, 99, DT_PIPE_RX_RT_SWP, &Uuid),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtPcieCmd_NwOpenPipe(Fix.Drv, Fix.Nw, DT_PIPE_RX_RT_SWP, -1, NULL),
                 DTAPI_E_INVALID_ARG);
    FINISH(Fix);
}

// Software pipes run out after 2,047 pipes, and a closed one's number is used again.
DT_TEST(SoftwarePipesRunOut)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;

    DtPartRef Uuid = {0, PORT};
    DtPartRef Last = {0, PORT};
    for (int Id = SIM_NW_FIRST_SWP; Id <= SIM_NW_MAX_PIPES; Id++)
    {
        if (DtPcieCmd_NwOpenPipe(Fix.Drv, Fix.Nw, DT_PIPE_RX_RT_SWP, -1, &Uuid) !=
                DTAPI_OK ||
            IdOf(Uuid) != Id)
        {
            DT_FAIL("pipe %d", Id);
        }
        if (Id == 100)
            Last = Uuid;
    }
    DT_ASSERT_EQ(DtPcieCmd_NwOpenPipe(Fix.Drv, Fix.Nw, DT_PIPE_TX_RT_SWP, -1, &Uuid),
                 DTAPI_E_OUT_OF_RESOURCES);
    DT_ASSERT_OK(DtPcieCmd_NwClosePipe(Fix.Drv, Last));
    DT_ASSERT_OK(DtPcieCmd_NwOpenPipe(Fix.Drv, Fix.Nw, DT_PIPE_TX_RT_SWP, -1, &Uuid));
    DT_ASSERT_EQ(IdOf(Uuid), 100);
    FINISH(Fix);
}

// Closing checks the pipe as DtDfNw_PipeClose does, and a closing handle closes its
// pipes.
DT_TEST(ClosingPipes)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;

    DtPartRef Hwp = {0, PORT};
    DtPartRef Swp = {0, PORT};
    DT_ASSERT_OK(DtPcieCmd_NwOpenPipe(Fix.Drv, Fix.Nw, DT_PIPE_RX_RT_HWP, -1, &Hwp));
    DT_ASSERT_OK(DtPcieCmd_NwOpenPipe(Fix.Drv, Fix.Nw, DT_PIPE_TX_RT_SWP, -1, &Swp));

    OsDrv* Other = OsDrv_Open(INDEX);
    DT_ASSERT(Other != NULL);
    DT_ASSERT_EQ(DtPcieCmd_NwClosePipe(Other, Hwp), DTAPI_E_NOT_FOUND);
    DT_ASSERT_EQ(DtPcieCmd_NwClosePipe(Fix.Drv, Fix.Nw), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_NwClosePipe(Fix.Drv, (DtPartRef){Fix.Nw.Uuid | 3 << 20, PORT}),
                 DTAPI_E_NOT_FOUND);

    DT_ASSERT_OK(DtPcieCmd_NwClosePipe(Fix.Drv, Hwp));
    DT_ASSERT(LastWas(DT_FUNC_CODE_NW_CMD, Hwp, DT_NW_CMD_PIPE_CLOSE,
                      sizeof(DtIoctlNwCmdPipeCloseInput), NULL));
    DT_ASSERT_EQ(DtPcieCmd_NwClosePipe(Fix.Drv, Hwp), DTAPI_E_NOT_INITIALIZED);
    DT_ASSERT_OK(DtPcieCmd_NwClosePipe(Fix.Drv, Swp));
    DT_ASSERT_EQ(DtPcieCmd_NwClosePipe(Fix.Drv, Swp), DTAPI_E_INVALID_ARG);

    DtPartRef OtherPipe = {0, PORT};
    DT_ASSERT_OK(DtPcieCmd_NwOpenPipe(Other, Fix.Nw, DT_PIPE_TX_RT_HWP, -1, &OtherPipe));
    DT_ASSERT_OK(DtPcieCmd_NwOpenPipe(Other, Fix.Nw, DT_PIPE_RX_RT_SWP, -1, &Swp));
    OsDrv_Close(Other);
    SimNwPipeState State;
    SimDtPcie_GetNwPipeState(IdOf(OtherPipe), &State);
    DT_ASSERT(State.Exists && !State.InUse);
    SimDtPcie_GetNwPipeState(IdOf(Swp), &State);
    DT_ASSERT(!State.Exists);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Pipe commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Every command's bytes on the wire.
DT_TEST(PipeCommandsOnTheWire)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;

    DtPartRef Rx = {0, PORT};
    DT_ASSERT_OK(DtPcieCmd_NwOpenPipe(Fix.Drv, Fix.Nw, DT_PIPE_RX_RT_SWP, -1, &Rx));

    OsDmaBuffer Buf;
    DT_ASSERT_EQ(OsDmaBuffer_Alloc(65536, &Buf), 0);
    SimDtPcie_RegisterPipeBufferAsLinux(true);
    DT_ASSERT_OK(DtPcieCmd_PipeSetSharedBufferAs(Fix.Drv, Rx, &Buf, false));
    DtIoctlPipeCmdSetSharedBufferInput Shared;
    DT_ASSERT(LastWas(DT_FUNC_CODE_PIPE_CMD, Rx, DT_PIPE_CMD_SET_SHARED_BUFFER,
                      sizeof(Shared), &Shared));
    DT_ASSERT(Shared.m_BufferAddr == (uint64_t)(uintptr_t)Buf.Data);
    DT_ASSERT_EQ(Shared.m_BufferSize, 65536);

    DT_ASSERT_OK(DtPcieCmd_PipeSetOpMode(Fix.Drv, Rx, DT_PIPE_OPMODE_STANDBY));
    DtIoctlPipeCmdSetOpModeInput Mode;
    DT_ASSERT(LastWas(DT_FUNC_CODE_PIPE_CMD, Rx, DT_PIPE_CMD_SET_OPERATIONAL_MODE,
                      sizeof(Mode), &Mode));
    DT_ASSERT_EQ(Mode.m_OpMode, DT_PIPE_OPMODE_STANDBY);

    DT_ASSERT_OK(DtPcieCmd_PipeSetRxReadOffset(Fix.Drv, Rx, 4096));
    DtIoctlPipeCmdSetRxReadOffsetInput Read;
    DT_ASSERT(LastWas(DT_FUNC_CODE_PIPE_CMD, Rx, DT_PIPE_CMD_SET_RX_READ_OFFSET,
                      sizeof(Read), &Read));
    DT_ASSERT_EQ(Read.m_RxReadOffset, 4096);

    uint32_t Offset = 1;
    DT_ASSERT_OK(DtPcieCmd_PipeGetRxWriteOffset(Fix.Drv, Rx, &Offset));
    DT_ASSERT_EQ(Offset, 0);
    DT_ASSERT(LastWas(DT_FUNC_CODE_PIPE_CMD, Rx, DT_PIPE_CMD_GET_RX_WRITE_OFFSET,
                      sizeof(DtIoctlInputDataHdr), NULL));

    DtIpFilter Filter = FilterOn(5004);
    static const uint8_t Source[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
                                       0,    0,    0,    0,    0, 0, 0, 1};
    memcpy(Filter.SrcIp, Source, sizeof(Source));
    Filter.SrcPort[2] = 1234;
    Filter.DstPort[1] = 5005;
    Filter.VlanId[0] = 7;
    Filter.VlanId[1] = 9;
    Filter.Flags |= DT_PIPE_IPFLT_FLAG_EN_SRCIP_IPV6;
    DT_ASSERT_OK(DtPcieCmd_PipeSetIpFilter(Fix.Drv, Rx, &Filter));
    DtIoctlPipeCmdSetIpFilterInput Sent;
    DT_ASSERT(LastWas(DT_FUNC_CODE_PIPE_CMD, Rx, DT_PIPE_CMD_SET_IPFILTER, sizeof(Sent),
                      &Sent));
    DT_ASSERT_MEM(Sent.m_DstIp, Filter.DstIp, 16);
    DT_ASSERT_MEM(Sent.m_SrcIp, Source, 16);
    DT_ASSERT_EQ(Sent.m_DstPort[0], 5004);
    DT_ASSERT_EQ(Sent.m_DstPort[1], 5005);
    DT_ASSERT_EQ(Sent.m_SrcPort[2], 1234);
    DT_ASSERT_EQ(Sent.m_VlanId[0], 7);
    DT_ASSERT_EQ(Sent.m_VlanId[1], 9);
    DT_ASSERT_EQ(Sent.m_Flags, (int)Filter.Flags);

    DT_ASSERT_OK(DtPcieCmd_PipeFlush(Fix.Drv, Rx));
    DT_ASSERT(LastWas(DT_FUNC_CODE_PIPE_CMD, Rx, DT_PIPE_CMD_ISSUE_PIPE_FLUSH,
                      sizeof(DtIoctlPipeCmdIssuePipeFlushInput), NULL));
    DT_ASSERT_OK(DtPcieCmd_PipeReleaseSharedBuffer(Fix.Drv, Rx));
    DT_ASSERT(LastWas(DT_FUNC_CODE_PIPE_CMD, Rx, DT_PIPE_CMD_RELEASE_SHARED_BUFFER,
                      sizeof(DtIoctlPipeCmdReleaseSharedBufferInput), NULL));

    DtPartRef Tx = {0, PORT};
    DT_ASSERT_OK(DtPcieCmd_NwOpenPipe(Fix.Drv, Fix.Nw, DT_PIPE_TX_RT_SWP, -1, &Tx));
    DT_ASSERT_OK(DtPcieCmd_PipeSetTxWriteOffset(Fix.Drv, Tx, 8));
    DtIoctlPipeCmdSetTxWriteOffsetInput Write;
    DT_ASSERT(LastWas(DT_FUNC_CODE_PIPE_CMD, Tx, DT_PIPE_CMD_SET_TX_WRITE_OFFSET,
                      sizeof(Write), &Write));
    DT_ASSERT_EQ(Write.m_TxWriteOffset, 8);
    DT_ASSERT_OK(DtPcieCmd_PipeGetTxReadOffset(Fix.Drv, Tx, &Offset));
    DT_ASSERT(LastWas(DT_FUNC_CODE_PIPE_CMD, Tx, DT_PIPE_CMD_GET_TX_READ_OFFSET,
                      sizeof(DtIoctlInputDataHdr), NULL));

    DtPipeProps Props;
    DT_ASSERT_OK(DtPcieCmd_PipeGetProps(Fix.Drv, Tx, &Props));
    DT_ASSERT(LastWas(DT_FUNC_CODE_PIPE_CMD, Tx, DT_PIPE_CMD_GET_PROPERTIES,
                      sizeof(DtIoctlInputDataHdr), NULL));
    DtPipeStatus Status;
    DT_ASSERT_OK(DtPcieCmd_PipeGetStatus(Fix.Drv, Tx, &Status));
    DT_ASSERT(LastWas(DT_FUNC_CODE_PIPE_CMD, Tx, DT_PIPE_CMD_GET_STATUS,
                      sizeof(DtIoctlInputDataHdr), NULL));

    DT_ASSERT_EQ(DtPcieCmd_PipeSetOpMode(Fix.Drv, Tx, 3), DTAPI_E_INVALID_ARG);
    DT_ASSERT(LastWas(DT_FUNC_CODE_PIPE_CMD, Tx, DT_PIPE_CMD_GET_STATUS,
                      sizeof(DtIoctlInputDataHdr), NULL));
    OsDmaBuffer_Free(&Buf);
    FINISH(Fix);
}

// A hardware pipe takes a buffer on a page in whole prefetch units, a software pipe any;
// a second buffer is in use; both hand-offs work.
DT_TEST(SharedBuffers)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;

    DtPartRef Hwp = {0, PORT};
    DtPartRef Swp = {0, PORT};
    DT_ASSERT_OK(DtPcieCmd_NwOpenPipe(Fix.Drv, Fix.Nw, DT_PIPE_TX_RT_HWP, -1, &Hwp));
    DT_ASSERT_OK(DtPcieCmd_NwOpenPipe(Fix.Drv, Fix.Nw, DT_PIPE_RX_RT_SWP, -1, &Swp));

    OsDmaBuffer Small;
    OsDmaBuffer Large;
    DT_ASSERT_EQ(OsDmaBuffer_Alloc(4096, &Small), 0);
    DT_ASSERT_EQ(OsDmaBuffer_Alloc(16 * 4096, &Large), 0);
    for (int AsLinux = 0; AsLinux < 2; AsLinux++)
    {
        SimDtPcie_RegisterPipeBufferAsLinux(AsLinux != 0);
        DT_ASSERT_EQ(DtPcieCmd_PipeSetSharedBufferAs(Fix.Drv, Hwp, &Small, !AsLinux),
                     DTAPI_E_INVALID_ARG);
        DT_ASSERT_OK(DtPcieCmd_PipeSetSharedBufferAs(Fix.Drv, Hwp, &Large, !AsLinux));
        DT_ASSERT_EQ(DtPcieCmd_PipeSetSharedBufferAs(Fix.Drv, Hwp, &Large, !AsLinux),
                     DTAPI_E_IN_USE);
        DT_ASSERT_OK(DtPcieCmd_PipeSetSharedBufferAs(Fix.Drv, Swp, &Small, !AsLinux));

        SimNwPipeState State;
        SimDtPcie_GetNwPipeState(IdOf(Hwp), &State);
        DT_ASSERT(State.BufferSet);
        DT_ASSERT_EQ(State.BufferSize, 16 * 4096);
        SimDtPcie_GetNwPipeState(IdOf(Swp), &State);
        DT_ASSERT_EQ(State.BufferSize, 4096);

        DT_ASSERT_OK(DtPcieCmd_PipeReleaseSharedBuffer(Fix.Drv, Hwp));
        DT_ASSERT_OK(DtPcieCmd_PipeReleaseSharedBuffer(Fix.Drv, Hwp));
        DT_ASSERT_OK(DtPcieCmd_PipeReleaseSharedBuffer(Fix.Drv, Swp));
    }
    DT_ASSERT_EQ(DtPcieCmd_PipeSetSharedBuffer(Fix.Drv, Swp, NULL), DTAPI_E_INVALID_ARG);
    OsDmaBuffer_Free(&Small);
    OsDmaBuffer_Free(&Large);
    FINISH(Fix);
}

// Properties by kind, the status of each mode, the direction of each command, and pipes
// that take no commands.
DT_TEST(PropertiesStatusAndDirections)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;

    DtPartRef Hwp = {0, PORT};
    DtPartRef Swp = {0, PORT};
    DT_ASSERT_OK(DtPcieCmd_NwOpenPipe(Fix.Drv, Fix.Nw, DT_PIPE_TX_RT_HWP, -1, &Hwp));
    DT_ASSERT_OK(DtPcieCmd_NwOpenPipe(Fix.Drv, Fix.Nw, DT_PIPE_RX_RT_SWP, -1, &Swp));

    DtPipeProps Props;
    DT_ASSERT_OK(DtPcieCmd_PipeGetProps(Fix.Drv, Hwp, &Props));
    DT_ASSERT_EQ(Props.Caps,
                 DT_PIPE_CAP_TX | DT_PIPE_CAP_HWP | DT_PIPE_CAP_RT | DT_PIPE_CAP_JFRAME);
    DT_ASSERT_EQ(Props.PrefetchSize, SIM_NW_HWP_PREFETCH_PAGES);
    DT_ASSERT_EQ(Props.DataWidth, 64);
    DT_ASSERT_EQ(Props.Type, DT_PIPE_TX_RT_HWP);
    DT_ASSERT_OK(DtPcieCmd_PipeGetProps(Fix.Drv, Swp, &Props));
    DT_ASSERT_EQ(Props.Caps,
                 DT_PIPE_CAP_RX | DT_PIPE_CAP_SWP | DT_PIPE_CAP_RT | DT_PIPE_CAP_JFRAME);
    DT_ASSERT_EQ(Props.PrefetchSize, 1);
    DT_ASSERT_EQ(Props.Type, DT_PIPE_RX_RT_SWP);

    DtPipeStatus Status;
    DT_ASSERT_OK(DtPcieCmd_PipeGetStatus(Fix.Drv, Swp, &Status));
    DT_ASSERT_EQ(Status.OpStatus, DT_BLOCK_OPSTATUS_IDLE);
    DT_ASSERT_OK(DtPcieCmd_PipeSetOpMode(Fix.Drv, Swp, DT_PIPE_OPMODE_RUN));
    DT_ASSERT_OK(DtPcieCmd_PipeGetStatus(Fix.Drv, Swp, &Status));
    DT_ASSERT_EQ(Status.OpStatus, DT_BLOCK_OPSTATUS_RUN);
    DT_ASSERT_EQ(Status.StatusFlags, 0);
    DT_ASSERT_EQ(Status.ErrorFlags, 0);
    DT_ASSERT_EQ(DtPcieCmd_PipeSetOpMode(Fix.Drv, Hwp, DT_PIPE_OPMODE_STANDBY),
                 DTAPI_E_NOT_INITIALIZED);

    uint32_t Offset = 0;
    DT_ASSERT_EQ(DtPcieCmd_PipeSetTxWriteOffset(Fix.Drv, Swp, 0), DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtPcieCmd_PipeGetTxReadOffset(Fix.Drv, Swp, &Offset),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtPcieCmd_PipeSetRxReadOffset(Fix.Drv, Hwp, 0), DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtPcieCmd_PipeGetRxWriteOffset(Fix.Drv, Hwp, &Offset),
                 DTAPI_E_NOT_SUPPORTED);
    DtIpFilter Filter = FilterOn(5004);
    DT_ASSERT_EQ(DtPcieCmd_PipeSetIpFilter(Fix.Drv, Hwp, &Filter), DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtPcieCmd_PipeSetTxWriteOffset(Fix.Drv, Hwp, 0), DTAPI_E_INVALID_ARG);

    DT_ASSERT_EQ(DtPcieCmd_PipeFlush(Fix.Drv, Fix.Nw), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_PipeFlush(Fix.Drv, (DtPartRef){Fix.Nw.Uuid | 2 << 20, PORT}),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_PipeFlush(Fix.Drv, (DtPartRef){Fix.Nw.Uuid | 500 << 20, PORT}),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_OK(DtPcieCmd_PipeFlush(Fix.Drv, (DtPartRef){Fix.Nw.Uuid | 10 << 20, PORT}));
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmit +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// A software pipe's packet goes to the scheduler at the first interval that sees it
// within 15 ms, and out at its time.
DT_TEST(SoftwarePipeSendsAtIntervals)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;

    OsDmaBuffer Buf;
    DtPartRef Tx = StartPipe(&Fix, DT_PIPE_TX_RT_SWP, 65536, &Buf);
    DT_ASSERT(Tx.Uuid != 0);

    uint8_t Frame[1600];
    size_t Size = MakeFrame(Frame, 1000, 5004, 1200, 3);
    size_t Packet = PutPacket(&Buf, 0, Frame, Size, T0 + 50 * MS);
    DT_ASSERT_OK(DtPcieCmd_PipeSetTxWriteOffset(Fix.Drv, Tx, (uint32_t)Packet));

    uint32_t Read = 1;
    SimDtPcie_AdvanceNwTime(30 * MS);
    DT_ASSERT_OK(DtPcieCmd_PipeGetTxReadOffset(Fix.Drv, Tx, &Read));
    DT_ASSERT_EQ(Read, 0);
    SimDtPcie_AdvanceNwTime(10 * MS);
    DT_ASSERT_OK(DtPcieCmd_PipeGetTxReadOffset(Fix.Drv, Tx, &Read));
    DT_ASSERT_EQ(Read, Packet);
    DT_ASSERT_EQ(SimDtPcie_NwSentCount(), 0);

    SimDtPcie_AdvanceNwTime(10 * MS - 1);
    DT_ASSERT_EQ(SimDtPcie_NwSentCount(), 0);
    SimDtPcie_AdvanceNwTime(1);
    DT_ASSERT_EQ(SimDtPcie_NwSentCount(), 1);
    SimNwPacket Sent;
    DT_ASSERT(SimDtPcie_GetNwSent(0, &Sent));
    DT_ASSERT_EQ(Sent.TodNs, T0 + 50 * MS);
    DT_ASSERT_EQ(Sent.PipeId, IdOf(Tx));
    DT_ASSERT_EQ(Sent.Size, Size);
    DT_ASSERT_MEM(Sent.Frame, Frame, Size);

    DT_ASSERT_OK(DtPcieCmd_NwClosePipe(Fix.Drv, Tx));
    OsDmaBuffer_Free(&Buf);
    FINISH(Fix);
}

// Over the software pipes the earliest packet goes first; a packet that is late goes out
// at once.
DT_TEST(EarliestFirstAndLateAtOnce)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;

    OsDmaBuffer BufA;
    OsDmaBuffer BufB;
    DtPartRef A = StartPipe(&Fix, DT_PIPE_TX_RT_SWP, 65536, &BufA);
    DtPartRef B = StartPipe(&Fix, DT_PIPE_TX_RT_SWP, 65536, &BufB);
    DT_ASSERT(A.Uuid != 0 && B.Uuid != 0);

    uint8_t Frame[1600];
    size_t Size = MakeFrame(Frame, 1000, 5004, 100, 0);
    size_t Offset = PutPacket(&BufA, 0, Frame, Size, T0 + 4 * MS);
    Offset += PutPacket(&BufA, Offset, Frame, Size, T0 + 8 * MS);
    DT_ASSERT_OK(DtPcieCmd_PipeSetTxWriteOffset(Fix.Drv, A, (uint32_t)Offset));
    Offset = PutPacket(&BufB, 0, Frame, Size, T0 + 2 * MS);
    Offset += PutPacket(&BufB, Offset, Frame, Size, T0 + 6 * MS);
    DT_ASSERT_OK(DtPcieCmd_PipeSetTxWriteOffset(Fix.Drv, B, (uint32_t)Offset));

    SimDtPcie_AdvanceNwTime(20 * MS);
    DT_ASSERT_EQ(SimDtPcie_NwSentCount(), 4);
    static const int Pipes[4] = {1, 0, 1, 0};
    for (int i = 0; i < 4; i++)
    {
        SimNwPacket Sent;
        DT_ASSERT(SimDtPcie_GetNwSent(i, &Sent));
        DT_ASSERT_EQ(Sent.TodNs, T0 + 10 * MS);
        DT_ASSERT_EQ(Sent.PipeId, IdOf(Pipes[i] ? B : A));
    }
    OsDmaBuffer_Free(&BufA);
    OsDmaBuffer_Free(&BufB);
    FINISH(Fix);
}

// A packet more than 10 s from the time sets invalid time and stops the pipe until it is
// flushed.
DT_TEST(InvalidTimeStopsUntilFlush)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;

    OsDmaBuffer Swb;
    OsDmaBuffer Hwb;
    DtPartRef Swp = StartPipe(&Fix, DT_PIPE_TX_RT_SWP, 65536, &Swb);
    DtPartRef Hwp = StartPipe(&Fix, DT_PIPE_TX_RT_HWP, 65536, &Hwb);
    DT_ASSERT(Swp.Uuid != 0 && Hwp.Uuid != 0);

    uint8_t Frame[1600];
    size_t Size = MakeFrame(Frame, 1000, 5004, 100, 0);
    size_t Packet = PutPacket(&Swb, 0, Frame, Size, T0 + 11000 * MS);
    DT_ASSERT_OK(DtPcieCmd_PipeSetTxWriteOffset(Fix.Drv, Swp, (uint32_t)Packet));
    Packet = PutPacket(&Hwb, 0, Frame, Size, T0 - 10001 * MS);
    DT_ASSERT_OK(DtPcieCmd_PipeSetTxWriteOffset(Fix.Drv, Hwp, (uint32_t)Packet));

    DtPipeStatus Status;
    DT_ASSERT_OK(DtPcieCmd_PipeGetStatus(Fix.Drv, Hwp, &Status));
    DT_ASSERT_EQ(Status.ErrorFlags, DT_PIPE_ERROR_INVALID_TIME);
    DT_ASSERT_OK(DtPcieCmd_PipeGetStatus(Fix.Drv, Swp, &Status));
    DT_ASSERT_EQ(Status.ErrorFlags, 0);
    SimDtPcie_AdvanceNwTime(10 * MS);
    DT_ASSERT_OK(DtPcieCmd_PipeGetStatus(Fix.Drv, Swp, &Status));
    DT_ASSERT_EQ(Status.ErrorFlags, DT_PIPE_ERROR_INVALID_TIME);

    SimDtPcie_AdvanceNwTime(12000 * MS);
    DT_ASSERT_EQ(SimDtPcie_NwSentCount(), 0);

    DT_ASSERT_OK(DtPcieCmd_PipeFlush(Fix.Drv, Swp));
    DT_ASSERT_OK(DtPcieCmd_PipeFlush(Fix.Drv, Hwp));
    DT_ASSERT_OK(DtPcieCmd_PipeGetStatus(Fix.Drv, Swp, &Status));
    DT_ASSERT_EQ(Status.ErrorFlags, 0);
    DT_ASSERT_OK(DtPcieCmd_PipeGetStatus(Fix.Drv, Hwp, &Status));
    DT_ASSERT_EQ(Status.ErrorFlags, 0);
    uint32_t Read = 1;
    DT_ASSERT_OK(DtPcieCmd_PipeGetTxReadOffset(Fix.Drv, Swp, &Read));
    DT_ASSERT_EQ(Read, 0);
    OsDmaBuffer_Free(&Swb);
    OsDmaBuffer_Free(&Hwb);
    FINISH(Fix);
}

// A hardware pipe hands its packets over at once, and each goes out at its time; in
// STANDBY the write offset waits for RUN.
DT_TEST(HardwarePipeSendsAtItsTime)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;

    OsDmaBuffer Buf;
    DtPartRef Tx = StartPipe(&Fix, DT_PIPE_TX_RT_HWP, 65536, &Buf);
    DT_ASSERT(Tx.Uuid != 0);

    uint8_t Frame[1600];
    size_t Size = MakeFrame(Frame, 1000, 5004, 200, 9);
    size_t Offset = PutPacket(&Buf, 0, Frame, Size, T0 + 3 * MS);
    DT_ASSERT_OK(DtPcieCmd_PipeSetTxWriteOffset(Fix.Drv, Tx, (uint32_t)Offset));
    uint32_t Read = 0;
    DT_ASSERT_OK(DtPcieCmd_PipeGetTxReadOffset(Fix.Drv, Tx, &Read));
    DT_ASSERT_EQ(Read, Offset);
    DtPipeStatus Status;
    DT_ASSERT_OK(DtPcieCmd_PipeGetStatus(Fix.Drv, Tx, &Status));
    DT_ASSERT_EQ(Status.StatusFlags, DT_PIPE_STATUS_PACKET_WAITING);

    SimDtPcie_AdvanceNwTime(3 * MS - 1);
    DT_ASSERT_EQ(SimDtPcie_NwSentCount(), 0);
    SimDtPcie_AdvanceNwTime(1);
    DT_ASSERT_EQ(SimDtPcie_NwSentCount(), 1);

    DT_ASSERT_OK(DtPcieCmd_PipeSetOpMode(Fix.Drv, Tx, DT_PIPE_OPMODE_STANDBY));
    size_t Next = Offset + PutPacket(&Buf, Offset, Frame, Size, T0 - 5000 * MS);
    DT_ASSERT_OK(DtPcieCmd_PipeSetTxWriteOffset(Fix.Drv, Tx, (uint32_t)Next));
    DT_ASSERT_OK(DtPcieCmd_PipeGetTxReadOffset(Fix.Drv, Tx, &Read));
    DT_ASSERT_EQ(Read, Offset);
    DT_ASSERT_OK(DtPcieCmd_PipeSetOpMode(Fix.Drv, Tx, DT_PIPE_OPMODE_RUN));
    DT_ASSERT_OK(DtPcieCmd_PipeGetTxReadOffset(Fix.Drv, Tx, &Read));
    DT_ASSERT_EQ(Read, Next);
    DT_ASSERT_EQ(SimDtPcie_NwSentCount(), 2);
    SimNwPacket Sent;
    DT_ASSERT(SimDtPcie_GetNwSent(1, &Sent));
    DT_ASSERT_EQ(Sent.TodNs, T0 + 3 * MS);
    OsDmaBuffer_Free(&Buf);
    FINISH(Fix);
}

// A packet whose header does not check is counted, and the pipe's data skipped.
DT_TEST(BadHeaderIsSkipped)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;

    OsDmaBuffer Buf;
    DtPartRef Tx = StartPipe(&Fix, DT_PIPE_TX_RT_HWP, 65536, &Buf);
    DT_ASSERT(Tx.Uuid != 0);
    memset(Buf.Data, 0x5A, 64);
    DT_ASSERT_OK(DtPcieCmd_PipeSetTxWriteOffset(Fix.Drv, Tx, 64));
    uint32_t Read = 0;
    DT_ASSERT_OK(DtPcieCmd_PipeGetTxReadOffset(Fix.Drv, Tx, &Read));
    DT_ASSERT_EQ(Read, 64);
    SimNwCounters Counters;
    SimDtPcie_GetNwCounters(&Counters);
    DT_ASSERT_EQ(Counters.HeaderErrors, 1);
    DT_ASSERT_EQ(SimDtPcie_NwSentCount(), 0);
    OsDmaBuffer_Free(&Buf);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Receive +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Reads the packet at Offset of a receive buffer and checks it holds Frame, arrived at
// TodNs as substream SubStream. Returns its size, 0 when it does not check.
static size_t CheckPacket(const OsDmaBuffer* Buf, size_t Offset, const uint8_t* Frame,
                          size_t Size, uint64_t TodNs, int SubStream)
{
    DtEthIpFields Header;

    if (!DtEthIp_Read(Buf->Data + Offset, &Header) || Header.Jumbo ||
        Header.FrameSize != (int)Size ||
        Header.NumWords != DtEthIp_NumWords((int)Size, 8) ||
        Header.PacketType != DT_ETHIP_TYPE_IPV4 ||
        Header.Protocol != DT_ETHIP_PROTO_UDP || Header.IpAddressOffset != 44 ||
        Header.PortOffset != 52 || Header.SubStream != SubStream ||
        !Header.TimestampValid || Header.Seconds != TodNs / 1000000000u ||
        Header.Nanoseconds != TodNs % 1000000000u ||
        memcmp(Buf->Data + Offset + DT_ETHIP_HEADER_SIZE, Frame, Size) != 0)
    {
        return 0;
    }
    return (size_t)Header.NumWords * 8;
}

// Through the loopback a sent packet arrives at a hardware pipe whose filter takes it,
// at once and as the substream of the port that matched; one no filter takes goes to the
// operating system at the next interval.
DT_TEST(LoopbackToHardwarePipe)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;

    OsDmaBuffer TxBuf;
    OsDmaBuffer RxBuf;
    DtPartRef Tx = StartPipe(&Fix, DT_PIPE_TX_RT_HWP, 65536, &TxBuf);
    DtPartRef Rx = StartPipe(&Fix, DT_PIPE_RX_RT_HWP, 65536, &RxBuf);
    DT_ASSERT(Tx.Uuid != 0 && Rx.Uuid != 0);
    DtIpFilter Filter = FilterOn(6000);
    Filter.DstPort[1] = 5004;
    Filter.Flags |= DT_PIPE_IPFLT_FLAG_EN_DSTPORT1;
    DT_ASSERT_OK(DtPcieCmd_PipeSetIpFilter(Fix.Drv, Rx, &Filter));
    SimDtPcie_SetNwLoopback(true);

    uint8_t Frame[1600];
    uint8_t Other[1600];
    size_t Size = MakeFrame(Frame, 1000, 5004, 1000, 1);
    size_t OtherSize = MakeFrame(Other, 1000, 5005, 10, 2);
    size_t Offset = PutPacket(&TxBuf, 0, Frame, Size, T0 + 1 * MS);
    Offset += PutPacket(&TxBuf, Offset, Other, OtherSize, T0 + 2 * MS);
    DT_ASSERT_OK(DtPcieCmd_PipeSetTxWriteOffset(Fix.Drv, Tx, (uint32_t)Offset));

    uint32_t Write = 1;
    DT_ASSERT_OK(DtPcieCmd_PipeGetRxWriteOffset(Fix.Drv, Rx, &Write));
    DT_ASSERT_EQ(Write, 0);
    SimDtPcie_AdvanceNwTime(1 * MS);
    DT_ASSERT_OK(DtPcieCmd_PipeGetRxWriteOffset(Fix.Drv, Rx, &Write));
    size_t Got = CheckPacket(&RxBuf, 0, Frame, Size, T0 + 1 * MS, 1);
    DT_ASSERT(Got != 0);
    DT_ASSERT_EQ(Write, Got);

    SimDtPcie_AdvanceNwTime(8 * MS);
    SimNwCounters Counters;
    SimDtPcie_GetNwCounters(&Counters);
    DT_ASSERT_EQ(Counters.ToOperatingSystem, 0);
    SimDtPcie_AdvanceNwTime(1 * MS);
    SimDtPcie_GetNwCounters(&Counters);
    DT_ASSERT_EQ(Counters.ToOperatingSystem, 1);
    DT_ASSERT_OK(DtPcieCmd_PipeGetRxWriteOffset(Fix.Drv, Rx, &Write));
    DT_ASSERT_EQ(Write, Got);
    OsDmaBuffer_Free(&TxBuf);
    OsDmaBuffer_Free(&RxBuf);
    FINISH(Fix);
}

// A software pipe gets an arrived frame only at the next interval, and only in RUN.
DT_TEST(SoftwarePipeReceivesAtIntervals)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;

    OsDmaBuffer Buf;
    DtPartRef Rx = StartPipe(&Fix, DT_PIPE_RX_RT_SWP, 65536, &Buf);
    DT_ASSERT(Rx.Uuid != 0);
    DtIpFilter Filter = FilterOn(5004);
    DT_ASSERT_OK(DtPcieCmd_PipeSetIpFilter(Fix.Drv, Rx, &Filter));

    uint8_t Frame[1600];
    size_t Size = MakeFrame(Frame, 1000, 5004, 500, 4);
    DT_ASSERT(SimDtPcie_InjectNwFrame(Frame, Size, T0 + 13 * MS));
    SimDtPcie_AdvanceNwTime(19 * MS);
    uint32_t Write = 1;
    DT_ASSERT_OK(DtPcieCmd_PipeGetRxWriteOffset(Fix.Drv, Rx, &Write));
    DT_ASSERT_EQ(Write, 0);
    SimDtPcie_AdvanceNwTime(1 * MS);
    DT_ASSERT_OK(DtPcieCmd_PipeGetRxWriteOffset(Fix.Drv, Rx, &Write));
    size_t Got = CheckPacket(&Buf, 0, Frame, Size, T0 + 13 * MS, 0);
    DT_ASSERT(Got != 0);
    DT_ASSERT_EQ(Write, Got);

    DT_ASSERT_OK(DtPcieCmd_PipeSetOpMode(Fix.Drv, Rx, DT_PIPE_OPMODE_IDLE));
    DT_ASSERT(SimDtPcie_InjectNwFrame(Frame, Size, T0 + 21 * MS));
    SimDtPcie_AdvanceNwTime(10 * MS);
    SimNwCounters Counters;
    SimDtPcie_GetNwCounters(&Counters);
    DT_ASSERT_EQ(Counters.ToOperatingSystem, 1);
    OsDmaBuffer_Free(&Buf);
    FINISH(Fix);
}

// A frame that does not fit is lost with the overflow error, which the next one that fits
// clears.
DT_TEST(OverflowLosesAndClears)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;

    OsDmaBuffer Buf;
    DtPartRef Rx = StartPipe(&Fix, DT_PIPE_RX_RT_SWP, 4096, &Buf);
    DT_ASSERT(Rx.Uuid != 0);
    DtIpFilter Filter = FilterOn(5004);
    DT_ASSERT_OK(DtPcieCmd_PipeSetIpFilter(Fix.Drv, Rx, &Filter));

    uint8_t Frame[1600];
    size_t Size = MakeFrame(Frame, 1000, 5004, 1468, 0);
    for (int i = 0; i < 3; i++)
        DT_ASSERT(SimDtPcie_InjectNwFrame(Frame, Size, T0 + 1 * MS));
    SimDtPcie_AdvanceNwTime(10 * MS);
    DtPipeStatus Status;
    DT_ASSERT_OK(DtPcieCmd_PipeGetStatus(Fix.Drv, Rx, &Status));
    DT_ASSERT_EQ(Status.ErrorFlags, DT_PIPE_ERROR_OVERFLOW);
    SimNwCounters Counters;
    SimDtPcie_GetNwCounters(&Counters);
    DT_ASSERT_EQ(Counters.Lost, 1);
    uint32_t Write = 0;
    DT_ASSERT_OK(DtPcieCmd_PipeGetRxWriteOffset(Fix.Drv, Rx, &Write));
    DT_ASSERT_EQ(Write, 2 * 1528);

    DT_ASSERT_OK(DtPcieCmd_PipeSetRxReadOffset(Fix.Drv, Rx, Write));
    DT_ASSERT(SimDtPcie_InjectNwFrame(Frame, Size, T0 + 11 * MS));
    SimDtPcie_AdvanceNwTime(10 * MS);
    DT_ASSERT_OK(DtPcieCmd_PipeGetStatus(Fix.Drv, Rx, &Status));
    DT_ASSERT_EQ(Status.ErrorFlags, 0);
    DT_ASSERT_OK(DtPcieCmd_PipeGetRxWriteOffset(Fix.Drv, Rx, &Write));
    DT_ASSERT_EQ(Write, (3 * 1528) % 4096);
    OsDmaBuffer_Free(&Buf);
    FINISH(Fix);
}

// The filter as DtPipe_IsPacketForPipe applies it: addresses, their version, the source
// port of substream 0, and VLAN tags.
DT_TEST(SoftwareFilter)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;

    OsDmaBuffer Buf;
    DtPartRef Rx = StartPipe(&Fix, DT_PIPE_RX_RT_SWP, 65536, &Buf);
    DT_ASSERT(Rx.Uuid != 0);

    uint8_t Frame[1600];
    size_t Size = MakeFrame(Frame, 1000, 5004, 100, 0);
    static const struct
    {
        uint32_t AddFlags;
        uint8_t SrcIp3;
        uint16_t SrcPort;
        bool Taken;
    } Cases[] = {
        {0, 0, 0, true},
        {DT_PIPE_IPFLT_FLAG_EN_SRCIP_IPV4, 10, 0, true},
        {DT_PIPE_IPFLT_FLAG_EN_SRCIP_IPV4, 11, 0, false},
        {DT_PIPE_IPFLT_FLAG_EN_SRCIP_IPV6, 10, 0, false},
        {DT_PIPE_IPFLT_FLAG_EN_SRCPORT0, 0, 1000, true},
        {DT_PIPE_IPFLT_FLAG_EN_SRCPORT0, 0, 1001, false},
        {DT_PIPE_IPFLT_FLAG_VLAN0_1AD, 0, 0, false},
    };
    for (size_t i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        DtIpFilter Filter = FilterOn(5004);
        static const uint8_t Source[4] = {192, 168, 1, 0};

        memcpy(Filter.SrcIp, Source, sizeof(Source));
        Filter.SrcIp[3] = Cases[i].SrcIp3;
        Filter.SrcPort[0] = Cases[i].SrcPort;
        Filter.Flags |= Cases[i].AddFlags;
        DT_ASSERT_OK(DtPcieCmd_PipeSetIpFilter(Fix.Drv, Rx, &Filter));
        DT_ASSERT_OK(DtPcieCmd_PipeFlush(Fix.Drv, Rx));

        DT_ASSERT(SimDtPcie_InjectNwFrame(Frame, Size, T0 + (10 * i + 1) * MS));
        SimDtPcie_AdvanceNwTime(10 * MS);
        uint32_t Write = 0;
        DT_ASSERT_OK(DtPcieCmd_PipeGetRxWriteOffset(Fix.Drv, Rx, &Write));
        if ((Write != 0) != Cases[i].Taken)
            DT_FAIL("case %zu", i);
    }
    OsDmaBuffer_Free(&Buf);
    FINISH(Fix);
}

// With the clock the host's, packets move as time passes.
DT_TEST(RealTimeClock)
{
    Fixture Fix;
    if (!Open(&Fix, DtFailures))
        return;

    SimDtPcie_Reset();
    SimDtPcie_SetDta2110Index(INDEX);
    OsDrv_Close(Fix.Drv);
    Fix.Drv = OsDrv_Open(INDEX);
    DT_ASSERT(Fix.Drv != NULL);

    OsDmaBuffer Buf;
    DtPartRef Tx = StartPipe(&Fix, DT_PIPE_TX_RT_SWP, 65536, &Buf);
    DT_ASSERT(Tx.Uuid != 0);
    uint32_t Seconds = 0;
    uint32_t Nanoseconds = 0;
    DT_ASSERT_OK(DtPcieCmd_GetTimeOfDay(Fix.Drv, &Seconds, &Nanoseconds));
    uint64_t Now = (uint64_t)Seconds * 1000000000u + Nanoseconds;

    uint8_t Frame[1600];
    size_t Size = MakeFrame(Frame, 1000, 5004, 100, 0);
    size_t Packet = PutPacket(&Buf, 0, Frame, Size, Now + 100 * MS);
    DT_ASSERT_OK(DtPcieCmd_PipeSetTxWriteOffset(Fix.Drv, Tx, (uint32_t)Packet));
    for (int i = 0; i < 200 && SimDtPcie_NwSentCount() == 0; i++)
        OsTime_SleepMs(5);
    DT_ASSERT_EQ(SimDtPcie_NwSentCount(), 1);
    SimNwPacket Sent;
    DT_ASSERT(SimDtPcie_GetNwSent(0, &Sent));
    DT_ASSERT_EQ(Sent.TodNs, Now + 100 * MS);
    OsDmaBuffer_Free(&Buf);
    FINISH(Fix);
}

DT_TEST_MAIN("SimNw", DT_RUN(Dta2110IsThereWhenAdded), DT_RUN(PortAndFunction),
             DT_RUN(MacAddressAndPhySpeed), DT_RUN(OpenPipesByType),
             DT_RUN(SoftwarePipesRunOut), DT_RUN(ClosingPipes),
             DT_RUN(PipeCommandsOnTheWire), DT_RUN(SharedBuffers),
             DT_RUN(PropertiesStatusAndDirections), DT_RUN(SoftwarePipeSendsAtIntervals),
             DT_RUN(EarliestFirstAndLateAtOnce), DT_RUN(InvalidTimeStopsUntilFlush),
             DT_RUN(HardwarePipeSendsAtItsTime), DT_RUN(BadHeaderIsSkipped),
             DT_RUN(LoopbackToHardwarePipe), DT_RUN(SoftwarePipeReceivesAtIntervals),
             DT_RUN(OverflowLosesAndClears), DT_RUN(SoftwareFilter),
             DT_RUN(RealTimeClock))
