// #*#*#*#*#*#*#*#*#*#*#*#*#*#* cdtapi_nmos.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Public C API joining the AV FIFO to NMOS through dtnmos
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The NMOS bridge connects the AV FIFOs of a DekTec IP port to NMOS. With it, a program
// registers a FIFO as an NMOS sender or receiver, lets an NMOS controller connect it, and
// converts between SDP flows and FIFO configurations. The node itself, and everything
// else NMOS, comes from dtnmos.
//
// The bridge never changes a FIFO on its own. It converts; the program decides, and
// applies the result on the thread that owns the FIFO.
//
// The bridge is part of the library when it is built with CDTAPI_WITH_NMOS, and this
// header is installed only then. A library built without it exports the same functions,
// which return DTAPI_E_NOT_SUPPORTED; DtapiHasNmos() tells which build a program has.
//
// When a function fails, GetLastException() says why. Addresses must be literal IP
// addresses: the bridge does not look up host names, and returns DTAPI_E_NOT_SUPPORTED
// for one.

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "cdtapi.h"         // Results.
#include "cdtapi_avfifo.h"  // The FIFOs and their configurations.
#include "cdtapi_service.h" // The PTP clock slave of a port.

// dtnmos includes
#include "dtnmos_node.h" // Nodes, their devices, senders and receivers.
#include "dtnmos_sdp.h"  // Flows.

#ifdef __cplusplus
extern "C"
{
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Nodes +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// In NMOS, a node (the program) holds devices, and each device holds the senders and
// receivers of one piece of equipment. The bridge represents each IP port of a DekTec
// card as one NMOS device. So a program:
//
// 1. opens a node with dtnmos (DtNmosNode_Open);
// 2. registers each IP port it uses with DtNmosAvFifo_AddDevice;
// 3. registers each FIFO of that port with DtNmosAvFifo_AddSender or
//    DtNmosAvFifo_AddReceiver.
//
// The configs are those of dtnmos. Fields the program leaves empty, such as IDs and
// labels, the bridge fills in. The IDs it makes are the same every time the program
// runs, so that a registry and its controllers recognize the port after a restart.
//

// Registers an IP port of a DekTec card with an NMOS node, as an NMOS device. Its senders
// and receivers are added to this device later.
//
// Device must be attached, and Port (counted from 1) must be an IP port. Config may be
// NULL; if it gives no ID or label, the bridge makes them, e.g. the label
// "DTA-2110 2110000076 port 1". The device's ID is returned in *Id: pass it to
// DtNmosAvFifo_AddSender() and DtNmosAvFifo_AddReceiver(). To unregister the port, call
// DtNmosNode_Remove() with the ID.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_DEVICE         Device is not attached
//   DTAPI_E_NO_SUCH_PORT   Device has no such port
//   DTAPI_E_NOT_SUPPORTED  the port has no AV FIFO
//   DTAPI_E_INVALID_ARG    Config->Size is too small, or the node already has the ID
//   DTAPI_E_STATE          the node is not open
CDTAPI_API DtapiResult DtNmosAvFifo_AddDevice(DtNmosNode* Node, const DtDevice* Device,
                                              int Port, const DtNmosDeviceConfig* Config,
                                              DtNmosId* Id);

// Adds an NMOS receiver for Fifo to Node, and returns its ID in *Id.
//
// Fifo must be attached; it need not be configured yet. In Config, set DeviceId to the
// ID DtNmosAvFifo_AddDevice() returned, and set Label and Media. If Id is empty, the
// bridge makes one from the device and the label. If InterfaceIp is empty, the bridge
// uses the address of the FIFO's port.
//
// The node calls Activate, with User, when a controller connects or disconnects the
// receiver. Activate runs on a thread of the node and must not touch the FIFO; see
// "Activations" below.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG         an argument is NULL, Config->Size is too small, or
//                               Config has no DeviceId, or neither an Id nor a Label
//   DTAPI_E_NOT_ATTACHED        Fifo is not attached
//   DTAPI_E_NO_ADAPTER_IP_ADDR  the port has no IP address
// and the errors of DtNmosNode_AddReceiver().
CDTAPI_API DtapiResult DtNmosAvFifo_AddReceiver(DtNmosNode* Node, AvFifo_RxFifo* Fifo,
                                                const DtNmosReceiverConfig* Config,
                                                DtNmosReceiverActivateFunc Activate,
                                                void* User, DtNmosId* Id);

// Adds an NMOS sender for Fifo to Node, and returns its ID in *Id.
//
// In Config, set DeviceId and Label as for a receiver. If Config->Flow is NULL, the
// bridge describes the stream with DtNmosAvFifo_FlowFromTxFifo(); Fifo must then be
// configured and have IP parameters. To change the description, e.g. to add HDR, make
// the flow with DtNmosAvFifo_FlowFromTxFifo(), change it, and pass it in Config->Flow.
// If SourceIp is empty, the bridge uses the address the FIFO sends from.
//
// The node calls Activate, with User, when a controller enables, disables or redirects
// the sender. Activate runs on a thread of the node and must not touch the FIFO.
//
// Returns DTAPI_OK, or the errors of DtNmosAvFifo_AddReceiver() and, when the bridge
// describes the stream, of DtNmosAvFifo_FlowFromTxFifo().
CDTAPI_API DtapiResult DtNmosAvFifo_AddSender(DtNmosNode* Node, AvFifo_TxFifo* Fifo,
                                              const DtNmosSenderConfig* Config,
                                              DtNmosSenderActivateFunc Activate,
                                              void* User, DtNmosId* Id);

// Fills *Clock with the clock of the node, as IS-04 has it, from the PTP clock slave of
// port Port, counted from 1, of Device: the grandmaster the slave has chosen, with
// whether it is traceable to TAI and whether the slave is locked to it yet; without a
// grandmaster, an internal clock. Give it to the node in the Clock of its
// DtNmosNodeConfig, and later with DtNmosNode_SetClock(), e.g. each time the program
// checks its lock; the node registers again only when the clock changed.
//
// *Clock is internal after a failure too. Returns DTAPI_OK, DTAPI_E_INVALID_ARG for a
// NULL Clock, or the errors of DtDevice_GetPtpStatus(), such as
// DTAPI_E_CONNECT_TO_SERVICE when DtapiService does not run.
CDTAPI_API DtapiResult DtNmosAvFifo_ClockFromPort(const DtDevice* Device, int Port,
                                                  DtNmosClock* Clock);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Activations +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// When a controller connects, disconnects or redirects a sender or receiver, the node
// calls its Activate callback on a thread of the node. The FIFO, however, belongs to the
// program's thread that reads or writes it, and only that thread may change it. So:
//
// 1. In the callback, convert the activation into a change with
//    DtNmosAvFifo_RxChangeFromActivation() or DtNmosAvFifo_TxChangeFromActivation().
//    This fails for a stream the FIFO cannot handle; return the failure to the node.
// 2. Pass the change to the FIFO's thread, e.g. through a queue, and wait for the result.
// 3. On the FIFO's thread, apply it with DtNmosAvFifo_ApplyRxChange() or
//    DtNmosAvFifo_ApplyTxChange(), and return the result to the callback.
// 4. The callback returns the result to the node, which reports it to the controller.
//
// A change contains no pointers, so it can be copied with =. Examples/DtNmos2110.c shows
// the whole sequence.
//

// A change to a receive FIFO, made from a controller's activation.
typedef struct DtNmosAvFifoRxChange
{
    size_t Size;       // Set by the bridge
    bool MasterEnable; // False: stop receiving; the other fields are not used
    bool HasConfig;    // True: Video or Audio holds a new configuration; false: keep
                       // the FIFO's configuration and change only IpPars
    DtNmosMedia Media; // Which of Video and Audio is valid, when HasConfig
    St2110_RxConfigVideo Video;
    St2110_RxConfigAudio Audio;
    AvFifo_IpPars IpPars; // The stream to receive
    uint64_t AtNs;        // When the controller wants the change to take effect, in
                          // nanoseconds of TAI since the PTP epoch
} DtNmosAvFifoRxChange;

// A change to a transmit FIFO, made from a controller's activation. Only the destination
// changes: the FIFO keeps its format, payload type and other IP parameters, and always
// sends from its port's own address.
typedef struct DtNmosAvFifoTxChange
{
    size_t Size;                 // Set by the bridge
    bool MasterEnable;           // False: stop sending; the other fields are not used
    uint8_t DestinationIp[16];   // 4 bytes for IPv4, 16 for IPv6, as in AvFifo_IpPars
    IpProtocolVersion IpVersion; // Of DestinationIp
    int DestinationPort;         // UDP port
    uint64_t AtNs;               // As in DtNmosAvFifoRxChange
} DtNmosAvFifoTxChange;

// Applies Change to Fifo. Call it on the thread that owns the FIFO.
//
// Stops the FIFO. Then, unless MasterEnable is false, configures it if HasConfig is true,
// sets the new IP parameters and starts it again. Frames the program still holds stay
// valid. If a step fails, the FIFO stays stopped.
//
// Returns DTAPI_OK, DTAPI_E_INVALID_ARG for a NULL argument or a Size that is too small,
// or the error of the FIFO call that failed.
CDTAPI_API DtapiResult DtNmosAvFifo_ApplyRxChange(AvFifo_RxFifo* Fifo,
                                                  const DtNmosAvFifoRxChange* Change);

// Applies Change to Fifo. Call it on the thread that owns the FIFO.
//
// If MasterEnable is false, stops the FIFO. Otherwise, stops it, replaces the
// destination address and port in its IP parameters, and starts it again. If a step
// fails, the FIFO stays stopped.
//
// Returns DTAPI_OK, DTAPI_E_INVALID_ARG for a NULL argument or a Size that is too small,
// DTAPI_E_NO_IPPARS if the FIFO has no IP parameters yet (it is then left as it was), or
// the error of the FIFO call that failed.
CDTAPI_API DtapiResult DtNmosAvFifo_ApplyTxChange(AvFifo_TxFifo* Fifo,
                                                  const DtNmosAvFifoTxChange* Change);

// Converts the activation of a receiver into *Change. Call it in the callback.
//
// Format is the frame format the program wants the FIFO to deliver. If the activation
// carries an SDP, *Change gets the FIFO configuration for it, as
// DtNmosAvFifo_RxConfigFromFlow() makes it. If it carries only a new address and port,
// *Change keeps the FIFO's configuration and changes only the IP parameters.
//
// Returns DTAPI_OK, DTAPI_E_INVALID_ARG for a NULL argument or an invalid address or
// port, or the errors of DtNmosAvFifo_RxConfigFromFlow(); a stream the FIFO cannot
// receive in Format is DTAPI_E_NOT_SUPPORTED.
CDTAPI_API DtapiResult DtNmosAvFifo_RxChangeFromActivation(
    const DtNmosReceiverActivation* Activation, St2110_RxFrameFormat Format,
    DtNmosAvFifoRxChange* Change);

// Converts the activation of a sender into *Change. Call it in the callback.
//
// Returns DTAPI_OK, DTAPI_E_INVALID_ARG for a NULL argument or an invalid address or
// port, or DTAPI_E_NOT_SUPPORTED for a host name.
CDTAPI_API DtapiResult DtNmosAvFifo_TxChangeFromActivation(
    const DtNmosSenderActivation* Activation, DtNmosAvFifoTxChange* Change);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Flows +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A flow (DtNmosFlow) describes one RTP stream, as an SDP media section does: its format,
// destination, source and timing. These functions convert between flows and FIFO
// configurations, without a node; DtNmosSdp_Parse() and DtNmosSdp_Write() of dtnmos
// convert between flows and SDP text.
//

// Fills *Flow with a description of the stream Fifo sends, e.g. to write an SDP file or
// to register an NMOS sender.
//
// Fifo must be configured and have IP parameters; it need not be started. The source
// address is the address the FIFO sends from. Fields the FIFO does not know get common
// defaults: the colorimetry follows the frame size (BT601 for SD, BT709 for HD, BT2020
// for UHD), and the transfer characteristic is SDR; change those in *Flow for HDR. The
// reference clock is the grandmaster of the port's PTP clock slave while the slave is
// locked to it, ptp=IEEE1588-2008:<grandmaster>:<domain>, and otherwise the port's MAC
// address (localmac), also when DtapiService does not run. *Flow owns no memory.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_NOT_ATTACHED   Fifo is not attached
//   DTAPI_E_CONFIG         Fifo is not configured
//   DTAPI_E_NO_IPPARS      Fifo has no IP parameters
//   DTAPI_E_NOT_SUPPORTED  Fifo sends raw audio, whose encoding it does not know, or raw
//                          RTP packets, whose content it does not know
// and the errors of checking the port's network.
CDTAPI_API DtapiResult DtNmosAvFifo_FlowFromTxFifo(AvFifo_TxFifo* Fifo, DtNmosFlow* Flow);

// Converts Flow into the configuration of a receive FIFO that delivers frames in Format.
//
// Fills *Video for a video flow or *Audio for an audio flow; the other may be NULL.
// Fills *IpPars with the destination, port and payload type, and, if the flow names a
// source, a source filter for it.
//
// Which video flows a format accepts:
//   Raw                             any ST 2110-20 video, delivered as it arrives
//   Uyvy422_8b, Yuv422p_8b          YCbCr-4:2:2, 8 bits
//   Uyvy422_10b, Uyvy422_10b_to_8b  YCbCr-4:2:2, 10 bits
// Format does not apply to audio: L16 and L24 are delivered as L16BE and L24BE, and
// AM824 as raw bytes.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_NOT_SUPPORTED  the FIFO cannot receive the flow in Format, or the flow is
//                          compressed video, ancillary data, or the second path of an
//                          ST 2022-7 pair; GetLastException() names a format that works
//   DTAPI_E_INVALID_ARG    an argument is NULL, Flow->Size is too small, the
//                          configuration for the flow's media is NULL, or an address is
//                          invalid
CDTAPI_API DtapiResult DtNmosAvFifo_RxConfigFromFlow(const DtNmosFlow* Flow,
                                                     St2110_RxFrameFormat Format,
                                                     St2110_RxConfigVideo* Video,
                                                     St2110_RxConfigAudio* Audio,
                                                     AvFifo_IpPars* IpPars);

// Converts Flow into the configuration of a transmit FIFO that sends it.
//
// Fills *Video for a video flow or *Audio for an audio flow; the other may be NULL.
// Fills *IpPars with the destination, port and payload type. The source address is not
// part of it: a FIFO always sends from its port's own address.
//
// The FIFO sends YCbCr-4:2:2 video of 8 or 10 bits, and L16 or L24 audio. For interlaced
// and PsF video, the configured rate is the field rate, twice the flow's frame rate.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_NOT_SUPPORTED  the FIFO cannot send the flow, e.g. 4:4:4 or 12-bit video,
//                          AM824 audio, or what DtNmosAvFifo_RxConfigFromFlow() refuses
//   DTAPI_E_INVALID_ARG    as for DtNmosAvFifo_RxConfigFromFlow(), or the flow has no
//                          frame size, frame rate, sample rate or channel count
CDTAPI_API DtapiResult DtNmosAvFifo_TxConfigFromFlow(const DtNmosFlow* Flow,
                                                     St2110_TxConfigVideo* Video,
                                                     St2110_TxConfigAudio* Audio,
                                                     AvFifo_IpPars* IpPars);

// Updates the description of sender Id in Node after the program changed the format of
// Fifo. The node re-registers the sender with the new flow, as
// DtNmosAvFifo_FlowFromTxFifo() makes it.
//
// A new destination set by a controller needs no update: the node already knows it. If
// the program registered a flow of its own, it calls DtNmosNode_UpdateSender() instead.
//
// Returns DTAPI_OK, or the errors of DtNmosAvFifo_FlowFromTxFifo() and
// DtNmosNode_UpdateSender().
CDTAPI_API DtapiResult DtNmosAvFifo_UpdateSender(DtNmosNode* Node, const DtNmosId* Id,
                                                 AvFifo_TxFifo* Fifo);

#ifdef __cplusplus
}
#endif
