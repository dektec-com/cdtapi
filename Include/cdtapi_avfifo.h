// #*#*#*#*#*#*#*#*#*#*#*#*#*# cdtapi_avfifo.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Public C API for SMPTE 2110 video and audio through the A/V FIFO
//
// SPDX-License-Identifier: BSD-3-Clause
//
// An AV FIFO sends or receives one SMPTE ST 2110 stream, video or audio, on an IP port of
// a DekTec card. The program writes frames into a transmit FIFO, which packetizes them
// and has the card send each frame at its time of day; or it reads frames from a receive
// FIFO, which reassembles them from the packets that arrive. The names, values and
// layouts are those of DTAPI's AV FIFO, so that a program moves between the two easily.

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "cdtapi.h" // Results, devices and the time of day.

#ifdef __cplusplus
extern "C"
{
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// An exact ratio of two integers, e.g. a frame rate of 30000/1001.
typedef struct Ratio
{
    int Numerator;
    int Denominator;
} Ratio, FrameRate;

// The dimensions of the active video in pixels.
typedef struct VideoSize
{
    int Width;
    int Height;
} VideoSize;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= SMPTE 2110 receive +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The pixel format in which a receive FIFO delivers ST 2110-20 video frames. Raw delivers
// any video; the other formats convert YCbCr-4:2:2 of a certain bit depth.
typedef enum St2110_RxFrameFormat
{
    St2110_RxFrameFormat_Raw,               // The pixel groups as they arrive
    St2110_RxFrameFormat_Uyvy422_8b,        // 8-bit video, as 8-bit UYVY
    St2110_RxFrameFormat_Uyvy422_10b,       // 10-bit video, as 10-bit UYVY, packed least
                                            // significant bit first
    St2110_RxFrameFormat_Uyvy422_10b_to_8b, // 10-bit video, as 8-bit UYVY
    St2110_RxFrameFormat_Yuv422p_8b         // 8-bit video, as planes of Y, U and V
} St2110_RxFrameFormat;

// The format of ST 2110-30 and -31 audio.
typedef enum St2110_AudioFormat
{
    St2110_AudioFormat_L16BE, // 16-bit PCM, big endian
    St2110_AudioFormat_L24BE, // 24-bit PCM, big endian
    St2110_AudioFormat_Raw    // The packet payloads as they are, e.g. AM824 of ST 2110-31
} St2110_AudioFormat;

// How a receive FIFO receives audio. Each frame it delivers holds the samples of one
// packet, as they arrived: the FIFO does not convert them.
typedef struct St2110_RxConfigAudio
{
    St2110_AudioFormat Format;
    int SampleRate; // Samples per second, e.g. 48000
} St2110_RxConfigAudio;

// How a receive FIFO receives video.
typedef struct St2110_RxConfigVideo
{
    St2110_RxFrameFormat Format; // The format the frames are delivered in
} St2110_RxConfigVideo;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= SMPTE 2110 transmit +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// How the lines of a video picture are sent: all at once, or as two fields.
typedef enum St2110_VideoScanning
{
    St2110_VideoScanning_Progressive, // One picture per frame
    St2110_VideoScanning_Interlaced,  // Two fields per picture, taken at different times
    St2110_VideoScanning_PsF,         // Progressive pictures sent as two fields
} St2110_VideoScanning;

// The pixel format of the frames a program writes to a transmit FIFO.
typedef enum St2110_TxFrameFormat
{
    St2110_TxFrameFormat_Uyvy422_8b, // 8-bit UYVY
    St2110_TxFrameFormat_Uyvy422_10b // 10-bit UYVY, packed least significant bit first
} St2110_TxFrameFormat;

// How video is divided over RTP packets (the PM parameter of ST 2110-20).
typedef enum St2110_PackingMode
{
    St2110_PackingMode_General, // Whole pixel groups only
    St2110_PackingMode_Block    // Payloads whose size is a multiple of 180 bytes
} St2110_PackingMode;

// How a transmit FIFO spreads the packets of a frame over the frame period (the sender
// types of ST 2110-21).
typedef enum St2110_Scheduling
{
    St2110_Scheduling_Linear, // Evenly over the whole period (type NL, 2110TPNL)
    St2110_Scheduling_Gapped  // Over the active video only, with a gap for the vertical
                              // blanking (type N, 2110TPN)
} St2110_Scheduling;

// How a transmit FIFO packetizes video.
typedef struct St2110_VideoPacking
{
    bool OneLinePerPacket;          // True: a packet never holds data of two lines
    St2110_PackingMode PackingMode; // How video is divided over packets
    int PayloadSize; // Bytes of video per packet, a whole number of pixel groups; -1 for
                     // as many as a standard-size packet holds. Checked when the FIFO
                     // starts.
} St2110_VideoPacking;

// How to send ST 2110-40 ancillary data. Kept for DTAPI's layout; no function uses it.
typedef struct St2110_TxConfigAnc
{
    St2110_VideoScanning VideoScanning;
} St2110_TxConfigAnc;

// How a transmit FIFO sends audio.
typedef struct St2110_TxConfigAudio
{
    St2110_AudioFormat Format;
    int NumChannels;           // Channels in each packet
    int NumSamplesPerIpPacket; // Samples per channel in each packet: 48 is 1 ms at 48 kHz
    int SampleRate;            // Samples per second, e.g. 48000
} St2110_TxConfigAudio;

// When a transmit FIFO sends video.
typedef struct St2110_VideoTiming
{
    FrameRate Rate; // Frames per second for progressive video, fields per second for
                    // interlaced and PsF video
    St2110_Scheduling Scheduling;
    St2110_VideoScanning VideoScanning;
} St2110_VideoTiming;

// The full description of the video a transmit FIFO sends, which the library works out
// from St2110_TxConfigVideo. A program does not need it.
typedef struct St2110_TxConfigRawVideo
{
    Ratio ActiveVideo;           // Active-video fraction of the frame period
    bool Is420;                  // 4:2:0 chroma subsampling
    int NumRows;                 // Number of rows in a frame
    St2110_VideoPacking Packing; // Packetization parameters
    struct
    {
        int NumBytes;  // Number of bytes in a pixel group
        int NumPixels; // Number of pixels in a pixel group
    } PGroup;
    int RowSize;               // Number of bytes in a row
    St2110_VideoTiming Timing; // Timing
    int TrOffset;              // Nanoseconds from the start of a frame period to its
                               // first packet; a frame period starts at a whole number
                               // of periods since the epoch
} St2110_TxConfigRawVideo;

// How a transmit FIFO sends video.
typedef struct St2110_TxConfigVideo
{
    St2110_TxFrameFormat Format; // The format of the frames the program writes
    St2110_VideoPacking Packing;
    VideoSize Resolution; // Size of the active video, in pixels
    St2110_VideoTiming Timing;
} St2110_TxConfigVideo;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= AvFifo_IpPars +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The version of the Internet Protocol.
typedef enum IpProtocolVersion
{
    IpProtocolVersion_IPv4,
    IpProtocolVersion_IPv6
} IpProtocolVersion;

// How the stream travels: RTP over UDP, or UDP without RTP.
typedef enum IpTransportProtocol
{
    IpTransportProtocol_Rtp,
    IpTransportProtocol_Udp
} IpTransportProtocol;

// A source a receive FIFO accepts packets from (source-specific multicast).
typedef struct IpSrcFlt
{
    uint8_t IpAddr[16]; // 4 bytes for IPv4; 16 bytes for IPv6
    int Port;           // The source's UDP port, or -1 for any
} IpSrcFlt;

// The most source filters an AvFifo_IpPars holds.
#define AVFIFO_MAX_SRC_FLT 3

// The stream a FIFO sends or receives: its address and port, and how its packets are
// sent. The struct holds no pointers, so a copy made with = stands alone.
typedef struct AvFifo_IpPars
{
    uint8_t IpAddr[16]; // Destination or multicast group: 4 bytes for IPv4, 16 for IPv6
    IpProtocolVersion IpVersion;
    int Port;                            // UDP port
    IpSrcFlt SrcFlt[AVFIFO_MAX_SRC_FLT]; // Receive: the sources to accept packets from
    int NSrcFlt; // The number of SrcFlt in use, 0 to AVFIFO_MAX_SRC_FLT; 0 accepts any

    int DiffServ;        // Send: the DS byte of the IP header, e.g. 0x88 for AF41
    uint8_t Gateway[16]; // Send: the router to send through, or all zeros for the
                         // operating system's route
    int RtpPayloadType;  // 0 to 127
    int TimeToLive;      // Send: the IP time to live, or hop limit for IPv6
    IpTransportProtocol TransportProtocol;
    struct
    {
        int Id;       // 0 for no VLAN
        int Priority; // Send: the priority of the VLAN tag, 0 to 7
    } Vlan;
} AvFifo_IpPars;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= AvFifo_Frame +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// One unit of a stream: a video frame, a field of interlaced video, or the samples of
// one audio packet. Frames come from the FIFO's pool and go back to it.
typedef struct AvFifo_Frame
{
    uint32_t RtpTime; // RTP timestamp of the first sample
    DtTimeOfDay ToD;  // Send: when the card sends the frame; receive: when its first
                      // packet arrived

    uint8_t* Data;     // The frame's bytes
    int Field;         // Interlaced and PsF video: which field, 0 or 1
    int NumValidBytes; // Bytes of Data in use
    size_t Size;       // Bytes Data has room for

    // Set for received ST 2110-20 video only.
    bool Is420;  // The video is 4:2:0
    int NumRows; // Lines in the frame
} AvFifo_Frame;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Statistics +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// What a receive FIFO counted since it started.
typedef struct RxStatistics
{
    int FramesOk;         // Frames received whole, including those then dropped
    int FramesIncomplete; // Frames with missing packets
    int FramesSizeError;  // Frames of an unexpected size
    int Gaps;             // Jumps in the RTP sequence numbers between frames
    int IpPacketErrors;   // Packets with an invalid header
    int DroppedFrames;    // Frames dropped because the FIFO or its pool was full
    int SyncErrors;       // Times the FIFO lost the packet boundaries in the data from
                          // the card, and had to find the next packet
} RxStatistics;

// What a transmit FIFO counted since it started.
typedef struct TxStatistics
{
    int FramesOk; // Frames sent
} TxStatistics;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame properties +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// How the colour of a video picture is sampled, relative to its brightness.
typedef enum ChromaSubsampling
{
    ChromaSubsampling_Key, // A key (alpha) signal, without colour
    ChromaSubsampling_420,
    ChromaSubsampling_422,
    ChromaSubsampling_444,
} ChromaSubsampling;

// The video format of a received frame. A frame of interlaced video is one field:
// NLines and BytesPerFrame are those of the field, Height that of the whole picture.
typedef struct FrameProperties
{
    bool Is420;        // The video is 4:2:0
    int NLines;        // Lines in the frame
    int BytesPerLine;  // Bytes of one line
    int BytesPerFrame; // Bytes of the frame

    int Width;         // Pixels per line
    int Height;        // Lines of the whole picture
    bool IsInterlaced; // The frame is one field of interlaced video
    ChromaSubsampling Subsampling;
    int BitDepth; // Bits per sample
} FrameProperties;

// Works out the video format of a received frame from its size, its number of lines and
// whether it is 4:2:0.
//
// The formats it recognizes are 4:2:2 video of 8, 10, 12 or 16 bits in these sizes:
// 720x480 and 720x576 interlaced, 1920x1080 interlaced, and 1280x720, 1920x1080,
// 2048x1080 and 3840x2160 progressive.
//
// Returns DTAPI_OK and fills *Properties, or:
//   DTAPI_E_INVALID_ARG    Frame or Properties is NULL
//   DTAPI_E_UNSUP_FORMAT   no format matches
// On a failure *Properties is left as it was.
CDTAPI_API DtapiResult GetFrameProperties(const AvFifo_Frame* Frame,
                                          FrameProperties* Properties);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The FIFOs +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A receive FIFO receives one ST 2110 stream on an IP port of a DekTec card whose
// capabilities include DT_CAP_AVFIFO; a transmit FIFO sends one. Each FIFO has its own
// thread, a pipe on the card, and a pool of frames. A program uses a FIFO in these steps:
//
// 1. Alloc, then Attach it to a port. Ports are counted from 1.
// 2. ConfigureVideo or ConfigureAudio, and SetIpPars, while it is stopped.
// 3. Start.
// 4. Receive: Read a frame, use it, and give it back with ReturnToMemPool.
//    Send: take a frame with GetFromMemPool, fill it, and Write it.
// 5. Stop, then Detach and Free.
//
// Only one thread may use a FIFO at a time.
//
// A function that fails returns an error and sets the text GetLastException() returns
// on the calling thread. GetFromMemPool and SetMaxSize, which return no result, also set
// that text when they fail. The errors are:
//
//   DTAPI_E_INVALID_ARG         a NULL FIFO, frame or parameter, a parameter out of range
//   DTAPI_E_DEVICE              a device that is NULL or not attached
//   DTAPI_E_NO_SUCH_PORT        a port the device does not have
//   DTAPI_E_NOT_SUPPORTED       a port without DT_CAP_AVFIFO
//   DTAPI_E_ATTACHED            Attach of an attached FIFO
//   DTAPI_E_NOT_ATTACHED        a FIFO that must be attached and is not
//   DTAPI_E_STARTED             Clear, Configure, SetIpPars or Start of a started FIFO,
//                               which keeps running
//   DTAPI_E_NOT_STARTED         Write to a FIFO that is not started; UsesHwPipe before
//                               Start when the preference leaves the pipe open
//   DTAPI_E_CONFIG              Start or ReturnToMemPool before Configure
//   DTAPI_E_NO_IPPARS           Start before SetIpPars
//   DTAPI_E_NO_LINK             Start with the network link down
//   DTAPI_E_DISABLED            Start with the port's network interface disabled
//   DTAPI_E_NW_DRIVER           Start without the port's network interface
//   DTAPI_E_VLAN_NOT_FOUND      Start without the VLAN interface or its address
//   DTAPI_E_NO_ADAPTER_IP_ADDR  Start without an address of the IP version
//   DTAPI_E_BIND                Start when the port's address cannot be bound
//   DTAPI_E_OUT_OF_RESOURCES    Start with HwOrSwPipe_ForceHwPipe and no hardware pipe
//                               free, or when the FIFO's thread cannot start
//   DTAPI_E_MULTICASTJOIN       Start when joining the multicast group fails
//   DTAPI_E_DST_MAC_ADDR        Start of a transmit FIFO whose destination does not
//                               answer
//   DTAPI_E_INVALID_FORMAT      Write of a frame of the wrong size for the configuration
//   DTAPI_E_FIFO_FULL           Write to a full FIFO; the frame stays the application's
//   DTAPI_E_OUT_OF_MEM          no memory
//
// and the result of a driver command that fails. After a failed Start the FIFO is
// attached and stopped.
//

// Which pipe on the card a FIFO uses. A hardware pipe handles the packets on the card;
// a software pipe has the FIFO's thread handle them.
typedef enum HwOrSwPipe
{
    HwOrSwPipe_Auto,        // Hardware for video, or software when none is free;
                            // software for audio
    HwOrSwPipe_ForceHwPipe, // Hardware only; Start fails when none is free
    HwOrSwPipe_UseSwPipe,   // Software only
    HwOrSwPipe_PreferHwPipe // Hardware, or software when none is free
} HwOrSwPipe;

// Returns the message of the last failure on the calling thread, or "" if there was
// none. The message stays until the next failure on that thread.
CDTAPI_API const char* GetLastException(void);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Receiving -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

typedef struct AvFifo_RxFifoC AvFifo_RxFifo;

// Creates a receive FIFO. Returns NULL when there is not enough memory.
CDTAPI_API AvFifo_RxFifo* AvFifo_RxFifo_Alloc(void);

// Attaches Fifo to port Port of Device; ports are counted from 1. Attach uses
// HwOrSwPipe_Auto, Attach2 the pipe given. The FIFO opens a handle to the port of its
// own.
CDTAPI_API DtapiResult AvFifo_RxFifo_Attach(AvFifo_RxFifo* Fifo, const DtDevice* Device,
                                            int Port);
CDTAPI_API DtapiResult AvFifo_RxFifo_Attach2(AvFifo_RxFifo* Fifo, const DtDevice* Device,
                                             int Port, HwOrSwPipe Pipe);

// Discards the frames waiting in the FIFO. Fifo must be stopped.
CDTAPI_API DtapiResult AvFifo_RxFifo_Clear(AvFifo_RxFifo* Fifo);

// Sets what Fifo receives: video or audio, in which format. Fifo must be stopped. For
// audio, the FIFO holds 400 frames unless SetMaxSize set another size.
CDTAPI_API DtapiResult AvFifo_RxFifo_ConfigureAudio(AvFifo_RxFifo* Fifo,
                                                    const St2110_RxConfigAudio* Config);
CDTAPI_API DtapiResult AvFifo_RxFifo_ConfigureVideo(AvFifo_RxFifo* Fifo,
                                                    const St2110_RxConfigVideo* Config);

// Stops Fifo and detaches it from its port. Frames the program still holds stay valid
// until Free.
CDTAPI_API DtapiResult AvFifo_RxFifo_Detach(AvFifo_RxFifo* Fifo);

// Detaches Fifo if needed, and frees it. NULL does nothing.
CDTAPI_API void AvFifo_RxFifo_Free(AvFifo_RxFifo* Fifo);

// Frees *Fifo, as AvFifo_RxFifo_Free does, and sets *Fifo to NULL.
CDTAPI_API void AvFifo_RxFifo_Freep(AvFifo_RxFifo** Fifo);

// Returns how many frames wait in Fifo to be read, or 0 when Fifo is NULL.
CDTAPI_API int AvFifo_RxFifo_GetFifoLoad(const AvFifo_RxFifo* Fifo);

// Returns how many frames Fifo holds at most: 4, or 400 for audio, unless SetMaxSize
// set another size.
CDTAPI_API int AvFifo_RxFifo_GetMaxSize(const AvFifo_RxFifo* Fifo);

// Returns what Fifo counted since it was started.
CDTAPI_API RxStatistics AvFifo_RxFifo_GetStatistics(const AvFifo_RxFifo* Fifo);

// Takes the oldest frame from Fifo, or returns NULL when none is waiting. The frame is
// the program's until it gives it back with AvFifo_RxFifo_ReturnToMemPool().
CDTAPI_API AvFifo_Frame* AvFifo_RxFifo_Read(AvFifo_RxFifo* Fifo);

// Gives a frame that AvFifo_RxFifo_Read() returned back to Fifo's pool.
CDTAPI_API DtapiResult AvFifo_RxFifo_ReturnToMemPool(AvFifo_RxFifo* Fifo,
                                                     AvFifo_Frame* Frame);

// Sets the stream Fifo receives: its group or address, port and sources. Fifo must be
// stopped. Up to AVFIFO_MAX_SRC_FLT source filters may be used; they must have the same
// address and may differ in port only.
CDTAPI_API DtapiResult AvFifo_RxFifo_SetIpPars(AvFifo_RxFifo* Fifo,
                                               const AvFifo_IpPars* IpPars);

// Sets how many frames Fifo holds at most. Fifo must be stopped. When Size is below 1,
// Fifo is started or memory runs out, the size stays as it was and GetLastException()
// says why.
CDTAPI_API void AvFifo_RxFifo_SetMaxSize(AvFifo_RxFifo* Fifo, int Size);

// Starts receiving. Fifo checks the port's network, opens a pipe, sets the card's
// packet filter and joins the multicast group. Fifo starts empty, with its statistics at
// zero.
CDTAPI_API DtapiResult AvFifo_RxFifo_Start(AvFifo_RxFifo* Fifo);

// Stops receiving and releases the pipe. Frames waiting in Fifo stay there until the
// next Start. Stopping a stopped FIFO does nothing.
CDTAPI_API DtapiResult AvFifo_RxFifo_Stop(AvFifo_RxFifo* Fifo);

// Sets *UsesHwPipe to whether Fifo uses a hardware pipe. This is known after Start,
// and before it for HwOrSwPipe_ForceHwPipe and HwOrSwPipe_UseSwPipe; otherwise it
// returns DTAPI_E_NOT_STARTED.
CDTAPI_API DtapiResult AvFifo_RxFifo_UsesHwPipe(const AvFifo_RxFifo* Fifo,
                                                bool* UsesHwPipe);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Transmitting -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

typedef struct AvFifo_TxFifoC AvFifo_TxFifo;

// Creates a transmit FIFO. Returns NULL when there is not enough memory.
CDTAPI_API AvFifo_TxFifo* AvFifo_TxFifo_Alloc(void);

// Attaches Fifo to port Port of Device, as AvFifo_RxFifo_Attach() does.
CDTAPI_API DtapiResult AvFifo_TxFifo_Attach(AvFifo_TxFifo* Fifo, const DtDevice* Device,
                                            int Port);
CDTAPI_API DtapiResult AvFifo_TxFifo_Attach2(AvFifo_TxFifo* Fifo, const DtDevice* Device,
                                             int Port, HwOrSwPipe Pipe);

// Discards the frames waiting in Fifo to be sent. Fifo must be stopped.
CDTAPI_API DtapiResult AvFifo_TxFifo_Clear(AvFifo_TxFifo* Fifo);

// Sets what Fifo sends: video or audio, in which format. Fifo must be stopped. The
// configuration is checked at once; DTAPI_E_INVALID_ARG when it is not valid.
CDTAPI_API DtapiResult AvFifo_TxFifo_ConfigureAudio(AvFifo_TxFifo* Fifo,
                                                    const St2110_TxConfigAudio* Config);
CDTAPI_API DtapiResult AvFifo_TxFifo_ConfigureVideo(AvFifo_TxFifo* Fifo,
                                                    const St2110_TxConfigVideo* Config);

// Stops Fifo and detaches it from its port.
CDTAPI_API DtapiResult AvFifo_TxFifo_Detach(AvFifo_TxFifo* Fifo);

// Detaches Fifo if needed, and frees it. NULL does nothing.
CDTAPI_API void AvFifo_TxFifo_Free(AvFifo_TxFifo* Fifo);

// Frees *Fifo, as AvFifo_TxFifo_Free does, and sets *Fifo to NULL.
CDTAPI_API void AvFifo_TxFifo_Freep(AvFifo_TxFifo** Fifo);

// Returns how many frames wait in Fifo to be sent, or 0 when Fifo is NULL.
CDTAPI_API int AvFifo_TxFifo_GetFifoLoad(const AvFifo_TxFifo* Fifo);

// Takes a frame from Fifo's pool, with room for Size bytes, for the program to fill and
// pass to AvFifo_TxFifo_Write(). Returns NULL before Configure or when there is not
// enough memory.
CDTAPI_API AvFifo_Frame* AvFifo_TxFifo_GetFromMemPool(AvFifo_TxFifo* Fifo, int Size);

// Returns how many frames Fifo holds at most.
CDTAPI_API int AvFifo_TxFifo_GetMaxSize(const AvFifo_TxFifo* Fifo);

// Returns what Fifo counted since it was started: the frames it sent.
CDTAPI_API TxStatistics AvFifo_TxFifo_GetStatistics(const AvFifo_TxFifo* Fifo);

// Sets where Fifo sends to: the destination address and UDP port, the RTP payload type
// and how the packets are sent. Fifo must be stopped. The stream is always RTP;
// TransportProtocol is ignored.
CDTAPI_API DtapiResult AvFifo_TxFifo_SetIpPars(AvFifo_TxFifo* Fifo,
                                               const AvFifo_IpPars* IpPars);

// Sets how many frames Fifo holds at most, as AvFifo_RxFifo_SetMaxSize() does.
CDTAPI_API void AvFifo_TxFifo_SetMaxSize(AvFifo_TxFifo* Fifo, int Size);

// Starts sending. Fifo checks the port's network, finds the MAC address of the
// destination, and opens a pipe. Fifo starts empty, with its statistics at zero. The
// card sends each frame at the time of day in its ToD, on the card's clock.
CDTAPI_API DtapiResult AvFifo_TxFifo_Start(AvFifo_TxFifo* Fifo);

// Stops sending. Frames already passed to the card and not yet sent are discarded;
// frames still waiting in Fifo stay there until the next Start.
CDTAPI_API DtapiResult AvFifo_TxFifo_Stop(AvFifo_TxFifo* Fifo);

// Sets *UsesHwPipe to whether Fifo uses a hardware pipe, as AvFifo_RxFifo_UsesHwPipe()
// does.
CDTAPI_API DtapiResult AvFifo_TxFifo_UsesHwPipe(const AvFifo_TxFifo* Fifo,
                                                bool* UsesHwPipe);

// Passes Frame, taken from AvFifo_TxFifo_GetFromMemPool() and filled, to Fifo to be
// sent at Frame->ToD. Set NumValidBytes to the frame size of the configured format, and
// RtpTime and ToD. Once the frame is sent, or cannot be, it goes back to the pool. When
// the FIFO is full, the frame stays the program's: write it again a little later.
CDTAPI_API DtapiResult AvFifo_TxFifo_Write(AvFifo_TxFifo* Fifo, AvFifo_Frame* Frame);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Timing helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// ST 2110 ties every frame to the clock of the network, PTP. These functions convert
// between a time of day on that clock and the RTP timestamp of a frame: video counts at
// 90 kHz, audio at its sample rate. They also round a time to the nearest frame or sample
// period, the "grid", which starts at the PTP epoch.
//
// A time on the grid converts to the timestamp of that grid point, and a timestamp to
// the time of its tick, rounded down to a nanosecond. At a fractional frame rate, such as
// 30000/1001, a frame can start a quarter, a half or three quarters of a tick after its
// timestamp.
//

// Converts an audio RTP timestamp, at SampleRate samples per second, to a time of day.
// A 32-bit timestamp wraps around, so *ToD, usually the current time, picks the time
// nearest to it.
CDTAPI_API DtTimeOfDay Rtp2Tod_Audio(uint32_t RtpTime, const DtTimeOfDay* ToD,
                                     int SampleRate);

// Converts a video RTP timestamp, at 90 kHz, to a time of day. *ToD, usually the
// current time, picks the time nearest to it.
CDTAPI_API DtTimeOfDay Rtp2Tod_Video(uint32_t RtpTime, const DtTimeOfDay* ToD);

// Rounds *ToD to the nearest sample period of audio at SampleRate samples per second.
CDTAPI_API DtTimeOfDay Tod2Grid_Audio(const DtTimeOfDay* ToD, int SampleRate);

// Rounds *ToD to the nearest frame or field period of video at *Rate per second.
CDTAPI_API DtTimeOfDay Tod2Grid_Video(const DtTimeOfDay* ToD, const FrameRate* Rate);

// Converts *ToD to an audio RTP timestamp at SampleRate samples per second, rounded to
// the nearest sample.
CDTAPI_API uint32_t Tod2Rtp_Audio(const DtTimeOfDay* ToD, int SampleRate);

// Converts *ToD to a video RTP timestamp at 90 kHz. It rounds down, counting from one
// eighth of a tick after *ToD, so that a frame of a fractional frame rate gets its own
// timestamp.
CDTAPI_API uint32_t Tod2Rtp_Video(const DtTimeOfDay* ToD);

#ifdef __cplusplus
} // extern "C"
#endif
