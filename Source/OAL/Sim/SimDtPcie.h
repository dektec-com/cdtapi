// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# SimDtPcie.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The emulated DtPcie device, the values it reports, and its test controls
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Identity +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// What the emulated card reports about itself. Tests compare against these values rather
// than against literals, so that a change to the emulated card is a one-line edit here.
//
// The card is a DTA-2178 with firmware variant 1, at that variant's latest firmware
// version: eight SDI/ASI ports and a genlock input. Ports 1 and 5 carry 12G, as
// SimDta2178.c describes. A DekTec PCI device ID is the type number in hexadecimal, so
// 2178 becomes 0x0882.
//
// The serial number starts with 9. DekTec serial numbers start with the type number, and
// no card has type number 9, so a serial number in a log shows that it is emulated.
//

#define SIM_TYPE_NUMBER 2178
#define SIM_SERIAL 9217800001ULL
#define SIM_HARDWARE_REVISION 1
#define SIM_FIRMWARE_VERSION 4
#define SIM_FIRMWARE_VARIANT 1
#define SIM_VENDOR_ID 0x1A0E
#define SIM_DEVICE_ID 0x0882

// The subsystem IDs, the firmware build date, the PCI location and the PCIe link, as a
// DTA-2178 in a PCIe 3.0 x4 slot reported them with driver 3.6.4. It had no subsystem
// IDs, and the x8 card ran on four lanes.
#define SIM_SUBSYSTEM_VENDOR_ID 0
#define SIM_SUBSYSTEM_ID 0
#define SIM_FW_BUILD_YEAR 2024
#define SIM_FW_BUILD_MONTH 1
#define SIM_FW_BUILD_DAY 22
#define SIM_FW_BUILD_HOUR 16
#define SIM_FW_BUILD_MINUTE 54
#define SIM_BUS_NUMBER 4
#define SIM_SLOT_NUMBER 0
#define SIM_PCIE_NUM_LANES 4
#define SIM_PCIE_MAX_LANES 8
#define SIM_PCIE_LINK_SPEED 3
#define SIM_PCIE_MAX_SPEED 3
#define SIM_PCIE_MAX_PAYLOAD_SIZE 256
#define SIM_PCIE_MAX_READ_REQUEST_SIZE 512
#define SIM_PCIE_MAX_SLOT_POWER 25000

// The driver version reported. A DTA-2178 was seen with it, and it is new enough for
// every driver function the emulator has.
#define SIM_DRIVER_MAJOR 3
#define SIM_DRIVER_MINOR 6
#define SIM_DRIVER_MICRO 4
#define SIM_DRIVER_BUILD 398

// The driver index of the DTA-2178, unless a test moves it. A DTA-2110 is there too only
// when a test or CDTAPI_SIM_DTA2110 adds one. A scan finds just these devices: they
// replace any real hardware rather than add to it.
#define SIM_DEVICE_INDEX 0

// The ports: 1 to 8 are SDI/ASI inputs and outputs, 9 is the genlock reference input and
// 10 the internal genlock reference, a virtual port. As on the card, the odd SDI ports
// start as inputs and the even ones as outputs.
#define SIM_PORT_COUNT 10
#define SIM_SDI_PORT_COUNT 8

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test controls +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The emulated device keeps its state, such as its I/O configuration, for the whole
// process and across handles, as a card does across opens. A test that changes it starts
// with SimDtPcie_Reset(). None of these controls is thread-safe: one test at a time
// changes the emulator's state.
//
// A fault makes the emulated driver refuse or mangle the commands of one function code
// (the DT_FUNC_CODE_ number of an IOCTL), until the next reset. Tests use faults to
// exercise the failure paths of the layers above without a card. Up to SIM_MAX_FAULTS
// function codes can have a fault at once; a second fault for the same code replaces the
// first.
//

// The number of function codes that can have a fault at once, and the number of
// properties that can be overridden at once.
#define SIM_MAX_FAULTS 4
#define SIM_MAX_OVERRIDES 8

// Restores the power-on state of the whole emulator: the default I/O configuration, the
// device index, firmware status and driver version above, and no signals, overrides,
// exclusive access or faults. Then applies the environment variables described below.
void SimDtPcie_Reset(void);

// Makes the card report Status, a DT_FWSTATUS_ value, as its firmware status.
void SimDtPcie_SetFirmwareStatus(int Status);

// Makes the driver report this version.
void SimDtPcie_SetDriverVersion(int Major, int Minor, int Micro, int Build);

// Overrides a property of the card: it is absent when Present is false, and has Value
// otherwise. Up to SIM_MAX_OVERRIDES properties can be overridden at once; overriding a
// property again replaces its earlier override.
void SimDtPcie_OverrideProperty(const char* Name, int PortIndex, bool Present,
                                uint64_t Value);

// Overrides a string property of the card in the same way. It shares the
// SIM_MAX_OVERRIDES slots with SimDtPcie_OverrideProperty(). An override of a string
// property leaves a value property of the same name alone, and the other way round.
// Value is ignored when Present is false, and is cut to the length the driver can return.
void SimDtPcie_OverrideString(const char* Name, int PortIndex, bool Present,
                              const char* Value);

// Makes reading a property fail with the driver status Status: a string property when
// IsString is true, a value property otherwise. It shares the SIM_MAX_OVERRIDES slots
// with the overrides.
void SimDtPcie_FailProperty(const char* Name, int PortIndex, bool IsString,
                            uint32_t Status);

// What the SDI receiver of a port reports, in the driver's terms: the fields of
// DT_SDIRX_CMD_GET_SDI_STATUS2. Its flags are integers, 0 for false.
typedef struct SimSdiSignal
{
    int CarrierDetect;  // The driver's carrier-detect flag
    int SdiLock;        // The driver's SDI-lock flag
    int LineLock;       // The driver's line-lock flag
    int Valid;          // The driver's valid flag
    int NumSymsHanc;    // Symbols per line in HANC, EAV and SAV included
    int NumSymsVidVanc; // Symbols per line in the active part
    int NumLinesF1;     // Lines of field 1
    int NumLinesF2;     // Lines of field 2
    int IsLevelB;       // The signal is 3G level B
    uint32_t PayloadId; // The VPID, 0 for none
    int FramePeriod;    // Nanoseconds, 0 for unknown
    int SdiRate;        // A DT_DRV_SDIRATE_ value
} SimSdiSignal;

// Sets the signal at the input of the SDI port at PortIndex, 0 to SIM_SDI_PORT_COUNT - 1.
// NULL takes the signal away, as after a reset: nothing is detected and the SDI rate is
// unknown. The receiver reports the signal only while the port is an input. On a port
// configured for ASI it reports only the carrier, as the driver does.
void SimDtPcie_SetSdiSignal(int PortIndex, const SimSdiSignal* Signal);

// Hides the signal of the SDI port at PortIndex from the receiver's next Reads status
// requests, as if the signal arrived only then. A reset, a new signal or a new delay
// ends the delay.
void SimDtPcie_DelaySdiSignal(int PortIndex, int Reads);

// Set an SDI source or sink that uses a file. They serve a program that calls no test
// controls, such as FFmpeg with DekTec devices: the reset reads them from the
// environment variables CDTAPI_SIM_SDI_SOURCE and CDTAPI_SIM_SDI_SINK. A value the reset
// cannot use is reported on stderr and ignored. Each function returns false, and changes
// nothing, for a value it cannot use.
//
// Source is "<port>:<vidstd>:<file>". The SDI port, numbered from 1, receives a locked
// signal of the video standard, and its channel receives the frames of the file, as
// SimChSdiRx_SetFileSource() describes; after the last frame, the first comes again. The
// video standard is a DTAPI_VIDSTD_ name without its prefix, such as 1080I50, in any
// case.
//
// Sink is "<port>:<file>". Every frame the SDI port sends is also written to the file, as
// SimSdiTx_SetFileSink() describes.
//
// The file is the rest of the value, so it may contain colons.
bool SimDtPcie_SetSdiSource(const char* Source);
bool SimDtPcie_SetSdiSink(const char* Sink);

// Moves the DTA-2178 to another driver index. A scan then finds it only by looking past
// the indices before it.
void SimDtPcie_SetDta2178Index(int Index);

// Adds a DTA-2110 (SimDta2110.h) at driver index Index, which must differ from the
// DTA-2178's. A negative Index takes it away.
//
// A reset puts the DTA-2110 at the index in CDTAPI_SIM_DTA2110, or leaves it out when
// that variable is not set. A program without test controls of its own, such as an
// example, can then use the card. Its handles address its own objects and properties,
// and it has an interface in the emulated network (SimNet.h). The property overrides and
// failures, the firmware status, the driver version and the faults apply to both devices.
void SimDtPcie_SetDta2110Index(int Index);

// Returns the number of open handles to the emulated device. A test uses it to check that
// a layer above closes what it opens.
int SimDtPcie_OpenHandleCount(void);

// Makes every command with FunctionCode fail with the driver status Status.
void SimDtPcie_FailWithStatus(int FunctionCode, uint32_t Status);

// Makes every command with FunctionCode return one byte less output than it needs.
void SimDtPcie_AnswerShort(int FunctionCode);

// Copies the input of the last command that reached the emulated driver, exactly as it
// arrived, whether or not it was carried out. A test uses it to check every field a layer
// above sent. Copies at most Size bytes into Buf, and stores the command's function code
// in *FunctionCode. Returns the size of the input, or 0 when no command arrived since the
// reset. Up to SIM_MAX_RECORDED_INPUT bytes of an input are kept.
size_t SimDtPcie_LastInput(int* FunctionCode, void* Buf, size_t Size);

#define SIM_MAX_RECORDED_INPUT 1024

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Emulator parts +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Helpers for the emulated functions, not for tests.
//

// Opens the file at Path as fopen() does, but with fopen_s() where MSVC deprecates
// fopen(). Returns NULL when it cannot open the file.
FILE* SimDtPcie_OpenFile(const char* Path, const char* Mode);

// Checks whether Handle has exclusive access to the DTA-2178 object whose UUID has index
// ObjectIndex + 1, and returns what the driver would:
//
//   DT_STATUS_OK                 Handle has it
//   DT_STATUS_EXCL_ACCESS_REQD   no handle has it
//   DT_STATUS_IN_USE             another handle has it
//
// The caller holds the emulator's lock.
uint32_t SimDtPcie_CheckExclAccess(void* Handle, int ObjectIndex);

// Take and release the emulator's lock. A test control takes it to read or change state
// that a command on another thread may be using. The lock is not recursive.
void SimDtPcie_Lock(void);
void SimDtPcie_Unlock(void);
