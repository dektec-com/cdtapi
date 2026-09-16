// #*#*#*#*#*#*#*#*#*#*#*#*#*#* CDtapiLite.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Public C API for DekTec SDI interfaces
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_H
#define CDTAPILITE_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>

// CDtapiLite includes
#include "CDtapiLite_Constants.h" // Result codes and configuration constants.
#include "CDtapiLite_Version.h"   // Library version.

#ifdef __cplusplus
extern "C"
{
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Symbol export +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A consumer that links the shared library on Windows needs the import declaration, so
// the default here is "importing"; the library build itself defines CDTAPILITE_EXPORTS.
// A static build defines CDTAPILITE_STATIC and gets neither.
//

#if defined(CDTAPILITE_STATIC)
    #define CDTAPILITE_API
#elif defined(_WIN32) || defined(_WIN64)
    #if defined(CDTAPILITE_EXPORTS)
        #define CDTAPILITE_API __declspec(dllexport)
    #else
        #define CDTAPILITE_API __declspec(dllimport)
    #endif
#else
    #define CDTAPILITE_API __attribute__((visibility("default")))
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Global functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Returns the library version as a string, for example "1.0.0". The returned pointer is
// static storage owned by the library and must not be freed.
CDTAPILITE_API const char* DtapiLiteGetVersion(void);

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
CDTAPILITE_API unsigned int DtapiVidStd2IoStd(int VideoStandard, int LinkStandard,
                                              int* Value, int* SubValue);

// Returns the name of a result code's macro, for example "DTAPI_E_IN_USE", or "???" for a
// value that is no result code. The names are those DTAPI gives: of each pair of names
// for one value the first, DTAPI_E_NO_DT_INPUT and DTAPI_E_NO_DT_OUTPUT, and "???" for
// DTAPI_E_INVALID_NUM_INPUTS, DTAPI_E_DISABLED and DTAPI_E_EXCEPTION, which DTAPI does
// not name. The returned string is static and must not be freed.
CDTAPILITE_API const char* DtapiResult2Str(unsigned int Result);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Time of day +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A time from a device's time-of-day clock.
typedef struct DtTimeOfDay
{
    unsigned int Seconds;     // Integer number of seconds part of the TOD time
    unsigned int Nanoseconds; // Number of nanoseconds part of the TOD time
} DtTimeOfDay;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtHwFuncDesc +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// One hardware function: a public port of a device.
//

// The sizes of its two strings, MAX_DEVICE_NAME_SIZE and MAX_DEVICE_DESC_SIZE, are in
// CDtapiLite_Constants.h.

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
CDTAPILITE_API unsigned int DtapiHwFuncScan(int NumEntries, int* NumEntriesResult,
                                            DtHwFuncDesc* HwFuncs);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtDeviceDesc +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// One device, as DTAPI's DtDeviceDesc describes it, with its fields under DTAPI's names
// without the m_ prefix. An addition of CDtapiLite: CDTAPI.h has no device scan, so a
// program that uses it cannot be built against CDTAPI.h.
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
    int Category;                          // DTAPI_CAT_ value
    int64_t Serial;                        // Unique serial number of the device
    int PciBusNumber;                      // PCI bus number
    int SlotNumber;                        // PCI slot number
    int UsbAddress;                        // USB address; 0 for a PCIe device
    int TypeNumber;                        // Device type number, 2178 for a DTA-2178
    int SubType;                           // Device subtype: 0 for none, 1 for A, ...
    int DeviceId;                          // PCI device ID
    int VendorId;                          // PCI vendor ID
    int SubsystemId;                       // PCI subsystem ID
    int SubVendorId;                       // PCI subsystem vendor ID
    int NumHwFuncs;                        // Number of hardware functions: the ports
    int HardwareRevision;                  // Hardware revision, such as 302 for 3.2
    int FirmwareVersion;                   // Firmware version
    int FirmwareVariant;                   // Firmware variant
    DtFirmwareStatus FirmwareStatus;       // Firmware status
    DtFwBuildDateTime FwBuildDate;         // Firmware build date and time
    int NumDtInpChan;                      // Number of ports that are inputs
    int NumDtOutpChan;                     // Number of ports that are outputs
    int NumPorts;                          // Number of physical ports
    unsigned char Ip[4];                   // IPv4 address; DTE-31xx only
    unsigned char IpV6[MAX_IPV6_ADDR][16]; // IPv6 addresses; DTE-31xx only
    unsigned char MacAddr[6];              // MAC address; DTE-31xx only
    int PcieNumLanes;                      // Number of PCIe lanes in use
    int PcieMaxLanes;                      // Maximum number of PCIe lanes
    int PcieLinkSpeed;                     // PCIe generation of the link
    int PcieMaxSpeed;                      // PCIe generation the link can reach
    int PcieMaxPayloadSize;                // Maximum PCIe payload size in bytes
    int PcieMaxReadRequestSize;            // Maximum PCIe read request size in bytes
    int PcieMaxSlotPower;                  // Maximum PCIe slot power in milliwatts
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
CDTAPILITE_API unsigned int DtapiDeviceScan(int NumEntries, int* NumEntriesResult,
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
    unsigned int Vpid;         // Raw VPID, 0 if not available
    unsigned int Vpid2;        // Raw VPID of 3G level B's second channel; always 0
    DtAspectRatio AspectRatio; // From the VPID: 4:3 or 16:9; unknown without one

    // What the input carries before the hardware processes it: 12G or 6G 4K on one link
    // that the port scales to 3G is VidStd 1080p and LinkStd -1, but 2160p here.
    int OriginalVidStd;
    int OriginalLinkStd;
} DtDetVidStd;

typedef struct DtDeviceC DtDevice;

// Allocates a detached device object. Returns NULL when memory runs out.
CDTAPILITE_API DtDevice* DtDevice_Alloc(void);

// Detaches the device object if it is attached, and frees it. NULL is allowed.
CDTAPILITE_API void DtDevice_Free(DtDevice* Device);

// Frees *Device as DtDevice_Free does and sets *Device to NULL. NULL is allowed.
CDTAPILITE_API void DtDevice_Freep(DtDevice** Device);

// Attaches to the device with this serial number. Returns DTAPI_E_ATTACHED when already
// attached, DTAPI_E_DRIVER_INCOMP for a driver that is too old, and
// DTAPI_E_NO_SUCH_DEVICE when no device has the serial number. Succeeds with
// DTAPI_OK_OBSOLETE_FW or DTAPI_OK_TAINTED_FW when the device's firmware is obsolete or
// tainted.
CDTAPILITE_API unsigned int DtDevice_AttachToSerial(DtDevice* Device,
                                                    int64_t SerialNumber);

// Detaches from the device.
CDTAPILITE_API unsigned int DtDevice_Detach(DtDevice* Device);

// Sets one I/O configuration of a port, numbered from 1. Returns DTAPI_E_OBSOLETE_FW or
// DTAPI_E_TAINTED_FW for a device whose firmware is, DTAPI_E_NO_SUCH_PORT for a port the
// device does not have, and DTAPI_E_INVALID_ARG for a combination of group, value and
// sub-value that is no configuration; otherwise the driver's result.
CDTAPILITE_API unsigned int DtDevice_SetIoConfig(DtDevice* Device, int Port, int Group,
                                                 int Value, int SubValue);

// Makes a port an output: DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT.
CDTAPILITE_API unsigned int DtDevice_SetToOutput(DtDevice* Device, int Port);

// Makes a port an input: DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT.
CDTAPILITE_API unsigned int DtDevice_SetToInput(DtDevice* Device, int Port);

// Waits until a video standard is detected on a port, numbered from 1, and returns it.
// Detection is retried every 5 ms, without a time limit, also while it fails. Returns at
// once with every field unknown for a null Device and for a port that detection cannot
// be attached to; see DtDevice_DetectVidStd.
CDTAPILITE_API DtDetVidStd DtDevice_WaitForSignal(DtDevice* Device, int Port);

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
CDTAPILITE_API unsigned int DtDevice_DetectVidStd(DtDevice* Device, int Port,
                                                  int* VidStd);

// DtDevice_WaitForSignal with a time limit and a result. Waits up to TimeoutMs
// milliseconds, retrying detection every 5 ms; 0 tries once, and a negative TimeoutMs
// waits without a limit. Returns DTAPI_OK with *Result filled in when a standard is
// detected, DTAPI_E_TIMEOUT with every field of *Result unknown when none is within the
// time, and DTAPI_E_INVALID_ARG for a null Device or Result. The reasons attaching can
// fail, as DtDevice_DetectVidStd lists them, are returned at once.
CDTAPILITE_API unsigned int DtDevice_WaitForSignalTimeout(DtDevice* Device, int Port,
                                                          int TimeoutMs,
                                                          DtDetVidStd* Result);

// Reads the device's time-of-day clock. *TimeOfDay is zero when this fails.
CDTAPILITE_API unsigned int DtDevice_GetTimeOfDay(const DtDevice* Device,
                                                  DtTimeOfDay* TimeOfDay);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtInpChannel +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// An SDI input channel on a port of a DtPcie card, delivering DTAPI's raw SDI frames:
// every line, EAV first, with 8-, 10- or 16-bit symbols. It works directly on the DMA
// ring of the card's receive channel, without a thread of its own: ReadFrame waits for
// the card's format events and assembles each frame straight into the caller's buffer.
//
// SD, HD and 3G standards are received; 4K, ASI, 8-bit symbols, active-video-only and
// compressed modes are not. A channel attaches exclusively and is configured for the
// port's I/O standard when it attaches and whenever that standard is set through the
// channel.
//
// Every function taking a DtInpChannel returns DTAPI_E_INVALID_ARG for a null pointer,
// and DTAPI_E_NOT_ATTACHED when the channel is not attached.
//

typedef struct DtInpChannelC DtInpChannel;

// Allocates a detached input channel. Returns NULL when memory runs out.
CDTAPILITE_API DtInpChannel* DtInpChannel_Alloc(void);

// Detaches the channel, discarding what it has received, and frees it. NULL is allowed.
CDTAPILITE_API void DtInpChannel_Free(DtInpChannel* InpChannel);

// Frees *InpChannel as DtInpChannel_Free does and sets *InpChannel to NULL.
CDTAPILITE_API void DtInpChannel_Freep(DtInpChannel** InpChannel);

// Attaches to a port of an attached device, numbered from 1, exclusively. The channel
// uses its own handle to the device, so the device object may be detached afterwards.
//
// Returns, in DTAPI's order: DTAPI_E_ATTACHED; DTAPI_E_DEVICE for a detached Device;
// DTAPI_E_OBSOLETE_FW or DTAPI_E_TAINTED_FW; DTAPI_E_NO_SUCH_PORT; DTAPI_E_NO_DT_INPUT
// for a port that cannot be, or is not configured as, an input; DTAPI_E_NOT_SUPPORTED for
// a port without an ASI/SDI receiver and for an ASI I/O standard;
// DTAPI_E_NOT_FOUND and DTAPI_E_DRIVER_INCOMP for a receiver the driver does not describe
// or is too old for; DTAPI_E_IN_USE when another user has the port; and the driver's
// result of any command.
CDTAPILITE_API unsigned int DtInpChannel_AttachToPort(DtInpChannel* InpChannel,
                                                      DtDevice* Device, int Port);

// Stops receiving and discards what the channel holds, and clears the overflow flag.
CDTAPILITE_API unsigned int DtInpChannel_ClearFifo(DtInpChannel* InpChannel);

// Clears the latched flags in Latched; DTAPI_RX_FIFO_OVF is the only one.
CDTAPILITE_API unsigned int DtInpChannel_ClearFlags(DtInpChannel* InpChannel,
                                                    int Latched);

// Detaches. With DTAPI_INSTANT_DETACH, 1, what the channel holds is discarded first; both
// modes stop receiving. A ReadFrame waiting on another thread returns DTAPI_E_CANCELLED;
// DTAPI_E_TIMEOUT when it has not returned after 100 ms, and the channel stays attached.
CDTAPILITE_API unsigned int DtInpChannel_Detach(DtInpChannel* InpChannel, int DetachMode);

// Detects the I/O standard of the signal on the port: the value and sub-value that
// DtapiVidStd2IoStd gives for the detected video standard. Fails as that function does
// when no standard is detected.
CDTAPILITE_API unsigned int DtInpChannel_DetectIoStd(DtInpChannel* InpChannel, int* Value,
                                                     int* SubValue);

// The bytes of complete frames waiting to be read, as raw frames in the current receive
// mode; 0 while not receiving or not in sync.
CDTAPILITE_API unsigned int DtInpChannel_GetFifoLoad(DtInpChannel* InpChannel,
                                                     int* FifoLoad);

// DTAPI's FIFO size for SDI input, 48 MB. The largest frame a channel reads is smaller.
CDTAPILITE_API unsigned int DtInpChannel_GetMaxFifoSize(DtInpChannel* InpChannel,
                                                        int* MaxFifoSize);

// The status flags and the latched flags: DTAPI_RX_FIFO_OVF when the card's ring for the
// channel was full, which loses frames.
CDTAPILITE_API unsigned int DtInpChannel_GetFlags(DtInpChannel* InpChannel, int* Flags,
                                                  int* Latched);

// Sets an I/O configuration of the channel's port, while not receiving
// (DTAPI_E_NOT_IDLE). A new SDI standard reconfigures the channel for it. Returns
// DTAPI_E_INVALID_ARG for a combination that is no configuration and for an output
// direction, and DTAPI_E_NOT_SUPPORTED for 6G, 12G, ASI and any other direction.
CDTAPILITE_API unsigned int DtInpChannel_SetIoConfig(DtInpChannel* InpChannel, int Group,
                                                     int Value, int SubValue);

// DTAPI_RXCTRL_RCV starts receiving from the next frame on; DTAPI_RXCTRL_IDLE stops.
// Receiving in the 8-bit mode, or on a port configured for 4K, fails with
// DTAPI_E_CONFIG_RAW_SDI, as it does in DTAPI.
CDTAPILITE_API unsigned int DtInpChannel_SetRxControl(DtInpChannel* InpChannel,
                                                      int RxControl);

// Sets the receive mode while not receiving: DTAPI_RXMODE_SDI_FULL, optionally with
// DTAPI_RXMODE_SDI_10B or DTAPI_RXMODE_SDI_16B, 8-bit without either. Any other mode
// gives DTAPI_E_INVALID_MODE; receiving gives DTAPI_E_NOT_IDLE.
CDTAPILITE_API unsigned int DtInpChannel_SetRxMode(DtInpChannel* InpChannel, int RxMode);

// Reads one frame into FrameBuffer, which holds *FrameSize bytes, and sets *FrameSize to
// the frame's size. Waits up to TimeOut milliseconds, or without a limit for -1.
//
// Returns, in DTAPI's order: DTAPI_E_BUF_TOO_SMALL for a size of 0;
// DTAPI_E_INVALID_TIMEOUT for a time-out of 0 or below -1; DTAPI_E_INVALID_SIZE for a
// negative size or one not a multiple of 4; DTAPI_E_INVALID_BUF for a buffer address not
// a multiple of 4; DTAPI_E_BUF_TOO_SMALL for a buffer smaller than a frame;
// DTAPI_E_TIMEOUT; and DTAPI_E_CANCELLED when the channel is detached meanwhile.
// *FrameSize is 0 after a failure from the buffer size check on.
CDTAPILITE_API unsigned int DtInpChannel_ReadFrame(DtInpChannel* InpChannel,
                                                   char* FrameBuffer, int* FrameSize,
                                                   int TimeOut);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // CDTAPILITE_H
