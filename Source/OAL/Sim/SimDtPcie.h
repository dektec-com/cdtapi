// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# SimDtPcie.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - The emulated DtPcie device, the values it reports, and its test controls
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Identity +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// What the emulated card says about itself. Tests compare against these rather than
// against literals, so that changing the emulated card is a one-line edit here.
//
// It presents as a DTA-2178 with firmware variant 1, eight 12G-SDI/ASI ports with
// genlock, at the latest firmware version of that variant. DekTec PCI device IDs are the
// type number in hexadecimal, so 2178 becomes 0x0882.
//
// The serial number starts with 9 because DekTec serials start with the type number,
// and no card has type number 9: a serial seen in a log is recognisably emulated.
//

#define SIM_TYPE_NUMBER 2178
#define SIM_SERIAL 9217800001ULL
#define SIM_HARDWARE_REVISION 1
#define SIM_FIRMWARE_VERSION 4
#define SIM_FIRMWARE_VARIANT 1
#define SIM_VENDOR_ID 0x1A0E
#define SIM_DEVICE_ID 0x0882

// The subsystem IDs, the firmware build date, the location and the PCIe link are those a
// DTA-2178 in a PCIe 3.0 x4 slot reported with driver 3.6.4: no subsystem IDs, and an x8
// card on four lanes.
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

// The driver version the DTA-2178 in the Linux machine runs, new enough for every driver
// function the emulator has.
#define SIM_DRIVER_MAJOR 3
#define SIM_DRIVER_MINOR 6
#define SIM_DRIVER_MICRO 4
#define SIM_DRIVER_BUILD 398

// The emulator presents exactly one device, at index zero unless a test moves it. A scan
// returns just that one, which replaces the hardware rather than adding to it.
#define SIM_DEVICE_INDEX 0

// Ports 1 to 8 are SDI/ASI inputs and outputs, port 9 the genlock reference input, and
// port 10 the internal genlock reference, a virtual port. The odd SDI ports start as
// inputs and the even ones as outputs, as the card's defaults are.
#define SIM_PORT_COUNT 10
#define SIM_SDI_PORT_COUNT 8

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test controls +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The emulated device keeps its state, such as its I/O configuration, for the whole
// process and across handles, as a card does across opens. Tests that change it start
// from SimDtPcieReset. None of this is thread-safe; the emulator's state is meant to be
// changed by one test at a time.
//
// A fault makes the emulated driver refuse or mangle commands of one function code, the
// DT_FUNC_CODE_ number of an IOCTL, from then until the next reset. It is how the
// failure paths of the layers above are exercised without a card. Faults for up to four
// function codes can be active together; a second fault for the same code replaces the
// first.
//

// Restores the power-on state: the default I/O configuration, the identity above, and
// no faults.
void SimDtPcieReset(void);

// Makes the card report this firmware status, one of the DT_FWSTATUS_ values.
void SimDtPcieSetFirmwareStatus(int Status);

// Makes the driver report this version.
void SimDtPcieSetDriverVersion(int Major, int Minor, int Micro, int Build);

// Replaces a property of the card: absent when Present is false, otherwise with Value.
// Up to eight properties can be overridden together; overriding one again replaces the
// earlier override.
void SimDtPcieOverrideProperty(const char* Name, int PortIndex, bool Present,
                               uint64_t Value);

// Replaces a string property of the card the same way, sharing the eight slots. An
// override of one kind leaves the property of the other kind with that name as it is.
// Value is ignored when Present is false, and is cut to what the driver can answer.
void SimDtPcieOverrideString(const char* Name, int PortIndex, bool Present,
                             const char* Value);

// Makes reading a string property, when IsString, or a value property fail with the
// driver status Status, sharing the eight slots with the overrides above.
void SimDtPcieFailProperty(const char* Name, int PortIndex, bool IsString,
                           uint32_t Status);

// What the SDI receiver of a port reports, in the driver's terms: the fields of
// DT_SDIRX_CMD_GET_SDI_STATUS2, with its flags as integers, 0 for false.
typedef struct SimSdiSignal
{
    int CarrierDetect;
    int SdiLock;
    int LineLock;
    int Valid;
    int NumSymsHanc;    // Symbols per line in HANC, EAV and SAV included
    int NumSymsVidVanc; // Symbols per line in the active part
    int NumLinesF1;
    int NumLinesF2;
    int IsLevelB;
    uint32_t PayloadId; // The VPID, 0 for none
    int FramePeriod;    // Nanoseconds, 0 for unknown
    int SdiRate;        // A DT_DRV_SDIRATE_ value
} SimSdiSignal;

// Feeds the input of the SDI port at PortIndex, 0 to SIM_SDI_PORT_COUNT - 1, with Signal;
// NULL, as after a reset, leaves it without a signal: nothing detected and the SDI rate
// unknown. The receiver reports the signal only while the port is an input; on a port
// configured for ASI it reports the carrier alone, as the driver does.
void SimDtPcieSetSdiSignal(int PortIndex, const SimSdiSignal* Signal);

// Hides the signal of the SDI port at PortIndex from the next Reads status requests that
// the receiver answers, as if it arrived only then. A reset, a new signal or a new delay
// ends it.
void SimDtPcieDelaySdiSignal(int PortIndex, int Reads);

// Moves the device to another driver index, so that it is found only by looking past the
// indices before it.
void SimDtPcieSetIndex(int Index);

// The number of handles to the emulated device that are open, so that a test can check
// that a layer above closes what it opens.
int SimDtPcieOpenHandles(void);

// Refuses every command with FunctionCode with the driver status Status.
void SimDtPcieFailWithStatus(int FunctionCode, uint32_t Status);

// Answers every command with FunctionCode with one byte fewer than its output needs.
void SimDtPcieAnswerShort(int FunctionCode);

// Copies the input of the most recent command that reached the emulated driver, exactly
// as it arrived and whether or not it was carried out, so that a test can check every
// field a layer above sent. Copies at most Size bytes into Buf, stores the command's
// function code in *FunctionCode, and returns the size of the input; 0 when no command
// has arrived since the reset. Inputs are kept up to SIM_MAX_RECORDED_INPUT bytes.
size_t SimDtPcieLastInput(int* FunctionCode, void* Buf, size_t Size);

#define SIM_MAX_RECORDED_INPUT 1024

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Emulator parts +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// For the emulated functions, not for tests.
//

// Whether Handle holds the part whose UUID has index PartIndex plus one, as
// DtBc_ExclAccessCheck answers: DT_STATUS_OK when it does, DT_STATUS_EXCL_ACCESS_REQD
// when nobody does, DT_STATUS_IN_USE when another handle does. Called with the
// emulator's lock held.
uint32_t SimDtPcieCheckAccess(void* Handle, int PartIndex);

// Takes and releases the emulator's lock, for a test control that reads or changes state
// a command on another thread may be using. Not recursive.
void SimDtPcieLock(void);
void SimDtPcieUnlock(void);
