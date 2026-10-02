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
// The functions of this layer return the DTAPI result codes of cdtapi.h. A result can
// then be passed up to the program unchanged.
//

// Tells whether Result is a success: DTAPI_OK, or a DTAPI_OK_* success with a warning.
#define DT_SUCCEEDED(Result) ((Result) < DTAPI_E)

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Sends commands to the DtPcie driver. There is one function per driver command, and
// each takes and returns plain C types. The driver's structures stay inside this layer,
// so the code above it does not depend on their layout.
//
// Most commands are for one object of the driver: a building block, a driver function,
// or the device's core function. A DtDrvObject names that object.
//

// Names the driver object a command is for: a building block, a driver function, or the
// device's core function. The driver gives each object a UUID; the command header holds
// that UUID together with the object's port index.
typedef struct DtDrvObject
{
    int Uuid;      // The UUID the driver gave the object
    int PortIndex; // The object's port, from 0; DT_PROPERTY_DEVICE for the device itself
} DtDrvObject;

// The version of the DtPcie driver.
typedef struct DtDriverVersion
{
    int Major; // The major version number
    int Minor; // The minor version number
    int Micro; // The micro version number
    int Build; // The build number
} DtDriverVersion;

// What the driver reports about a device: its identity, its firmware, and its PCIe link.
typedef struct DtDeviceInfo
{
    int TypeNumber;       // The type number: 2178 for a DTA-2178
    int SubType;          // The sub-type: 0 for none, 1 for A, and so on
    int64_t Serial;       // The serial number
    int HardwareRevision; // The hardware revision
    int FirmwareVersion;  // The firmware version
    int FirmwareVariant;  // The firmware variant
    int FirmwareStatus;   // One of the DT_FWSTATUS_* values
    uint16_t VendorId;    // The PCI vendor ID
    uint16_t DeviceId;    // The PCI device ID
    uint16_t SubVendorId; // The PCI subsystem vendor ID
    uint16_t SubSystemId; // The PCI subsystem ID

    // When the firmware was built.
    int FwBuildYear;   // The year, such as 2026
    int FwBuildMonth;  // The month, 1 to 12
    int FwBuildDay;    // The day of the month, 1 to 31
    int FwBuildHour;   // The hour, 0 to 23
    int FwBuildMinute; // The minute, 0 to 59

    // Where the device is, and how its PCIe link runs.
    int BusNumber;              // The PCI bus number
    int SlotNumber;             // The PCI slot number
    int PcieNumLanes;           // The number of lanes the link uses
    int PcieMaxLanes;           // The number of lanes the device can use
    int PcieLinkSpeed;          // The PCIe generation the link runs at
    int PcieMaxSpeed;           // The highest PCIe generation the link can reach
    int PcieMaxPayloadSize;     // The maximum payload size, in bytes
    int PcieMaxReadRequestSize; // The maximum read request size, in bytes
    int PcieMaxSlotPower;       // In milliwatts; 0 when the driver has no GET_DEV_INFO2
} DtDeviceInfo;

// Reads the version of the driver behind Drv into *Version.
DtapiResult DtPcieCmd_GetDriverVersion(OsDrv* Drv, DtDriverVersion* Version);

// Tells whether CDTAPI supports a driver of version *Version: 1.3.1 or later. The build
// number is ignored.
bool DtPcieCmd_VersionIsSupported(const DtDriverVersion* Version);

// Tells whether *Version is Major.Minor.Micro.Build or later. The four numbers are
// compared in that order.
bool DtPcieCmd_VersionAtLeast(const DtDriverVersion* Version, int Major, int Minor,
                              int Micro, int Build);

// Reads the identity of the device behind Drv into *Info. Uses the command
// GET_DEV_INFO2. An older driver does not have that command; then this uses
// GET_DEV_INFO, and PcieMaxSlotPower is 0.
DtapiResult DtPcieCmd_GetDeviceInfo(OsDrv* Drv, DtDeviceInfo* Info);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Properties -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the properties of a device. A property is a named value the driver keeps for the
// device, such as PORT_COUNT, or for one of its ports, such as the capability 3GSDI. The
// driver gives the value for the device's own hardware revision and firmware.
//
// Each function clears its output first. They have these results in common:
//
// | Result                | When                                           |
// |-----------------------|------------------------------------------------|
// | DTAPI_OK              | The property was read                          |
// | DTAPI_E_INVALID_ARG   | Drv, Name or the output is NULL                |
// | DTAPI_E_BUF_TOO_SMALL | Name is longer than the driver accepts         |
// | DTAPI_E_NOT_FOUND     | The device or port does not have the property  |
//

// The port index of a property of the device as a whole, rather than of one port.
#define DT_PROPERTY_DEVICE -1

// Reads integer property Name of the port PortIndex, counted from 0, or of the device
// with DT_PROPERTY_DEVICE. Stores the value in *Value.
DtapiResult DtPcieCmd_GetPropertyInt(OsDrv* Drv, const char* Name, int PortIndex,
                                     int* Value);

// Reads boolean property Name of the port PortIndex, counted from 0, or of the device
// with DT_PROPERTY_DEVICE. Stores the value in *Value.
DtapiResult DtPcieCmd_GetPropertyBool(OsDrv* Drv, const char* Name, int PortIndex,
                                      bool* Value);

// The size of a buffer that holds any string property, terminator included. The driver
// gives at most 96 characters.
#define DT_PROPERTY_STR_SIZE 97

// Reads string property Name of the port PortIndex, counted from 0, or of the device with
// DT_PROPERTY_DEVICE. Stores it in Str, a buffer of Size bytes. Returns
// DTAPI_E_BUF_TOO_SMALL when the string does not fit; Str is then empty.
DtapiResult DtPcieCmd_GetPropertyStr(OsDrv* Drv, const char* Name, int PortIndex,
                                     char* Str, size_t Size);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- I/O configuration -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads and sets the I/O configuration of ports. The functions take the DtIoConfig of
// cdtapi.h: a port number from 1, and a group, value and sub-value as DTAPI_IOCONFIG_*
// codes, -1 for none. They convert it to what the driver takes: a port index from 0, and
// the codes as names. Some I/O direction values name another port in ParXtra[0]; that
// port is converted to an index too.
//
// The driver takes a list of configurations in one command. The functions for a single
// configuration send a list of one.
//

// Reads the configuration of group Configs[i].Group on port Configs[i].Port, for each of
// the Count entries, and fills in the other fields. Count must be at least 1. When this
// fails, Configs is unchanged.
DtapiResult DtPcieCmd_GetIoConfigList(OsDrv* Drv, DtIoConfig* Configs, int Count);

// Applies the Count configurations of Configs in one command; Count must be at least 1.
// The driver checks them. Before sending anything, this returns DTAPI_E_INVALID_ISI for a
// LOOPS2TS output whose ISI, ParXtra[1], is not between 0 and 255.
DtapiResult DtPcieCmd_SetIoConfigList(OsDrv* Drv, const DtIoConfig* Configs, int Count);

// Reads one configuration, as DtPcieCmd_GetIoConfigList() does.
DtapiResult DtPcieCmd_GetIoConfig(OsDrv* Drv, DtIoConfig* Config);

// Applies one configuration, as DtPcieCmd_SetIoConfigList() does.
DtapiResult DtPcieCmd_SetIoConfig(OsDrv* Drv, const DtIoConfig* Config);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Time of day -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Reads the device's time-of-day clock, in seconds and nanoseconds.
DtapiResult DtPcieCmd_GetTimeOfDay(OsDrv* Drv, uint32_t* Seconds, uint32_t* Nanoseconds);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Clocks -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Controls the clocks of a device: the genlock controller, the time-of-day clock control
// and the transmit clock counters. Each is an object of the device, not of a port; Object
// is the one the device layer found. The functions convert the driver's answers into
// DTAPI's values.
//

// One transmit clock of the genlock controller.
typedef struct DtClockProps
{
    int ClockIndex;           // The index the frequency offset functions take
    int ClockType;            // DTAPI_TXCLK_FRACTIONAL or DTAPI_TXCLK_NON_FRACTIONAL
    int StepSizePpt;          // The step size of the offset, in parts per trillion
    int RangePpt;             // The largest offset either way, in parts per trillion
    int64_t FrequencyMicroHz; // The centre frequency, in microhertz
} DtClockProps;

// Reads the clock counter Object (DT_CLKCNT_CMD_GET_TICK_COUNT). *Count is its 32-bit
// count, which wraps; *FrequencyHz is the frequency it counts at. Both are 0 after a
// failure.
DtapiResult DtPcieCmd_ClkCntGetTickCount(OsDrv* Drv, DtDrvObject Object, uint32_t* Count,
                                         int* FrequencyHz);

// Reads the transmit clocks of the genlock controller Object
// (DT_GENLOCKCTRL_CMD_GET_DCO_CLK_PROPS) into Props, an array of MaxProps entries.
// Stores the number of clocks in *NumProps.
//
// | Result                | When                                                |
// |-----------------------|-----------------------------------------------------|
// | DTAPI_OK              | The clocks are in Props                             |
// | DTAPI_E_BUF_TOO_SMALL | There are more than MaxProps clocks; Props is empty |
// | DTAPI_E_DEV_DRIVER    | The driver gave a clock type it does not define     |
//
// The driver itself reports success when there are more clocks than MaxProps, and leaves
// entries unfilled; this function turns that into DTAPI_E_BUF_TOO_SMALL.
DtapiResult DtPcieCmd_GenlockGetClockProps(OsDrv* Drv, DtDrvObject Object,
                                           DtClockProps* Props, int MaxProps,
                                           int* NumProps);

// Reads the frequency offset of clock ClockIndex of the genlock controller Object
// (DT_GENLOCKCTRL_CMD_GET_DCO_FREQ_OFFSET). *OffsetPpt is the offset in parts per
// trillion; *FrequencyMicroHz is the frequency with the offset applied. Both are 0
// after a failure.
DtapiResult DtPcieCmd_GenlockGetFreqOffset(OsDrv* Drv, DtDrvObject Object, int ClockIndex,
                                           int* OffsetPpt, int64_t* FrequencyMicroHz);

// Reads the state of the genlock controller Object (DT_GENLOCKCTRL_CMD_GET_STATE2) into
// *State, converted as DTAPI does it. The driver's free run becomes DTAPI_GENL_LOCKED,
// as in the driver's own genlock event, and a state the driver does not define becomes
// DTAPI_GENL_NO_REF. The video standards become DTAPI_VIDSTD_* codes. *State is all
// zeroes after a failure.
DtapiResult DtPcieCmd_GenlockGetState(OsDrv* Drv, DtDrvObject Object,
                                      DtGenlockState* State);

// Sets the frequency offset of clock ClockIndex of the genlock controller Object
// (DT_GENLOCKCTRL_CMD_SET_DCO_FREQ_OFFSET), in parts per trillion.
//
// | Result              | When                                                  |
// |---------------------|-------------------------------------------------------|
// | DTAPI_OK            | The offset is set                                     |
// | DTAPI_E_IN_USE      | The device is genlocked                               |
// | DTAPI_E_INVALID_ARG | The offset is out of the clock's range, or there is   |
// |                     | no clock ClockIndex                                   |
DtapiResult DtPcieCmd_GenlockSetFreqOffset(OsDrv* Drv, DtDrvObject Object, int ClockIndex,
                                           int OffsetPpt);

// Reads the state of the time-of-day clock control Object (DT_TODCLOCKCTRL_CMD_GET_STATE)
// into *State, converted as DTAPI does it. A state the driver does not define becomes
// DTAPI_TODCLK_FREE_RUN, and an unknown reference DTAPI_TODREF_INTERNAL. *State is all
// zeroes after a failure.
DtapiResult DtPcieCmd_TodClkCtrlGetState(OsDrv* Drv, DtDrvObject Object,
                                         DtTimeOfDayState* State);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SDI receiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the status of an SDI receiver. A command for a driver function goes to the
// function's UUID and to the index of its port. The device layer reads the UUID from
// the function's properties.
//

// The status of an SDI receiver's input, converted from the driver's fields.
typedef struct DtSdiRxStatus
{
    bool CarrierDetect; // True when there is a signal at the input
    bool SdiLock;       // True when the receiver is locked to the SDI stream
    bool LineLock;      // True when the receiver is locked to the lines
    bool Valid;         // True when the fields below describe the input
    int NumSymsHanc;    // The symbols per line in HANC, including EAV and SAV
    int NumSymsVidVanc; // The symbols per line in the active part
    int NumLinesF1;     // The number of lines in field 1
    int NumLinesF2;     // The number of lines in field 2
    bool IsLevelB;      // True for 3G level B
    uint32_t PayloadId; // The SMPTE 352 VPID; 0 for none
    double FrameRate;   // Frames per second; 0 when the driver reports no frame period
    int SdiRate; // A DT_DRV_SDIRATE_* value; UNKNOWN for one the driver does not define
} DtSdiRxStatus;

// Reads the status of the SDI receiver Object into *Status. Clears *Status first.
DtapiResult DtPcieCmd_SdiRxGetStatus(OsDrv* Drv, DtDrvObject Object,
                                     DtSdiRxStatus* Status);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SDI receive channel -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Controls the CHSDIRX driver function, an SDI receive channel. The channel writes the
// SDI lines of a port into a DMA ring, which the driver allocates, and reports how far
// it has written. The process maps the ring into its memory, reads from it, and tells
// the driver how far it has read. Every command goes to the channel's UUID and port
// index.
//

// The properties of the channel's DMA and formatter (DT_CHSDIRX_CMD_GET_PROPS).
typedef struct DtChSdiRxProps
{
    uint32_t DmaCaps;    // DT_CDMAC_CAP_* flags
    int PrefetchSize;    // In pages; the ring's size is a multiple of this
    int PcieDataWidth;   // The bits per PCIe data word
    int ReorderBufSize;  // The size of the reorder buffer, in bytes
    int StreamAlignment; // In bits; each part of the ring's format is padded to this
} DtChSdiRxProps;

// The configuration of the channel (DT_CHSDIRX_CMD_CONFIGURE).
typedef struct DtChSdiRxConfig
{
    int NumPorts;           // 1, or 4 for quad link
    int PortIndices[4];     // The ports, from 0, in link order
    int DmaMinSize;         // The smallest ring, in bytes; the driver may make it larger
    int FmtIntInterval;     // The time between format events, in microseconds
    int FmtIntDelay;        // The time from a frame's start to its first event, in us
    int FmtNumIntsPerFrame; // The number of format events per frame
    int NumSymsHanc;        // The symbols per line in HANC, including EAV and SAV
    int NumSymsVidVanc;     // The symbols per line in the active part
    int NumLines;           // The lines per frame
    int SdiRate;            // A DT_DRV_SDIRATE_* value
    bool AssumeInterlaced;  // True to treat the input as interlaced
    bool Scale12GTo3G;      // True to scale a 12G input down to 3G
} DtChSdiRxConfig;

// A format event of the channel: which frame the formatter is in, how far it is, and
// whether it is in sync.
typedef struct DtChSdiRxEvent
{
    int FrameId;   // The 16 least significant bits of the frame number
    int SeqNumber; // The event's number within the frame; 0 for the first
    bool InSync;   // True when the formatter is in sync with the input
} DtChSdiRxEvent;

// Attaches to the channel, exclusively when Exclusive is true. FriendlyName names the
// user and has at most DT_CHAN_FRIENDLY_NAME_MAX_LENGTH characters. Returns
// DTAPI_E_INVALID_ARG, without sending a command, for an empty or longer name.
DtapiResult DtPcieCmd_ChSdiRxAttach(OsDrv* Drv, DtDrvObject Object, bool Exclusive,
                                    const char* FriendlyName);

// Detaches from the channel.
DtapiResult DtPcieCmd_ChSdiRxDetach(OsDrv* Drv, DtDrvObject Object);

// Configures the channel, which must be idle. Without sending a command, returns
// DTAPI_E_INVALID_ARG when NumPorts is not 1 to 4, and DTAPI_E_INVALID_RATE when SdiRate
// is not a DT_DRV_SDIRATE_* value.
DtapiResult DtPcieCmd_ChSdiRxConfigure(OsDrv* Drv, DtDrvObject Object,
                                       const DtChSdiRxConfig* Config);

// Reads this user's operational mode of the channel, a DT_FUNC_OPMODE_* value.
DtapiResult DtPcieCmd_ChSdiRxGetOpMode(OsDrv* Drv, DtDrvObject Object, int* OpMode);

// Sets this user's operational mode of the channel, a DT_FUNC_OPMODE_* value.
DtapiResult DtPcieCmd_ChSdiRxSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);

// Waits up to TimeoutMs milliseconds for the channel's next format event, and stores it
// in *Event. Returns DTAPI_E_TIMEOUT when none comes.
DtapiResult DtPcieCmd_ChSdiRxWaitForFmtEvent(OsDrv* Drv, DtDrvObject Object,
                                             int TimeoutMs, DtChSdiRxEvent* Event);

// Reads how far the channel has written into the ring, as an offset from its start.
DtapiResult DtPcieCmd_ChSdiRxGetWriteOffset(OsDrv* Drv, DtDrvObject Object,
                                            uint32_t* Offset);

// Tells the channel how far this user has read the ring, as an offset from its start.
DtapiResult DtPcieCmd_ChSdiRxSetReadOffset(OsDrv* Drv, DtDrvObject Object,
                                           uint32_t Offset);

// Reads the channel's properties into *Props.
DtapiResult DtPcieCmd_ChSdiRxGetProps(OsDrv* Drv, DtDrvObject Object,
                                      DtChSdiRxProps* Props);

// Reads the status of the channel's input into *Status. The driver answers with the
// status of the channel's SDI receiver, as DtPcieCmd_SdiRxGetStatus() gives it.
DtapiResult DtPcieCmd_ChSdiRxGetSdiStatus(OsDrv* Drv, DtDrvObject Object,
                                          DtSdiRxStatus* Status);

// Maps the channel's ring, once configured, into the process. *Buffer receives its
// address, *BufSize its size in bytes, and *MaxLoad the most bytes it may hold.
//
// On Windows, the driver maps the ring itself and gives its address. On Linux, the
// driver gives address 0, and this function maps the ring from the device, at offset
// DT_MMAP_PORT_MEM_SEGMENT_SIZE times (port index + 1). *MappedByCdtapi tells which
// happened: when it is true, DtPcieCmd_ChSdiRxUnmapDmaBuf() must release the mapping.
DtapiResult DtPcieCmd_ChSdiRxMapDmaBuf(OsDrv* Drv, DtDrvObject Object, uint8_t** Buffer,
                                       int* BufSize, int* MaxLoad, bool* MappedByCdtapi);

// Releases a mapping that DtPcieCmd_ChSdiRxMapDmaBuf() made itself, as MappedByCdtapi
// says. A mapping the driver made is released when the user detaches.
void DtPcieCmd_ChSdiRxUnmapDmaBuf(OsDrv* Drv, uint8_t* Buffer, int BufSize,
                                  bool MappedByCdtapi);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Exclusive access -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Claims and releases driver objects. Only one file handle at a time can hold a driver
// function or building block. The driver checks the holder on each command that changes
// the object. Closing a handle releases everything it holds.
//

// Sends exclusive access command Cmd, a DT_EXCLUSIVE_ACCESS_CMD_* value, for Object.
// What each command returns:
//
// - Acquire: DTAPI_E_IN_USE when any handle holds Object, this one included.
// - Check: DTAPI_OK when this handle holds Object, DTAPI_E_IN_USE when another handle
//   holds it, and DTAPI_E_EXCL_ACCESS_REQD when no handle holds it.
// - Probe: DTAPI_E_IN_USE when any handle holds Object.
// - Release: DTAPI_E_IN_USE when another handle holds Object.
DtapiResult DtPcieCmd_ExclAccess(OsDrv* Drv, DtDrvObject Object, int Cmd);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SDI transmit blocks -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Controls the building blocks a port transmits SDI with. There is no channel driver
// function for transmitting, so the process controls the objects of the port's AF_DMA
// and AF_ASISDITX API functions itself:
//
// - CDMAC, the DMA controller, which reads a buffer the process allocates;
// - BURSTFIFO, the FIFO after it;
// - SDITXF, the formatter;
// - the switches around SDIDMX12G, the 12G demultiplexer;
// - SDITXP, the protocol encoder;
// - SDITXPHY, the driver function of the PHY.
//
// Every command goes to the object's UUID and port index. An operational mode is a
// DT_BLOCK_OPMODE_* value, or a DT_FUNC_OPMODE_* value for SDITXPHY. For any other
// value, the functions return DTAPI_E_INVALID_ARG without sending a command.
//

// The properties of a DMA controller (DT_CDMAC_CMD_GET_PROPERTIES).
typedef struct DtCdmacProps
{
    uint32_t Caps;      // DT_CDMAC_CAP_* flags
    int PrefetchSize;   // In pages; a buffer's size is a multiple of this
    int PcieDataWidth;  // The bits per PCIe data word
    int ReorderBufSize; // The size of the reorder buffer, in bytes
} DtCdmacProps;

// Reads the properties of the DMA controller Object into *Props.
DtapiResult DtPcieCmd_CdmacGetProps(OsDrv* Drv, DtDrvObject Object, DtCdmacProps* Props);

// Registers Buf with the DMA controller Object, for Direction DT_CDMAC_DIR_TX or
// DT_CDMAC_DIR_RX. The process allocated Buf, and must keep it until the buffer is
// freed. Returns DTAPI_E_INVALID_ARG for another direction, and for a buffer that is
// empty or larger than INT_MAX bytes.
//
// The Windows and Linux drivers take the buffer in different ways; see
// OsDmaBuffer_DescribeHandOff(). DtPcieCmd_CdmacAllocateBuffer() uses the way of the
// platform it runs on. DtPcieCmd_CdmacAllocateBufferAs() takes the way as an argument,
// true for Windows's, so that tests can try both ways on every platform.
//
// When the buffer is passed as the output, as on Windows, only the fixed output structure
// is checked against the size of the answer, not the buffer. Whether the driver reports
// the whole buffer has not been checked on a card.
DtapiResult DtPcieCmd_CdmacAllocateBuffer(OsDrv* Drv, DtDrvObject Object, int Direction,
                                          const OsDmaBuffer* Buf);
DtapiResult DtPcieCmd_CdmacAllocateBufferAs(OsDrv* Drv, DtDrvObject Object, int Direction,
                                            const OsDmaBuffer* Buf, bool BufferIsOutput);

// Unregisters the buffer of the DMA controller Object, which must be idle.
DtapiResult DtPcieCmd_CdmacFreeBuffer(OsDrv* Drv, DtDrvObject Object);

// Empties the pipeline of the DMA controller Object, which must be idle.
DtapiResult DtPcieCmd_CdmacFlushChannel(OsDrv* Drv, DtDrvObject Object);

// Sets the operational mode of the DMA controller Object.
DtapiResult DtPcieCmd_CdmacSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);

// Sets the test mode of the DMA controller Object, a DT_CDMAC_TESTMODE_* value. The
// controller must be idle.
DtapiResult DtPcieCmd_CdmacSetTestMode(OsDrv* Drv, DtDrvObject Object, int TestMode);

// Reads how far the card has read the transmit buffer, as an offset from its start.
DtapiResult DtPcieCmd_CdmacGetTxReadOffset(OsDrv* Drv, DtDrvObject Object,
                                           uint32_t* Offset);

// Tells the card how far the transmit buffer holds data for it, as an offset from its
// start.
DtapiResult DtPcieCmd_CdmacSetTxWriteOffset(OsDrv* Drv, DtDrvObject Object,
                                            uint32_t Offset);

// Reads the load of the reorder buffer into *Load, and its minimum or maximum since the
// last clear into *MinMaxLoad.
DtapiResult DtPcieCmd_CdmacGetReorderBufStatus(OsDrv* Drv, DtDrvObject Object, int* Load,
                                               int* MinMaxLoad);

// Clears the minimum or maximum load of the reorder buffer.
DtapiResult DtPcieCmd_CdmacClearReorderBufMinMax(OsDrv* Drv, DtDrvObject Object);

// The properties of a burst FIFO (DT_BURSTFIFO_CMD_GET_PROPERTIES).
typedef struct DtBurstFifoProps
{
    uint32_t Caps; // DT_BURSTFIFO_CAP_* flags
    int DataWidth; // The bits per data word
    int FifoSize;  // The size of the FIFO, in bytes
} DtBurstFifoProps;

// The load and free space of a burst FIFO, in bytes.
typedef struct DtBurstFifoStatus
{
    int CurFree; // The free space now
    int CurLoad; // The load now
    int MaxFree; // The most free space since the last clear
    int MaxLoad; // The highest load since the last clear
} DtBurstFifoStatus;

// Reads the properties of the burst FIFO Object into *Props.
DtapiResult DtPcieCmd_BurstFifoGetProps(OsDrv* Drv, DtDrvObject Object,
                                        DtBurstFifoProps* Props);

// Reads the load and free space of the burst FIFO Object into *Status.
DtapiResult DtPcieCmd_BurstFifoGetStatus(OsDrv* Drv, DtDrvObject Object,
                                         DtBurstFifoStatus* Status);

// Clears the most free space when ClearMaxFree is true, and the highest load when
// ClearMaxLoad is true.
DtapiResult DtPcieCmd_BurstFifoClearMax(OsDrv* Drv, DtDrvObject Object, bool ClearMaxFree,
                                        bool ClearMaxLoad);

// Reads the number of overflows and underflows of the burst FIFO Object. The count does
// not change while data flows without interruption, so a change since the last reading
// means one occurred.
DtapiResult DtPcieCmd_BurstFifoGetOvfUflCount(OsDrv* Drv, DtDrvObject Object,
                                              uint32_t* Count);

// Sets the operational mode of the burst FIFO Object.
DtapiResult DtPcieCmd_BurstFifoSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);

// A format event of the transmit formatter: which frame goes out, how far it is, whether
// the formatter ran out of data since the last wait, and when the frame started.
typedef struct DtSdiTxFEvent
{
    int FrameId;    // The 16 least significant bits of the frame ID in the frame's header
    int SeqNumber;  // The event's number within the frame; 0 for the first
    bool Underflow; // True when the formatter ran out of data since the last wait
    bool SofTimeValid;       // True when SofSeconds and SofNanoseconds hold a time
    uint32_t SofSeconds;     // The time the frame started: seconds
    uint32_t SofNanoseconds; // The time the frame started: nanoseconds
} DtSdiTxFEvent;

// Sets the operational mode of the formatter Object. The formatter takes
// DT_BLOCK_OPMODE_IDLE and DT_BLOCK_OPMODE_RUN. DT_BLOCK_OPMODE_STANDBY is sent, and the
// driver refuses it.
DtapiResult DtPcieCmd_SdiTxFSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);

// Makes the formatter Object give a format event every NumLinesPerEvent lines, and the
// start time of a frame every NumSofsBetweenTod frames.
DtapiResult DtPcieCmd_SdiTxFSetFmtEventSetting(OsDrv* Drv, DtDrvObject Object,
                                               int NumLinesPerEvent,
                                               int NumSofsBetweenTod);

// Reads the alignment, in bits, that each part of the buffer's format is padded to.
DtapiResult DtPcieCmd_SdiTxFGetStreamAlignment(OsDrv* Drv, DtDrvObject Object,
                                               int* AlignmentInBits);

// Waits up to TimeoutMs milliseconds for the formatter's next format event, and stores it
// in *Event. The driver takes -1, for no limit, to 1000, and refuses any other value.
//
// | Result               | When                                    |
// |----------------------|-----------------------------------------|
// | DTAPI_OK             | An event came                           |
// | DTAPI_E_TIMEOUT      | No event came within TimeoutMs          |
// | DTAPI_E_INVALID_MODE | The formatter is not running; at once   |
DtapiResult DtPcieCmd_SdiTxFWaitForFmtEvent(OsDrv* Drv, DtDrvObject Object, int TimeoutMs,
                                            DtSdiTxFEvent* Event);

// Connects input InputIndex of the switch Object to its output OutputIndex.
DtapiResult DtPcieCmd_SwitchSetPosition(OsDrv* Drv, DtDrvObject Object, int InputIndex,
                                        int OutputIndex);

// Sets the operational mode of the switch Object.
DtapiResult DtPcieCmd_SwitchSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);

// Sets the operational mode of the 12G demultiplexer Object.
DtapiResult DtPcieCmd_SdiDmx12GSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);

// Sets the operational mode of the protocol encoder Object.
DtapiResult DtPcieCmd_SdiTxPSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);

// Sets what the protocol encoder Object does to the stream: clamp the video symbols when
// Clamp is true, insert the checksum of each ancillary data packet when AdpChecksum is
// true, and insert the CRC of each line when LineCrc is true.
DtapiResult DtPcieCmd_SdiTxPSetGenerationMode(OsDrv* Drv, DtDrvObject Object, bool Clamp,
                                              bool AdpChecksum, bool LineCrc);

// Sets the operational mode of the PHY Object, a DT_FUNC_OPMODE_* value.
DtapiResult DtPcieCmd_SdiTxPhySetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);

// Reads the PHY's underflow flag, which the PHY sets when it runs out of data. Reading
// does not clear the flag; DtPcieCmd_SdiTxPhyClearUnderflowFlag() does.
DtapiResult DtPcieCmd_SdiTxPhyGetUnderflowFlag(OsDrv* Drv, DtDrvObject Object,
                                               bool* Underflow);

// Clears the PHY's underflow flag.
DtapiResult DtPcieCmd_SdiTxPhyClearUnderflowFlag(OsDrv* Drv, DtDrvObject Object);

// Delays the start of each frame the PHY sends by OffsetNs nanoseconds.
DtapiResult DtPcieCmd_SdiTxPhySetStartOfFrameOffset(OsDrv* Drv, DtDrvObject Object,
                                                    int OffsetNs);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ASI blocks -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Controls the building blocks of an ASI port. As for SDI transmit, the process controls
// the objects of the port's API functions itself: AF_ASISDIRX or AF_ASISDITX, and AF_DMA.
// Receiving uses the driver function ASIRX and a CDMAC in its receive direction.
// Transmitting uses the gate ASITXG, and the PHY or the serialiser ASITXSER.
//
// The functions take and give the driver's own values: DT_ASIRX_*, DT_ASITXG_* and the
// operational modes. They return DTAPI_E_INVALID_ARG, without sending a command, for a
// value that is none of these, and DTAPI_E_DEV_DRIVER when the driver answers with one.
//

// The status of the input of ASIRX.
typedef struct DtAsiRxStatus
{
    int PacketSize;     // A DT_ASIRX_PCKSIZE_* value
    bool CarrierDetect; // True when there is a signal at the input
    bool AsiLock;       // True when the receiver is locked to the ASI stream
    int Polarity;       // DT_ASIRX_POLARITY_NORMAL, INVERT or UNKNOWN
} DtAsiRxStatus;

// Sets the operational mode of ASIRX: DT_FUNC_OPMODE_IDLE or RUN.
DtapiResult DtPcieCmd_AsiRxSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);

// Reads the operational status of ASIRX: DT_FUNC_OPSTATUS_IDLE or RUN.
DtapiResult DtPcieCmd_AsiRxGetOpStatus(OsDrv* Drv, DtDrvObject Object, int* OpStatus);

// Sets how ASIRX divides the stream into packets: DT_ASIRX_PCKMODE_AUTO or RAW.
DtapiResult DtPcieCmd_AsiRxSetPacketMode(OsDrv* Drv, DtDrvObject Object, int Mode);

// Reads how ASIRX divides the stream into packets: DT_ASIRX_PCKMODE_AUTO or RAW.
DtapiResult DtPcieCmd_AsiRxGetPacketMode(OsDrv* Drv, DtDrvObject Object, int* Mode);

// Sets how ASIRX handles the polarity of the input: DT_ASIRX_POLARITY_AUTO, NORMAL or
// INVERT.
DtapiResult DtPcieCmd_AsiRxSetPolarityCtrl(OsDrv* Drv, DtDrvObject Object, int Polarity);

// Reads how ASIRX handles the polarity of the input: DT_ASIRX_POLARITY_AUTO, NORMAL or
// INVERT.
DtapiResult DtPcieCmd_AsiRxGetPolarityCtrl(OsDrv* Drv, DtDrvObject Object, int* Polarity);

// Sets how ASIRX finds the packets: DT_ASIRX_SYNCMODE_AUTO, 188 or 204.
DtapiResult DtPcieCmd_AsiRxSetSyncMode(OsDrv* Drv, DtDrvObject Object, int Mode);

// Reads how ASIRX finds the packets: DT_ASIRX_SYNCMODE_AUTO, 188 or 204.
DtapiResult DtPcieCmd_AsiRxGetSyncMode(OsDrv* Drv, DtDrvObject Object, int* Mode);

// Reads the status of the input of ASIRX into *Status.
DtapiResult DtPcieCmd_AsiRxGetStatus(OsDrv* Drv, DtDrvObject Object,
                                     DtAsiRxStatus* Status);

// Reads the bitrate of the stream on the wire, in bits per second. With 204-byte packets,
// it is the rate of those packets. It is 0 while no packets come.
DtapiResult DtPcieCmd_AsiRxGetTsBitrate(OsDrv* Drv, DtDrvObject Object, int* Bitrate);

// Reads the number of 8b/10b code violations since the receiver started.
DtapiResult DtPcieCmd_AsiRxGetViolCount(OsDrv* Drv, DtDrvObject Object, int* Count);

// Sets the operational mode of ASITXG, a DT_BLOCK_OPMODE_* value. In STANDBY, the gate
// sends only K28.5 symbols; in RUN, it sends what the buffer holds.
DtapiResult DtPcieCmd_AsiTxGSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);

// Reads the operational mode of ASITXG, a DT_BLOCK_OPMODE_* value.
DtapiResult DtPcieCmd_AsiTxGGetOpMode(OsDrv* Drv, DtDrvObject Object, int* OpMode);

// Sets the polarity of what ASITXG sends: DT_ASITXG_POL_NORMAL or INVERT.
DtapiResult DtPcieCmd_AsiTxGSetPolarity(OsDrv* Drv, DtDrvObject Object, int Polarity);

// Reads the polarity of what ASITXG sends: DT_ASITXG_POL_NORMAL or INVERT.
DtapiResult DtPcieCmd_AsiTxGGetPolarity(OsDrv* Drv, DtDrvObject Object, int* Polarity);

// Makes ASITXG discard the part of a symbol stream it has taken in.
DtapiResult DtPcieCmd_AsiTxGClearInputState(OsDrv* Drv, DtDrvObject Object);

// Sets the operational mode of ASITXSER, a DT_BLOCK_OPMODE_* value, on a port that has
// one.
DtapiResult DtPcieCmd_AsiTxSerSetOpMode(OsDrv* Drv, DtDrvObject Object, int OpMode);

// Reads the operational mode of ASITXSER, a DT_BLOCK_OPMODE_* value.
DtapiResult DtPcieCmd_AsiTxSerGetOpMode(OsDrv* Drv, DtDrvObject Object, int* OpMode);

// Reads how far the card has written into the receive buffer, as an offset from its
// start.
DtapiResult DtPcieCmd_CdmacGetRxWriteOffset(OsDrv* Drv, DtDrvObject Object,
                                            uint32_t* Offset);

// Tells the card how far the process has read the receive buffer, as an offset from its
// start.
DtapiResult DtPcieCmd_CdmacSetRxReadOffset(OsDrv* Drv, DtDrvObject Object,
                                           uint32_t Offset);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Network port -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Controls the NW driver function of an IP port. The EMAC commands control its Ethernet
// MAC; the NW commands open and close its pipes. Both go to the function's UUID and port
// index.
//
// A pipe carries packets between the port and a buffer the process shares with the
// card. Opening a pipe of a given type gives the pipe's UUID. The PIPE commands, and
// closing the pipe, go to that UUID with the same port index.
//
// Pipe types are DT_PIPE_* values, capabilities DT_PIPE_CAP_* flags, operational modes
// DT_PIPE_OPMODE_* values, and the status and error flags DT_PIPE_STATUS_* and
// DT_PIPE_ERROR_* flags.
//

// Reads the port's current MAC address into Mac, a buffer of 6 bytes.
DtapiResult DtPcieCmd_NwGetMacAddress(OsDrv* Drv, DtDrvObject Object, uint8_t* Mac);

// Reads the speed of the PHY, a DT_PHY_SPEED_* value. It is DT_PHY_SPEED_NO_LINK while
// the link is down.
DtapiResult DtPcieCmd_NwGetPhySpeed(OsDrv* Drv, DtDrvObject Object, int* Speed);

// Opens a pipe of Type on the port of the network function Object. When every pipe of
// Type is in use, opens one of TypeFallback instead; pass -1 for no fallback. *Pipe
// receives the pipe; its UUID is 0 after a failure.
DtapiResult DtPcieCmd_NwOpenPipe(OsDrv* Drv, DtDrvObject Object, int Type,
                                 int TypeFallback, DtDrvObject* Pipe);

// Closes a pipe this handle opened.
DtapiResult DtPcieCmd_NwClosePipe(OsDrv* Drv, DtDrvObject Pipe);

// The properties of a pipe (DT_PIPE_CMD_GET_PROPERTIES).
typedef struct DtPipeProps
{
    uint32_t Caps;    // DT_PIPE_CAP_* flags
    int PrefetchSize; // In pages; a buffer's size is a multiple of this
    int DataWidth;    // In bits; each packet is padded to this, and a full buffer keeps
                      // one data word free
    int Type;         // A DT_PIPE_* value
} DtPipeProps;

// Reads the properties of Pipe into *Props.
DtapiResult DtPcieCmd_PipeGetProps(OsDrv* Drv, DtDrvObject Pipe, DtPipeProps* Props);

// The state of a pipe (DT_PIPE_CMD_GET_STATUS).
typedef struct DtPipeStatus
{
    int OpStatus;         // A DT_BLOCK_OPSTATUS_* value
    uint32_t StatusFlags; // DT_PIPE_STATUS_* flags
    uint32_t ErrorFlags;  // DT_PIPE_ERROR_* flags
} DtPipeStatus;

// Reads the state of Pipe into *Status.
DtapiResult DtPcieCmd_PipeGetStatus(OsDrv* Drv, DtDrvObject Pipe, DtPipeStatus* Status);

// Gives Pipe the buffer Buf to share with the card. The process allocated Buf, and must
// keep it until the pipe releases it. The buffer is passed as for
// DtPcieCmd_CdmacAllocateBuffer(), and DtPcieCmd_PipeSetSharedBufferAs() takes the way
// to pass it as an argument in the same way. Returns DTAPI_E_INVALID_ARG for a buffer
// that is empty or larger than INT_MAX bytes.
DtapiResult DtPcieCmd_PipeSetSharedBuffer(OsDrv* Drv, DtDrvObject Pipe,
                                          const OsDmaBuffer* Buf);
DtapiResult DtPcieCmd_PipeSetSharedBufferAs(OsDrv* Drv, DtDrvObject Pipe,
                                            const OsDmaBuffer* Buf, bool BufferIsOutput);

// Makes Pipe release its shared buffer.
DtapiResult DtPcieCmd_PipeReleaseSharedBuffer(OsDrv* Drv, DtDrvObject Pipe);

// Empties Pipe, and clears DT_PIPE_ERROR_INVALID_TIME: a pipe stops after a packet with
// a bad time stamp, and this makes it go on.
DtapiResult DtPcieCmd_PipeFlush(OsDrv* Drv, DtDrvObject Pipe);

// Sets the operational mode of Pipe, a DT_PIPE_OPMODE_* value. Returns
// DTAPI_E_INVALID_ARG, without sending a command, for any other value.
DtapiResult DtPcieCmd_PipeSetOpMode(OsDrv* Drv, DtDrvObject Pipe, int OpMode);

// Tells a receive pipe how far the process has read its buffer, as an offset from its
// start.
DtapiResult DtPcieCmd_PipeSetRxReadOffset(OsDrv* Drv, DtDrvObject Pipe, uint32_t Offset);

// Reads how far a receive pipe has written into its buffer, as an offset from its start.
DtapiResult DtPcieCmd_PipeGetRxWriteOffset(OsDrv* Drv, DtDrvObject Pipe,
                                           uint32_t* Offset);

// Tells a transmit pipe how far the process has written into its buffer, as an offset
// from its start.
DtapiResult DtPcieCmd_PipeSetTxWriteOffset(OsDrv* Drv, DtDrvObject Pipe, uint32_t Offset);

// Reads how far a transmit pipe has read its buffer, as an offset from its start.
DtapiResult DtPcieCmd_PipeGetTxReadOffset(OsDrv* Drv, DtDrvObject Pipe, uint32_t* Offset);

// Which packets a receive pipe takes. Flags are DT_PIPE_IPFLT_FLAG_* values. The filter
// is on with DT_PIPE_IPFLT_FLAG_EN_FILT. Each address and port only counts when its own
// flag is set; both VLAN IDs count with DT_PIPE_IPFLT_FLAG_EN_VLAN.
typedef struct DtIpFilter
{
    uint8_t DstIp[16];   // The destination address, in network byte order; IPv4 in the
                         // first 4 bytes
    uint16_t DstPort[3]; // The destination ports, in host byte order
    uint8_t SrcIp[16];   // The source address, in network byte order; IPv4 in the
                         // first 4 bytes
    uint16_t SrcPort[3]; // The source ports, in host byte order
    int VlanId[2];       // The VLAN IDs
    uint32_t Flags;      // DT_PIPE_IPFLT_FLAG_* flags
} DtIpFilter;

// Sets which packets the receive pipe Pipe takes.
DtapiResult DtPcieCmd_PipeSetIpFilter(OsDrv* Drv, DtDrvObject Pipe,
                                      const DtIpFilter* Filter);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- VPD -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the Vital Product Data (VPD) of a card, which it holds in its own EEPROM. The
// EEPROM has a read-only section, which the factory writes, a read-write section, and
// whatever lies beyond them. The commands go to the device, not to a port. This layer
// only reads.
//

// Where the sections are in the EEPROM, and how large it is (GET_PROPERTIES).
typedef struct DtVpdProps
{
    int RoOffset;      // The start of the read-only section, in bytes
    int RoSize;        // The size of the read-only section, in bytes
    int RwOffset;      // The start of the read-write section, in bytes
    int RwSize;        // The size of the read-write section, in bytes
    int EepromSize;    // The size of the EEPROM, in bytes
    int MaxItemLength; // The longest item, in bytes
} DtVpdProps;

// Reads where the sections are in the EEPROM into *Props.
DtapiResult DtPcieCmd_VpdGetProps(OsDrv* Drv, DtVpdProps* Props);

// Reads Count bytes of the EEPROM from Offset into Buf, whichever section they are in.
// When NumRead is not NULL, *NumRead receives the number of bytes the driver read, which
// can be fewer. Returns DTAPI_E_INVALID_ARG when Count is not positive.
DtapiResult DtPcieCmd_VpdRawRead(OsDrv* Drv, uint32_t Offset, uint8_t* Buf, int Count,
                                 int* NumRead);
