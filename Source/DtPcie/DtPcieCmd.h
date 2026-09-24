// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtPcieCmd.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - DtPcie driver commands: typed commands on top of the OS abstraction
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "OAL/OsAbstractionLayer.h" // Device handles and IOCTL transport.
#include "OAL/OsDmaBuffer.h"        // Buffers registered for DMA.
#include "cdtapi.h"                 // DTAPI result codes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Results +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// This layer returns the public DTAPI result codes from cdtapi.h, so that a result
// can travel up to the API unchanged.
//

// True for DTAPI_OK and for the DTAPI_OK_* successes that carry a warning.
#define DT_SUCCEEDED(Result) ((Result) < DTAPI_E)

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// One function per driver command, each taking and returning plain C types. The driver
// structures stay inside this layer, so nothing above it depends on their layout.
//

// The driver object a command is addressed to: a block controller, a driver function, or
// the core function of the device. It is named by the UUID the driver gave it and the
// port it belongs to; the two travel together, as the header of every such command
// carries them. An object of the device rather than of one of its ports, the core
// function among them, has DT_PROPERTY_DEVICE for its port.
typedef struct DtDrvObject
{
    int Uuid;
    int PortIndex; // From 0, or DT_PROPERTY_DEVICE
} DtDrvObject;

typedef struct DtDriverVersion
{
    int Major;
    int Minor;
    int Micro;
    int Build;
} DtDriverVersion;

typedef struct DtDeviceInfo
{
    int TypeNumber; // 2178 for a DTA-2178
    int SubType;    // 0 for none, 1 for A, and so on
    int64_t Serial;
    int HardwareRevision;
    int FirmwareVersion;
    int FirmwareVariant;
    int FirmwareStatus; // One of the DT_FWSTATUS_* values
    uint16_t VendorId;
    uint16_t DeviceId;
    uint16_t SubVendorId;
    uint16_t SubSystemId;

    // When the firmware was built.
    int FwBuildYear;
    int FwBuildMonth;
    int FwBuildDay;
    int FwBuildHour;
    int FwBuildMinute;

    // Where the device is and how its PCIe link runs.
    int BusNumber;
    int SlotNumber;
    int PcieNumLanes;
    int PcieMaxLanes;
    int PcieLinkSpeed;          // PCIe generation of the link
    int PcieMaxSpeed;           // PCIe generation the link can reach
    int PcieMaxPayloadSize;     // Bytes
    int PcieMaxReadRequestSize; // Bytes
    int PcieMaxSlotPower;       // Milliwatts; 0 from a driver without GET_DEV_INFO2
} DtDeviceInfo;

// Reads the version of the driver behind Drv.
DtapiResult DtPcieCmd_GetDriverVersion(OsDrv* Drv, DtDriverVersion* Version);

// True when a DtPcie driver of this version is new enough: 1.3.1 or later. The build
// number does not count.
bool DtPcieCmd_VersionIsSupported(const DtDriverVersion* Version);

// True when Version is Major.Minor.Micro.Build or later, comparing the four numbers in
// that order.
bool DtPcieCmd_VersionAtLeast(const DtDriverVersion* Version, int Major, int Minor,
                              int Micro, int Build);

// Reads the identity of the device behind Drv. Uses GET_DEV_INFO2, and falls back to the
// original GET_DEV_INFO for a driver that predates it, whose PCIe part has no slot power.
DtapiResult DtPcieCmd_GetDeviceInfo(OsDrv* Drv, DtDeviceInfo* Info);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Properties -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A property is a named value the driver holds for the device, such as PORT_COUNT, or
// for one of its ports, such as the capability 3GSDI. It is always read for the device
// behind Drv, with its own hardware revision and firmware.
//
// The functions clear their output first, and fail with DTAPI_E_BUF_TOO_SMALL for a name
// longer than the driver accepts and with DTAPI_E_NOT_FOUND for a property the device
// does not have.
//

// The port index of a property that belongs to the device rather than to a port.
#define DT_PROPERTY_DEVICE -1

// Reads an integer property. PortIndex counts from zero, or is DT_PROPERTY_DEVICE.
DtapiResult DtPcieCmd_GetPropertyInt(OsDrv* Drv, const char* Name, int PortIndex,
                                     int* Value);

// Reads a boolean property. PortIndex counts from zero, or is DT_PROPERTY_DEVICE.
DtapiResult DtPcieCmd_GetPropertyBool(OsDrv* Drv, const char* Name, int PortIndex,
                                      bool* Value);

// The size of a buffer that holds every string property, terminator included: the
// driver answers with at most 96 characters.
#define DT_PROPERTY_STR_SIZE 97

// Reads a string property into Str, which holds Size bytes. PortIndex counts from zero,
// or is DT_PROPERTY_DEVICE. Fails with DTAPI_E_BUF_TOO_SMALL, leaving Str empty, when
// the string does not fit.
DtapiResult DtPcieCmd_GetPropertyStr(OsDrv* Drv, const char* Name, int PortIndex,
                                     char* Str, size_t Size);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- I/O configuration -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A configuration is cdtapi.h's DtIoConfig: a port number from 1, and the group, value
// and sub-value as DTAPI_IOCONFIG_ codes, -1 for none. This layer converts to what the
// driver takes: a port index from 0, the codes as names, and, for the I/O direction
// values that name another port in ParXtra[0], that port as an index as well.
//
// The driver takes a list in one command. A list of one is the request the single forms
// send.
//

// Reads the configurations of Configs[i].Group on Configs[i].Port, and fills in the other
// fields of each. Count must be at least 1. When this fails, the entries are as they
// were.
DtapiResult DtPcieCmd_GetIoConfigList(OsDrv* Drv, DtIoConfig* Configs, int Count);

// Applies Count configurations together, Count at least 1. The driver validates them;
// this layer only converts them, and refuses a LOOPS2TS output whose ParXtra[1], the
// ISI, is outside 0 to 255 with DTAPI_E_INVALID_ISI, before sending anything.
DtapiResult DtPcieCmd_SetIoConfigList(OsDrv* Drv, const DtIoConfig* Configs, int Count);

// The same for one configuration.
DtapiResult DtPcieCmd_GetIoConfig(OsDrv* Drv, DtIoConfig* Config);
DtapiResult DtPcieCmd_SetIoConfig(OsDrv* Drv, const DtIoConfig* Config);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Time of day -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Reads the device's time-of-day clock.
DtapiResult DtPcieCmd_GetTimeOfDay(OsDrv* Drv, uint32_t* Seconds, uint32_t* Nanoseconds);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SDI receiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A command for a driver function rather than for the device goes to the function's UUID,
// which the device layer reads from the function's properties, and to the index of the
// port the function belongs to.
//

// What an SDI receiver reports of its input, converted from the driver's own fields.
typedef struct DtSdiRxStatus
{
    bool CarrierDetect;
    bool SdiLock;       // Locked to the SDI stream
    bool LineLock;      // Locked to the lines
    bool Valid;         // The counters below describe the input
    int NumSymsHanc;    // Symbols per line in HANC, EAV and SAV included
    int NumSymsVidVanc; // Symbols per line in the active part
    int NumLinesF1;
    int NumLinesF2;
    bool IsLevelB;      // 3G level B
    uint32_t PayloadId; // The SMPTE 352 VPID, 0 for none
    double FrameRate;   // Frames per second, 0 when the driver reports no frame period
    int SdiRate;        // -1 unknown, 0 SD, 1 HD, 2 3G, 3 6G, 4 12G
} DtSdiRxStatus;

// Reads the status of the SDI receiver Object. Clears *Status first.
DtapiResult DtPcieCmd_SdiRxGetStatus(OsDrv* Drv, DtDrvObject Object,
                                     DtSdiRxStatus* Status);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SDI receive channel -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The CHSDIRX driver function: a receive channel that writes the SDI lines of a port
// into a DMA ring the driver allocates, and reports how far it has written. The process
// maps the ring, reads from it, and tells the driver how far it has read. Every command
// goes to the channel's UUID and port index.
//

// What the channel's DMA and formatter are like, as DT_CHSDIRX_CMD_GET_PROPS reports.
typedef struct DtChSdiRxProps
{
    uint32_t DmaCaps;    // DT_CDMAC_CAP_ flags
    int PrefetchSize;    // In pages; the ring is a multiple of this many pages
    int PcieDataWidth;   // Bits per PCIe data word
    int ReorderBufSize;  // Bytes
    int StreamAlignment; // Bits; every part of the ring's format is padded to this
} DtChSdiRxProps;

// How the channel is configured, as DT_CHSDIRX_CMD_CONFIGURE takes it.
typedef struct DtChSdiRxConfig
{
    int NumPorts;       // 1, or 4 for quad link
    int PortIndices[4]; // The ports, from 0, in link order
    int DmaMinSize;     // Bytes; the driver may make the ring larger
    int FmtIntInterval; // Microseconds between format events
    int FmtIntDelay;    // Microseconds from the start of a frame to its first event
    int FmtNumIntsPerFrame;
    int NumSymsHanc;    // Symbols per line in HANC, EAV and SAV included
    int NumSymsVidVanc; // Symbols per line in the active part
    int NumLines;       // Lines per frame
    int SdiRate;        // DT_DRV_SDIRATE_ value
    bool AssumeInterlaced;
    bool Scale12GTo3G;
} DtChSdiRxConfig;

// A format event: which frame the formatter is in, how far, and whether it is in sync.
typedef struct DtChSdiRxEvent
{
    int FrameId;   // The 16 least significant bits of the frame number
    int SeqNumber; // 0 for the first event of a frame
    bool InSync;
} DtChSdiRxEvent;

// Attaches to the channel, exclusively or shared, under a friendly name of at most
// DT_CHAN_FRIENDLY_NAME_MAX_LENGTH characters. A longer or empty name gives
// DTAPI_E_INVALID_ARG without a command.
DtapiResult DtPcieCmd_ChSdiRxAttach(OsDrv* Drv, DtDrvObject Object, bool Exclusive,
                                    const char* FriendlyName);

// Detaches from the channel.
DtapiResult DtPcieCmd_ChSdiRxDetach(OsDrv* Drv, DtDrvObject Object);

// Configures the channel, which must be idle.
DtapiResult DtPcieCmd_ChSdiRxConfigure(OsDrv* Drv, DtDrvObject Object,
                                       const DtChSdiRxConfig* Config);

// Reads and sets this user's operational mode, a DT_FUNC_OPMODE_ value.
DtapiResult DtPcieCmd_ChSdiRxGetOpMode(OsDrv* Drv, DtDrvObject Object, int* OpMode);
DtapiResult DtPcieCmd_ChSdiRxSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);

// Waits up to TimeoutMs milliseconds for the next format event. Gives DTAPI_E_TIMEOUT
// when none comes.
DtapiResult DtPcieCmd_ChSdiRxWaitForFmtEvent(OsDrv* Drv, DtDrvObject Object,
                                             int TimeoutMs, DtChSdiRxEvent* Event);

// Reads how far the channel has written into the ring, as an offset from its start.
DtapiResult DtPcieCmd_ChSdiRxGetWriteOffset(OsDrv* Drv, DtDrvObject Object,
                                            uint32_t* Offset);

// Tells the channel how far this user has read.
DtapiResult DtPcieCmd_ChSdiRxSetReadOffset(OsDrv* Drv, DtDrvObject Object,
                                           uint32_t Offset);

// Reads the channel's properties.
DtapiResult DtPcieCmd_ChSdiRxGetProps(OsDrv* Drv, DtDrvObject Object,
                                      DtChSdiRxProps* Props);

// Reads the status of the channel's input, as DtPcieCmd_SdiRxGetStatus does for the
// receiver.
DtapiResult DtPcieCmd_ChSdiRxGetSdiStatus(OsDrv* Drv, DtDrvObject Object,
                                          DtSdiRxStatus* Status);

// Maps the configured ring into the process. On Windows the driver maps it during the
// command and returns its address; on Linux the driver returns address 0, and the ring
// is then mapped from the device at offset DT_MMAP_PORT_MEM_SEGMENT_SIZE times the port
// index plus one. *MappedHere is true in the second case, in which
// DtPcieCmd_ChSdiRxUnmapDmaBuf must release the mapping.
DtapiResult DtPcieCmd_ChSdiRxMapDmaBuf(OsDrv* Drv, DtDrvObject Object, uint8_t** Buffer,
                                       int* BufSize, int* MaxLoad, bool* MappedHere);

// Releases a mapping DtPcieCmd_ChSdiRxMapDmaBuf made itself; one the driver made goes
// with the detach.
void DtPcieCmd_ChSdiRxUnmapDmaBuf(OsDrv* Drv, uint8_t* Buffer, int BufSize,
                                  bool MappedHere);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Exclusive access -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// One file handle at a time can hold a driver function or building block. The driver
// checks the holder on the commands that change an object, and the handle lets go of what
// it holds when it is closed.
//

// Issues EXCL_ACCESS_CMD Cmd, a DT_EXCLUSIVE_ACCESS_CMD_ value, for Object. Acquiring an
// object that is held, also by this handle, gives DTAPI_E_IN_USE. Checking gives DTAPI_OK
// for an object this handle holds, DTAPI_E_EXCL_ACCESS_REQD for one nobody holds, and
// DTAPI_E_IN_USE for one another handle holds; probing gives DTAPI_E_IN_USE for an object
// anyone holds. Releasing an object another handle holds gives DTAPI_E_IN_USE.
DtapiResult DtPcieCmd_ExclAccess(OsDrv* Drv, DtDrvObject Object, int Cmd);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SDI transmit blocks -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A port transmits without a channel driver function. The process drives the objects of
// the port's AF_DMA and AF_ASISDITX API functions itself: the DMA controller CDMAC, which
// reads a buffer the process allocates, the burst FIFO behind it, the formatter SDITXF,
// the switches around the 12G demultiplexer SDIDMX12G, the protocol encoder SDITXP and
// the driver function SDITXPHY. Every command goes to the object's UUID and port index.
//
// Operational modes are DT_BLOCK_OPMODE_ values, and DT_FUNC_OPMODE_ values for SDITXPHY;
// any other value gives DTAPI_E_INVALID_ARG without a command.
//

// What a DMA controller is like, as DT_CDMAC_CMD_GET_PROPERTIES reports.
typedef struct DtCdmacProps
{
    uint32_t Caps;      // DT_CDMAC_CAP_ flags
    int PrefetchSize;   // In pages; a buffer is a multiple of this many pages
    int PcieDataWidth;  // Bits per PCIe data word
    int ReorderBufSize; // Bytes
} DtCdmacProps;

DtapiResult DtPcieCmd_CdmacGetProps(OsDrv* Drv, DtDrvObject Object, DtCdmacProps* Props);

// Registers Buf, which the process allocated and keeps until the buffer is freed, for
// Direction, DT_CDMAC_DIR_TX or DT_CDMAC_DIR_RX. The buffer travels as this platform's
// driver takes it, as OsDmaBuffer_DescribeHandOff describes;
// DtPcieCmd_CdmacAllocateBufferAs chooses the convention, true for Windows's, so that
// both can be tested on every platform. A direction the driver does not define, an empty
// buffer and one larger than an int can count give DTAPI_E_INVALID_ARG.
//
// With the buffer as the output, as on Windows, the answer's size is not checked against
// the buffer's: the driver reports the output it was given, which could not be confirmed
// on a card.
DtapiResult DtPcieCmd_CdmacAllocateBuffer(OsDrv* Drv, DtDrvObject Object, int Direction,
                                          const OsDmaBuffer* Buf);
DtapiResult DtPcieCmd_CdmacAllocateBufferAs(OsDrv* Drv, DtDrvObject Object, int Direction,
                                            const OsDmaBuffer* Buf, bool BufferIsOutput);

// Lets go of the registered buffer. The controller must be idle.
DtapiResult DtPcieCmd_CdmacFreeBuffer(OsDrv* Drv, DtDrvObject Object);

// Empties the controller's pipeline. The controller must be idle.
DtapiResult DtPcieCmd_CdmacIssueChannelFlush(OsDrv* Drv, DtDrvObject Object);

DtapiResult DtPcieCmd_CdmacSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);

// Sets a DT_CDMAC_TESTMODE_ value. The controller must be idle.
DtapiResult DtPcieCmd_CdmacSetTestMode(OsDrv* Drv, DtDrvObject Object, int TestMode);

// Reads how far the card has read the buffer, as an offset from its start.
DtapiResult DtPcieCmd_CdmacGetTxReadOffset(OsDrv* Drv, DtDrvObject Object,
                                           uint32_t* Offset);

// Tells the card how far the buffer holds data for it.
DtapiResult DtPcieCmd_CdmacSetTxWriteOffset(OsDrv* Drv, DtDrvObject Object,
                                            uint32_t Offset);

// Reads the reorder buffer's load and its minimum or maximum since the last clear.
DtapiResult DtPcieCmd_CdmacGetReorderBufStatus(OsDrv* Drv, DtDrvObject Object, int* Load,
                                               int* MinMaxLoad);

DtapiResult DtPcieCmd_CdmacClearReorderBufMinMax(OsDrv* Drv, DtDrvObject Object);

// What a burst FIFO is like, as DT_BURSTFIFO_CMD_GET_PROPERTIES reports.
typedef struct DtBurstFifoProps
{
    uint32_t Caps; // DT_BURSTFIFO_CAP_ flags
    int DataWidth; // Bits per data word
    int FifoSize;  // Bytes
} DtBurstFifoProps;

// A burst FIFO's load and free space, now and at their maximum since the last clear.
typedef struct DtBurstFifoStatus
{
    int CurFree;
    int CurLoad;
    int MaxFree;
    int MaxLoad;
} DtBurstFifoStatus;

DtapiResult DtPcieCmd_BurstFifoGetProps(OsDrv* Drv, DtDrvObject Object,
                                        DtBurstFifoProps* Props);
DtapiResult DtPcieCmd_BurstFifoGetStatus(OsDrv* Drv, DtDrvObject Object,
                                         DtBurstFifoStatus* Status);
DtapiResult DtPcieCmd_BurstFifoClearMax(OsDrv* Drv, DtDrvObject Object, bool ClearMaxFree,
                                        bool ClearMaxLoad);

// Reads the count of overflows and underflows. It stands still while data flows.
DtapiResult DtPcieCmd_BurstFifoGetOvfUflCount(OsDrv* Drv, DtDrvObject Object,
                                              uint32_t* Count);

DtapiResult DtPcieCmd_BurstFifoSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);

// A transmit format event: the frame going out, how far, whether the formatter ran out of
// data since the last wait, and the time the frame started when that was taken.
typedef struct DtSdiTxFEvent
{
    int FrameId;   // The 16 least significant bits of the frame ID in the frame's header
    int SeqNumber; // 0 for the first event of a frame
    bool Underflow;
    bool SofTimeValid;
    uint32_t SofSeconds;
    uint32_t SofNanoseconds;
} DtSdiTxFEvent;

// The formatter takes DT_BLOCK_OPMODE_IDLE and DT_BLOCK_OPMODE_RUN.
DtapiResult DtPcieCmd_SdiTxFSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);

// Asks for a format event every NumLinesPerEvent lines, and a start-of-frame time every
// NumSofsBetweenTod frames.
DtapiResult DtPcieCmd_SdiTxFSetFmtEventSetting(OsDrv* Drv, DtDrvObject Object,
                                               int NumLinesPerEvent,
                                               int NumSofsBetweenTod);

// Reads the bits every part of the buffer's format is padded to.
DtapiResult DtPcieCmd_SdiTxFGetStreamAlignment(OsDrv* Drv, DtDrvObject Object,
                                               int* AlignmentInBits);

// Waits up to TimeoutMs milliseconds, -1 to 1000, for the next format event. Gives
// DTAPI_E_TIMEOUT when none comes, and DTAPI_E_INVALID_MODE at once while the formatter
// is not running.
DtapiResult DtPcieCmd_SdiTxFWaitForFmtEvent(OsDrv* Drv, DtDrvObject Object, int TimeoutMs,
                                            DtSdiTxFEvent* Event);

// Connects input InputIndex to output OutputIndex.
DtapiResult DtPcieCmd_SwitchSetPosition(OsDrv* Drv, DtDrvObject Object, int InputIndex,
                                        int OutputIndex);
DtapiResult DtPcieCmd_SwitchSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);

DtapiResult DtPcieCmd_SdiDmx12GSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);

DtapiResult DtPcieCmd_SdiTxPSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);

// Makes the encoder clamp video symbols, and insert the checksum of each ancillary data
// packet and the CRC of each line.
DtapiResult DtPcieCmd_SdiTxPSetGenerationMode(OsDrv* Drv, DtDrvObject Object, bool Clamp,
                                              bool AdpChecksum, bool LineCrc);

DtapiResult DtPcieCmd_SdiTxPhySetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);

// Reads and clears the flag the PHY sets when its data ran out. It stays set until
// cleared.
DtapiResult DtPcieCmd_SdiTxPhyGetUnderflowFlag(OsDrv* Drv, DtDrvObject Object,
                                               bool* Underflow);
DtapiResult DtPcieCmd_SdiTxPhyClearUnderflowFlag(OsDrv* Drv, DtDrvObject Object);

// Delays the start of each frame by OffsetNs nanoseconds.
DtapiResult DtPcieCmd_SdiTxPhySetStartOfFrameOffset(OsDrv* Drv, DtDrvObject Object,
                                                    int OffsetNs);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ASI blocks -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// An ASI port is driven as an SDI transmit port is: the process drives the objects of the
// port's AF_ASISDIRX or AF_ASISDITX and AF_DMA itself. Receiving uses the driver function
// ASIRX and CDMAC in its receive direction, transmitting the gate ASITXG and the PHY or
// the serialiser ASITXSER. The values these functions take and give are the driver's,
// DT_ASIRX_, DT_ASITXG_ and the operational modes; a value that is none of them gives
// DTAPI_E_INVALID_ARG without a command, and an answer that is none of them gives
// DTAPI_E_DEV_DRIVER.
//

// What ASIRX reports of its input: DT_ASIRX_PCKSIZE_ and DT_ASIRX_POLARITY_ values.
typedef struct DtAsiRxStatus
{
    int PacketSize;
    bool CarrierDetect;
    bool AsiLock;
    int Polarity; // NORMAL, INVERT or UNKNOWN
} DtAsiRxStatus;

// ASIRX's operational mode: DT_FUNC_OPMODE_IDLE or RUN; and its status, which is
// DT_FUNC_OPSTATUS_IDLE or RUN.
DtapiResult DtPcieCmd_AsiRxSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);
DtapiResult DtPcieCmd_AsiRxGetOpStatus(OsDrv* Drv, DtDrvObject Object, int* OpStatus);

// Packetisation, DT_ASIRX_PCKMODE_AUTO or RAW.
DtapiResult DtPcieCmd_AsiRxSetPacketMode(OsDrv* Drv, DtDrvObject Object, int Mode);
DtapiResult DtPcieCmd_AsiRxGetPacketMode(OsDrv* Drv, DtDrvObject Object, int* Mode);

// Polarity control, DT_ASIRX_POLARITY_AUTO, NORMAL or INVERT.
DtapiResult DtPcieCmd_AsiRxSetPolarityCtrl(OsDrv* Drv, DtDrvObject Object, int Polarity);
DtapiResult DtPcieCmd_AsiRxGetPolarityCtrl(OsDrv* Drv, DtDrvObject Object, int* Polarity);

// Packet synchronisation, DT_ASIRX_SYNCMODE_AUTO, 188 or 204.
DtapiResult DtPcieCmd_AsiRxSetSyncMode(OsDrv* Drv, DtDrvObject Object, int Mode);
DtapiResult DtPcieCmd_AsiRxGetSyncMode(OsDrv* Drv, DtDrvObject Object, int* Mode);

DtapiResult DtPcieCmd_AsiRxGetStatus(OsDrv* Drv, DtDrvObject Object,
                                     DtAsiRxStatus* Status);

// The rate of the stream on the wire, in bits a second: of 204-byte packets when those
// come, which the channel layer converts to a rate of 188-byte packets; 0 while no
// packets come.
DtapiResult DtPcieCmd_AsiRxGetTsBitrate(OsDrv* Drv, DtDrvObject Object, int* Bitrate);

// The count of 8b/10b code violations since the receiver started.
DtapiResult DtPcieCmd_AsiRxGetViolCount(OsDrv* Drv, DtDrvObject Object, int* Count);

// ASITXG's operational mode, a DT_BLOCK_OPMODE_ value: STANDBY sends K28.5 only, RUN
// what the buffer holds.
DtapiResult DtPcieCmd_AsiTxGSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);
DtapiResult DtPcieCmd_AsiTxGGetOpMode(OsDrv* Drv, DtDrvObject Object, int* OpMode);

// The polarity of what ASITXG sends, DT_ASITXG_POL_NORMAL or INVERT.
DtapiResult DtPcieCmd_AsiTxGSetPolarity(OsDrv* Drv, DtDrvObject Object, int Polarity);
DtapiResult DtPcieCmd_AsiTxGGetPolarity(OsDrv* Drv, DtDrvObject Object, int* Polarity);

// Forgets the part of a symbol stream the gate had taken in.
DtapiResult DtPcieCmd_AsiTxGClearInputState(OsDrv* Drv, DtDrvObject Object);

// ASITXSER's operational mode, a DT_BLOCK_OPMODE_ value, on a port that has one.
DtapiResult DtPcieCmd_AsiTxSerSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);
DtapiResult DtPcieCmd_AsiTxSerGetOpMode(OsDrv* Drv, DtDrvObject Object, int* OpMode);

// How far the card has written the receive buffer, and how far the process has read it,
// as offsets from its start.
DtapiResult DtPcieCmd_CdmacGetRxWriteOffset(OsDrv* Drv, DtDrvObject Object,
                                            uint32_t* Offset);
DtapiResult DtPcieCmd_CdmacSetRxReadOffset(OsDrv* Drv, DtDrvObject Object,
                                           uint32_t Offset);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Network port -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The NW driver function of an IP port: its Ethernet MAC through the EMAC commands, its
// pipes through the NW commands, both to the function's UUID and port index. A pipe is
// opened by type and named by the UUID the opening gives, which the PIPE commands, and
// closing it, go to with the same port index.
//
// Pipe types are DT_PIPE_ values, capabilities DT_PIPE_CAP_ flags, operational modes
// DT_PIPE_OPMODE_ values, and status and error flags DT_PIPE_STATUS_ and DT_PIPE_ERROR_
// flags.
//

// Reads the port's current MAC address into Mac, which holds 6 bytes.
DtapiResult DtPcieCmd_NwGetMacAddress(OsDrv* Drv, DtDrvObject Object, uint8_t* Mac);

// Reads the PHY speed, a DT_PHY_SPEED_ value; DT_PHY_SPEED_NO_LINK while the link is
// down.
DtapiResult DtPcieCmd_NwGetPhySpeed(OsDrv* Drv, DtDrvObject Object, int* Speed);

// Opens a pipe of Type, or of Fallback when every pipe of Type is in use; -1 for no
// fallback. *Pipe receives the pipe, in the port of the network function Object; its UUID
// is 0 after a failure.
DtapiResult DtPcieCmd_NwOpenPipe(OsDrv* Drv, DtDrvObject Object, int Type,
                                 int TypeFallback, DtDrvObject* Pipe);

// Closes a pipe this handle opened.
DtapiResult DtPcieCmd_NwClosePipe(OsDrv* Drv, DtDrvObject Pipe);

// What a pipe is, as DT_PIPE_CMD_GET_PROPERTIES reports.
typedef struct DtPipeProps
{
    uint32_t Caps;    // DT_PIPE_CAP_ flags
    int PrefetchSize; // In pages; a buffer is a multiple of this many pages
    int DataWidth;    // Bits; a packet is padded to this and a full buffer keeps one free
    int Type;         // A DT_PIPE_ value
} DtPipeProps;

DtapiResult DtPcieCmd_PipeGetProps(OsDrv* Drv, DtDrvObject Pipe, DtPipeProps* Props);

// A pipe's state, as DT_PIPE_CMD_GET_STATUS reports.
typedef struct DtPipeStatus
{
    int OpStatus;         // A DT_BLOCK_OPSTATUS_ value
    uint32_t StatusFlags; // DT_PIPE_STATUS_ flags
    uint32_t ErrorFlags;  // DT_PIPE_ERROR_ flags
} DtPipeStatus;

DtapiResult DtPcieCmd_PipeGetStatus(OsDrv* Drv, DtDrvObject Pipe, DtPipeStatus* Status);

// Gives the pipe Buf, which the process allocated and keeps until the pipe lets go of it.
// The buffer travels as for DtPcieCmd_CdmacAllocateBuffer, and
// DtPcieCmd_PipeSetSharedBufferAs chooses the convention the same way. An empty buffer
// and one larger than an int can count give DTAPI_E_INVALID_ARG.
DtapiResult DtPcieCmd_PipeSetSharedBuffer(OsDrv* Drv, DtDrvObject Pipe,
                                          const OsDmaBuffer* Buf);
DtapiResult DtPcieCmd_PipeSetSharedBufferAs(OsDrv* Drv, DtDrvObject Pipe,
                                            const OsDmaBuffer* Buf, bool BufferIsOutput);

// Lets go of the shared buffer.
DtapiResult DtPcieCmd_PipeReleaseSharedBuffer(OsDrv* Drv, DtDrvObject Pipe);

// Empties the pipe and clears an invalid time.
DtapiResult DtPcieCmd_PipeFlush(OsDrv* Drv, DtDrvObject Pipe);

// Sets a DT_PIPE_OPMODE_ value; any other gives DTAPI_E_INVALID_ARG without a command.
DtapiResult DtPcieCmd_PipeSetOpMode(OsDrv* Drv, DtDrvObject Pipe, int OpMode);

// The offsets of a receive pipe: how far the process has read, and how far the pipe has
// written.
DtapiResult DtPcieCmd_PipeSetRxReadOffset(OsDrv* Drv, DtDrvObject Pipe, uint32_t Offset);
DtapiResult DtPcieCmd_PipeGetRxWriteOffset(OsDrv* Drv, DtDrvObject Pipe,
                                           uint32_t* Offset);

// The offsets of a transmit pipe: how far the process has written, and how far the pipe
// has read.
DtapiResult DtPcieCmd_PipeSetTxWriteOffset(OsDrv* Drv, DtDrvObject Pipe, uint32_t Offset);
DtapiResult DtPcieCmd_PipeGetTxReadOffset(OsDrv* Drv, DtDrvObject Pipe, uint32_t* Offset);

// Which packets a receive pipe takes. The addresses are in network byte order, IPv4 in
// their first 4 bytes; the ports are numbers. Flags are DT_PIPE_IPFLT_FLAG_ values: the
// filter is on with DT_PIPE_IPFLT_FLAG_EN_FILT, and each address, port and VLAN counts
// only with its own flag.
typedef struct DtIpFilter
{
    uint8_t DstIp[16];
    uint16_t DstPort[3];
    uint8_t SrcIp[16];
    uint16_t SrcPort[3];
    int VlanId[2];
    uint32_t Flags;
} DtIpFilter;

DtapiResult DtPcieCmd_PipeSetIpFilter(OsDrv* Drv, DtDrvObject Pipe,
                                      const DtIpFilter* Filter);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- VPD -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The Vital Product Data a card holds in its own EEPROM: a read-only section the factory
// writes, a read-write section, and whatever lies beyond them. The commands go to the
// device rather than to a port. Only reading is here; writing, the items by keyword and
// a public interface are for later.
//

// Where the sections lie in the EEPROM, and how large it is, as GET_PROPERTIES gives it.
typedef struct DtVpdProperties
{
    int RoOffset;
    int RoSize;
    int RwOffset;
    int RwSize;
    int EepromSize;
    int MaxItemLength;
} DtVpdProperties;

// Reads where the sections lie.
DtapiResult DtPcieCmd_VpdGetProperties(OsDrv* Drv, DtVpdProperties* Props);

// Reads Count bytes of the EEPROM from Offset, whatever section they belong to. Fails
// with DTAPI_E_INVALID_ARG for a count that is not positive, and gives in *NumRead, when
// NumRead is not NULL, how many bytes the driver read, which can be fewer.
DtapiResult DtPcieCmd_VpdRawRead(OsDrv* Drv, uint32_t Offset, uint8_t* Buf, int Count,
                                 int* NumRead);
