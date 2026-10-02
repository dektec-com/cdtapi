// #*#*#*#*#*#*#*#*#*#*#*#*# DtNmosAvFifoStub.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The NMOS bridge of the AV FIFO in a build without CDTAPI_WITH_NMOS
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The bridge's functions, exported in every build so that every build has the same
// exports: a program built against a library with NMOS loads against one without, and
// learns from DtapiHasNmos, or from DTAPI_E_NOT_SUPPORTED, that NMOS is not there. They
// take pointers only, so nothing here needs a header of dtnmos's.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "AvFifo/DtAvError.h" // The failure text.
#include "cdtapi_avfifo.h"    // The FIFOs' configurations.
#include "cdtapi_constants.h" // Result codes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The types of dtnmos the bridge's functions take, as cdtapi_nmos.h has them through
// dtnmos's headers; here only their names, as only pointers to them are passed. The
// callbacks are passed on and never called, so any function pointer stands for them.
typedef struct DtNmosAvFifoRxChange DtNmosAvFifoRxChange;
typedef struct DtNmosAvFifoTxChange DtNmosAvFifoTxChange;
typedef struct DtNmosReceiverActivation DtNmosReceiverActivation;
typedef struct DtNmosSenderActivation DtNmosSenderActivation;
typedef struct DtNmosFlow DtNmosFlow;
typedef struct DtNmosDeviceConfig DtNmosDeviceConfig;
typedef struct DtNmosId DtNmosId;
typedef struct DtNmosNode DtNmosNode;
typedef struct DtNmosReceiverConfig DtNmosReceiverConfig;
typedef struct DtNmosSenderConfig DtNmosSenderConfig;
typedef void (*DtNmosReceiverActivateFunc)(void);
typedef void (*DtNmosSenderActivateFunc)(void);

// The declarations of cdtapi_nmos.h, which this build does not install.
CDTAPI_API DtapiResult DtNmosAvFifo_ApplyRxChange(AvFifo_RxFifo* Fifo,
                                                  const DtNmosAvFifoRxChange* Change);
CDTAPI_API DtapiResult DtNmosAvFifo_ApplyTxChange(AvFifo_TxFifo* Fifo,
                                                  const DtNmosAvFifoTxChange* Change);
CDTAPI_API DtapiResult DtNmosAvFifo_RxChangeFromActivation(
    const DtNmosReceiverActivation* Activation, St2110_RxFrameFormat Format,
    DtNmosAvFifoRxChange* Change);
CDTAPI_API DtapiResult DtNmosAvFifo_TxChangeFromActivation(
    const DtNmosSenderActivation* Activation, DtNmosAvFifoTxChange* Change);
CDTAPI_API DtapiResult DtNmosAvFifo_AddDevice(DtNmosNode* Node, const DtDevice* Device,
                                              int Port, const DtNmosDeviceConfig* Config,
                                              DtNmosId* Id);
CDTAPI_API DtapiResult DtNmosAvFifo_AddReceiver(DtNmosNode* Node, AvFifo_RxFifo* Fifo,
                                                const DtNmosReceiverConfig* Config,
                                                DtNmosReceiverActivateFunc Activate,
                                                void* User, DtNmosId* Id);
CDTAPI_API DtapiResult DtNmosAvFifo_AddSender(DtNmosNode* Node, AvFifo_TxFifo* Fifo,
                                              const DtNmosSenderConfig* Config,
                                              DtNmosSenderActivateFunc Activate,
                                              void* User, DtNmosId* Id);
CDTAPI_API DtapiResult DtNmosAvFifo_FlowFromTxFifo(AvFifo_TxFifo* Fifo, DtNmosFlow* Flow);
CDTAPI_API DtapiResult DtNmosAvFifo_RxConfigFromFlow(const DtNmosFlow* Flow,
                                                     St2110_RxFrameFormat Format,
                                                     St2110_RxConfigVideo* Video,
                                                     St2110_RxConfigAudio* Audio,
                                                     AvFifo_IpPars* IpPars);
CDTAPI_API DtapiResult DtNmosAvFifo_TxConfigFromFlow(const DtNmosFlow* Flow,
                                                     St2110_TxConfigVideo* Video,
                                                     St2110_TxConfigAudio* Audio,
                                                     AvFifo_IpPars* IpPars);
CDTAPI_API DtapiResult DtNmosAvFifo_UpdateSender(DtNmosNode* Node, const DtNmosId* Id,
                                                 AvFifo_TxFifo* Fifo);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NoNmos -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult NoNmos(const char* Where)
{
    return DtAvError_Set(DTAPI_E_NOT_SUPPORTED, Where,
                         "The library was built without NMOS, CDTAPI_WITH_NMOS");
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Build +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtapiHasNmos -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtapiHasNmos(void)
{
    return false;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Nodes +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_AddDevice -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtNmosAvFifo_AddDevice(DtNmosNode* Node, const DtDevice* Device, int Port,
                                   const DtNmosDeviceConfig* Config, DtNmosId* Id)
{
    (void)Node;
    (void)Device;
    (void)Port;
    (void)Config;
    (void)Id;
    return NoNmos("DtNmosAvFifo_AddDevice");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_AddReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtNmosAvFifo_AddReceiver(DtNmosNode* Node, AvFifo_RxFifo* Fifo,
                                     const DtNmosReceiverConfig* Config,
                                     DtNmosReceiverActivateFunc Activate, void* User,
                                     DtNmosId* Id)
{
    (void)Node;
    (void)Fifo;
    (void)Config;
    (void)Activate;
    (void)User;
    (void)Id;
    return NoNmos("DtNmosAvFifo_AddReceiver");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_AddSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtNmosAvFifo_AddSender(DtNmosNode* Node, AvFifo_TxFifo* Fifo,
                                   const DtNmosSenderConfig* Config,
                                   DtNmosSenderActivateFunc Activate, void* User,
                                   DtNmosId* Id)
{
    (void)Node;
    (void)Fifo;
    (void)Config;
    (void)Activate;
    (void)User;
    (void)Id;
    return NoNmos("DtNmosAvFifo_AddSender");
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Activations +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_ApplyRxChange -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtNmosAvFifo_ApplyRxChange(AvFifo_RxFifo* Fifo,
                                       const DtNmosAvFifoRxChange* Change)
{
    (void)Fifo;
    (void)Change;
    return NoNmos("DtNmosAvFifo_ApplyRxChange");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_ApplyTxChange -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtNmosAvFifo_ApplyTxChange(AvFifo_TxFifo* Fifo,
                                       const DtNmosAvFifoTxChange* Change)
{
    (void)Fifo;
    (void)Change;
    return NoNmos("DtNmosAvFifo_ApplyTxChange");
}

// .-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_RxChangeFromActivation -.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult
DtNmosAvFifo_RxChangeFromActivation(const DtNmosReceiverActivation* Activation,
                                    St2110_RxFrameFormat Format,
                                    DtNmosAvFifoRxChange* Change)
{
    (void)Activation;
    (void)Format;
    (void)Change;
    return NoNmos("DtNmosAvFifo_RxChangeFromActivation");
}

// .-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_TxChangeFromActivation -.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtNmosAvFifo_TxChangeFromActivation(const DtNmosSenderActivation* Activation,
                                                DtNmosAvFifoTxChange* Change)
{
    (void)Activation;
    (void)Change;
    return NoNmos("DtNmosAvFifo_TxChangeFromActivation");
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Flows +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_FlowFromTxFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtNmosAvFifo_FlowFromTxFifo(AvFifo_TxFifo* Fifo, DtNmosFlow* Flow)
{
    (void)Fifo;
    (void)Flow;
    return NoNmos("DtNmosAvFifo_FlowFromTxFifo");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_RxConfigFromFlow -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtNmosAvFifo_RxConfigFromFlow(const DtNmosFlow* Flow,
                                          St2110_RxFrameFormat Format,
                                          St2110_RxConfigVideo* Video,
                                          St2110_RxConfigAudio* Audio,
                                          AvFifo_IpPars* IpPars)
{
    (void)Flow;
    (void)Format;
    (void)Video;
    (void)Audio;
    (void)IpPars;
    return NoNmos("DtNmosAvFifo_RxConfigFromFlow");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_TxConfigFromFlow -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtNmosAvFifo_TxConfigFromFlow(const DtNmosFlow* Flow,
                                          St2110_TxConfigVideo* Video,
                                          St2110_TxConfigAudio* Audio,
                                          AvFifo_IpPars* IpPars)
{
    (void)Flow;
    (void)Video;
    (void)Audio;
    (void)IpPars;
    return NoNmos("DtNmosAvFifo_TxConfigFromFlow");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAvFifo_UpdateSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtNmosAvFifo_UpdateSender(DtNmosNode* Node, const DtNmosId* Id,
                                      AvFifo_TxFifo* Fifo)
{
    (void)Node;
    (void)Id;
    (void)Fifo;
    return NoNmos("DtNmosAvFifo_UpdateSender");
}
