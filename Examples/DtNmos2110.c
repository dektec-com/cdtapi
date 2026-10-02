// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtNmos2110.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Example: an NMOS node whose receiver or sender is an AV FIFO
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Makes an IP port of a DekTec card an NMOS node, registered with the registry at
// --registry. The node has the port as its device, and on it a receiver (--receive) or a
// sender (--send) of an AV FIFO. An NMOS controller can then connect them (IS-05):
// the receiver receives the stream the controller names, and the sender sends a moving
// test pattern, or with --audio a tone, to where the controller says. The program runs
// for --seconds, and prints each change:
//
//     9211000001:1  node 0b5c3a1e-... at http://192.168.1.20:41005
//     9211000001:1  receiver 7d1f20c4-...  video as 10b
//     9211000001:1  receive 239.1.1.1:5000
//     9211000001:1  received 250 frames
//     9211000001:1  stop
//
// How a controller's request reaches the FIFO: the node calls a callback on a thread of
// its own, but only the program's main thread may touch the FIFO. So the callback
// converts the request into a change, refusing what the FIFO cannot do, puts the change
// in a mailbox, and waits. The main thread takes it between two frames, applies it, and
// answers; the callback passes the answer on to the controller.
//
// A scheduled request is applied when the node calls the callback for it, which is at
// the scheduled time, as ActivationLeadMs is 0 here.
//
// Needs a library built with the NMOS bridge, and dtnmos with libcurl and the server.
// Exits with 0 when the time is over, 2 when there is no IP port, and 1 when a call fails
// or the command line is wrong.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Example includes
#include "Common/ExampleAvFifo.h"  // The AV FIFO header and what the 2110 examples share.
#include "Common/ExampleCommon.h"  // The API and what the examples share.
#include "Common/ExampleMailbox.h" // The callback's way to the FIFO's owner.

// CDTAPI includes
#include "cdtapi_nmos.h" // The NMOS bridge, and dtnmos.

// How long the callback waits for the main thread's answer.
#define ANSWER_TIMEOUT_MS 5000

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static const ExampleOption g_Options[] = {
    {"--receive", false, "Register a receiver of a receive FIFO"},
    {"--send", false, "Register a sender of a transmit FIFO"},
    {"--registry", true, "The base URL of the registry, e.g. http://registry:8010"},
    {"--host", true, "The address of the node's APIs; found on the way to the registry"},
    {"--api-port", true, "The port of the node's APIs; any free one without it"},
    {"--seconds", true, "How long the node runs; 60 without it"},
    {"--serial", true, "The device's serial number; the first device with an IP port"},
    {"--port", true, "The port number; the first IP port"},
    {"--format", true, "Receive: raw, 8b, 10b, 10bto8b or planar; 10b without it"},
    {"--ip", true, "Send: the group to send to until a controller says; 239.1.2.3"},
    {"--udp", true, "Send: the UDP port to send to until a controller says; 5004"},
    {"--width", true, "Send: pixels a row; 1920 without it"},
    {"--height", true, "Send: rows a frame; 1080 without it"},
    {"--rate", true, "Send: frames a second; 50 without it"},
    {"--8bit", false, "Send 8-bit video; 10-bit without it"},
    {"--audio", true, "Audio of this many channels instead of video"},
    {"--pipe", true, EXAMPLE_PIPE_HELP},
};

// The namespace of this example's node IDs, a version 4 UUID of its own, so that a node
// of the same port and direction has the same ID each time it runs.
static const DtNmosId g_Namespace = {"3e9c4b27-5a10-4d8e-b6f1-2c7a9d0e4f58"};

// What the callback needs: the mailbox to the main thread, and the frame format to make
// a receiver's change for. Only the main thread touches the FIFOs.
typedef struct Program
{
    ExampleMailbox* Mailbox;
    St2110_RxFrameFormat Format; // Of the receiver
} Program;

// A change the callback puts in the mailbox: of the receiver or of the sender.
typedef struct Posted
{
    bool Receiver;
    DtNmosAvFifoRxChange Rx;
    DtNmosAvFifoTxChange Tx;
} Posted;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AskOwner -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Puts Change in the mailbox, waits for the main thread's answer, and returns it as the
// node's result, which the node passes on to the controller.
//
static DtNmosResult AskOwner(Program* Prog, const Posted* Change)
{
    unsigned int Result = DTAPI_OK;
    char Text[EXAMPLE_MAILBOX_TEXT_SIZE];
    if (!ExampleMailbox_Ask(Prog->Mailbox, Change, sizeof(*Change), ANSWER_TIMEOUT_MS,
                            &Result, Text))
        return DtNmos_SetLastError(DTNMOS_E_TIMEOUT, "The FIFO's owner did not answer");
    return Result == DTAPI_OK ? DTNMOS_OK : DtNmos_SetLastError(DTNMOS_E_STATE, Text);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ActivateReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The receiver's callback, called on a thread of the node when a controller connects or
// disconnects it. A stream the FIFO cannot receive is refused here, without asking the
// main thread.
//
static DtNmosResult ActivateReceiver(void* User, const DtNmosId* Receiver,
                                     const DtNmosReceiverActivation* Activation)
{
    (void)Receiver;
    Program* Prog = (Program*)User;
    Posted Change;
    memset(&Change, 0, sizeof(Change));
    Change.Receiver = true;
    if (DtNmosAvFifo_RxChangeFromActivation(Activation, Prog->Format, &Change.Rx) !=
        DTAPI_OK)
        return DtNmos_SetLastError(DTNMOS_E_INVALID_ARGUMENT, GetLastException());
    return AskOwner(Prog, &Change);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ActivateSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The sender's callback, called on a thread of the node when a controller enables,
// disables or redirects it.
//
static DtNmosResult ActivateSender(void* User, const DtNmosId* Sender,
                                   const DtNmosSenderActivation* Activation)
{
    (void)Sender;
    Program* Prog = (Program*)User;
    Posted Change;
    memset(&Change, 0, sizeof(Change));
    if (DtNmosAvFifo_TxChangeFromActivation(Activation, &Change.Tx) != DTAPI_OK)
        return DtNmos_SetLastError(DTNMOS_E_INVALID_ARGUMENT, GetLastException());
    return AskOwner(Prog, &Change);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FormatFrom -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets *Format to the frame format --format names ("raw", "8b", "10b", also when
// not given, "10bto8b" or "planar"). Returns false for another name.
//
static bool FormatFrom(const char* Name, St2110_RxFrameFormat* Format)
{
    if (Name == NULL || strcmp(Name, "10b") == 0)
        *Format = St2110_RxFrameFormat_Uyvy422_10b;
    else if (strcmp(Name, "raw") == 0)
        *Format = St2110_RxFrameFormat_Raw;
    else if (strcmp(Name, "8b") == 0)
        *Format = St2110_RxFrameFormat_Uyvy422_8b;
    else if (strcmp(Name, "10bto8b") == 0)
        *Format = St2110_RxFrameFormat_Uyvy422_10b_to_8b;
    else if (strcmp(Name, "planar") == 0)
        *Format = St2110_RxFrameFormat_Yuv422p_8b;
    else
        return false;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrintAddress -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Prints what a change did: where the FIFO now receives or sends to, or that it stopped.
//
static void PrintChange(const DtHwFuncDesc* Port, bool Receiver, bool Enabled,
                        const uint8_t Ip[16], int UdpPort)
{
    if (!Enabled)
        printf("%lld:%d  stop\n", (long long)Port->SerialNumber, Port->Port);
    else
        printf("%lld:%d  %s %u.%u.%u.%u:%d\n", (long long)Port->SerialNumber, Port->Port,
               Receiver ? "receive" : "send to", Ip[0], Ip[1], Ip[2], Ip[3], UdpPort);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Pattern -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Fills frame Number with the test pattern DtTransmit2110 sends: vertical bars that move
// to the left, or a 1 kHz square wave for audio.
//
static void Pattern(const ExampleAvConfig* Config, int Number, uint8_t* Data, int Size)
{
    if (Config->Channels > 0)
    {
        int Bytes = 3 * Config->Channels;
        for (int i = 0; i + Bytes <= Size; i += Bytes)
        {
            int Sample = (i / Bytes + Number * Config->SamplesPerFrame) % 48;
            uint32_t Value = Sample < 24 ? 0x200000u : 0xE00000u;
            for (int c = 0; c < Config->Channels; c++)
            {
                Data[i + 3 * c] = (uint8_t)(Value >> 16);
                Data[i + 3 * c + 1] = (uint8_t)(Value >> 8);
                Data[i + 3 * c + 2] = (uint8_t)Value;
            }
        }
        return;
    }
    for (int Row = 0; Row < Config->Height; Row++)
    {
        uint8_t* Line = Data + (size_t)Row * (size_t)ExampleAv_RowBytes(Config);
        for (int Pixel = 0; Pixel < Config->Width; Pixel += 2)
        {
            int Bar = ((Pixel + Number * 16) / 128) % 8;
            ExampleAv_WritePgroup(Config, Line, Pixel / 2, Bar % 2 == 0 ? 128 : 200,
                                  16 + Bar * 28, Bar < 4 ? 128 : 80);
        }
    }
}

// The state of the main thread, which owns the FIFO.
typedef struct Owner
{
    const DtHwFuncDesc* Port;
    DtDevice* Device;
    AvFifo_RxFifo* Rx; // Of --receive
    AvFifo_TxFifo* Tx; // Of --send
    const ExampleAvConfig* Config;
    bool Running;     // The FIFO is started
    int Frames;       // Received or sent since it started
    int64_t StartNs;  // Send: the time of day of the first frame
    int64_t PeriodNs; // Send: of a frame
} Owner;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrintFrames -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Prints how many frames the FIFO received or sent, if it is running. For a sender the
// FIFO's own count is used, as the program writes frames ahead of their time.
//
static void PrintFrames(const Owner* O)
{
    if (!O->Running)
        return;
    const int Frames =
        O->Rx != NULL ? O->Frames : AvFifo_TxFifo_GetStatistics(O->Tx).FramesOk;
    printf("%lld:%d  %s %d frames\n", (long long)O->Port->SerialNumber, O->Port->Port,
           O->Rx != NULL ? "received" : "sent", Frames);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Apply -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Applies a change the callback posted, and answers it.
//
static void Apply(Owner* O, ExampleMailbox* Mailbox, const Posted* Change)
{
    PrintFrames(O);
    unsigned int Result = DTAPI_OK;
    bool Enabled = false;
    if (Change->Receiver)
    {
        Result = DtNmosAvFifo_ApplyRxChange(O->Rx, &Change->Rx);
        Enabled = Change->Rx.MasterEnable;
        if (Result == DTAPI_OK)
            PrintChange(O->Port, true, Enabled, Change->Rx.IpPars.IpAddr,
                        Change->Rx.IpPars.Port);
    }
    else
    {
        Result = DtNmosAvFifo_ApplyTxChange(O->Tx, &Change->Tx);
        Enabled = Change->Tx.MasterEnable;
        if (Result == DTAPI_OK)
            PrintChange(O->Port, false, Enabled, Change->Tx.DestinationIp,
                        Change->Tx.DestinationPort);
    }
    O->Running = Result == DTAPI_OK && Enabled;
    O->Frames = 0;
    if (Result != DTAPI_OK)
        ExampleAv_Failed(Change->Receiver ? "DtNmosAvFifo_ApplyRxChange"
                                          : "DtNmosAvFifo_ApplyTxChange",
                         Result);
    if (O->Running && O->Tx != NULL)
    {
        DtTimeOfDay Now;
        if (DtDevice_GetTimeOfDay(O->Device, &Now) == DTAPI_OK)
            O->StartNs = ExampleAv_ToNs(&Now) + 100 * 1000 * 1000;
    }
    ExampleMailbox_Answer(Mailbox, Result,
                          Result == DTAPI_OK ? NULL : GetLastException());
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Receive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the frames that arrived, counts them, and gives them back to the FIFO.
//
static void Receive(Owner* O)
{
    for (AvFifo_Frame* Frame = AvFifo_RxFifo_Read(O->Rx); Frame != NULL;
         Frame = AvFifo_RxFifo_Read(O->Rx))
    {
        O->Frames++;
        AvFifo_RxFifo_ReturnToMemPool(O->Rx, Frame);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Send -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Keeps the FIFO half full with frames, one frame period apart in time of day. The card
// sends each frame at its time.
//
static void Send(Owner* O)
{
    const int Size = ExampleAv_FrameBytes(O->Config);
    while (AvFifo_TxFifo_GetFifoLoad(O->Tx) < AvFifo_TxFifo_GetMaxSize(O->Tx) / 2)
    {
        AvFifo_Frame* Frame = AvFifo_TxFifo_GetFromMemPool(O->Tx, Size);
        if (Frame == NULL)
            return;
        Pattern(O->Config, O->Frames, Frame->Data, Size);
        Frame->NumValidBytes = Size;
        Frame->NumRows = O->Config->Height;
        Frame->ToD = ExampleAv_FromNs(O->StartNs + (int64_t)O->Frames * O->PeriodNs);
        Frame->RtpTime = O->Config->Channels > 0
                             ? Tod2Rtp_Audio(&Frame->ToD, O->Config->SampleRate)
                             : Tod2Rtp_Video(&Frame->ToD);
        // A frame the full FIFO refuses stays the program's, to write again.
        unsigned int Result = AvFifo_TxFifo_Write(O->Tx, Frame);
        while (Result == DTAPI_E_FIFO_FULL)
        {
            Example_SleepMs(1);
            Result = AvFifo_TxFifo_Write(O->Tx, Frame);
        }
        if (Result != DTAPI_OK)
        {
            ExampleAv_Failed("AvFifo_TxFifo_Write", Result);
            O->Running = false;
            return;
        }
        O->Frames++;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Run -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The main thread's loop, for Seconds: applies each change the callback posts, and in
// between receives or sends frames.
//
static void Run(Owner* O, ExampleMailbox* Mailbox, int64_t Seconds)
{
    const int64_t EndMs = Example_NowMs() + Seconds * 1000;
    while (Example_NowMs() < EndMs)
    {
        Posted Change;
        if (ExampleMailbox_Take(Mailbox, &Change, sizeof(Change)))
            Apply(O, Mailbox, &Change);
        if (O->Running && O->Rx != NULL)
            Receive(O);
        else if (O->Running)
            Send(O);
        Example_SleepMs(2);
    }
    PrintFrames(O);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConfigureTx -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Configures the transmit FIFO for the stream the command line describes. The bridge
// registers that stream; the FIFO starts when a controller enables the sender.
//
static unsigned int ConfigureTx(AvFifo_TxFifo* Fifo, const ExampleAvConfig* Config)
{
    unsigned int Result = DTAPI_OK;
    if (Config->Channels > 0)
    {
        St2110_TxConfigAudio Audio;
        memset(&Audio, 0, sizeof(Audio));
        Audio.Format = St2110_AudioFormat_L24BE;
        Audio.NumChannels = Config->Channels;
        Audio.NumSamplesPerIpPacket = 48;
        Audio.SampleRate = Config->SampleRate;
        Result = AvFifo_TxFifo_ConfigureAudio(Fifo, &Audio);
    }
    else
    {
        St2110_TxConfigVideo Video;
        memset(&Video, 0, sizeof(Video));
        Video.Format = Config->EightBit ? St2110_TxFrameFormat_Uyvy422_8b
                                        : St2110_TxFrameFormat_Uyvy422_10b;
        Video.Packing.PayloadSize = -1;
        Video.Resolution.Width = Config->Width;
        Video.Resolution.Height = Config->Height;
        Video.Timing.Rate.Numerator = Config->Rate;
        Video.Timing.Rate.Denominator = 1;
        Result = AvFifo_TxFifo_ConfigureVideo(Fifo, &Video);
    }
    if (Result != DTAPI_OK)
        return Result;
    AvFifo_IpPars Pars;
    ExampleAv_IpPars(Config, &Pars);
    return AvFifo_TxFifo_SetIpPars(Fifo, &Pars);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OpenNode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Opens the node and serves its APIs. Its ID is made from the port and the direction,
// so that it is the same each run. Returns EXAMPLE_OK, or the exit code of the failure.
//
static int OpenNode(DtNmosNode* Node, const DtHwFuncDesc* Port, bool Receives,
                    const char* Registry, const char* Host, int64_t ApiPort)
{
    char Name[128];
    snprintf(Name, sizeof(Name), "%lld:%d %s", (long long)Port->SerialNumber, Port->Port,
             Receives ? "receive" : "send");
    DtNmosNodeConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    DtNmosId_FromName(&g_Namespace, Name, &Config.Id);
    char Label[160];
    snprintf(Label, sizeof(Label), "DtNmos2110 %s", Name);
    Config.Label = Label;
    Config.ApiHost = Host;
    Config.ApiPort = (uint16_t)ApiPort;
    Config.RegistrationUrl = Registry;
    Config.Http = DtNmos_CurlHttp;
    if (DtNmosNode_Open(Node, &Config) != DTNMOS_OK ||
        DtNmosNode_Serve(Node) != DTNMOS_OK)
    {
        printf("Cannot open the node: %s\n", DtNmos_GetLastError());
        return EXAMPLE_FAILED;
    }
    char Url[256];
    size_t Size = sizeof(Url);
    if (DtNmosNode_ApiUrl(Node, Url, &Size) != DTNMOS_OK)
        snprintf(Url, sizeof(Url), "?");
    printf("%lld:%d  node %s at %s\n", (long long)Port->SerialNumber, Port->Port,
           Config.Id.Text, Url);
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AddToNode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Adds the port as a device to the node, and the FIFO as its receiver or sender.
// Returns EXAMPLE_OK, or the exit code of the failure.
//
static int AddToNode(DtNmosNode* Node, Owner* O, Program* Prog, const char* FormatName)
{
    DtNmosId DeviceId;
    unsigned int Result =
        DtNmosAvFifo_AddDevice(Node, O->Device, O->Port->Port, NULL, &DeviceId);
    if (Result != DTAPI_OK)
        return ExampleAv_Failed("DtNmosAvFifo_AddDevice", Result);
    DtNmosId Id;
    if (O->Rx != NULL)
    {
        DtNmosReceiverConfig Config;
        memset(&Config, 0, sizeof(Config));
        Config.Size = sizeof(Config);
        Config.DeviceId = DeviceId;
        Config.Label = "receiver";
        Config.Media = O->Config->Channels > 0 ? DTNMOS_MEDIA_AUDIO : DTNMOS_MEDIA_VIDEO;
        Result =
            DtNmosAvFifo_AddReceiver(Node, O->Rx, &Config, ActivateReceiver, Prog, &Id);
        if (Result != DTAPI_OK)
            return ExampleAv_Failed("DtNmosAvFifo_AddReceiver", Result);
        printf("%lld:%d  receiver %s  %s%s\n", (long long)O->Port->SerialNumber,
               O->Port->Port, Id.Text, O->Config->Channels > 0 ? "audio" : "video as ",
               O->Config->Channels > 0 ? "" : FormatName);
        return EXAMPLE_OK;
    }
    DtNmosSenderConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    Config.DeviceId = DeviceId;
    Config.Label = "sender";
    Result = DtNmosAvFifo_AddSender(Node, O->Tx, &Config, ActivateSender, Prog, &Id);
    if (Result != DTAPI_OK)
        return ExampleAv_Failed("DtNmosAvFifo_AddSender", Result);
    printf("%lld:%d  sender %s\n", (long long)O->Port->SerialNumber, O->Port->Port,
           Id.Text);
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AttachAndRun -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Attaches the device and the FIFO, opens the node, adds the port and the FIFO, and
// runs the main loop. Returns the program's exit code.
//
static int AttachAndRun(Owner* O, Program* Prog, DtNmosNode* Node, int Argc, char** Argv,
                        const char* FormatName, int64_t ApiPort, int64_t Seconds)
{
    unsigned int Result = DtDevice_AttachToSerial(O->Device, O->Port->SerialNumber);
    if (Result != DTAPI_OK)
        return Example_Failed("DtDevice_AttachToSerial", Result);
    if (O->Rx != NULL)
    {
        Result = ExampleAv_AttachRx(O->Rx, O->Device, O->Port->Port, O->Config);
        if (Result != DTAPI_OK)
            return ExampleAv_Failed("AvFifo_RxFifo_Attach", Result);
    }
    else
    {
        Result = ExampleAv_AttachTx(O->Tx, O->Device, O->Port->Port, O->Config);
        if (Result == DTAPI_OK)
            Result = ConfigureTx(O->Tx, O->Config);
        if (Result != DTAPI_OK)
            return ExampleAv_Failed("Configuring the TxFifo", Result);
    }

    int Exit =
        OpenNode(Node, O->Port, O->Rx != NULL, Example_Value(Argc, Argv, "--registry"),
                 Example_Value(Argc, Argv, "--host"), ApiPort);
    if (Exit == EXAMPLE_OK)
        Exit = AddToNode(Node, O, Prog, FormatName);
    if (Exit == EXAMPLE_OK)
        Run(O, Prog->Mailbox, Seconds);
    // The node is closed before the FIFO stops, so that no callback waits for an answer.
    DtNmosNode_Close(Node);
    if (O->Rx != NULL)
        AvFifo_RxFifo_Stop(O->Rx);
    else
        AvFifo_TxFifo_Stop(O->Tx);
    return Exit;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main(int Argc, char** Argv)
{
    // Each line shows when it is printed, also through a pipe, as the program runs on.
    setvbuf(stdout, NULL, _IONBF, 0);
    int64_t Serial = 0;
    int64_t PortNumber = 0;
    int64_t ApiPort = 0;
    int64_t Seconds = 60;
    ExampleAvConfig Config;
    if (!Example_CheckArguments(
            Argc, Argv, "Registers an NMOS receiver or sender of an AV FIFO.", g_Options,
            (int)(sizeof(g_Options) / sizeof(g_Options[0]))) ||
        !Example_Int64(Argc, Argv, "--serial", &Serial) ||
        !Example_Int64(Argc, Argv, "--port", &PortNumber) ||
        !Example_Int64(Argc, Argv, "--api-port", &ApiPort) ||
        !Example_Int64(Argc, Argv, "--seconds", &Seconds) ||
        !ExampleAv_Config(Argc, Argv, &Config))
    {
        return EXAMPLE_FAILED;
    }
    const bool Receives = Example_HasFlag(Argc, Argv, "--receive");
    if (Receives == Example_HasFlag(Argc, Argv, "--send"))
    {
        printf("Give one of --receive and --send\n");
        return EXAMPLE_FAILED;
    }
    if (Example_Value(Argc, Argv, "--registry") == NULL)
    {
        printf("Give the registry with --registry\n");
        return EXAMPLE_FAILED;
    }
    if (ApiPort < 0 || ApiPort > 65535)
    {
        printf("Not a port: %lld\n", (long long)ApiPort);
        return EXAMPLE_FAILED;
    }
    Program Prog;
    memset(&Prog, 0, sizeof(Prog));
    const char* FormatName = Example_Value(Argc, Argv, "--format");
    if (!FormatFrom(FormatName, &Prog.Format))
    {
        printf("Unknown receive format: %s; raw, 8b, 10b, 10bto8b or planar\n",
               FormatName);
        return EXAMPLE_FAILED;
    }
    if (FormatName == NULL)
        FormatName = "10b";

    DtHwFuncDesc Port;
    int Exit = EXAMPLE_FAILED;
    unsigned int Result =
        Example_FindPort(Serial, (int)PortNumber, ExampleAv_IsIpPort, &Port);
    Owner O;
    memset(&O, 0, sizeof(O));
    O.Port = &Port;
    O.Config = &Config;
    O.PeriodNs = ExampleAv_PeriodNs(&Config);
    O.Device = DtDevice_Alloc();
    if (Receives)
        O.Rx = AvFifo_RxFifo_Alloc();
    else
        O.Tx = AvFifo_TxFifo_Alloc();
    DtNmosNode* Node = DtNmosNode_Alloc();
    Prog.Mailbox = ExampleMailbox_Create();
    if (Result == DTAPI_E_NOT_FOUND)
    {
        printf("No IP port that suits\n");
        Exit = EXAMPLE_NOTHING;
    }
    else if (Result != DTAPI_OK)
        Exit = Example_Failed("DtapiHwFuncScan", Result);
    else if (O.Device == NULL || (O.Rx == NULL && O.Tx == NULL) || Node == NULL ||
             Prog.Mailbox == NULL)
        Exit = Example_Failed("Allocating", DTAPI_E_OUT_OF_MEM);
    else
        Exit = AttachAndRun(&O, &Prog, Node, Argc, Argv, FormatName, ApiPort, Seconds);

    DtNmosNode_Free(Node);
    ExampleMailbox_Free(Prog.Mailbox);
    AvFifo_RxFifo_Free(O.Rx);
    AvFifo_TxFifo_Free(O.Tx);
    DtDevice_Free(O.Device);
    return Exit;
}
