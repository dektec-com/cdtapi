// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtPcieCmd.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - DtPcie driver commands: typed commands on top of the OS abstraction
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DT_PCIE_CMD_H
#define CDTAPILITE_DT_PCIE_CMD_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CDtapiLite includes
#include "CDtapiLite.h"             // DTAPI result codes.
#include "OAL/OsAbstractionLayer.h" // Device handles and IOCTL transport.
#include "OAL/OsDmaBuffer.h"        // Buffers registered for DMA.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Results +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// This layer returns the public DTAPI result codes from CDtapiLite.h, so that a result
// can travel up to the API unchanged.
//

// True for DTAPI_OK and for the DTAPI_OK_* successes that carry a warning.
#define DT_SUCCEEDED(Result) ((Result) < DTAPI_E)

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// One function per driver command, each taking and returning plain C types. The driver
// structures stay inside this layer, so nothing above it depends on their layout.
//

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
unsigned int DtPcieCmdGetDriverVersion(OsDrv* Drv, DtDriverVersion* Version);

// True when a DtPcie driver of this version is new enough: 1.3.1 or later, the minimum
// DTAPI accepts (Utility.h, DtPcieMin*). The build number does not count.
bool DtPcieCmdVersionIsSupported(const DtDriverVersion* Version);

// True when Version is Major.Minor.Micro.Build or later, comparing the four numbers in
// that order, as DTAPI's DtVersion does.
bool DtPcieCmdVersionAtLeast(const DtDriverVersion* Version, int Major, int Minor,
                             int Micro, int Build);

// Reads the identity of the device behind Drv. Uses GET_DEV_INFO2, and falls back to the
// original GET_DEV_INFO for a driver that predates it, whose PCIe part has no slot power.
unsigned int DtPcieCmdGetDeviceInfo(OsDrv* Drv, DtDeviceInfo* Info);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Properties -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A property is a named value the driver holds for the device, such as PORT_COUNT, or
// for one of its ports, such as the capability 3GSDI. It is always read for the device
// behind Drv, with its own hardware revision and firmware, as DTAPI's Device class does.
//
// The functions clear their output first, and fail with DTAPI_E_BUF_TOO_SMALL for a name
// longer than the driver accepts and with DTAPI_E_NOT_FOUND for a property the device
// does not have.
//

// The port index of a property that belongs to the device rather than to a port.
#define DT_PROPERTY_DEVICE -1

// Reads an integer property. PortIndex counts from zero, or is DT_PROPERTY_DEVICE.
unsigned int DtPcieCmdGetPropertyInt(OsDrv* Drv, const char* Name, int PortIndex,
                                     int* Value);

// Reads a boolean property. PortIndex counts from zero, or is DT_PROPERTY_DEVICE.
unsigned int DtPcieCmdGetPropertyBool(OsDrv* Drv, const char* Name, int PortIndex,
                                      bool* Value);

// The size of a buffer that holds every string property, terminator included: the
// driver answers with at most 96 characters.
#define DT_PROPERTY_STR_SIZE 97

// Reads a string property into Str, which holds Size bytes. PortIndex counts from zero,
// or is DT_PROPERTY_DEVICE. Fails with DTAPI_E_BUF_TOO_SMALL, leaving Str empty, when
// the string does not fit.
unsigned int DtPcieCmdGetPropertyStr(OsDrv* Drv, const char* Name, int PortIndex,
                                     char* Str, size_t Size);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- I/O configuration -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The fields are as DTAPI's DtIoConfig has them: a port number from 1, and the group,
// value and sub-value as DTAPI_IOCONFIG_ codes, -1 for none. This layer converts to what
// the driver takes: a port index from 0, the codes as names, and, for the I/O direction
// values that name another port in ParXtra[0], that port as an index as well.
//

typedef struct DtIoConfig
{
    int Port;
    int Group;
    int Value;
    int SubValue;
    int64_t ParXtra[2];
} DtIoConfig;

// Reads the configuration of Config->Group on Config->Port, and fills in the other
// fields.
unsigned int DtPcieCmdGetIoConfig(OsDrv* Drv, DtIoConfig* Config);

// Applies one configuration. The driver validates it; this layer only converts it, and
// refuses a LOOPS2TS output whose ParXtra[1], the ISI, is outside 0 to 255 with
// DTAPI_E_INVALID_ISI, as DTAPI does before sending it.
unsigned int DtPcieCmdSetIoConfig(OsDrv* Drv, const DtIoConfig* Config);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Time of day -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Reads the device's time-of-day clock.
unsigned int DtPcieCmdGetTimeOfDay(OsDrv* Drv, uint32_t* Seconds, uint32_t* Nanoseconds);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SDI receiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A command for a driver function rather than for the device goes to the function's UUID,
// which the device layer reads from the function's properties, and to the index of the
// port the function belongs to.
//

// What an SDI receiver reports of its input, converted as DTAPI's SDIRX proxy does.
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

// Reads the status of the SDI receiver with this UUID in the port with this index.
// Clears *Status first.
unsigned int DtPcieCmdSdiRxGetStatus(OsDrv* Drv, int Uuid, int PortIndex,
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
// DTAPI_E_INVALID_ARG, as DTAPI's proxy refuses one.
unsigned int DtPcieCmdChSdiRxAttach(OsDrv* Drv, int Uuid, int PortIndex, bool Exclusive,
                                    const char* FriendlyName);

// Detaches from the channel.
unsigned int DtPcieCmdChSdiRxDetach(OsDrv* Drv, int Uuid, int PortIndex);

// Configures the channel, which must be idle.
unsigned int DtPcieCmdChSdiRxConfigure(OsDrv* Drv, int Uuid, int PortIndex,
                                       const DtChSdiRxConfig* Config);

// Reads and sets this user's operational mode, a DT_FUNC_OPMODE_ value.
unsigned int DtPcieCmdChSdiRxGetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int* OpMode);
unsigned int DtPcieCmdChSdiRxSetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int OpMode);

// Waits up to TimeoutMs milliseconds for the next format event. Gives DTAPI_E_TIMEOUT
// when none comes.
unsigned int DtPcieCmdChSdiRxWaitForFmtEvent(OsDrv* Drv, int Uuid, int PortIndex,
                                             int TimeoutMs, DtChSdiRxEvent* Event);

// Reads how far the channel has written into the ring, as an offset from its start.
unsigned int DtPcieCmdChSdiRxGetWriteOffset(OsDrv* Drv, int Uuid, int PortIndex,
                                            uint32_t* Offset);

// Tells the channel how far this user has read.
unsigned int DtPcieCmdChSdiRxSetReadOffset(OsDrv* Drv, int Uuid, int PortIndex,
                                           uint32_t Offset);

// Reads the channel's properties.
unsigned int DtPcieCmdChSdiRxGetProps(OsDrv* Drv, int Uuid, int PortIndex,
                                      DtChSdiRxProps* Props);

// Reads the status of the channel's input, as DtPcieCmdSdiRxGetStatus does for the
// receiver.
unsigned int DtPcieCmdChSdiRxGetSdiStatus(OsDrv* Drv, int Uuid, int PortIndex,
                                          DtSdiRxStatus* Status);

// Maps the configured ring into the process. On Windows the driver maps it during the
// command and returns its address; on Linux the driver returns address 0, and the ring
// is then mapped from the device at offset DT_MMAP_PORT_MEM_SEGMENT_SIZE times the port
// index plus one, as DtProxyCHSDIRX::MapDmaBufferToUser does. *Mapped is true in the
// second case, in which DtPcieCmdChSdiRxUnmapDmaBuf must release the mapping.
unsigned int DtPcieCmdChSdiRxMapDmaBuf(OsDrv* Drv, int Uuid, int PortIndex,
                                       uint8_t** Buffer, int* BufSize, int* MaxLoad,
                                       bool* Mapped);

// Releases a mapping DtPcieCmdChSdiRxMapDmaBuf made itself; one the driver made goes with
// the detach.
void DtPcieCmdChSdiRxUnmapDmaBuf(OsDrv* Drv, uint8_t* Buffer, int BufSize, bool Mapped);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Exclusive access -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// One file handle at a time can hold a driver function or building block. The driver
// checks the holder on the commands that change a part, and the handle lets go of what
// it holds when it is closed.
//

// Issues EXCL_ACCESS_CMD Cmd, a DT_EXCLUSIVE_ACCESS_CMD_ value, for the part with this
// UUID in the port with this index. Acquiring a part that is held, also by this handle,
// gives DTAPI_E_IN_USE. Checking gives DTAPI_OK for a part this handle holds,
// DTAPI_E_EXCL_ACCESS_REQD for one nobody holds, and DTAPI_E_IN_USE for one another
// handle holds; probing gives DTAPI_E_IN_USE for a part anyone holds. Releasing a part
// another handle holds gives DTAPI_E_IN_USE.
unsigned int DtPcieCmdExclAccess(OsDrv* Drv, int Uuid, int PortIndex, int Cmd);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SDI transmit blocks -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A port transmits without a channel driver function. The process drives the parts of
// the port's AF_DMA and AF_ASISDITX API functions itself: the DMA controller CDMAC, which
// reads a buffer the process allocates, the burst FIFO behind it, the formatter SDITXF,
// the switches around the 12G demultiplexer SDIDMX12G, the protocol encoder SDITXP and
// the driver function SDITXPHY. Every command goes to the part's UUID and port index.
//
// Operational modes are DT_BLOCK_OPMODE_ values, and DT_FUNC_OPMODE_ values for SDITXPHY;
// any other value gives DTAPI_E_INVALID_ARG without a command, as DTAPI's proxies refuse
// a mode they cannot convert.
//

// What a DMA controller is like, as DT_CDMAC_CMD_GET_PROPERTIES reports.
typedef struct DtCdmacProps
{
    uint32_t Caps;      // DT_CDMAC_CAP_ flags
    int PrefetchSize;   // In pages; a buffer is a multiple of this many pages
    int PcieDataWidth;  // Bits per PCIe data word
    int ReorderBufSize; // Bytes
} DtCdmacProps;

unsigned int DtPcieCmdCdmacGetProps(OsDrv* Drv, int Uuid, int PortIndex,
                                    DtCdmacProps* Props);

// Registers Buf, which the process allocated and keeps until the buffer is freed, for
// Direction, DT_CDMAC_DIR_TX or DT_CDMAC_DIR_RX. The buffer travels as this platform's
// driver takes it, as OsDmaDescribeHandOff describes; DtPcieCmdCdmacAllocateBufferAs
// chooses the convention, true for Windows's, so that both can be tested on every
// platform. A direction the driver does not define, an empty buffer and one larger than
// an int can count give DTAPI_E_INVALID_ARG.
//
// With the buffer as the output, as on Windows, the answer's size is not checked against
// the buffer's: the driver reports the output it was given, which could not be confirmed
// on a card.
unsigned int DtPcieCmdCdmacAllocateBuffer(OsDrv* Drv, int Uuid, int PortIndex,
                                          int Direction, const OsDmaBuffer* Buf);
unsigned int DtPcieCmdCdmacAllocateBufferAs(OsDrv* Drv, int Uuid, int PortIndex,
                                            int Direction, const OsDmaBuffer* Buf,
                                            bool BufferIsOutput);

// Lets go of the registered buffer. The controller must be idle.
unsigned int DtPcieCmdCdmacFreeBuffer(OsDrv* Drv, int Uuid, int PortIndex);

// Empties the controller's pipeline. The controller must be idle.
unsigned int DtPcieCmdCdmacIssueChannelFlush(OsDrv* Drv, int Uuid, int PortIndex);

unsigned int DtPcieCmdCdmacSetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int OpMode);

// Sets a DT_CDMAC_TESTMODE_ value. The controller must be idle.
unsigned int DtPcieCmdCdmacSetTestMode(OsDrv* Drv, int Uuid, int PortIndex, int TestMode);

// Reads how far the card has read the buffer, as an offset from its start.
unsigned int DtPcieCmdCdmacGetTxReadOffset(OsDrv* Drv, int Uuid, int PortIndex,
                                           uint32_t* Offset);

// Tells the card how far the buffer holds data for it.
unsigned int DtPcieCmdCdmacSetTxWriteOffset(OsDrv* Drv, int Uuid, int PortIndex,
                                            uint32_t Offset);

// Reads the reorder buffer's load and its minimum or maximum since the last clear.
unsigned int DtPcieCmdCdmacGetReorderBufStatus(OsDrv* Drv, int Uuid, int PortIndex,
                                               int* Load, int* MinMaxLoad);

unsigned int DtPcieCmdCdmacClearReorderBufMinMax(OsDrv* Drv, int Uuid, int PortIndex);

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

unsigned int DtPcieCmdBurstFifoGetProps(OsDrv* Drv, int Uuid, int PortIndex,
                                        DtBurstFifoProps* Props);
unsigned int DtPcieCmdBurstFifoGetStatus(OsDrv* Drv, int Uuid, int PortIndex,
                                         DtBurstFifoStatus* Status);
unsigned int DtPcieCmdBurstFifoClearMax(OsDrv* Drv, int Uuid, int PortIndex, bool MaxFree,
                                        bool MaxLoad);

// Reads the count of overflows and underflows. It stands still while data flows.
unsigned int DtPcieCmdBurstFifoGetOvfUflCount(OsDrv* Drv, int Uuid, int PortIndex,
                                              uint32_t* Count);

unsigned int DtPcieCmdBurstFifoSetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int OpMode);

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
unsigned int DtPcieCmdSdiTxFSetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int OpMode);

// Asks for a format event every NumLinesPerEvent lines, and a start-of-frame time every
// NumSofsBetweenTod frames.
unsigned int DtPcieCmdSdiTxFSetFmtEventSetting(OsDrv* Drv, int Uuid, int PortIndex,
                                               int NumLinesPerEvent,
                                               int NumSofsBetweenTod);

// Reads the bits every part of the buffer's format is padded to.
unsigned int DtPcieCmdSdiTxFGetStreamAlignment(OsDrv* Drv, int Uuid, int PortIndex,
                                               int* AlignmentBits);

// Waits up to TimeoutMs milliseconds, -1 to 1000, for the next format event. Gives
// DTAPI_E_TIMEOUT when none comes, and DTAPI_E_INVALID_MODE at once while the formatter
// is not running.
unsigned int DtPcieCmdSdiTxFWaitForFmtEvent(OsDrv* Drv, int Uuid, int PortIndex,
                                            int TimeoutMs, DtSdiTxFEvent* Event);

// Connects input InputIndex to output OutputIndex.
unsigned int DtPcieCmdSwitchSetPosition(OsDrv* Drv, int Uuid, int PortIndex,
                                        int InputIndex, int OutputIndex);
unsigned int DtPcieCmdSwitchSetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int OpMode);

unsigned int DtPcieCmdSdiDmx12GSetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int OpMode);

unsigned int DtPcieCmdSdiTxPSetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int OpMode);

// Makes the encoder clamp video symbols, and insert ANC checksums and line CRCs.
unsigned int DtPcieCmdSdiTxPSetGenerationMode(OsDrv* Drv, int Uuid, int PortIndex,
                                              bool Clamp, bool AncChecksum, bool LineCrc);

unsigned int DtPcieCmdSdiTxPhySetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int OpMode);

// Reads and clears the flag the PHY sets when its data ran out. It stays set until
// cleared.
unsigned int DtPcieCmdSdiTxPhyGetUnderflowFlag(OsDrv* Drv, int Uuid, int PortIndex,
                                               bool* Underflow);
unsigned int DtPcieCmdSdiTxPhyClearUnderflowFlag(OsDrv* Drv, int Uuid, int PortIndex);

// Delays the start of each frame by OffsetNs nanoseconds.
unsigned int DtPcieCmdSdiTxPhySetStartOfFrameOffset(OsDrv* Drv, int Uuid, int PortIndex,
                                                    int OffsetNs);

#endif // CDTAPILITE_DT_PCIE_CMD_H
