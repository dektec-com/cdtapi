// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* cdtapi.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Public C API for DekTec SDI and DVB-ASI interfaces
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>

// CDTAPI includes
#include "cdtapi_constants.h" // Result codes and configuration constants.
#include "cdtapi_version.h"   // Library version.

#ifdef __cplusplus
extern "C"
{
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Symbol export +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Nothing has to be defined to use this header: a program that links one of the static
// libraries, which is what the SDK ships for an application to link, compiles as it is.
//
// CDTAPI_DLL says the program links the DLL's import library instead, which lets the
// compiler call straight through the import table. Without it a DLL still links and
// runs, through a thunk the linker writes. The library's own build defines
// CDTAPI_EXPORTS.
//

#if defined(_WIN32) || defined(_WIN64)
    #if defined(CDTAPI_EXPORTS)
        #define CDTAPI_API __declspec(dllexport)
    #elif defined(CDTAPI_DLL)
        #define CDTAPI_API __declspec(dllimport)
    #else
        #define CDTAPI_API
    #endif
#else
    #define CDTAPI_API __attribute__((visibility("default")))
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Results +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// What every function that can fail returns: a DTAPI_OK or DTAPI_E_ code from
// cdtapi_constants.h. Results below DTAPI_E are successes. The type is unsigned int
// on every platform CDTAPI supports, as DTAPI's DTAPI_RESULT is.
//

typedef uint32_t DtapiResult;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Global functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Returns the library version as a string, for example "6.13.0": the major and minor
// number of the DTAPI whose behaviour the library reproduces, and a patch number of its
// own. The returned pointer is static storage owned by the library and must not be freed.
CDTAPI_API const char* DtapiGetVersion(void);

// Converts a video standard to the I/O standard group value and sub-value that select it,
// for use with SetIoConfig and DTAPI_IOCONFIG_IOSTD. LinkStandard is -1 for anything but
// 4K; for 4K it says how the picture is carried: 0 and 1 are four 3G links per SMPTE 425
// level A and annex B, 2 is one 6G link, 3 is one 12G link.
//
// Sets *Value and *SubValue to -1 before anything can fail. Returns DTAPI_E_INVALID_ARG
// for a null output pointer, DTAPI_E_INVALID_LINKSTD for a link standard that does not
// suit the video standard, and DTAPI_E_INVALID_VIDSTD for an unknown video standard.
// 4K that is not on the one link of its rate, 6G up to 30 frames and 12G from 50, gives
// the I/O standard of one of its links: HD-SDI or 3G-SDI with the 1080p standard of the
// same rate.
CDTAPI_API DtapiResult DtapiVidStd2IoStd(int VideoStandard, int LinkStandard, int* Value,
                                         int* SubValue);

// Returns the name of a result code's macro, for example "DTAPI_E_IN_USE", or "???" for a
// value that is no result code. The names are those DTAPI gives: of each pair of names
// for one value the first, DTAPI_E_NO_DT_INPUT and DTAPI_E_NO_DT_OUTPUT, and "???" for
// DTAPI_E_INVALID_NUM_INPUTS, DTAPI_E_DISABLED and DTAPI_E_EXCEPTION, which DTAPI does
// not name. The returned string is static and must not be freed.
CDTAPI_API const char* DtapiResult2Str(DtapiResult Result);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Time of day +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A time from a device's time-of-day clock.
typedef struct DtTimeOfDay
{
    uint32_t Seconds;     // Integer number of seconds part of the TOD time
    uint32_t Nanoseconds; // Number of nanoseconds part of the TOD time
} DtTimeOfDay;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtHwFuncDesc +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// One hardware function: a public port of a device.
//

// The sizes of its two strings, MAX_DEVICE_NAME_SIZE and MAX_DEVICE_DESC_SIZE, are in
// cdtapi_constants.h.

typedef struct DtHwFuncDesc
{
    char DeviceName[MAX_DEVICE_NAME_SIZE];  // Serial and port, as "<serial>:<port>"
    char Description[MAX_DEVICE_DESC_SIZE]; // Type and port, as "DTA-2178 port 1"
    int64_t SerialNumber;
    int Port;     // Port number, from 1
    int IsSdi;    // 1 when the port can carry SD-, HD-, 3G-, 6G- or 12G-SDI
    int IsAvFifo; // 1 when the port has an AV FIFO
    int IsInput;  // 1 when the port can be an input
    int IsOutput; // 1 when the port can be an output
    int IsAsi;    // 1 when the port can carry DVB-ASI
} DtHwFuncDesc;

// Describes the public ports of every device, in the order the driver numbers the
// devices. HwFuncs holds NumEntries descriptors; *NumEntriesResult receives how many
// ports there are, also when they do not all fit. A device that cannot be attached, for
// example because its driver is too old, is left out.
//
// Returns DTAPI_OK and fills all NumEntries descriptors; those beyond the last port are
// all zero apart from DeviceName "0:0" and Description "DTA-0 port 0", as CDTAPI fills
// them. Returns DTAPI_E_BUF_TOO_SMALL when there are more ports than NumEntries, leaving
// HwFuncs untouched; with NumEntries 0 and HwFuncs NULL this asks for the count. Returns
// DTAPI_E_INVALID_ARG for a null NumEntriesResult or a negative NumEntries,
// DTAPI_E_INVALID_BUF for a null HwFuncs with NumEntries not 0, and DTAPI_E_OUT_OF_MEM.
CDTAPI_API DtapiResult DtapiHwFuncScan(int NumEntries, int* NumEntriesResult,
                                       DtHwFuncDesc* HwFuncs);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtDeviceDesc +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// One device, as DTAPI's DtDeviceDesc describes it, with its fields under DTAPI's names
// without the m_ prefix.
//

// Device categories.
#define DTAPI_CAT_ALL -1 // All devices
#define DTAPI_CAT_PCI 0  // PCI or PCI-Express device
#define DTAPI_CAT_USB 1  // USB-2 or USB-3 device
#define DTAPI_CAT_NW 2   // Network device
#define DTAPI_CAT_IP 3   // Network appliance: DTE-31xx
#define DTAPI_CAT_NIC 4  // Non-DekTec network card
#define DTAPI_CAT_NWAP 5 // Network Advanced Protocol (VLAN device)

// The number of IPv6 addresses a device descriptor holds.
#define MAX_IPV6_ADDR 3

// Whether the firmware suits the driver.
typedef enum DtFirmwareStatus
{
    DTAPI_FWSTATUS_UNDEFINED = -1, // Cannot be determined
    DTAPI_FWSTATUS_UPTODATE,       // The latest released version the driver supports
    DTAPI_FWSTATUS_BETA,           // The latest version the driver supports, not released
    DTAPI_FWSTATUS_OLD,            // Not the latest version
    DTAPI_FWSTATUS_NEW,            // Newer than the driver supports
    DTAPI_FWSTATUS_TAINTED,        // An intermediate version the driver does not support
    DTAPI_FWSTATUS_OBSOLETE        // No longer supported
} DtFirmwareStatus;

// When a firmware was built.
typedef struct DtFwBuildDateTime
{
    int Year;
    int Month;
    int Day;
    int Hour;
    int Minute;
} DtFwBuildDateTime;

typedef struct DtDeviceDesc
{
    int Category;                    // DTAPI_CAT_ value
    int64_t Serial;                  // Unique serial number of the device
    int PciBusNumber;                // PCI bus number
    int SlotNumber;                  // PCI slot number
    int UsbAddress;                  // USB address; 0 for a PCIe device
    int TypeNumber;                  // Device type number, 2178 for a DTA-2178
    int SubType;                     // Device subtype: 0 for none, 1 for A, ...
    int DeviceId;                    // PCI device ID
    int VendorId;                    // PCI vendor ID
    int SubsystemId;                 // PCI subsystem ID
    int SubVendorId;                 // PCI subsystem vendor ID
    int NumHwFuncs;                  // Number of hardware functions: the ports
    int HardwareRevision;            // Hardware revision, such as 302 for 3.2
    int FirmwareVersion;             // Firmware version
    int FirmwareVariant;             // Firmware variant
    DtFirmwareStatus FirmwareStatus; // Firmware status
    DtFwBuildDateTime FwBuildDate;   // Firmware build date and time
    int NumDtInpChan;                // Number of ports that are inputs
    int NumDtOutpChan;               // Number of ports that are outputs
    int NumPorts;                    // Number of physical ports
    uint8_t Ip[4];                   // IPv4 address; DTE-31xx only
    uint8_t IpV6[MAX_IPV6_ADDR][16]; // IPv6 addresses; DTE-31xx only
    uint8_t MacAddr[6];              // MAC address; DTE-31xx only
    int PcieNumLanes;                // Number of PCIe lanes in use
    int PcieMaxLanes;                // Maximum number of PCIe lanes
    int PcieLinkSpeed;               // PCIe generation of the link
    int PcieMaxSpeed;                // PCIe generation the link can reach
    int PcieMaxPayloadSize;          // Maximum PCIe payload size in bytes
    int PcieMaxReadRequestSize;      // Maximum PCIe read request size in bytes
    int PcieMaxSlotPower;            // Maximum PCIe slot power in milliwatts
} DtDeviceDesc;

// Describes every device, in the order the driver numbers them, as DTAPI's
// DtapiDeviceScan does for PCIe devices. DvcDescArr holds NumEntries descriptors; they
// are filled while they fit, and *NumEntriesResult receives how many devices there are.
// A device that cannot be attached, for example because its driver is too old, is left
// out.
//
// A port counts as an input or output by its capabilities; a port that can be both
// counts by its current I/O direction. When reading a direction fails, DTAPI stops
// counting, and so does this.
//
// Returns DTAPI_OK; DTAPI_E_BUF_TOO_SMALL when there are more devices than NumEntries,
// after filling all NumEntries; with NumEntries 0 and DvcDescArr NULL this asks for the
// count. Returns DTAPI_E_INVALID_ARG for a null NumEntriesResult or a negative
// NumEntries, and DTAPI_E_INVALID_BUF for a null DvcDescArr with NumEntries not 0.
CDTAPI_API DtapiResult DtapiDeviceScan(int NumEntries, int* NumEntriesResult,
                                       DtDeviceDesc* DvcDescArr);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtDevice +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A device object, attached to one DekTec device at a time. Every function taking a
// DtDevice returns DTAPI_E_INVALID_ARG for a null pointer, and, except where it says
// otherwise, DTAPI_E_NOT_ATTACHED when it needs a device and the object is not attached
// to one.
//

// The picture aspect ratio a VPID gives.
typedef enum DtAspectRatio
{
    DT_AR_UNKNOWN, // Unknown aspect ratio
    DT_AR_4_3,     // 4x3
    DT_AR_16_9,    // 16x9
    DT_AR_14_9     // 14x9
} DtAspectRatio;

// The video standard detected on an input port.
typedef struct DtDetVidStd
{
    int VidStd;                // DTAPI_VIDSTD_ code, DTAPI_VIDSTD_UNKNOWN when none
    int LinkStd;               // How 4K is carried, 0 to 3; -1 for none
    int LinkNr;                // The VPID's link number from 1; -1 without a VPID
    uint32_t Vpid;             // Raw VPID, 0 if not available
    uint32_t Vpid2;            // Raw VPID of 3G level B's second channel; always 0
    DtAspectRatio AspectRatio; // From the VPID: 4:3 or 16:9; unknown without one

    // What the input carries before the hardware processes it: 12G or 6G 4K on one link
    // that the port scales to 3G is VidStd 1080p and LinkStd -1, but 2160p here.
    int OriginalVidStd;
    int OriginalLinkStd;
} DtDetVidStd;

// One I/O configuration of a port, as DTAPI's DtIoConfig. Group, Value and SubValue are
// DTAPI_IOCONFIG_ codes, -1 for none. ParXtra holds what some values take besides: the
// port a double-buffered or monitor output copies, numbered from 1, in ParXtra[0]; the
// ISI of a loop-through to a transport stream in ParXtra[1]; -1 where nothing is taken.
typedef struct DtIoConfig
{
    int Port; // Numbered from 1
    int Group;
    int Value;
    int SubValue;
    int64_t ParXtra[2];
} DtIoConfig;

typedef struct DtDevice DtDevice;

// Allocates a detached device object. Returns NULL when memory runs out.
CDTAPI_API DtDevice* DtDevice_Alloc(void);

// Detaches the device object if it is attached, and frees it. NULL is allowed.
CDTAPI_API void DtDevice_Free(DtDevice* Device);

// Frees *Device as DtDevice_Free does and sets *Device to NULL. NULL is allowed.
CDTAPI_API void DtDevice_Freep(DtDevice** Device);

// Attaches to the device with this serial number. Returns DTAPI_E_ATTACHED when already
// attached, DTAPI_E_DRIVER_INCOMP for a driver that is too old, and
// DTAPI_E_NO_SUCH_DEVICE when no device has the serial number. Succeeds with
// DTAPI_OK_OBSOLETE_FW or DTAPI_OK_TAINTED_FW when the device's firmware is obsolete or
// tainted.
CDTAPI_API DtapiResult DtDevice_AttachToSerial(DtDevice* Device, int64_t SerialNumber);

// Detaches from the device.
CDTAPI_API DtapiResult DtDevice_Detach(DtDevice* Device);

// Sets the Count I/O configurations in Configs, which the driver applies together or not
// at all, so that a port and the ports that copy it change at once. Every entry is
// checked before any is applied.
//
// Returns DTAPI_E_INVALID_ARG for a negative Count or a null Configs with a Count above
// 0; DTAPI_E_OBSOLETE_FW or DTAPI_E_TAINTED_FW for a device whose firmware is; for the
// first entry that fails, DTAPI_E_NO_SUCH_PORT for a port the device does not have and
// DTAPI_E_INVALID_ARG for a combination of group, value and sub-value that is no
// configuration; DTAPI_E_INVALID_ISI for an ISI outside 0 to 255; otherwise the driver's
// result. A Count of 0 does nothing.
CDTAPI_API DtapiResult DtDevice_SetIoConfig(DtDevice* Device, const DtIoConfig* Configs,
                                            int Count);

// Reads the Count I/O configurations Configs names by Port and Group, and fills in their
// Value, SubValue and ParXtra, which are -1 wherever this fails.
//
// Returns DTAPI_E_INVALID_ARG for a negative Count or a null Configs with a Count above
// 0; for the first entry that fails, DTAPI_E_NO_SUCH_PORT for a port the device does not
// have, DTAPI_E_OBSOLETE_FW or DTAPI_E_TAINTED_FW for a device whose firmware is,
// DTAPI_E_INVALID_ARG for a Group that is neither a group nor a boolean I/O capability,
// and DTAPI_E_NOT_SUPPORTED for a group the port has no capability of; otherwise the
// driver's result. A Count of 0 does nothing.
CDTAPI_API DtapiResult DtDevice_GetIoConfig(DtDevice* Device, DtIoConfig* Configs,
                                            int Count);

// Makes a port an output: DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT.
CDTAPI_API DtapiResult DtDevice_SetToOutput(DtDevice* Device, int Port);

// Makes a port an input: DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT.
CDTAPI_API DtapiResult DtDevice_SetToInput(DtDevice* Device, int Port);

// Waits until a video standard is detected on a port, numbered from 1, and returns it.
// Detection is retried every 5 ms, without a time limit, also while it fails. Returns at
// once with every field unknown for a null Device and for a port that detection cannot
// be attached to; see DtDevice_DetectVidStd.
CDTAPI_API DtDetVidStd DtDevice_WaitForSignal(DtDevice* Device, int Port);

// Detects the video standard on an input port, numbered from 1, and sets *VidStd to it:
// a DTAPI_VIDSTD_ code, or DTAPI_VIDSTD_UNKNOWN when there is no valid, locked signal or
// it matches no standard. *VidStd is written only when this returns DTAPI_OK.
//
// Returns DTAPI_E_INVALID_ARG for a null pointer; DTAPI_E_DEVICE when Device is not
// attached; DTAPI_E_OBSOLETE_FW or DTAPI_E_TAINTED_FW; DTAPI_E_NO_SUCH_PORT for a port
// outside the device's ports, the internal ones included; DTAPI_E_NOT_SUPPORTED for a
// port that is not an input or has no SDI receiver the high-level Matrix API can use;
// DTAPI_E_NOT_FOUND when the driver describes no SDI receiver for the port;
// DTAPI_E_DRIVER_INCOMP for a driver older than 1.4.0.111; and the driver's result when
// reading the receiver fails, such as DTAPI_E_INVALID_MODE for a port that is not
// configured as an input.
CDTAPI_API DtapiResult DtDevice_DetectVidStd(DtDevice* Device, int Port, int* VidStd);

// DtDevice_WaitForSignal with a time limit and a result. Waits up to TimeoutMs
// milliseconds, retrying detection every 5 ms; 0 tries once, and a negative TimeoutMs
// waits without a limit. Returns DTAPI_OK with *Result filled in when a standard is
// detected, DTAPI_E_TIMEOUT with every field of *Result unknown when none is within the
// time, and DTAPI_E_INVALID_ARG for a null Device or Result. The reasons attaching can
// fail, as DtDevice_DetectVidStd lists them, are returned at once.
CDTAPI_API DtapiResult DtDevice_WaitForSignalTimeout(DtDevice* Device, int Port,
                                                     int TimeoutMs, DtDetVidStd* Result);

// Reads the device's time-of-day clock. *TimeOfDay is zero when this fails.
CDTAPI_API DtapiResult DtDevice_GetTimeOfDay(const DtDevice* Device,
                                             DtTimeOfDay* TimeOfDay);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtInpChannel +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// An input channel on a port of a DtPcie card, SDI or ASI by the port's I/O standard.
//
// On SDI it delivers DTAPI's raw SDI frames: every line, EAV first, with 8-, 10- or
// 16-bit symbols. It works directly on the DMA ring of the card's receive channel,
// without a thread of its own: ReadFrame waits for the card's format events and
// assembles each frame straight into the caller's buffer. SD, HD and 3G standards are
// received; 4K, 8-bit symbols, active-video-only and compressed modes are not.
//
// On ASI it delivers a transport stream through Read, in the receive modes DTAPI has for
// ASI: DTAPI_RXMODE_ST188, ST204, STMP2, STRAW and STTRP, with DTAPI_RXMODE_TIMESTAMP32
// or TIMESTAMP_TOD. The card writes into a buffer of 16 MB, which Read converts from
// straight into the caller's buffer, again without a thread; the load and the FIFO are
// DTAPI's, 8 MB at most.
//
// A channel attaches exclusively and is configured for the port's I/O standard when it
// attaches and whenever that standard is set through the channel, switching between SDI
// and ASI as the standard does.
//
// Every function taking a DtInpChannel returns DTAPI_E_INVALID_ARG for a null pointer,
// and DTAPI_E_NOT_ATTACHED when the channel is not attached.
//

typedef struct DtInpChannel DtInpChannel;

// Allocates a detached input channel. Returns NULL when memory runs out.
CDTAPI_API DtInpChannel* DtInpChannel_Alloc(void);

// Detaches the channel, discarding what it has received, and frees it. A ReadFrame
// waiting on another thread returns DTAPI_E_CANCELLED first; this waits for that as long
// as it takes. NULL is allowed.
CDTAPI_API void DtInpChannel_Free(DtInpChannel* InpChannel);

// Frees *InpChannel as DtInpChannel_Free does and sets *InpChannel to NULL.
CDTAPI_API void DtInpChannel_Freep(DtInpChannel** InpChannel);

// Attaches to a port of an attached device, numbered from 1, exclusively. The channel
// uses its own handle to the device, so the device object may be detached afterwards.
// On ASI the output sends K28.5 from here on, and so from a switch to ASI; a receiver
// needs a moment to lock to it, and a stream sent at once loses its start. DekTec's
// DtPlay waits 200 ms before sending, as DtTransmitTs does.
//
// Returns, in DTAPI's order: DTAPI_E_ATTACHED; DTAPI_E_DEVICE for a detached Device;
// DTAPI_E_OBSOLETE_FW or DTAPI_E_TAINTED_FW; DTAPI_E_NO_SUCH_PORT; DTAPI_E_NO_DT_INPUT
// for a port that cannot be, or is not configured as, an input; DTAPI_E_NOT_SUPPORTED for
// a port without an ASI/SDI receiver; DTAPI_E_NOT_FOUND and DTAPI_E_DRIVER_INCOMP for a
// receiver the driver does not describe or is too old for; DTAPI_E_IN_USE when another
// user has the port; and the driver's result of any command.
CDTAPI_API DtapiResult DtInpChannel_AttachToPort(DtInpChannel* InpChannel,
                                                 DtDevice* Device, int Port);

// Converts the lines of a 4K frame over Threads threads of the library's own, 1 for the
// thread that reads, which is the default. A 2160p frame costs some milliseconds of
// processor time a frame to convert, which is more than a slow core has to spare at 50
// or 60 frames a second; the lines of a frame are independent, so they divide over as
// many threads as the machine can give them. The threads exist until the channel is
// detached or the count is set again.
//
// No other standard divides: the lines of a packed frame share a byte at each boundary.
// Whatever the count, the bytes are the same.
//
// DTAPI_E_INVALID_ARG below 1, DTAPI_E_NOT_SUPPORTED on a channel whose signal has no
// lines to divide, and DTAPI_E_OUT_OF_MEM when the threads or their buffers cannot be
// had, after which the channel converts in the reading thread again.
CDTAPI_API DtapiResult DtInpChannel_SetConversionThreads(DtInpChannel* InpChannel,
                                                         int Threads);

// Stops receiving and discards what the channel holds, and clears the overflow flag.
CDTAPI_API DtapiResult DtInpChannel_ClearFifo(DtInpChannel* InpChannel);

// Clears the latched flags in Latched: DTAPI_RX_FIFO_OVF, and on ASI DTAPI_RX_SYNC_ERR.
CDTAPI_API DtapiResult DtInpChannel_ClearFlags(DtInpChannel* InpChannel, int Latched);

// Detaches. With DTAPI_INSTANT_DETACH, 1, what the channel holds is discarded first; both
// modes stop receiving. A read waiting on another thread returns DTAPI_E_CANCELLED;
// DTAPI_E_TIMEOUT when it has not returned after 100 ms, and the channel then stays
// attached and usable. DTAPI_E_NOT_ATTACHED when another thread detached it meanwhile.
CDTAPI_API DtapiResult DtInpChannel_Detach(DtInpChannel* InpChannel, int DetachMode);

// Detects the I/O standard of the signal on the port: the value and sub-value that
// DtapiVidStd2IoStd gives for the detected video standard. Fails as that function does
// when no standard is detected, and with DTAPI_E_NOT_SUPPORTED on ASI.
CDTAPI_API DtapiResult DtInpChannel_DetectIoStd(DtInpChannel* InpChannel, int* Value,
                                                int* SubValue);

// The bytes of complete frames waiting to be read, as raw frames in the current receive
// mode; on ASI the bytes Read would deliver now. 0 while not receiving.
CDTAPI_API DtapiResult DtInpChannel_GetFifoLoad(DtInpChannel* InpChannel, int* FifoLoad);

// The largest load GetFifoLoad can report: the complete frames the channel's ring holds
// when full, as raw frames in the current receive mode. At least two frames. On a 4K
// port, where the channel does not receive, DTAPI's FIFO size of 48 MB; on ASI its 8 MB.
CDTAPI_API DtapiResult DtInpChannel_GetMaxFifoSize(DtInpChannel* InpChannel,
                                                   int* MaxFifoSize);

// The status flags and the latched flags: DTAPI_RX_FIFO_OVF when the card's ring for the
// channel was full, which loses frames. On ASI, DTAPI_RX_FIFO_OVF when packets were lost,
// in the card or because the FIFO was full, and DTAPI_RX_SYNC_ERR for a packet the card
// received without packet sync, as DTAPI sets them.
CDTAPI_API DtapiResult DtInpChannel_GetFlags(DtInpChannel* InpChannel, int* Flags,
                                             int* Latched);

// Sets an I/O configuration of the channel's port, while not receiving
// (DTAPI_E_NOT_IDLE), with DTAPI's ParXtra0 and ParXtra1, -1 where the configuration
// takes none. A new SDI standard reconfigures the channel for it. A standard that
// crosses between SDI and ASI switches the channel to the other, with that side's
// default receive mode, DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B or
// DTAPI_RXMODE_ST188; when the switch fails the channel is left detached. Returns
// DTAPI_E_INVALID_ARG for a combination that is no configuration, for an output
// direction and for an input that shares the antenna of a port ParXtra0 does not name,
// and DTAPI_E_NOT_SUPPORTED for 6G, 12G and any other direction, which DTAPI would
// apply.
CDTAPI_API DtapiResult DtInpChannel_SetIoConfig(DtInpChannel* InpChannel, int Group,
                                                int Value, int SubValue, int64_t ParXtra0,
                                                int64_t ParXtra1);

// Reads the I/O configuration of group Group of the channel's port, as
// DtDevice_GetIoConfig reads it: *Value, and *SubValue, *ParXtra0 and *ParXtra1 where
// they are not NULL, which are -1 after a failure. Returns DTAPI_E_INVALID_ARG for a
// group that is none, before DTAPI_E_NOT_ATTACHED, and DTAPI_E_NOT_SUPPORTED for a group
// the port does not have.
CDTAPI_API DtapiResult DtInpChannel_GetIoConfig(DtInpChannel* InpChannel, int Group,
                                                int* Value, int* SubValue,
                                                int64_t* ParXtra0, int64_t* ParXtra1);

// DTAPI_RXCTRL_RCV starts receiving from the next frame on; DTAPI_RXCTRL_IDLE stops.
// Receiving in the 8-bit mode, or on a port configured for 4K, fails with
// DTAPI_E_CONFIG_RAW_SDI, as it does in DTAPI. On ASI, starting empties the FIFO and
// clears DTAPI_RX_FIFO_OVF, and so does stopping; a value that is neither gives
// DTAPI_E_INVALID_ARG.
CDTAPI_API DtapiResult DtInpChannel_SetRxControl(DtInpChannel* InpChannel, int RxControl);

// Sets the receive mode while not receiving: on SDI DTAPI_RXMODE_SDI_FULL, optionally
// with DTAPI_RXMODE_SDI_10B or DTAPI_RXMODE_SDI_16B, 8-bit without either; on ASI one of
// the modes above. Any other mode gives DTAPI_E_INVALID_MODE; receiving gives
// DTAPI_E_NOT_IDLE.
CDTAPI_API DtapiResult DtInpChannel_SetRxMode(DtInpChannel* InpChannel, int RxMode);

// Reads one frame into FrameBuffer, which holds *FrameSize bytes, and sets *FrameSize to
// the frame's size. Waits up to TimeOut milliseconds, or without a limit for -1. The
// buffer is a void pointer, so that a buffer of any type is passed without a cast.
//
// Returns, in DTAPI's order: DTAPI_E_BUF_TOO_SMALL for a size of 0;
// DTAPI_E_INVALID_TIMEOUT for a time-out of 0 or below -1; DTAPI_E_INVALID_SIZE for a
// negative size or one not a multiple of 4; DTAPI_E_INVALID_BUF for a buffer address not
// a multiple of 4; DTAPI_E_IN_USE while a ReadFrame on another thread has not returned,
// where DTAPI lets both wait; DTAPI_E_BUF_TOO_SMALL for a buffer smaller than a frame;
// DTAPI_E_NOT_SDI_MODE on ASI; DTAPI_E_TIMEOUT; and DTAPI_E_CANCELLED when the channel is
// detached meanwhile. *FrameSize is 0 after a failure from the buffer size check on.
CDTAPI_API DtapiResult DtInpChannel_ReadFrame(DtInpChannel* InpChannel, void* FrameBuffer,
                                              int* FrameSize, int TimeOut);

// ReadFrame, and the time of day at which the frame arrived, as the card stamps it in
// the frame's header with its time-of-day clock, which DTAPI_IOCONFIG_TODREFSEL selects.
// ArrivalTime may be NULL; it is zero after a failure.
CDTAPI_API DtapiResult DtInpChannel_ReadFrame2(DtInpChannel* InpChannel,
                                               void* FrameBuffer, int* FrameSize,
                                               int TimeOut, DtTimeOfDay* ArrivalTime);

// The functions below are ASI's, and give DTAPI_E_NOT_SUPPORTED on SDI.

// Reads NumBytesToRead bytes of the transport stream into Buffer, in the receive mode.
// With a time-out of 0 it waits as long as it takes, reading 1 MB at a time when more
// is asked for than the FIFO holds; otherwise it waits up to TimeOut milliseconds, or
// without a limit for -1, for all of it, and reads nothing when the time runs out.
//
// Returns, in DTAPI's order: DTAPI_OK at once for 0 bytes; DTAPI_E_INVALID_TIMEOUT for a
// time-out below -1; DTAPI_E_IN_USE while a Read on another thread has not returned,
// where DTAPI lets both wait; DTAPI_E_INVALID_SIZE for a negative size or one not a
// multiple of 4; DTAPI_E_INVALID_BUF for a buffer address not a multiple of 4;
// DTAPI_E_INVALID_SIZE, with a time-out, for more than the FIFO's 8 MB;
// DTAPI_E_TIMEOUT; and DTAPI_E_CANCELLED when the channel is detached meanwhile, which
// DTAPI does not see with a time-out of 0.
CDTAPI_API DtapiResult DtInpChannel_Read(DtInpChannel* InpChannel, void* Buffer,
                                         int NumBytesToRead, int TimeOut);

// The state of the ASI input: the packet size found, DTAPI_PCKSIZE_188, 204 or INV;
// NumInv DTAPI_NOT_SUPPORTED; ClkDet DTAPI_CLKDET_OK or FAIL; AsiLock DTAPI_ASI_INLOCK or
// 0; RateOk DTAPI_INPRATE_OK above 900 bit/s, else LOW; AsiInv DTAPI_ASIINV_NORMAL,
// INVERT, or DTAPI_NOT_SUPPORTED when unknown.
CDTAPI_API DtapiResult DtInpChannel_GetStatus(DtInpChannel* InpChannel, int* PacketSize,
                                              int* NumInv, int* ClkDet, int* AsiLock,
                                              int* RateOk, int* AsiInv);

// The rate of the transport stream, in bits a second of 188-byte packets: the card
// measures 204-byte packets with their 16 extra bytes, which are left out but in
// DTAPI_RXMODE_STRAW.
CDTAPI_API DtapiResult DtInpChannel_GetTsRateBps(DtInpChannel* InpChannel, int* TsRate);

// The count of 8b/10b code violations the card has seen.
CDTAPI_API DtapiResult DtInpChannel_GetViolCount(DtInpChannel* InpChannel,
                                                 int* ViolCount);

// How the input's polarity is taken: DTAPI_POLARITY_AUTO, NORMAL or INVERT. Another value
// gives DTAPI_E_INVALID_MODE before anything else.
CDTAPI_API DtapiResult DtInpChannel_PolarityControl(DtInpChannel* InpChannel,
                                                    int Polarity);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtOutpChannel +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// An output channel on a port of a DtPcie card, SDI or ASI by the port's I/O standard.
//
// On SDI it takes DTAPI's raw SDI frames: every line, EAV first, with 10- or 16-bit
// symbols. Write converts the frames straight into the card's DMA buffer for the port,
// without a software FIFO; the channel's frame IDs count from 0 each time it leaves idle.
// While sending, a thread of the channel keeps the signal: when the card holds less than
// a frame, it writes a black frame, and a frame a write had only partly written follows
// it. After the first frame of a run the thread waits half of that frame before it does
// so. SD, HD and 3G standards are transmitted; 4K, 8-bit symbols and active-video-only
// modes are not.
//
// On ASI it takes a transport stream, as DTAPI does: Write puts it in a FIFO of 8 MB, and
// a thread of the channel codes it into the 8b/10b symbols the card sends, at the rate
// SetTsRateBps sets, in the transmit modes DTAPI has for ASI: DTAPI_TXMODE_188, 204,
// ADD16, MIN16 and RAW, with DTAPI_TXMODE_BURST or TXONTIME. The port sends K28.5 from
// the moment the channel attaches. Double-buffered and monitor outputs that name the port
// in ParXtra[0] of their I/O direction send what it sends; the channel takes them too.
//
// A channel attaches exclusively and is configured for the port's I/O standard when it
// attaches and whenever that standard is set through the channel, switching between SDI
// and ASI as the standard does.
//
// Every function taking a DtOutpChannel returns DTAPI_E_INVALID_ARG for a null pointer,
// and DTAPI_E_NOT_ATTACHED when the channel is not attached.
//

typedef struct DtOutpChannel DtOutpChannel;

// Allocates a detached output channel. Returns NULL when memory runs out.
CDTAPI_API DtOutpChannel* DtOutpChannel_Alloc(void);

// Detaches the channel, discarding what it has not sent, and frees it. A Write or
// WriteFrame waiting on another thread returns DTAPI_E_CANCELLED first; this waits for
// that as long as it takes. NULL is allowed.
CDTAPI_API void DtOutpChannel_Free(DtOutpChannel* OutpChannel);

// Frees *OutpChannel as DtOutpChannel_Free does and sets *OutpChannel to NULL.
CDTAPI_API void DtOutpChannel_Freep(DtOutpChannel** OutpChannel);

// Attaches to a port of an attached device, numbered from 1, exclusively. The channel
// uses its own handle to the device, so the device object may be detached afterwards.
//
// Returns, in DTAPI's order: DTAPI_E_ATTACHED; DTAPI_E_DEVICE for a detached Device;
// DTAPI_E_OBSOLETE_FW or DTAPI_E_TAINTED_FW; DTAPI_E_NO_SUCH_PORT; DTAPI_E_NO_DT_OUTPUT
// for a port that cannot be, or is not configured as, an output; DTAPI_E_NOT_SUPPORTED
// for a port without an ASI/SDI transmitter; DTAPI_E_NOT_FOUND and DTAPI_E_DRIVER_INCOMP
// for a transmitter the driver does not describe or is too old for; DTAPI_E_IN_USE when
// another user has the port, or on ASI one of its slaves; and the driver's result of any
// command.
CDTAPI_API DtapiResult DtOutpChannel_AttachToPort(DtOutpChannel* OutpChannel,
                                                  DtDevice* Device, int Port);

// Codes the lines of a 4K frame over Threads threads of the library's own, 1 for the
// thread that writes, which is the default. It is DtInpChannel_SetConversionThreads the
// other way round, and what it says holds here too.
//
// What one call brings is what divides, so DtOutpChannel_WriteFrame always gets the whole
// of it and DtOutpChannel_Write gets it for as many whole lines as the call holds: a
// caller that writes a frame at a time is served, one that writes a line at a time is
// not, and neither is wrong.
//
// DTAPI_E_INVALID_ARG below 1, DTAPI_E_NOT_SUPPORTED on a channel whose signal has no
// lines to divide, and DTAPI_E_OUT_OF_MEM when the threads or their buffers cannot be
// had, after which the channel codes in the writing thread again.
CDTAPI_API DtapiResult DtOutpChannel_SetConversionThreads(DtOutpChannel* OutpChannel,
                                                          int Threads);

// Stops transmitting, discards what the channel has not sent, and clears the flags.
CDTAPI_API DtapiResult DtOutpChannel_ClearFifo(DtOutpChannel* OutpChannel);

// Detaches. With DTAPI_INSTANT_DETACH, 1, what the channel has not sent is discarded;
// with DTAPI_WAIT_UNTIL_SENT, 2, and while sending, this waits until the card has sent
// what was written, which ends when no more goes out for a second; on SDI it first writes
// a black frame after the last frame, as the card sends a frame only when data follows
// it. Both together give DTAPI_E_INVALID_FLAGS. Every mode stops transmitting. A Write or
// WriteFrame waiting on another thread returns DTAPI_E_CANCELLED; DTAPI_E_TIMEOUT when it
// has not returned after 100 ms, and the channel then stays attached and usable.
// DTAPI_E_NOT_ATTACHED when another thread detached it meanwhile.
CDTAPI_API DtapiResult DtOutpChannel_Detach(DtOutpChannel* OutpChannel, int DetachMode);

// The bytes the card has yet to send, as raw frames in the current transmit mode: the
// complete frames written and not yet taken, and what was written of the next; 0 while
// idle. Never more than the FIFO size. On ASI, as DTAPI estimates it: while holding what
// was written, while sending the FIFO and what the symbols in the card's buffers carry,
// but with DTAPI_TXMODE_TXONTIME the FIFO alone.
CDTAPI_API DtapiResult DtOutpChannel_GetFifoLoad(DtOutpChannel* OutpChannel,
                                                 int* FifoLoad);

// The largest load GetFifoLoad can report: the complete frames the channel's buffer holds
// when full, as raw frames in the current transmit mode, at least two. On a 4K port,
// where the channel does not transmit, DTAPI's FIFO size of 48 MB. On ASI 8 MB.
CDTAPI_API DtapiResult DtOutpChannel_GetFifoSize(DtOutpChannel* OutpChannel,
                                                 int* FifoSize);

// The same as GetFifoSize; on a 4K port DTAPI's maximum FIFO size of 64 MB, on ASI 8 MB.
CDTAPI_API DtapiResult DtOutpChannel_GetMaxFifoSize(DtOutpChannel* OutpChannel,
                                                    int* MaxFifoSize);

// The status flags and the latched flags: DTAPI_TX_FIFO_UFL when the channel wrote a
// black frame or the card's formatter ran out of data, and DTAPI_TX_DMA_UFL when the
// card's transmitter did. On ASI, DTAPI_TX_FIFO_UFL when the card ran out of symbols or
// stuffing inserted null packets, and DTAPI_TX_SYNC_ERR for a packet without its sync
// byte. The latched flags stay set until ClearFlags or ClearFifo.
CDTAPI_API DtapiResult DtOutpChannel_GetFlags(DtOutpChannel* OutpChannel, int* Status,
                                              int* Latched);

// Sets an I/O configuration of the channel's port, while idle (DTAPI_E_NOT_IDLE), with
// DTAPI's ParXtra0 and ParXtra1, -1 where the configuration takes none. A new SDI
// standard reconfigures the channel for it; the transmit mode is kept. A standard that
// crosses between SDI and ASI switches the channel to the other, with that side's
// default transmit mode; when the switch fails the channel is left detached. Returns
// DTAPI_E_INVALID_ARG for a combination that is no configuration, for an input
// direction, and for an output that names another port, DTAPI_IOCONFIG_DBLBUF,
// LOOPS2L3, LOOPS2TS or LOOPTHR, when ParXtra0 is no port.
CDTAPI_API DtapiResult DtOutpChannel_SetIoConfig(DtOutpChannel* OutpChannel, int Group,
                                                 int Value, int SubValue,
                                                 int64_t ParXtra0, int64_t ParXtra1);

// Reads the I/O configuration of group Group of the channel's port, as
// DtInpChannel_GetIoConfig does.
CDTAPI_API DtapiResult DtOutpChannel_GetIoConfig(DtOutpChannel* OutpChannel, int Group,
                                                 int* Value, int* SubValue,
                                                 int64_t* ParXtra0, int64_t* ParXtra1);

// DTAPI_TXCTRL_HOLD starts the card's pipeline without sending, so that what is written
// is kept; DTAPI_TXCTRL_SEND sends, and needs a frame written (DTAPI_E_INSUF_LOAD), also
// from idle, which goes through hold; DTAPI_TXCTRL_IDLE stops and discards what was
// written. In the 8-bit mode the channel holds and takes frames, but sending a frame
// fails with DTAPI_E_CONFIG_RAW_SDI, as in DTAPI; holding on a port configured for 4K
// fails with the same code. On ASI sending needs no data, but waits a few milliseconds
// for the card's burst FIFO to fill, DTAPI_E_TIMEOUT when it does not; holding refuses a
// rate that does not fit the packet size with DTAPI_E_INVALID_RATE.
CDTAPI_API DtapiResult DtOutpChannel_SetTxControl(DtOutpChannel* OutpChannel,
                                                  int TxControl);

// Sets the transmit mode. On SDI while idle: DTAPI_TXMODE_SDI_FULL, optionally with
// DTAPI_TXMODE_SDI_10B or DTAPI_TXMODE_SDI_16B, 8-bit without either; StuffMode is not
// used; any other mode gives DTAPI_E_INVALID_MODE, and a channel that is not idle
// DTAPI_E_NOT_IDLE. On ASI in any state: one of the modes above, DTAPI_E_NOT_IMPLEMENTED
// for DTAPI_TXMODE_RAWASI and DTAPI_E_INVALID_ARG for another; StuffMode 0 or 1, else
// DTAPI_E_INVALID_ARG, and not 1 with DTAPI_TXMODE_RAW, DTAPI_E_INVALID_MODE. With
// stuffing the channel keeps 50 ms of symbols in the card's buffer with null packets.
CDTAPI_API DtapiResult DtOutpChannel_SetTxMode(DtOutpChannel* OutpChannel, int TxMode,
                                               int StuffMode);

// Writes NumBytesToWrite bytes from Buffer, which is a pointer to constant data of any
// type. On SDI they are raw frames, and the stream is aligned on frames: at the start of
// each frame, bytes are skipped four at a time until they start line 1, and bytes too few
// to tell are kept for the next Write. On ASI they are the transport stream. Waits while
// the card, or on ASI the FIFO, has no room, for as long as that takes.
//
// Returns, in DTAPI's order: DTAPI_E_INVALID_SIZE for a negative size; DTAPI_E_IDLE while
// idle; DTAPI_E_INVALID_BUF for a size or a buffer address not a multiple of 4, which
// takes precedence over DTAPI_E_IDLE, and for a null buffer with bytes to write while not
// idle, where DTAPI would read it;
// DTAPI_E_IN_USE while a Write or WriteFrame on another thread has not returned, where
// DTAPI waits for it; and DTAPI_E_CANCELLED when the channel is detached meanwhile, or
// DTAPI_E_IDLE when it is set idle meanwhile.
CDTAPI_API DtapiResult DtOutpChannel_Write(DtOutpChannel* OutpChannel, const void* Buffer,
                                           int NumBytesToWrite);

// Writes one raw frame, which starts at line 1 and holds FrameSize bytes, exactly the
// size of a frame of the channel's standard in the current transmit mode. The frame goes
// into the card's buffer whole or not at all. Waits up to TimeOut milliseconds, or
// without a limit for -1, for room.
//
// Returns: DTAPI_E_INVALID_TIMEOUT for a time-out of 0 or below -1; DTAPI_E_INVALID_SIZE
// for a size that is not positive or not a multiple of 4; DTAPI_E_INVALID_BUF for a null
// frame or an address not a multiple of 4; DTAPI_E_IDLE while idle; DTAPI_E_IN_USE while
// a Write or WriteFrame on another thread has not returned; DTAPI_E_INCOMP_FRAME when a
// Write left bytes not yet in a frame, which a Write must complete or ClearFifo discard;
// DTAPI_E_INVALID_SIZE for a size other than a frame's; DTAPI_E_INVALID_FRAME for a frame
// that does not start with the EAV and line number of line 1, in SD with the EAV of a
// line in the vertical blanking of field 1; DTAPI_E_TIMEOUT; and DTAPI_E_CANCELLED when
// the channel is detached meanwhile, or DTAPI_E_IDLE when it is set idle meanwhile. On
// ASI, DTAPI_E_NOT_SDI_MODE after the checks of the channel's state.
CDTAPI_API DtapiResult DtOutpChannel_WriteFrame(DtOutpChannel* OutpChannel,
                                                const void* Frame, int FrameSize,
                                                int TimeOut);

// Clears the latched flags in Latched: DTAPI_TX_FIFO_UFL and DTAPI_TX_DMA_UFL, and on ASI
// DTAPI_TX_SYNC_ERR.
CDTAPI_API DtapiResult DtOutpChannel_ClearFlags(DtOutpChannel* OutpChannel, int Latched);

// The polarity of the ASI signal, DTAPI_TXPOL_NORMAL or DTAPI_TXPOL_INVERTED; another
// value gives DTAPI_E_INVALID_ARG, and so does DTAPI_TXPOL_INVERTED on SDI.
CDTAPI_API DtapiResult DtOutpChannel_SetTxPolarity(DtOutpChannel* OutpChannel,
                                                   int TxPolarity);

// The rate of the transport stream in bits a second of 188-byte packets, whatever the
// packet size; 10 Mbit/s after attaching. A rate of 0 or less, or one whose packets need
// more symbols than the line has, gives DTAPI_E_INVALID_RATE. On SDI both give
// DTAPI_E_NOT_SUPPORTED.
CDTAPI_API DtapiResult DtOutpChannel_GetTsRateBps(DtOutpChannel* OutpChannel,
                                                  int* TsRate);
CDTAPI_API DtapiResult DtOutpChannel_SetTsRateBps(DtOutpChannel* OutpChannel, int TsRate);

#ifdef __cplusplus
} // extern "C"
#endif
