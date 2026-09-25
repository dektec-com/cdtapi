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

// The output delay of the DTA-2110: how much later than its time a packet leaves the
// card. Every card gets this value.
#define DT_AV_OUTPUT_DELAY_NS 14800

// How long the receive thread sleeps after a pass that found nothing or failed.
#define DT_AV_RX_IDLE_SLEEP_MS 2

typedef struct DtAvPort
{
    DtDevice Device; // The FIFO's own handle
    int PortIndex;   // From 0
    DtDrvObject Nw;  // The network function
    HwOrSwPipe PipePreference;
    uint8_t Mac[6]; // Read by DtAvPort_CheckNetwork
} DtAvPort;

// Attaches to port PortIndex of Device with a pipe preference, through a handle of the
// port's own. Where names the calling function for the failure text.
DtapiResult DtAvPort_Attach(DtAvPort* Port, const DtDevice* Device, int PortIndex,
                            HwOrSwPipe Preference, const char* Where);

// Closes the port's handle, which closes the pipes it opened.
void DtAvPort_Detach(DtAvPort* Port);

// Checks the link, the MAC address and the operating system's interface for the IP
// version of Pars.
DtapiResult DtAvPort_CheckNetwork(DtAvPort* Port, const AvFifo_IpPars* Pars,
                                  const char* Where);

// Opens a receive or transmit pipe by the preference: a hardware pipe for ForceHwPipe,
// one with a software fallback for PreferHwPipe and for Auto when HardwareIfAuto, else a
// software pipe. A forced hardware pipe that is in use gives DTAPI_E_OUT_OF_RESOURCES.
DtapiResult DtAvPort_OpenPipe(DtAvPort* Port, DtAvPipe* Pipe, bool IsRx,
                              bool HardwareIfAuto, const char* Where);

// Whether a FIFO uses a hardware pipe: from the pipe when started, else from the
// preference, and DTAPI_E_NOT_STARTED when that leaves it open.
DtapiResult DtAvPort_UsesHwPipe(const DtAvPort* Port, bool Started, const DtAvPipe* Pipe,
                                int* UsesHwPipe, const char* Where);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= IP parameters +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A FIFO's copy of the application's IP parameters, with its own copy of the sources.
typedef struct DtAvIpPars
{
    AvFifo_IpPars Pars; // SrcFlt points at Sources
    IpSrcFlt Sources[3];
} DtAvIpPars;

// Checks and copies Pars: an IP version, a port from 0 to 65535, at most three sources of
// one address; DTAPI_E_INVALID_ARG otherwise.
DtapiResult DtAvIpPars_Copy(DtAvIpPars* Copy, const AvFifo_IpPars* Pars,
                            const char* Where);

// Whether the parameters are IPv6.
bool DtAvIpPars_IsIpV6(const DtAvIpPars* Ip);

// The sources as DtNet_Join takes them, 16 bytes each, into Sources; returns how many.
int DtAvIpPars_CopySources(const DtAvIpPars* Ip, uint8_t Sources[3 * 16]);

// What a FIFO carries: nothing yet, audio or video.
typedef enum DtAvKind
{
    DT_AV_KIND_NONE,
    DT_AV_KIND_AUDIO,
    DT_AV_KIND_VIDEO
} DtAvKind;
