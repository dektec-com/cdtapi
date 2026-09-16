// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# SimDtPcie.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - The emulated DtPcie device, the values it reports, and its test controls
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_SIM_DT_PCIE_H
#define CDTAPILITE_SIM_DT_PCIE_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
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

#define SIM_DRIVER_MAJOR 1
#define SIM_DRIVER_MINOR 99
#define SIM_DRIVER_MICRO 0
#define SIM_DRIVER_BUILD 0

// The emulator presents exactly one device, at index zero. A scan returns just that one,
// which replaces the hardware rather than adding to it.
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

// Restores the power-on state: the default I/O configuration and no faults.
void SimDtPcieReset(void);

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

#endif // CDTAPILITE_SIM_DT_PCIE_H
