// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* cdtapi.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Public C API for DekTec SDI, DVB-ASI and SMPTE ST 2110 interfaces
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CDTAPI is a C library for DekTec PCIe cards with SDI, DVB-ASI and SMPTE ST 2110 ports.
// It talks to the card's driver directly. Its functions, types and constants have the
// names and behaviour of DekTec's DTAPI, so that a program moves between the two easily.
//
// A program finds the cards with DtapiHwFuncScan() or DtapiDeviceScan(), attaches a
// DtDevice to one, and then a DtInpChannel or DtOutpChannel to a port to receive or send
// SDI or ASI. ST 2110 is in cdtapi_avfifo.h.

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
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
// A program that links a static library, as the SDK ships it, needs to define nothing.
//
// A program that links the DLL on Windows may define CDTAPI_DLL, so that the compiler
// calls the DLL directly rather than through a stub the linker adds; it works without it
// too. The library's own build defines CDTAPI_EXPORTS.
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
// A function that can fail returns DTAPI_OK or one of the DTAPI_E_ codes of
// cdtapi_constants.h. A result below DTAPI_E is a success, possibly with a warning, so
// test for failure with Result >= DTAPI_E. The type matches DTAPI's DTAPI_RESULT.
//

typedef uint32_t DtapiResult;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Global functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Returns the version of the library, e.g. "6.14.4". The first two numbers are those of
// the DTAPI version it matches; the third is CDTAPI's own. Do not free the string.
CDTAPI_API const char* DtapiGetVersion(void);

// Returns whether the library has the NMOS bridge of cdtapi_nmos.h (built with
// CDTAPI_WITH_NMOS). Without it, the bridge's functions return DTAPI_E_NOT_SUPPORTED.
CDTAPI_API bool DtapiHasNmos(void);

// Returns the name of a result code, e.g. "DTAPI_E_IN_USE", for messages. Do not free the
// string.
//
// A code with two names gets the first one: DTAPI_E_NO_DT_INPUT, not
// DTAPI_E_NO_DT_OUTPUT. An unknown code gets "???", as do DTAPI_E_INVALID_NUM_INPUTS,
// DTAPI_E_DISABLED and DTAPI_E_EXCEPTION.
CDTAPI_API const char* DtapiResult2Str(DtapiResult Result);

// Finds the I/O standard that sets a port to a video standard: the Value and SubValue
// to pass to DtDevice_SetIoConfig() with DTAPI_IOCONFIG_IOSTD.
//
// LinkStandard says how a 4K picture is carried, and is -1 for any other standard:
//   0  four 3G links, two-sample interleave (SMPTE ST 425-5)
//   1  four 3G links, square division (SMPTE ST 425-5 annex B)
//   2  one 6G link
//   3  one 12G link
// For four links, or for one link of the wrong rate for the frame rate, the result is
// the I/O standard of one link: HD-SDI or 3G-SDI with the 1080p standard of the same
// rate. One link carries 4K up to 30 frames per second as 6G-SDI, and from 50 as 12G-SDI.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG      Value or SubValue is NULL; neither is changed
//   DTAPI_E_INVALID_LINKSTD  LinkStandard does not suit VideoStandard
//   DTAPI_E_INVALID_VIDSTD   VideoStandard is unknown
// For the last two, *Value and *SubValue are -1.
CDTAPI_API DtapiResult DtapiVidStd2IoStd(int VideoStandard, int LinkStandard, int* Value,
                                         int* SubValue);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Time of day +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A timestamp from a device's time-of-day clock.
typedef struct DtTimeOfDay
{
    uint32_t Seconds;     // Whole seconds
    uint32_t Nanoseconds; // Nanoseconds within the second, below 1e9
} DtTimeOfDay;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtHwFuncDesc +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A hardware function is one port of a card, as a program sees it.
//

// The sizes of the strings, MAX_DEVICE_NAME_SIZE and MAX_DEVICE_DESC_SIZE, are in
// cdtapi_constants.h.

// The most IPv6 addresses a DtHwFuncDesc or DtDeviceDesc holds.
#define MAX_IPV6_ADDR 3

// One port of a card, as DtapiHwFuncScan() describes it.
typedef struct DtHwFuncDesc
{
    char DeviceName[MAX_DEVICE_NAME_SIZE];  // Serial and port, e.g. "2178000001:1"
    char Description[MAX_DEVICE_DESC_SIZE]; // Type and port, e.g. "DTA-2178 port 1"
    int64_t SerialNumber;                   // Of the card
    int Port;                               // Counted from 1
    bool IsSdi;                             // The port can carry SDI, of any rate
    bool IsAvFifo; // The port can carry ST 2110, through an AV FIFO
    bool IsInput;  // The port can be an input
    bool IsOutput; // The port can be an output
    bool IsAsi;    // The port can carry DVB-ASI

    // The addresses of an IP port, as the operating system had them at the time of the
    // scan; all zero for another port, and for an IP port whose network driver is not
    // installed. An address the port does not have is zero.
    uint8_t Ip[4];                   // IPv4 address
    uint8_t IpV6[MAX_IPV6_ADDR][16]; // IPv6 addresses: link-local, site-local and global,
                                     // in that order, from the first entry on
    uint8_t MacAddr[6];              // MAC address
} DtHwFuncDesc;

// Lists the ports of all DekTec cards in the system, one DtHwFuncDesc per port, in the
// order the driver numbers the cards.
//
// HwFuncs has room for NumEntries descriptors. *NumEntriesResult gets the number of
// ports, also when they do not fit. To ask for the number only, pass 0 and NULL. A card
// that cannot be attached, e.g. because its driver is too old, is left out.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_BUF_TOO_SMALL  more ports than NumEntries; HwFuncs is not changed
//   DTAPI_E_INVALID_ARG    NumEntriesResult is NULL, or NumEntries is negative
//   DTAPI_E_INVALID_BUF    HwFuncs is NULL and NumEntries is not 0
//   DTAPI_E_OUT_OF_MEM     not enough memory
// After DTAPI_OK the entries after the last port are zero, with DeviceName "0:0" and
// Description "DTA-0 port 0".
CDTAPI_API DtapiResult DtapiHwFuncScan(int NumEntries, int* NumEntriesResult,
                                       DtHwFuncDesc* HwFuncs);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtDeviceDesc +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A device is one DekTec card. DtDeviceDesc has DTAPI's fields, named without the m_
// prefix.
//

// The kinds of device, for DtDeviceDesc's Category.
#define DTAPI_CAT_ALL -1 // All devices
#define DTAPI_CAT_PCI 0  // PCI or PCI-Express device
#define DTAPI_CAT_USB 1  // USB-2 or USB-3 device
#define DTAPI_CAT_NW 2   // Network device
#define DTAPI_CAT_IP 3   // Network appliance: DTE-31xx
#define DTAPI_CAT_NIC 4  // Non-DekTec network card
#define DTAPI_CAT_NWAP 5 // Network Advanced Protocol (VLAN device)

// How a card's firmware relates to the versions the driver supports.
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

// When a card's firmware was built.
typedef struct DtFwBuildDateTime
{
    int Year;
    int Month;
    int Day;
    int Hour;
    int Minute;
} DtFwBuildDateTime;

// One card, as DtapiDeviceScan() describes it.
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
    int NumDtInpChan;                // Ports that can be inputs; see DtapiDeviceScan()
    int NumDtOutpChan;               // Ports that can be outputs; see DtapiDeviceScan()
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

// Lists all DekTec cards in the system, one DtDeviceDesc per card, in the order the
// driver numbers them.
//
// DvcDescArr has room for NumEntries descriptors. *NumEntriesResult gets the number of
// cards, also when they do not fit. To ask for the number only, pass 0 and NULL. A card
// that cannot be attached, e.g. because its driver is too old, is left out.
//
// NumDtInpChan and NumDtOutpChan count a port that can only be an input or only an
// output as that. An IP port counts as both; another port counts by its current
// direction. If reading a port's direction fails, the count stops at that port.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_BUF_TOO_SMALL  more cards than NumEntries; the first NumEntries are filled
//   DTAPI_E_INVALID_ARG    NumEntriesResult is NULL, or NumEntries is negative
//   DTAPI_E_INVALID_BUF    DvcDescArr is NULL and NumEntries is not 0
CDTAPI_API DtapiResult DtapiDeviceScan(int NumEntries, int* NumEntriesResult,
                                       DtDeviceDesc* DvcDescArr);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtDevice +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A DtDevice gives access to one DekTec card: its ports' configuration, its clocks and
// its genlock. A program creates one with DtDevice_Alloc(), attaches it to a card by
// serial number, and passes it to the channels and FIFOs it uses.
//
// Every function that takes a DtDevice returns DTAPI_E_INVALID_ARG when it is NULL, and
// DTAPI_E_NOT_ATTACHED when it needs a card and the DtDevice is not attached, unless its
// comment says otherwise.
//

// The aspect ratio of a picture, as a VPID gives it.
typedef enum DtAspectRatio
{
    DT_AR_UNKNOWN, // Unknown aspect ratio
    DT_AR_4_3,     // 4x3
    DT_AR_16_9,    // 16x9
    DT_AR_14_9     // 14x9
} DtAspectRatio;

// What a port detected on its input: the video standard and how it is carried.
typedef struct DtDetVidStd
{
    int VidStd;                // A DTAPI_VIDSTD_ code; DTAPI_VIDSTD_UNKNOWN for none
    int LinkStd;               // How 4K is carried, 0 to 3, as for DtapiVidStd2IoStd();
                               // -1 for other video
    int LinkNr;                // This link's number in the VPID, from 1; -1 without one
    uint32_t Vpid;             // The VPID as received; 0 without one
    uint32_t Vpid2;            // Always 0: the VPID of a level-B link's second channel,
                               // which is not received
    DtAspectRatio AspectRatio; // From the VPID; DT_AR_UNKNOWN without one

    // The signal as it arrives at the port, before the card converts it. E.g. 4K on one
    // 12G link that the port scales down to 3G is VidStd 1080p and LinkStd -1, but 2160p
    // here.
    int OriginalVidStd;
    int OriginalLinkStd;
} DtDetVidStd;

// The state of a card's genlock: whether it is locked to its reference input.
typedef struct DtGenlockState
{
    int State;            // A DTAPI_GENL_ value
    int RefVidStd;        // The video standard the reference is configured to
    int DetVidStd;        // The video standard detected at the reference input
    bool TofTimeValid;    // True when TofTime holds a time
    DtTimeOfDay TofTime;  // When the reference input last started a frame
    int TimeSinceLastTof; // Nanoseconds since then
    int64_t RefFrameNum;  // The number of that frame
} DtGenlockState;

// One setting of a port, e.g. its direction or its I/O standard. The codes are the
// DTAPI_IOCONFIG_ constants; -1 is none.
typedef struct DtIoConfig
{
    int Port;     // Counted from 1
    int Group;    // What is set, e.g. DTAPI_IOCONFIG_IODIR
    int Value;    // What it is set to, e.g. DTAPI_IOCONFIG_INPUT
    int SubValue; // A refinement of Value, e.g. a video standard
    // Extra parameters of some values; -1 when not used:
    // ParXtra[0]  the port (from 1) that a double-buffered, loop-through or monitor
    //             output, or a shared-antenna input, takes its signal from
    // ParXtra[1]  the ISI of a loop-through of a transport stream
    int64_t ParXtra[2];
} DtIoConfig;

// The state of a card's time-of-day clock, which times the frames it sends and
// receives.
typedef struct DtTimeOfDayState
{
    int State;                // A DTAPI_TODCLK_ value
    int TodReference;         // What the clock follows: DTAPI_TODREF_INTERNAL or
                              // DTAPI_TODREF_STEADYCLOCK
    int RefDeviation;         // How far the reference is off from the clock, in ppm
    DtTimeOfDay TodTimestamp; // The clock's time
    DtTimeOfDay RefTimestamp; // The reference's time at the same moment
} DtTimeOfDayState;

// One transmit clock of a card. The card's ASI and SDI outputs run on these clocks, and
// a program can set a clock a little faster or slower, in ppm.
typedef struct DtTxClockProperties
{
    int TxClockId;      // Pass it to the DtDevice_*TxClock* functions
    int ClockType;      // DTAPI_TXCLK_FRACTIONAL or DTAPI_TXCLK_NON_FRACTIONAL
    double Frequency;   // The nominal frequency, in Hz
    double RangePpm;    // How far the clock can be set off either way, in ppm
    double StepSizePpm; // The smallest change, approximately, in ppm
    int NumPorts;       // The number of entries in Ports
    int Ports[DTAPI_TXCLK_MAX_PORTS]; // The ports that run on the clock, from 1
} DtTxClockProperties;

typedef struct DtDevice DtDevice;

// Creates a DtDevice, not yet attached to a card. Returns NULL when there is not enough
// memory.
CDTAPI_API DtDevice* DtDevice_Alloc(void);

// Attaches Device to the card with serial number SerialNumber.
//
// Returns DTAPI_OK, or:
//   DTAPI_OK_OBSOLETE_FW    attached, but the card's firmware is obsolete
//   DTAPI_OK_TAINTED_FW     attached, but the card's firmware is an unsupported version
//   DTAPI_E_ATTACHED        Device is already attached
//   DTAPI_E_DRIVER_INCOMP   the driver is too old
//   DTAPI_E_NO_SUCH_DEVICE  no card has this serial number
//   DTAPI_E_OUT_OF_MEM      not enough memory
CDTAPI_API DtapiResult DtDevice_AttachToSerial(DtDevice* Device, int64_t SerialNumber);

// Detaches Device from its card, so that it can be attached to another.
CDTAPI_API DtapiResult DtDevice_Detach(DtDevice* Device);

// Detects the video standard of the SDI signal on input port Port (from 1), and sets
// *VidStd to its DTAPI_VIDSTD_ code. When there is no locked signal, or it matches no
// standard, *VidStd is DTAPI_VIDSTD_UNKNOWN. *VidStd is set only when the result is
// DTAPI_OK.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG    Device or VidStd is NULL
//   DTAPI_E_DEVICE         Device is not attached
//   DTAPI_E_OBSOLETE_FW    the card's firmware is obsolete
//   DTAPI_E_TAINTED_FW     the card's firmware is an unsupported version
//   DTAPI_E_NO_SUCH_PORT   the card has no such port
//   DTAPI_E_NOT_SUPPORTED  the port cannot be an input, or lacks CAP_MATRIX2
//   DTAPI_E_NOT_FOUND      the driver has no SDI receiver for the port
//   DTAPI_E_DRIVER_INCOMP  the driver is older than 1.4.0.111
//   DTAPI_E_INVALID_MODE   the port is not set to be an input
// and other errors of the driver when it reads the receiver.
CDTAPI_API DtapiResult DtDevice_DetectVidStd(DtDevice* Device, int Port, int* VidStd);

// Detaches Device if it is attached, and frees it. NULL does nothing.
CDTAPI_API void DtDevice_Free(DtDevice* Device);

// Frees *Device, as DtDevice_Free() does, and sets *Device to NULL. NULL does nothing.
CDTAPI_API void DtDevice_Freep(DtDevice** Device);

// Fills *State with the state of the card's genlock. A card that runs on its own clock,
// without a reference, reports DTAPI_GENL_LOCKED.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG    Device or State is NULL; *State is not changed
//   DTAPI_E_NOT_ATTACHED   Device is not attached
//   DTAPI_E_NOT_SUPPORTED  the card has no genlock
//   DTAPI_E_DRIVER_INCOMP  the driver is too old for genlock
// and the errors of the driver. After an error other than the first, *State is zero.
CDTAPI_API DtapiResult DtDevice_GetGenlockState(const DtDevice* Device,
                                                DtGenlockState* State);

// Reads settings of ports. For each of the Count entries of Configs, the program sets
// Port and Group, and the function fills in Value, SubValue and ParXtra; those stay -1
// for an entry it cannot read. A Count of 0 does nothing.
//
// Returns DTAPI_OK, or the error of the first entry that fails:
//   DTAPI_E_INVALID_ARG    Count is negative, Configs is NULL while Count is above 0, or
//                          Group is neither a group nor a yes/no capability
//   DTAPI_E_NO_SUCH_PORT   the card has no such port
//   DTAPI_E_OBSOLETE_FW    the card's firmware is obsolete
//   DTAPI_E_TAINTED_FW     the card's firmware is an unsupported version
//   DTAPI_E_NOT_SUPPORTED  the port has no capability of Group
// or an error of the driver.
CDTAPI_API DtapiResult DtDevice_GetIoConfig(DtDevice* Device, DtIoConfig* Configs,
                                            int Count);

// Reads the time of the card's time-of-day clock into *TimeOfDay.
//
// Returns DTAPI_OK, DTAPI_E_INVALID_ARG when Device or TimeOfDay is NULL (*TimeOfDay is
// then not changed), or another error, after which *TimeOfDay is zero.
CDTAPI_API DtapiResult DtDevice_GetTimeOfDay(const DtDevice* Device,
                                             DtTimeOfDay* TimeOfDay);

// Fills *State with the state of the card's time-of-day clock.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG    Device or State is NULL; *State is not changed
//   DTAPI_E_NOT_ATTACHED   Device is not attached
//   DTAPI_E_NOT_SUPPORTED  the card's clock cannot report its state
//   DTAPI_E_DRIVER_INCOMP  the driver is too old for it
// and the errors of the driver. After an error other than the first, *State is zero.
CDTAPI_API DtapiResult DtDevice_GetTimeOfDayState(const DtDevice* Device,
                                                  DtTimeOfDayState* State);

// Reads the counter of transmit clock TxClockId into *TxClockCount. The counter counts
// the clock's periods and wraps around at 32 bits.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG    Device or TxClockCount is NULL; *TxClockCount is not changed
//   DTAPI_E_NOT_ATTACHED   Device is not attached
//   DTAPI_E_NOT_SUPPORTED  the card has no transmit clocks, or no counter for this kind
//                          of clock
//   DTAPI_E_DRIVER_INCOMP  the driver is too old for them
//   DTAPI_E_NOT_FOUND      the card has no clock TxClockId
// and the errors of the driver. After an error other than the first, *TxClockCount is 0.
CDTAPI_API DtapiResult DtDevice_GetTxClockCount(const DtDevice* Device, int TxClockId,
                                                uint32_t* TxClockCount);

// Reads how far transmit clock TxClockId is set off its nominal frequency, in ppm, into
// *OffsetPpm.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG    Device or OffsetPpm is NULL (*OffsetPpm is not changed),
//                          TxClockId is negative, or the card has no such clock
//   DTAPI_E_NOT_ATTACHED   Device is not attached
//   DTAPI_E_NOT_SUPPORTED  the card has no transmit clocks
//   DTAPI_E_DRIVER_INCOMP  the driver is too old for them
// and the errors of the driver. After an error other than a NULL, *OffsetPpm is 0.
CDTAPI_API DtapiResult DtDevice_GetTxClockOffset(const DtDevice* Device, int TxClockId,
                                                 double* OffsetPpm);

// Lists the card's transmit clocks, one DtTxClockProperties per clock.
//
// Props has room for NumEntries entries. *NumEntriesResult gets the number of clocks,
// also when they do not fit. To ask for the number only, pass 0 and NULL.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG    Device or NumEntriesResult is NULL, or NumEntries is negative
//   DTAPI_E_INVALID_BUF    Props is NULL and NumEntries is not 0
//   DTAPI_E_NOT_ATTACHED   Device is not attached
//   DTAPI_E_NOT_SUPPORTED  the card has no transmit clocks
//   DTAPI_E_DRIVER_INCOMP  the driver is too old for them
//   DTAPI_E_BUF_TOO_SMALL  more clocks than NumEntries; Props is not changed
//   DTAPI_E_OUT_OF_MEM     not enough memory
// and the errors of the driver. After an error other than DTAPI_E_BUF_TOO_SMALL,
// *NumEntriesResult is 0.
CDTAPI_API DtapiResult DtDevice_GetTxClockProperties(const DtDevice* Device,
                                                     int NumEntries,
                                                     int* NumEntriesResult,
                                                     DtTxClockProperties* Props);

// Changes settings of ports: the Count entries of Configs. All entries are checked
// first, and the driver then applies them all or none, so that a port and the ports that
// follow it change together. A Count of 0 does nothing.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG   Count is negative, Configs is NULL while Count is above 0, or
//                         an entry's Group, Value and SubValue are not a valid setting
//   DTAPI_E_OBSOLETE_FW   the card's firmware is obsolete
//   DTAPI_E_TAINTED_FW    the card's firmware is an unsupported version
//   DTAPI_E_NO_SUCH_PORT  an entry's port does not exist
//   DTAPI_E_INVALID_ISI   an ISI is outside 0 to 255
// or an error of the driver. For the entries, the error is that of the first that
// fails.
CDTAPI_API DtapiResult DtDevice_SetIoConfig(DtDevice* Device, const DtIoConfig* Configs,
                                            int Count);

// Makes port Port (from 1) an input. Short for DtDevice_SetIoConfig() with
// DTAPI_IOCONFIG_IODIR and DTAPI_IOCONFIG_INPUT.
CDTAPI_API DtapiResult DtDevice_SetToInput(DtDevice* Device, int Port);

// Makes port Port (from 1) an output. Short for DtDevice_SetIoConfig() with
// DTAPI_IOCONFIG_IODIR and DTAPI_IOCONFIG_OUTPUT.
CDTAPI_API DtapiResult DtDevice_SetToOutput(DtDevice* Device, int Port);

// Sets transmit clock TxClockId OffsetPpm off its nominal frequency, rounded to a part
// per trillion. The clock belongs to the card: the change holds for every output on it,
// in every program, and stays after the program ends. It cannot be set while the card
// is genlocked.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG    Device is NULL, TxClockId is negative or not a clock of the
//                          card, or OffsetPpm is not finite or beyond the clock's range
//   DTAPI_E_NOT_ATTACHED   Device is not attached
//   DTAPI_E_NOT_SUPPORTED  the card has no transmit clocks
//   DTAPI_E_DRIVER_INCOMP  the driver is too old for them
//   DTAPI_E_IN_USE         the card is genlocked
// and the errors of the driver.
CDTAPI_API DtapiResult DtDevice_SetTxClockOffset(DtDevice* Device, int TxClockId,
                                                 double OffsetPpm);

// Waits until a video standard is detected on input port Port (from 1), and returns
// what was detected. It tries every 5 ms, without a time limit. When Device is NULL, or
// the port cannot detect a standard at all (see DtDevice_DetectVidStd()), it returns at
// once, with every field unknown. DtDevice_WaitForSignalTimeout() has a time limit.
CDTAPI_API DtDetVidStd DtDevice_WaitForSignal(DtDevice* Device, int Port);

// Waits up to TimeoutMs milliseconds until a video standard is detected on input port
// Port (from 1), and fills *Result with what was detected. It tries every 5 ms. A
// TimeoutMs of 0 tries once; a negative one waits without a limit.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_TIMEOUT      nothing detected in time; every field of *Result is unknown
//   DTAPI_E_INVALID_ARG  Device or Result is NULL
// or, at once, the errors of DtDevice_DetectVidStd() that mean the port cannot detect a
// standard: DTAPI_E_DEVICE, the firmware errors, DTAPI_E_NO_SUCH_PORT,
// DTAPI_E_NOT_SUPPORTED and DTAPI_E_NOT_FOUND. Other errors, DTAPI_E_DRIVER_INCOMP
// among them, are retried until the time is up.
CDTAPI_API DtapiResult DtDevice_WaitForSignalTimeout(DtDevice* Device, int Port,
                                                     int TimeoutMs, DtDetVidStd* Result);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Parallel work +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A channel converts each SDI frame between the form a program uses and the form the
// card carries. For large frames, such as 4K, this takes more time than one thread has,
// so a channel can divide the work of a frame (a "job") into pieces that run on several
// threads at once. A DtWorkerPool provides those threads. Several channels may share one
// pool.
//
// A pool gets its threads in one of three ways:
//   DtWorkerPool_StartThreads()   the pool starts threads of its own
//   DtWorkerPool_SetDispatch()    the pool hands each job to a function of the program,
//                                 e.g. one that uses OpenMP or the program's thread pool
//   DtWorkerPool_ExpectThreads()  threads of the program join the pool with
//                                 DtWorkerPool_Join() and work for it until dismissed
//
// A channel without a pool does all its work in the thread that calls it. That is the
// default. The frames are the same either way.
//

// Does piece PieceIndex of a job of NumPieces pieces. The library passes this function
// to the program's DtJobDispatchFunc, which calls it for every piece. The pieces may run
// in any order, on any thread.
typedef void (*DtJobFunc)(void* Context, int PieceIndex, int NumPieces);

// The program's function that runs a job, given to DtWorkerPool_SetDispatch(). It calls
// Job(Context, PieceIndex, NumPieces) once for each PieceIndex from 0 to NumPieces - 1,
// on the program's threads, and returns when all calls have finished.
//
// Job and Context are valid only until the function returns; there is nothing to free.
// A pool shared by several channels may call the function from several threads at once.
// Calling the pieces one after the other on the calling thread is correct, only slow.
typedef void (*DtJobDispatchFunc)(void* User, DtJobFunc Job, void* Context,
                                  int NumPieces);

// A pool of threads that channels divide their work over. The program and every channel
// that uses the pool hold a reference to it; the pool is freed when the last one lets
// go. So a program may call DtWorkerPool_Free() as soon as it has given the pool to its
// channels.
typedef struct DtWorkerPool DtWorkerPool;

// A thread of the program that works for a pool, with DtWorkerPool_Join(). Create a
// worker for each such thread before it joins, and free it after DtWorkerPool_Join()
// has returned. A worker may join again after it was dismissed.
typedef struct DtWorker DtWorker;

// Creates a worker for a thread that will join a pool. Returns NULL when there is not
// enough memory.
CDTAPI_API DtWorker* DtWorker_Alloc(void);

// Frees a worker that is not in DtWorkerPool_Join(). NULL does nothing.
CDTAPI_API void DtWorker_Free(DtWorker* Worker);

// Frees *Worker, as DtWorker_Free() does, and sets *Worker to NULL. NULL does nothing.
CDTAPI_API void DtWorker_Freep(DtWorker** Worker);

// Creates a pool without threads: until one of the three functions above gives it
// threads, it runs all work in the thread that calls the channel. Returns NULL when there
// is not enough memory.
CDTAPI_API DtWorkerPool* DtWorkerPool_Alloc(void);

// Makes the thread of Worker leave the pool: its DtWorkerPool_Join() returns once it has
// finished its current piece. If the thread has not joined yet, its next Join returns at
// once. Call it from another thread. NULL does nothing.
CDTAPI_API void DtWorkerPool_Dismiss(DtWorkerPool* Pool, DtWorker* Worker);

// Makes every thread in DtWorkerPool_Join() on Pool leave the pool. NULL does nothing.
CDTAPI_API void DtWorkerPool_DismissAll(DtWorkerPool* Pool);

// Sets Pool up to be worked for by up to NumThreads threads of the program. Each thread
// calls DtWorkerPool_Join() with a worker of its own and runs pieces until it is
// dismissed. This way the program keeps control of its threads' priority, processor
// affinity and names. While no thread has joined, the work runs in the thread that calls
// the channel.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG  Pool is NULL, or NumThreads is below 2
//   DTAPI_E_IN_USE       as for DtWorkerPool_StartThreads(), or a thread has joined
CDTAPI_API DtapiResult DtWorkerPool_ExpectThreads(DtWorkerPool* Pool, int NumThreads);

// Releases the program's reference to Pool. The pool is freed once no channel and no
// joined thread uses it. NULL does nothing.
CDTAPI_API void DtWorkerPool_Free(DtWorkerPool* Pool);

// Releases *Pool, as DtWorkerPool_Free() does, and sets *Pool to NULL.
CDTAPI_API void DtWorkerPool_Freep(DtWorkerPool** Pool);

// Makes the calling thread work for Pool: it runs pieces of the channels' jobs until
// DtWorkerPool_Dismiss() or DtWorkerPool_DismissAll() dismisses it. The last thread to
// leave first finishes the pieces still waiting. A joined thread holds a reference to
// the pool, so a program must dismiss its threads before the pool can be freed.
//
// Returns DTAPI_OK when dismissed, or:
//   DTAPI_E_INVALID_ARG    Pool or Worker is NULL
//   DTAPI_E_NOT_SUPPORTED  Pool was not set up with DtWorkerPool_ExpectThreads()
//   DTAPI_E_IN_USE         Worker has already joined, or as many threads as expected
//                          have
CDTAPI_API DtapiResult DtWorkerPool_Join(DtWorkerPool* Pool, DtWorker* Worker);

// Makes Pool hand each job to the program's function Dispatch, which runs the pieces on
// the program's own threads. NumThreads is how many pieces the program runs at once for
// this pool; a job has at most that many. Passing NULL for Dispatch removes the
// function, and the pool runs the work in the calling thread again.
//
// With OpenMP, all it takes is:
//
//     static void Dispatch(void* User, DtJobFunc Job, void* Context, int NumPieces)
//     {
//         (void)User;
//     #pragma omp parallel for
//         for (int i = 0; i < NumPieces; i++)
//             Job(Context, i, NumPieces);
//     }
//
//     DtWorkerPool_SetDispatch(Pool, Dispatch, NULL, 4);
//
// With a thread pool of the program's own, Dispatch calls that pool's function for
// running a job and waiting for it. User is passed to Dispatch unchanged.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG  Pool is NULL, or NumThreads is below 2 while Dispatch is not
//   DTAPI_E_IN_USE       as for DtWorkerPool_StartThreads()
CDTAPI_API DtapiResult DtWorkerPool_SetDispatch(DtWorkerPool* Pool,
                                                DtJobDispatchFunc Dispatch, void* User,
                                                int NumThreads);

// Starts NumThreads threads of the pool's own, named DtWorker.1, DtWorker.2 and so on,
// which run the pieces of the channels' jobs. The thread that calls a channel waits for
// them. The threads run until the pool is freed or set up differently.
//
// How many threads: as many pieces as the channels sharing the pool run at once (see
// DtInpChannel_SetWorkerPool()), and no more than the processor cores the program can
// spare. More than four per frame gain little, as the work then waits for memory. At
// least two are needed: with one thread nothing is divided.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG  Pool is NULL, or NumThreads is below 2
//   DTAPI_E_IN_USE       a channel that uses the pool has a signal: it has sized its
//                        buffers for the pool's threads
//   DTAPI_E_OUT_OF_MEM   a thread could not be created; the pool then has no threads
CDTAPI_API DtapiResult DtWorkerPool_StartThreads(DtWorkerPool* Pool, int NumThreads);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtInpChannel +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// An input channel receives what arrives on one input port of a card: SDI frames or a
// DVB-ASI transport stream, depending on the port's I/O standard. A program uses it in
// these steps:
//
// 1. DtInpChannel_Alloc, then DtInpChannel_AttachToPort.
// 2. Optionally SetRxMode, and SetIoConfig to change the port's I/O standard.
// 3. SetRxControl(DTAPI_RXCTRL_RCV) to start receiving.
// 4. SDI: ReadFrame for each frame. ASI: Read for the transport stream.
// 5. SetRxControl(DTAPI_RXCTRL_IDLE), Detach and Free.
//
// SDI frames are raw: every line from its EAV on, in 10- or 16-bit symbols. The channel
// receives SD, HD and 3G-SDI, and 4K (2160p) on one 6G or 12G link. It does not receive
// 4K on four links, level-B links, 8-bit symbols, or the active-video-only and
// compressed modes.
//
// ASI is received in the modes DTAPI_RXMODE_ST188, ST204, STMP2, STRAW and STTRP, with
// or without DTAPI_RXMODE_TIMESTAMP32 or TIMESTAMP_TOD. The channel's FIFO holds 8 MB.
//
// The channel has no thread of its own: a read takes the data from the card's buffer
// and converts it straight into the program's buffer. The channel has the port to
// itself; no other program can attach to it at the same time.
//
// Read, GetStatus, GetTsRateBps, GetViolCount and PolarityControl are for ASI only, and
// return DTAPI_E_NOT_SUPPORTED on SDI.
//
// Every function that takes a DtInpChannel returns DTAPI_E_INVALID_ARG when it is NULL,
// and DTAPI_E_NOT_ATTACHED when it is not attached.
//

typedef struct DtInpChannel DtInpChannel;

// A view of an SDI frame, from cdtapi_sdi.h.
typedef struct DtSdiView DtSdiView;

// Waits for the next SDI frame and points Frame, a view from DtSdiView_Alloc(), at it
// where it lies in the card's receive buffer, without copying it. Read it with
// DtSdiParser_Parse() or DtSdiView_GetActiveLine(), then give it back with
// DtInpChannel_ReleaseFrame(). Until then the card cannot use that part of its buffer
// again, so give each frame back within a frame period or so. The channel lends one
// frame at a time.
//
// The frame is the one DtInpChannel_ReadFrame() would deliver in 10 bits a symbol, and
// the receive mode must be 10-bit. A program uses either this or ReadFrame on a channel,
// not both. Waits up to TimeOut milliseconds, or without a limit for -1. ArrivalTime,
// which may be NULL, is set as by DtInpChannel_ReadFrame2().
//
// Returns DTAPI_OK, or:
//   DTAPI_E_IN_USE           a frame is lent and has not been given back, Frame holds a
//                            frame lent, or a read on another thread has not returned
//   DTAPI_E_INVALID_MODE     the receive mode is not 10 bits a symbol
//   DTAPI_E_INVALID_TIMEOUT  TimeOut is 0 or below -1
//   DTAPI_E_NOT_SDI_MODE     the port receives ASI
//   DTAPI_E_TIMEOUT          no frame arrived in time
//   DTAPI_E_CANCELLED        the channel was detached meanwhile
// After a failure Frame describes no frame.
CDTAPI_API DtapiResult DtInpChannel_AcquireFrame(DtInpChannel* InpChannel,
                                                 DtSdiView* Frame, int TimeOut,
                                                 DtTimeOfDay* ArrivalTime);

// Creates an input channel, not yet attached to a port. Returns NULL when there is not
// enough memory.
CDTAPI_API DtInpChannel* DtInpChannel_Alloc(void);

// Attaches InpChannel to input port Port (from 1) of Device, which must be attached.
// The channel opens its own handle to the card, so Device may be detached afterwards.
//
// Returns DTAPI_OK, or:
//   DTAPI_OK_FAILSAFE       attached; the port is a fail-safe port in fail-safe mode
//   DTAPI_E_ATTACHED        InpChannel is already attached
//   DTAPI_E_DEVICE          Device is not attached
//   DTAPI_E_OBSOLETE_FW     the card's firmware is obsolete
//   DTAPI_E_TAINTED_FW      the card's firmware is an unsupported version
//   DTAPI_E_NO_SUCH_PORT    the card has no such port
//   DTAPI_E_NO_DT_INPUT     the port cannot be an input, or is not set to be one
//   DTAPI_E_NOT_SUPPORTED   the port is a matrix port (CAP_MATRIX), carries neither SDI
//                           nor ASI, or is set to an I/O standard it does not support
//   DTAPI_E_NO_SUCH_DEVICE  the card has gone
//   DTAPI_E_DRIVER_INCOMP   the driver is too old
//   DTAPI_E_NOT_FOUND       the driver has no receiver for the port
//   DTAPI_E_IN_USE          another program has the port
//   DTAPI_E_OUT_OF_MEM      not enough memory
// and the errors of the driver.
CDTAPI_API DtapiResult DtInpChannel_AttachToPort(DtInpChannel* InpChannel,
                                                 DtDevice* Device, int Port);

// Stops receiving, discards the data waiting in the channel, and clears the overflow
// flag DTAPI_RX_FIFO_OVF.
CDTAPI_API DtapiResult DtInpChannel_ClearFifo(DtInpChannel* InpChannel);

// Clears the flags given in Latched that GetFlags() keeps set until cleared:
// DTAPI_RX_FIFO_OVF, and on ASI DTAPI_RX_SYNC_ERR.
CDTAPI_API DtapiResult DtInpChannel_ClearFlags(DtInpChannel* InpChannel, int Latched);

// Stops receiving and detaches InpChannel from its port. With DetachMode
// DTAPI_INSTANT_DETACH (1), the data waiting in the channel is discarded first.
//
// A read waiting on another thread returns DTAPI_E_CANCELLED. If it has not returned
// after 100 ms, Detach returns DTAPI_E_TIMEOUT and the channel stays attached and usable.
// DTAPI_E_NOT_ATTACHED means another thread detached it meanwhile.
CDTAPI_API DtapiResult DtInpChannel_Detach(DtInpChannel* InpChannel, int DetachMode);

// Detects the video standard on the port, and returns the I/O standard for it in *Value
// and *SubValue, as DtapiVidStd2IoStd() makes it. Pass these to SetIoConfig() to receive
// the signal. Returns the errors of DtapiVidStd2IoStd() when no standard is detected, and
// DTAPI_E_NOT_SUPPORTED on ASI.
CDTAPI_API DtapiResult DtInpChannel_DetectIoStd(DtInpChannel* InpChannel, int* Value,
                                                int* SubValue);

// Detaches InpChannel, discarding the data waiting in it, and frees it. A read waiting
// on another thread returns DTAPI_E_CANCELLED first; Free waits for that as long as it
// takes. NULL does nothing.
CDTAPI_API void DtInpChannel_Free(DtInpChannel* InpChannel);

// Frees *InpChannel, as DtInpChannel_Free() does, and sets *InpChannel to NULL.
CDTAPI_API void DtInpChannel_Freep(DtInpChannel** InpChannel);

// Sets *FifoLoad to how many bytes wait to be read: on SDI, the size of the complete
// frames waiting, in the current receive mode; on ASI, the bytes Read() would deliver
// now. 0 while not receiving.
CDTAPI_API DtapiResult DtInpChannel_GetFifoLoad(DtInpChannel* InpChannel, int* FifoLoad);

// Sets *Flags to the channel's current problems, and *Latched to those that occurred
// since ClearFlags() last cleared them:
//   DTAPI_RX_FIFO_OVF   data was lost: on SDI the card's buffer was full, on ASI packets
//                       were lost in the card or because the FIFO was full
//   DTAPI_RX_SYNC_ERR   ASI only: a packet arrived without packet sync
CDTAPI_API DtapiResult DtInpChannel_GetFlags(DtInpChannel* InpChannel, int* Flags,
                                             int* Latched);

// Reads the setting Group of the channel's port, as DtDevice_GetIoConfig() does, into
// *Value and, when they are not NULL, *SubValue, *ParXtra0 and *ParXtra1. After a failure
// they are -1.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG    Group is not a setting (checked before anything else)
//   DTAPI_E_NOT_SUPPORTED  the port does not have this setting
CDTAPI_API DtapiResult DtInpChannel_GetIoConfig(DtInpChannel* InpChannel, int Group,
                                                int* Value, int* SubValue,
                                                int64_t* ParXtra0, int64_t* ParXtra1);

// Sets *MaxFifoSize to the most bytes GetFifoLoad() can report: the complete frames the
// card's buffer holds, at least two, in the current receive mode. On ASI it is 8 MB; on
// a port set to 4K on four links or to level-B links, which the channel does not
// receive, 48 MB.
CDTAPI_API DtapiResult DtInpChannel_GetMaxFifoSize(DtInpChannel* InpChannel,
                                                   int* MaxFifoSize);

// Reports the state of the ASI input:
//   *PacketSize  the packet size found: DTAPI_PCKSIZE_188, _204 or _INV
//   *NumInv      always DTAPI_NOT_SUPPORTED
//   *ClkDet      DTAPI_CLKDET_OK or DTAPI_CLKDET_FAIL: whether a clock is detected
//   *AsiLock     DTAPI_ASI_INLOCK when locked, else 0
//   *RateOk      DTAPI_INPRATE_OK above 900 bit/s, else DTAPI_INPRATE_LOW
//   *AsiInv      DTAPI_ASIINV_NORMAL or _INVERT, or DTAPI_NOT_SUPPORTED when unknown
CDTAPI_API DtapiResult DtInpChannel_GetStatus(DtInpChannel* InpChannel, int* PacketSize,
                                              int* NumInv, int* ClkDet, int* AsiLock,
                                              int* RateOk, int* AsiInv);

// Sets *TsRate to the rate of the transport stream, in bits per second. The rate is
// that of 188-byte packets: of 204-byte packets, the 16 extra bytes are not counted,
// except in DTAPI_RXMODE_STRAW.
CDTAPI_API DtapiResult DtInpChannel_GetTsRateBps(DtInpChannel* InpChannel, int* TsRate);

// Sets *ViolCount to the number of 8b/10b code violations the card has seen on the ASI
// input.
CDTAPI_API DtapiResult DtInpChannel_GetViolCount(DtInpChannel* InpChannel,
                                                 int* ViolCount);

// Sets how the ASI input's polarity is handled: DTAPI_POLARITY_AUTO, _NORMAL or
// _INVERT. Another value returns DTAPI_E_INVALID_MODE.
CDTAPI_API DtapiResult DtInpChannel_PolarityControl(DtInpChannel* InpChannel,
                                                    int Polarity);

// Reads NumBytesToRead bytes of the ASI transport stream into Buffer, in the receive
// mode set.
//
// TimeOut 0 waits until all bytes have arrived, without a limit; the bytes are taken
// 1 MB at a time, so more than the FIFO holds may be asked for. Any other TimeOut waits
// up to that many milliseconds (-1: without a limit) until all bytes are there, and
// reads nothing when the time is up.
//
// Returns DTAPI_OK (at once for 0 bytes), or:
//   DTAPI_E_INVALID_TIMEOUT  TimeOut is below -1
//   DTAPI_E_IN_USE           a Read on another thread has not returned
//   DTAPI_E_INVALID_SIZE     NumBytesToRead is negative or not a multiple of 4, or, with
//                            a TimeOut other than 0, more than the FIFO's 8 MB
//   DTAPI_E_INVALID_BUF      Buffer's address is not a multiple of 4
//   DTAPI_E_TIMEOUT          the bytes did not arrive in time
//   DTAPI_E_CANCELLED        the channel was detached meanwhile
CDTAPI_API DtapiResult DtInpChannel_Read(DtInpChannel* InpChannel, void* Buffer,
                                         int NumBytesToRead, int TimeOut);

// Reads the next SDI frame into FrameBuffer. On entry *FrameSize is the size of
// FrameBuffer in bytes; on return it is the size of the frame. Waits up to TimeOut
// milliseconds for the frame, or without a limit for -1.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_BUF_TOO_SMALL    *FrameSize is 0, or FrameBuffer is smaller than the frame
//   DTAPI_E_INVALID_TIMEOUT  TimeOut is 0 or below -1
//   DTAPI_E_INVALID_SIZE     *FrameSize is negative or not a multiple of 4
//   DTAPI_E_INVALID_BUF      FrameBuffer is NULL, or its address not a multiple of 4
//   DTAPI_E_IN_USE           a read on another thread has not returned, or
//                            DtInpChannel_AcquireFrame() lent a frame not given back
//   DTAPI_E_NOT_SDI_MODE     the port receives ASI
//   DTAPI_E_TIMEOUT          no frame arrived in time
//   DTAPI_E_CANCELLED        the channel was detached meanwhile
// After the second DTAPI_E_BUF_TOO_SMALL and the errors below it, *FrameSize is 0.
CDTAPI_API DtapiResult DtInpChannel_ReadFrame(DtInpChannel* InpChannel, void* FrameBuffer,
                                              int* FrameSize, int TimeOut);

// Reads the next SDI frame, as DtInpChannel_ReadFrame() does, and sets *ArrivalTime to
// when it arrived, on the card's time-of-day clock (selected with
// DTAPI_IOCONFIG_TODREFSEL). ArrivalTime may be NULL; after a failure it is zero.
CDTAPI_API DtapiResult DtInpChannel_ReadFrame2(DtInpChannel* InpChannel,
                                               void* FrameBuffer, int* FrameSize,
                                               int TimeOut, DtTimeOfDay* ArrivalTime);

// Gives the frame that Frame describes back to the card, so that it can use that part of
// its buffer again. Frame then describes no frame. Detaching the channel, a new I/O
// standard, and setting the receive control or clearing the FIFO take a lent frame back
// too, and Frame describes no frame after them.
//
// Returns DTAPI_OK, or DTAPI_E_INVALID_ARG when Frame describes no frame this channel
// lent.
CDTAPI_API DtapiResult DtInpChannel_ReleaseFrame(DtInpChannel* InpChannel,
                                                 DtSdiView* Frame);

// Changes a setting of the channel's port, e.g. its I/O standard. The channel must not
// be receiving. ParXtra0 and ParXtra1 are as in DtIoConfig, -1 when not used.
//
// The channel follows a new I/O standard. When it changes between SDI and ASI, the
// channel switches too, with the default receive mode of the other side:
// DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B, or DTAPI_RXMODE_ST188. If that switch
// fails, the channel is left detached.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_NOT_IDLE       the channel is receiving
//   DTAPI_E_INVALID_ARG    Group, Value and SubValue are not a valid setting, the
//                          direction is output, or a shared-antenna input's ParXtra0 is
//                          not a port
//   DTAPI_E_NOT_SUPPORTED  another direction, or an I/O standard the port does not
//                          support; the channel is not changed
CDTAPI_API DtapiResult DtInpChannel_SetIoConfig(DtInpChannel* InpChannel, int Group,
                                                int Value, int SubValue, int64_t ParXtra0,
                                                int64_t ParXtra1);

// Starts or stops receiving: DTAPI_RXCTRL_RCV starts at the next frame, DTAPI_RXCTRL_IDLE
// stops. On ASI, both empty the FIFO and clear DTAPI_RX_FIFO_OVF.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_CONFIG_RAW_SDI  the receive mode is 8-bit, or the port is set to 4K on four
//                           links or to level-B links
//   DTAPI_E_INVALID_ARG     ASI: RxControl is neither value
CDTAPI_API DtapiResult DtInpChannel_SetRxControl(DtInpChannel* InpChannel, int RxControl);

// Sets the format the channel delivers. The channel must not be receiving.
//
// SDI: DTAPI_RXMODE_SDI_FULL, with DTAPI_RXMODE_SDI_10B or _16B for the symbol size
// (8-bit without either). No timestamp flag, as a raw frame has no room for one; use
// ReadFrame2() for the arrival time.
// ASI: one of the modes in the section above.
//
// Returns DTAPI_OK, DTAPI_E_INVALID_MODE for another mode, or DTAPI_E_NOT_IDLE while
// receiving.
CDTAPI_API DtapiResult DtInpChannel_SetRxMode(DtInpChannel* InpChannel, int RxMode);

// Gives InpChannel a worker pool to divide its work over (see "Parallel work").
//
// Before a read returns a frame, the channel prepares the data received from the card
// in the memory layout expected by the application. By default, this work is performed
// entirely by the reading thread. When a worker pool is assigned, the preparation is
// performed by jobs running on threads from that pool instead. Using a worker pool does
// not change the video data or video format of the frame; it only moves the preparation
// work to worker threads.
//
// The channel retains the assigned pool until another pool is assigned or the channel
// is detached. Switching the channel to ASI and back does not release the pool. A
// channel whose signal does not currently require frame preparation, such as ASI, still
// retains the pool so that it is available when preparation is required.
//
// NumThreads specifies the maximum number of pool threads that the channel may use
// for its jobs. It is a limit, not a reservation: the channel may use fewer threads,
// for example when the pool is already being used by other channels. If NumThreads
// is 0, the library selects the number of threads based on the video standard
// configured for the channel:
//
//   2160p50, 2160p60     4 threads. Preparing a 12G-SDI frame requires approximately
//                        four times as much work as preparing a 3G-SDI frame. At 50 or
//                        60 frames per second, this can exceed the capacity of a single
//                        slow core.
//
//   2160p24 to 2160p30   2 threads. The frame size is the same, but the lower frame rate
//                        reduces the processing required per unit of time.
//
//   Up to 3G-SDI         1 thread. Preparing a frame at 3G-SDI or below normally takes
//                        only a small part of the frame period, even on a slow core.
//                        Using multiple threads would therefore add overhead without
//                        providing a benefit. The pool is then not used.
//
// If a channel must not be delayed by other channels using the pool, assign it a
// dedicated worker pool.
//
// Returns DTAPI_E_INVALID_ARG if InpChannel is NULL or NumThreads is negative;
// DTAPI_E_NOT_ATTACHED if the channel is not attached;
// DTAPI_E_IN_USE if the channel is currently reading a frame; and
// DTAPI_E_OUT_OF_MEM if the required buffers cannot be allocated.
CDTAPI_API DtapiResult DtInpChannel_SetWorkerPool(DtInpChannel* InpChannel,
                                                  DtWorkerPool* Pool, int NumThreads);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtOutpChannel +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// An output channel sends on one output port of a card: SDI frames or a DVB-ASI
// transport stream, depending on the port's I/O standard. A program uses it in these
// steps:
//
// 1. DtOutpChannel_Alloc, then DtOutpChannel_AttachToPort.
// 2. Optionally SetTxMode, and SetIoConfig to change the port's I/O standard. For ASI,
//    SetTsRateBps.
// 3. SetTxControl(DTAPI_TXCTRL_HOLD), write the first data, then
//    SetTxControl(DTAPI_TXCTRL_SEND).
// 4. SDI: WriteFrame (or Write) for each frame. ASI: Write for the transport stream.
// 5. Detach, with DTAPI_WAIT_UNTIL_SENT to let the card send what is left, and Free.
//
// SDI frames are raw: every line from its EAV on, in 10- or 16-bit symbols, starting at
// line 1. The channel sends SD, HD and 3G-SDI, and 4K (2160p) on one 6G or 12G link. It
// does not send 4K on four links, level-B links, 8-bit symbols or active-video-only
// modes. While it sends, a thread of the channel keeps the signal going: when the
// program does not write in time, it sends a black frame.
//
// ASI is sent in the modes DTAPI_TXMODE_188, 204, ADD16, MIN16 and RAW, with or without
// DTAPI_TXMODE_BURST or TXONTIME. Write puts the stream in a FIFO of 8 MB, from which a
// thread of the channel sends it at the rate set. The port sends idle symbols (K28.5)
// from the moment the channel attaches. Outputs that copy the port (double-buffered and
// monitor outputs) send the same, and are attached along with it.
//
// The channel has the port to itself; no other program can attach to it at the same
// time.
//
// Every function that takes a DtOutpChannel returns DTAPI_E_INVALID_ARG when it is NULL,
// and DTAPI_E_NOT_ATTACHED when it is not attached.
//

typedef struct DtOutpChannel DtOutpChannel;

// Creates an output channel, not yet attached to a port. Returns NULL when there is not
// enough memory.
CDTAPI_API DtOutpChannel* DtOutpChannel_Alloc(void);

// Attaches OutpChannel to output port Port (from 1) of Device, which must be attached.
// The channel opens its own handle to the card, so Device may be detached afterwards.
//
// On ASI the port sends idle symbols from now on. A receiver needs a moment to lock to
// them, so a stream sent at once may lose its beginning; Examples/DtTransmitTs waits
// 200 ms.
//
// Returns DTAPI_OK, or:
//   DTAPI_OK_FAILSAFE       attached; the port is a fail-safe port in fail-safe mode
//   DTAPI_E_ATTACHED        OutpChannel is already attached
//   DTAPI_E_DEVICE          Device is not attached
//   DTAPI_E_OBSOLETE_FW     the card's firmware is obsolete
//   DTAPI_E_TAINTED_FW      the card's firmware is an unsupported version
//   DTAPI_E_NO_SUCH_PORT    the card has no such port
//   DTAPI_E_NO_DT_OUTPUT    the port cannot be an output, or is not set to be one
//   DTAPI_E_NOT_SUPPORTED   the port is a matrix port (CAP_MATRIX), carries neither SDI
//                           nor ASI, or is set to an I/O standard it does not support
//   DTAPI_E_NO_SUCH_DEVICE  the card has gone
//   DTAPI_E_DRIVER_INCOMP   the driver is too old
//   DTAPI_E_NOT_FOUND       the driver has no transmitter for the port
//   DTAPI_E_IN_USE          another program has the port, or, on ASI, a port that copies
//                           it
//   DTAPI_E_OUT_OF_MEM      not enough memory
// and the errors of the driver.
CDTAPI_API DtapiResult DtOutpChannel_AttachToPort(DtOutpChannel* OutpChannel,
                                                  DtDevice* Device, int Port);

// Stops sending, discards what the channel has not sent yet, and clears the underflow
// flags DTAPI_TX_FIFO_UFL and DTAPI_TX_DMA_UFL. On ASI, DTAPI_TX_SYNC_ERR stays set. If
// stopping fails, no flag is cleared.
CDTAPI_API DtapiResult DtOutpChannel_ClearFifo(DtOutpChannel* OutpChannel);

// Clears the flags given in Latched that GetFlags() keeps set until cleared:
// DTAPI_TX_FIFO_UFL and DTAPI_TX_DMA_UFL, and on ASI DTAPI_TX_SYNC_ERR.
CDTAPI_API DtapiResult DtOutpChannel_ClearFlags(DtOutpChannel* OutpChannel, int Latched);

// Stops sending and detaches OutpChannel from its port. Data not sent yet is lost, unless
// DetachMode is DTAPI_WAIT_UNTIL_SENT (2): Detach then first waits until the card has
// sent it, and gives up when nothing more goes out for a second. DTAPI_INSTANT_DETACH (1)
// discards it at once. Both flags together return DTAPI_E_INVALID_FLAGS.
//
// On SDI, a frame that Write() left incomplete is dropped, and a black frame follows the
// last one, as the card sends a frame only when data follows it.
//
// A write waiting on another thread returns DTAPI_E_CANCELLED. If it has not returned
// after 100 ms, Detach returns DTAPI_E_TIMEOUT and the channel stays attached and usable.
// DTAPI_E_NOT_ATTACHED means another thread detached it meanwhile.
CDTAPI_API DtapiResult DtOutpChannel_Detach(DtOutpChannel* OutpChannel, int DetachMode);

// Detaches OutpChannel, discarding what it has not sent, and frees it. A write waiting
// on another thread returns DTAPI_E_CANCELLED first; Free waits for that as long as it
// takes. NULL does nothing.
CDTAPI_API void DtOutpChannel_Free(DtOutpChannel* OutpChannel);

// Frees *OutpChannel, as DtOutpChannel_Free() does, and sets *OutpChannel to NULL.
CDTAPI_API void DtOutpChannel_Freep(DtOutpChannel** OutpChannel);

// Sets *FifoLoad to how many bytes are written and not yet sent; 0 while idle.
//
// SDI: the complete frames waiting, plus what was written of the next one, in the current
// transmit mode; never more than GetFifoSize().
// ASI: an estimate. While holding, the bytes written; while sending, the bytes in the
// FIFO plus those still in the card's buffers (the FIFO alone with
// DTAPI_TXMODE_TXONTIME).
CDTAPI_API DtapiResult DtOutpChannel_GetFifoLoad(DtOutpChannel* OutpChannel,
                                                 int* FifoLoad);

// Sets *FifoSize to the most bytes GetFifoLoad() can report: the complete frames the
// card's buffer holds, at least two, in the current transmit mode. On ASI it is 8 MB; on
// a port set to 4K on four links or to level-B links, which the channel does not send,
// 48 MB.
CDTAPI_API DtapiResult DtOutpChannel_GetFifoSize(DtOutpChannel* OutpChannel,
                                                 int* FifoSize);

// Sets *Status to the channel's current problems, and *Latched to those that occurred
// since they were last cleared:
//   DTAPI_TX_FIFO_UFL   the program did not write in time: on SDI the channel sent a
//                       black frame or the card ran out of data; on ASI the card ran out
//                       of symbols, or stuffing inserted null packets
//   DTAPI_TX_DMA_UFL    SDI: the card's transmitter ran out of data
//   DTAPI_TX_SYNC_ERR   ASI: a packet was written without its sync byte
// ClearFlags() and ClearFifo() clear the latched flags. On ASI, HOLD also clears
// DTAPI_TX_SYNC_ERR, and SEND forgets the card's earlier underflows.
CDTAPI_API DtapiResult DtOutpChannel_GetFlags(DtOutpChannel* OutpChannel, int* Status,
                                              int* Latched);

// Reads the setting Group of the channel's port, as DtInpChannel_GetIoConfig() does.
CDTAPI_API DtapiResult DtOutpChannel_GetIoConfig(DtOutpChannel* OutpChannel, int Group,
                                                 int* Value, int* SubValue,
                                                 int64_t* ParXtra0, int64_t* ParXtra1);

// Sets *MaxFifoSize as GetFifoSize() does, except that it is 64 MB on a port the
// channel does not send on. On ASI it is 8 MB.
CDTAPI_API DtapiResult DtOutpChannel_GetMaxFifoSize(DtOutpChannel* OutpChannel,
                                                    int* MaxFifoSize);

// Sets *TsRate to the rate at which the ASI transport stream is sent, in bits per second
// of 188-byte packets, whatever the packet size. It is 10 Mbit/s after attaching. On SDI
// it returns DTAPI_E_NOT_SUPPORTED.
CDTAPI_API DtapiResult DtOutpChannel_GetTsRateBps(DtOutpChannel* OutpChannel,
                                                  int* TsRate);

// Changes a setting of the channel's port, e.g. its I/O standard. The channel must be
// idle. ParXtra0 and ParXtra1 are as in DtIoConfig, -1 when not used.
//
// The channel follows a new SDI standard and keeps its transmit mode. When the standard
// changes between SDI and ASI, the channel switches too, with the default transmit mode
// of the other side. If that switch fails, the channel is left detached.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_NOT_IDLE       the channel is not idle
//   DTAPI_E_INVALID_ARG    Group, Value and SubValue are not a valid setting, the
//                          direction is input, or an output that copies another port
//                          (DTAPI_IOCONFIG_DBLBUF, LOOPS2L3, LOOPS2TS or LOOPTHR) has a
//                          ParXtra0 that is not a port
//   DTAPI_E_NOT_SUPPORTED  an I/O standard the port does not support; the channel is
//                          not changed
CDTAPI_API DtapiResult DtOutpChannel_SetIoConfig(DtOutpChannel* OutpChannel, int Group,
                                                 int Value, int SubValue,
                                                 int64_t ParXtra0, int64_t ParXtra1);

// Sets the rate at which the ASI transport stream is sent, in bits per second of
// 188-byte packets, whatever the packet size.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_RATE   TsRate is 0 or less, or too high for the line
//   DTAPI_E_NOT_SUPPORTED  the port sends SDI
CDTAPI_API DtapiResult DtOutpChannel_SetTsRateBps(DtOutpChannel* OutpChannel, int TsRate);

// Sets the channel's state:
//   DTAPI_TXCTRL_IDLE  stops, and discards what was written
//   DTAPI_TXCTRL_HOLD  prepares to send: written data is kept but not sent yet
//   DTAPI_TXCTRL_SEND  sends; from idle, the channel goes through HOLD first
//
// SDI: SEND needs a frame written first, so SEND from idle returns DTAPI_E_INSUF_LOAD and
// leaves the channel holding. In the 8-bit mode the channel holds and takes frames, but
// SEND returns DTAPI_E_CONFIG_RAW_SDI; so does HOLD on a port set to 4K on four links or
// to level-B links.
//
// ASI: SEND needs no data, but waits a few milliseconds for the card's buffer to fill,
// and returns DTAPI_E_TIMEOUT if it does not. HOLD returns DTAPI_E_INVALID_RATE for a
// rate that does not suit the packet size, except with DTAPI_TXMODE_TXONTIME.
CDTAPI_API DtapiResult DtOutpChannel_SetTxControl(DtOutpChannel* OutpChannel,
                                                  int TxControl);

// Sets the format of the data the program writes.
//
// SDI, while idle: DTAPI_TXMODE_SDI_FULL, with DTAPI_TXMODE_SDI_10B or _16B for the
// symbol size (8-bit without either). StuffMode is not used.
// ASI, at any time: one of the modes in the section above. With StuffMode 1, the channel
// fills gaps in the stream with null packets, keeping 50 ms of data in the card.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_MODE     the mode mixes SDI and ASI bits (checked first), is not
//                            valid for SDI, is DTAPI_TXMODE_192, or is DTAPI_TXMODE_RAW
//                            with a flag and StuffMode 1
//   DTAPI_E_NOT_IDLE         SDI: the channel is not idle
//   DTAPI_E_NOT_IMPLEMENTED  DTAPI_TXMODE_RAWASI
//   DTAPI_E_INVALID_ARG      another ASI mode, or StuffMode is neither 0 nor 1
CDTAPI_API DtapiResult DtOutpChannel_SetTxMode(DtOutpChannel* OutpChannel, int TxMode,
                                               int StuffMode);

// Sets the polarity of the ASI signal: DTAPI_TXPOL_NORMAL or DTAPI_TXPOL_INVERTED.
// Another value returns DTAPI_E_INVALID_ARG, as does DTAPI_TXPOL_INVERTED on SDI.
CDTAPI_API DtapiResult DtOutpChannel_SetTxPolarity(DtOutpChannel* OutpChannel,
                                                   int TxPolarity);

// Gives OutpChannel a worker pool to divide its work over (see "Parallel work").
//
// Before a written frame goes out, the channel prepares it in the layout the card sends.
// By default, this work is performed entirely by the writing thread. When a worker pool
// is assigned, the preparation is performed by jobs running on threads from that pool
// instead. What DtInpChannel_SetWorkerPool says about retaining the pool, about
// NumThreads and about the number of threads selected for 0 holds here as well.
//
// A channel can only divide the lines it has been given. DtOutpChannel_WriteFrame is
// given a whole frame, so it always divides. DtOutpChannel_Write is given a stretch of
// the stream, and divides the whole lines that stretch holds: a caller that writes a
// frame at a time gets the same as WriteFrame, and a caller that writes a line at a time
// gets no division, because there is nothing in that call to divide.
//
// Returns DTAPI_E_INVALID_ARG if OutpChannel is NULL or NumThreads is negative;
// DTAPI_E_NOT_ATTACHED if the channel is not attached;
// DTAPI_E_IN_USE if the channel is currently writing; and
// DTAPI_E_OUT_OF_MEM if the required buffers cannot be allocated.
CDTAPI_API DtapiResult DtOutpChannel_SetWorkerPool(DtOutpChannel* OutpChannel,
                                                   DtWorkerPool* Pool, int NumThreads);

// Writes NumBytesToWrite bytes from Buffer: on SDI raw frames, on ASI the transport
// stream. The bytes need not be whole frames. Waits as long as needed for room.
//
// On SDI, the channel keeps the stream aligned on frames: at the start of a frame, it
// skips bytes, four at a time, until they start line 1. Bytes too few to tell are kept
// for the next Write.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_SIZE  NumBytesToWrite is negative
//   DTAPI_E_INVALID_BUF   NumBytesToWrite or Buffer's address is not a multiple of 4, or
//                         Buffer is NULL while there are bytes to write
//   DTAPI_E_IDLE          the channel is idle, or was set idle meanwhile
//   DTAPI_E_IN_USE        a write on another thread has not returned
//   DTAPI_E_CANCELLED     the channel was detached meanwhile
CDTAPI_API DtapiResult DtOutpChannel_Write(DtOutpChannel* OutpChannel, const void* Buffer,
                                           int NumBytesToWrite);

// Writes one raw SDI frame. Frame starts at line 1 and holds FrameSize bytes: exactly
// one frame of the port's standard in the current transmit mode. The frame goes into the
// card's buffer whole or not at all. Waits up to TimeOut milliseconds for room, or
// without a limit for -1.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_TIMEOUT  TimeOut is 0 or below -1
//   DTAPI_E_INVALID_SIZE     FrameSize is not positive, not a multiple of 4, or not the
//                            size of a frame
//   DTAPI_E_INVALID_BUF      Frame is NULL, or its address not a multiple of 4
//   DTAPI_E_IDLE             the channel is idle, or was set idle meanwhile
//   DTAPI_E_IN_USE           a write on another thread has not returned
//   DTAPI_E_INCOMP_FRAME     a Write() left part of a frame; complete it with Write() or
//                            discard it with ClearFifo()
//   DTAPI_E_INVALID_FRAME    Frame does not start with the EAV of line 1 (in SD: of a
//                            line in the vertical blanking of field 1)
//   DTAPI_E_NOT_SDI_MODE     the port sends ASI
//   DTAPI_E_TIMEOUT          no room in time
//   DTAPI_E_CANCELLED        the channel was detached meanwhile
CDTAPI_API DtapiResult DtOutpChannel_WriteFrame(DtOutpChannel* OutpChannel,
                                                const void* Frame, int FrameSize,
                                                int TimeOut);

#ifdef __cplusplus
} // extern "C"
#endif
