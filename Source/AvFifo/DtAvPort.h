// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtAvPort.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - What the receive and transmit FIFOs share: the port, its network and pipes
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// CDTAPI includes
#include "Device/DtDevice.h" // The FIFO's own handle to the device.
#include "DtAvPipe.h"        // Pipes.
#include "cdtapi_avfifo.h"   // IP parameters and pipe preferences.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Port +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A FIFO opens its own handle to the device, so that it does not depend on the
// application's handle staying open. A receive and a transmit FIFO can share a port: no
// exclusive access is taken.
//
// Each function takes Where, the name of the calling public function, for the text of a
// failure.
//

// How much later than its scheduled time a packet leaves a DTA-2110. CDTAPI uses this
// value for every card.
#define DT_AV_OUTPUT_DELAY_NS 14800

// How long the receive thread sleeps after a pass that found no packets or failed.
#define DT_AV_RX_IDLE_SLEEP_MS 2

typedef struct DtAvPort
{
    DtDevice Device;           // The FIFO's own handle to the device
    int PortIndex;             // The port, counting from 0
    DtDrvObject Nw;            // The port's network function
    HwOrSwPipe PipePreference; // Whether to use a hardware or software pipe
    uint8_t Mac[6];            // The port's MAC address, read by DtAvPort_CheckNetwork
} DtAvPort;

// Attaches to port PortIndex of Device, through a handle of the port's own, and finds
// its network function. Preference says which kind of pipe to open later. Fails when
// Device is not attached, has no such port, or the port has no A/V FIFO.
DtapiResult DtAvPort_Attach(DtAvPort* Port, const DtDevice* Device, int PortIndex,
                            HwOrSwPipe Preference, const char* Where);

// Closes the port's handle, which also closes the pipes opened through it.
void DtAvPort_Detach(DtAvPort* Port);

// Checks that the port can be used for the IP version of Pars: that it has a link, and
// that the operating system's network interface for it is up and has an address. Reads
// the port's MAC address on the way. Returns DTAPI_OK, or the result that says what is
// wrong, such as DTAPI_E_NO_LINK or DTAPI_E_NO_ADAPTER_IP_ADDR.
DtapiResult DtAvPort_CheckNetwork(DtAvPort* Port, const AvFifo_IpPars* Pars,
                                  const char* Where);

// Opens a receive or a transmit pipe of the kind the port's preference asks for:
//
//   ForceHwPipe     A hardware pipe; DTAPI_E_OUT_OF_RESOURCES when none is free
//   PreferHwPipe    A hardware pipe, or a software pipe when none is free
//   Auto            As PreferHwPipe when HardwareIfAuto, else as UseSwPipe
//   UseSwPipe       A software pipe
//
DtapiResult DtAvPort_OpenPipe(DtAvPort* Port, DtAvPipe* Pipe, bool IsRx,
                              bool HardwareIfAuto, const char* Where);

// Gets whether a FIFO uses a hardware pipe. Once the FIFO is started, the open pipe
// says; before that, the preference says, if it decides it. Returns DTAPI_E_NOT_STARTED
// when the preference leaves it open and the FIFO is not started.
DtapiResult DtAvPort_UsesHwPipe(const DtAvPort* Port, bool Started, const DtAvPipe* Pipe,
                                bool* UsesHwPipe, const char* Where);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= IP parameters +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Checks the IP parameters before a FIFO accepts them. Returns DTAPI_E_INVALID_ARG when
// the IP version is unknown, the UDP port is outside 0 to 65535, there are more than
// AVFIFO_MAX_SRC_FLT source filters, or the source filters have different addresses.
DtapiResult DtAvIpPars_Check(const AvFifo_IpPars* Pars, const char* Where);

// Copies the source filter addresses into Sources, 16 bytes each, in the form DtNet_Join
// takes them. Returns how many there are.
int DtAvIpPars_CopySources(const AvFifo_IpPars* Pars,
                           uint8_t Sources[AVFIFO_MAX_SRC_FLT * 16]);

// Returns whether the parameters are for IPv6.
bool DtAvIpPars_IsIpV6(const AvFifo_IpPars* Pars);

// What a FIFO carries.
typedef enum DtAvKind
{
    DT_AV_KIND_NONE,  // Not known yet
    DT_AV_KIND_AUDIO, // Audio
    DT_AV_KIND_RAW,   // RTP packets as they are
    DT_AV_KIND_VIDEO  // Video
} DtAvKind;
