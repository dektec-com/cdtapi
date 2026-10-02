// #*#*#*#*#*#*#*#*#*#*#*#*#*# cdtapi_avfifo.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Public C API for SMPTE 2110 video and audio through the A/V FIFO
//
// SPDX-License-Identifier: BSD-3-Clause

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
//
// Names, values and layouts follow DTAPI conventions.
//

// An exact ratio of two integers.
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

// The pixel format used for received ST 2110-20 video frames.
typedef enum St2110_RxFrameFormat
{
    St2110_RxFrameFormat_Raw,               // The pixel groups as received
    St2110_RxFrameFormat_Uyvy422_8b,        // 8-bit UYVY
    St2110_RxFrameFormat_Uyvy422_10b,       // 10-bit UYVY, packed least significant bit
                                            // first
    St2110_RxFrameFormat_Uyvy422_10b_to_8b, // 10-bit video converted to 8-bit UYVY
    St2110_RxFrameFormat_Yuv422p_8b         // 8-bit planar YUV: Y, U and V planes
} St2110_RxFrameFormat;

// The format of ST 2110-30 audio samples.
typedef enum St2110_AudioFormat
{
    St2110_AudioFormat_L16BE, // 16-bit PCM, big endian
    St2110_AudioFormat_L24BE, // 24-bit PCM, big endian
    St2110_AudioFormat_Raw
} St2110_AudioFormat;

// Configures the reception of ST 2110-30 audio. Each frame is one packet's samples as
// received; the format and the sample rate do not change them.
typedef struct St2110_RxConfigAudio
{
    St2110_AudioFormat Format;
    int SampleRate; // In Hz.
} St2110_RxConfigAudio;

// Configures the reception of ST 2110-20 video.
typedef struct St2110_RxConfigVideo
{
    St2110_RxFrameFormat Format;
} St2110_RxConfigVideo;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= SMPTE 2110 transmit +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The video scanning mode.
typedef enum St2110_VideoScanning
{
    St2110_VideoScanning_Progressive,
    St2110_VideoScanning_Interlaced,
    St2110_VideoScanning_PsF,
} St2110_VideoScanning;

// The pixel format of the frames transmitted by the application.
typedef enum St2110_TxFrameFormat
{
    St2110_TxFrameFormat_Uyvy422_8b, // 8-bit UYVY
    St2110_TxFrameFormat_Uyvy422_10b // 10-bit UYVY, packed least significant bit first
} St2110_TxFrameFormat;

// How video data is packed into RTP packets.
typedef enum St2110_PackingMode
{
    St2110_PackingMode_General, // Whole pixel groups only
    St2110_PackingMode_Block    // Payloads whose size is a multiple of 180 bytes
} St2110_PackingMode;

// How the packets of a frame are distributed over time.
typedef enum St2110_Scheduling
{
    St2110_Scheduling_Linear,
    St2110_Scheduling_Gapped
} St2110_Scheduling;

// Video packetization parameters.
typedef struct St2110_VideoPacking
{
    bool OneLinePerPacket;
    St2110_PackingMode PackingMode;
    int PayloadSize; // Bytes of video per packet, whole pixel groups; -1 for the most a
                     // standard-size packet holds. Checked when the FIFO starts.
} St2110_VideoPacking;

// Configuration for transmitting ST 2110-40 ancillary data.
typedef struct St2110_TxConfigAnc
{
    St2110_VideoScanning VideoScanning;
} St2110_TxConfigAnc;

// Configuration for transmitting ST 2110-30 audio.
typedef struct St2110_TxConfigAudio
{
    St2110_AudioFormat Format;
    int NumChannels;
    int NumSamplesPerIpPacket;
    int SampleRate; // In Hz.
} St2110_TxConfigAudio;

// Timing parameters for transmitted video.
typedef struct St2110_VideoTiming
{
    FrameRate Rate; // The frame rate of progressive video, the field rate otherwise
    St2110_Scheduling Scheduling;
    St2110_VideoScanning VideoScanning;
} St2110_VideoTiming;

// Detailed configuration for transmitting ST 2110-20 video.
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
    int TrOffset;              // Nanoseconds from the start of a frame period, a whole
                               // number of frame periods since the epoch, to its first
                               // packet
} St2110_TxConfigRawVideo;

// Configuration for transmitting ST 2110-20 video.
typedef struct St2110_TxConfigVideo
{
    St2110_TxFrameFormat Format;
    St2110_VideoPacking Packing; // Packetization parameters
    VideoSize Resolution;        // Size of the active video
    St2110_VideoTiming Timing;   // Timing
} St2110_TxConfigVideo;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= AvFifo_IpPars +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// IP protocol version.
typedef enum IpProtocolVersion
{
    IpProtocolVersion_IPv4,
    IpProtocolVersion_IPv6
} IpProtocolVersion;

// RTP over UDP, or UDP without RTP.
typedef enum IpTransportProtocol
{
    IpTransportProtocol_Rtp,
    IpTransportProtocol_Udp
} IpTransportProtocol;

// A source filter for source-specific multicast.
typedef struct IpSrcFlt
{
    uint8_t IpAddr[16]; // 4 bytes for IPv4; 16 bytes for IPv6
    int Port;           // The source's UDP port, or -1 for any
} IpSrcFlt;

// The number of source filters an AvFifo_IpPars holds.
#define AVFIFO_MAX_SRC_FLT 3

// An IP endpoint. It holds its source filters, so a copy made with = stands alone.
typedef struct AvFifo_IpPars
{
    uint8_t IpAddr[16]; // 4 bytes for IPv4; 16 bytes for IPv6
    IpProtocolVersion IpVersion;
    int Port;
    IpSrcFlt SrcFlt[AVFIFO_MAX_SRC_FLT]; // Source filters for source-specific multicast
    int NSrcFlt; // The number of them in use, 0 to AVFIFO_MAX_SRC_FLT

    int DiffServ;        // Differentiated Services field
    uint8_t Gateway[16]; // Gateway address to use, or all zeros
    int RtpPayloadType;  // RTP payload type
    int TimeToLive;
    IpTransportProtocol TransportProtocol;
    struct
    {
        int Id;
        int Priority;
    } Vlan;
} AvFifo_IpPars;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= AvFifo_Frame +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// A video frame, an interlaced video field, or a set of audio samples.
typedef struct AvFifo_Frame
{
    uint32_t RtpTime; // RTP timestamp of the frame's first sample.
    DtTimeOfDay ToD;  // Transmit: when the frame starts; receive: when its first packet
                      // arrived

    uint8_t* Data;     // Frame data.
    int Field;         // Interlaced and PsF video: field number, 0 or 1.
    int NumValidBytes; // Number of valid bytes in Data.
    size_t Size;       // Size of the Data buffer in bytes.

    // Valid for received ST 2110-20 video only.
    bool Is420;  // 4:2:0 chroma subsampling
    int NumRows; // Rows in the frame
} AvFifo_Frame;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Statistics +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Statistics collected by a receive FIFO.
typedef struct RxStatistics
{
    int FramesOk;         // Frames received whole, those the FIFO had no room for too
    int FramesIncomplete; // Frames with missing packets.
    int FramesSizeError;  // Frames with an unexpected size.
    int Gaps;             // Gaps in the RTP sequence numbers between frames
    int IpPacketErrors;   // Packets with an invalid header.
    int DroppedFrames;    // Frames the FIFO or the frame pool had no room for
    int SyncErrors;       // Loss of synchronization.
} RxStatistics;

// Statistics collected by a transmit FIFO.
typedef struct TxStatistics
{
    int FramesOk; // Frames transmitted successfully.
} TxStatistics;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame properties +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

typedef enum ChromaSubsampling
{
    ChromaSubsampling_Key,
    ChromaSubsampling_420,
    ChromaSubsampling_422,
    ChromaSubsampling_444,
} ChromaSubsampling;

// Describes the video format of a received frame. For interlaced video, the frame
// represents a field: NLines and BytesPerFrame describe the field, while Height describes
// the complete picture.
typedef struct FrameProperties
{
    bool Is420;
    int NLines;
    int BytesPerLine;
    int BytesPerFrame;

    int Width;
    int Height;
    bool IsInterlaced;
    ChromaSubsampling Subsampling;
    int BitDepth;
} FrameProperties;

// Finds the video format of a received frame by its valid bytes, rows and 4:2:0
// subsampling, among 720x480 and 720x576 interlaced, 1920x1080 interlaced, 1280x720,
// 1920x1080, 2048x1080 and 3840x2160 progressive, all 4:2:2 in 8, 10, 12 and 16 bits.
// Returns DTAPI_OK and fills *Properties when found; DTAPI_E_INVALID_ARG for a null Frame
// or Properties, and DTAPI_E_UNSUP_FORMAT when no format matches, which both leave
// *Properties as it was.
CDTAPI_API DtapiResult GetFrameProperties(const AvFifo_Frame* Frame,
                                          FrameProperties* Properties);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The FIFOs +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A receive FIFO receives one SMPTE 2110 stream from an IP port of a DtPcie card that
// reports DT_CAP_AVFIFO; a transmit FIFO sends one stream. Each FIFO has its own thread,
// a card pipe, and a frame pool. Typical use is:
//
//   Alloc, Attach, Configure (audio or video), SetIpPars, Start,
//   Read and ReturnToMemPool, or GetFromMemPool and Write, ..., Stop, Detach, Free
//
// The Port argument is numbered starting at 1, as for DTAPI's AV FIFO, DtInpChannel,
// and DtOutpChannel.
//
// Every function that returns a result returns DTAPI_OK or an error. An error also sets
// the text returned by GetLastException on the calling thread. GetFromMemPool and
// SetMaxSize, which do not return a result, set the same text when they fail. Possible
// errors include:
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
// and the result returned by the driver for a command that fails. A failed Start leaves
// the FIFO attached and stopped.
//

// Which kind of pipe a FIFO uses: the one its stream prefers, hardware for video and
// software for audio, with software for video when no hardware pipe is free; only a
// hardware pipe; only a software pipe; or a hardware pipe and else a software pipe.
typedef enum HwOrSwPipe
{
    HwOrSwPipe_Auto,
    HwOrSwPipe_ForceHwPipe,
    HwOrSwPipe_UseSwPipe,
    HwOrSwPipe_PreferHwPipe
} HwOrSwPipe;

// Returns the last error message for the calling thread, or an empty string if none
// exists. The message remains unchanged until the next failure on that thread.
CDTAPI_API const char* GetLastException(void);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Receiving -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

typedef struct AvFifo_RxFifoC AvFifo_RxFifo;

// Allocates a receive FIFO. Returns NULL if memory allocation fails.
CDTAPI_API AvFifo_RxFifo* AvFifo_RxFifo_Alloc(void);

// Attaches the FIFO to a port, numbered starting at 1, using HwOrSwPipe_Auto or the
// specified pipe preference. The FIFO opens its own handle to the device.
CDTAPI_API DtapiResult AvFifo_RxFifo_Attach(AvFifo_RxFifo* Fifo, const DtDevice* Device,
                                            int Port);
CDTAPI_API DtapiResult AvFifo_RxFifo_Attach2(AvFifo_RxFifo* Fifo, const DtDevice* Device,
                                             int Port, HwOrSwPipe Pipe);

// Returns all frames currently in the FIFO to the frame pool.
CDTAPI_API DtapiResult AvFifo_RxFifo_Clear(AvFifo_RxFifo* Fifo);

// Configures the FIFO for audio or video. Audio uses a default FIFO size of 400 frames
// unless a size was set before.
CDTAPI_API DtapiResult AvFifo_RxFifo_ConfigureAudio(AvFifo_RxFifo* Fifo,
                                                    const St2110_RxConfigAudio* Config);
CDTAPI_API DtapiResult AvFifo_RxFifo_ConfigureVideo(AvFifo_RxFifo* Fifo,
                                                    const St2110_RxConfigVideo* Config);

// Stops and detaches the FIFO. Frames held by the application remain valid until Free.
CDTAPI_API DtapiResult AvFifo_RxFifo_Detach(AvFifo_RxFifo* Fifo);

// Frees the FIFO, detaching it first if necessary.
CDTAPI_API void AvFifo_RxFifo_Free(AvFifo_RxFifo* Fifo);

// Frees *Fifo as AvFifo_RxFifo_Free does and sets *Fifo to NULL.
CDTAPI_API void AvFifo_RxFifo_Freep(AvFifo_RxFifo** Fifo);

// Returns the number of frames currently in the FIFO, or 0 if Fifo is NULL.
CDTAPI_API int AvFifo_RxFifo_GetFifoLoad(const AvFifo_RxFifo* Fifo);

// Returns the maximum number of frames the FIFO can hold: 4 by default, or 400 after
// configuring it for audio, unless a different size was set.
CDTAPI_API int AvFifo_RxFifo_GetMaxSize(const AvFifo_RxFifo* Fifo);

// Returns the statistics collected by the FIFO since Start.
CDTAPI_API RxStatistics AvFifo_RxFifo_GetStatistics(const AvFifo_RxFifo* Fifo);

// Returns the oldest frame, or NULL if none is available. Ownership of the returned frame
// passes to the application until it is returned to the pool.
CDTAPI_API AvFifo_Frame* AvFifo_RxFifo_Read(AvFifo_RxFifo* Fifo);

// Returns a frame obtained from Read to the frame pool.
CDTAPI_API DtapiResult AvFifo_RxFifo_ReturnToMemPool(AvFifo_RxFifo* Fifo,
                                                     AvFifo_Frame* Frame);

// Configures the stream to receive. Up to AVFIFO_MAX_SRC_FLT source filters may be in
// use, all with the same source address and differing in port only.
CDTAPI_API DtapiResult AvFifo_RxFifo_SetIpPars(AvFifo_RxFifo* Fifo,
                                               const AvFifo_IpPars* IpPars);

// Sets the most frames the FIFO holds, while stopped. A size below 1, a started FIFO or
// a lack of memory keeps the size and sets GetLastException's text.
CDTAPI_API void AvFifo_RxFifo_SetMaxSize(AvFifo_RxFifo* Fifo, int Size);

// Starts receiving. The FIFO checks the network, opens a pipe, programs its filter, and
// joins the multicast group. The FIFO starts empty and its statistics are reset.
CDTAPI_API DtapiResult AvFifo_RxFifo_Start(AvFifo_RxFifo* Fifo);

// Stops receiving and releases the pipe. Frames already in the FIFO remain until Start.
CDTAPI_API DtapiResult AvFifo_RxFifo_Stop(AvFifo_RxFifo* Fifo);

// Reports whether the FIFO receives through a hardware pipe. The result is known after
// Start, and also before Start for HwOrSwPipe_ForceHwPipe and HwOrSwPipe_UseSwPipe.
CDTAPI_API DtapiResult AvFifo_RxFifo_UsesHwPipe(const AvFifo_RxFifo* Fifo,
                                                bool* UsesHwPipe);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Transmitting -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

// A transmit FIFO function without its own comment has the same behavior as the
// corresponding receive FIFO function.
typedef struct AvFifo_TxFifoC AvFifo_TxFifo;

CDTAPI_API AvFifo_TxFifo* AvFifo_TxFifo_Alloc(void);

CDTAPI_API DtapiResult AvFifo_TxFifo_Attach(AvFifo_TxFifo* Fifo, const DtDevice* Device,
                                            int Port);
CDTAPI_API DtapiResult AvFifo_TxFifo_Attach2(AvFifo_TxFifo* Fifo, const DtDevice* Device,
                                             int Port, HwOrSwPipe Pipe);

CDTAPI_API DtapiResult AvFifo_TxFifo_Clear(AvFifo_TxFifo* Fifo);

// As for the receive FIFO; the transmit FIFO also validates the configuration immediately
// and returns DTAPI_E_INVALID_ARG if it is invalid.
CDTAPI_API DtapiResult AvFifo_TxFifo_ConfigureAudio(AvFifo_TxFifo* Fifo,
                                                    const St2110_TxConfigAudio* Config);
CDTAPI_API DtapiResult AvFifo_TxFifo_ConfigureVideo(AvFifo_TxFifo* Fifo,
                                                    const St2110_TxConfigVideo* Config);

CDTAPI_API DtapiResult AvFifo_TxFifo_Detach(AvFifo_TxFifo* Fifo);

CDTAPI_API void AvFifo_TxFifo_Free(AvFifo_TxFifo* Fifo);

CDTAPI_API void AvFifo_TxFifo_Freep(AvFifo_TxFifo** Fifo);

CDTAPI_API int AvFifo_TxFifo_GetFifoLoad(const AvFifo_TxFifo* Fifo);

// Allocates a frame with a Data buffer of Size bytes. Returns NULL before Configure or
// if the required memory is unavailable.
CDTAPI_API AvFifo_Frame* AvFifo_TxFifo_GetFromMemPool(AvFifo_TxFifo* Fifo, int Size);

CDTAPI_API int AvFifo_TxFifo_GetMaxSize(const AvFifo_TxFifo* Fifo);

// Returns the number of frames transmitted since Start.
CDTAPI_API TxStatistics AvFifo_TxFifo_GetStatistics(const AvFifo_TxFifo* Fifo);

// Configures the stream to transmit: destination address, RTP payload type (0 to 127),
// and UDP port (0 to 65535). TransportProtocol is ignored; the streams are always RTP.
CDTAPI_API DtapiResult AvFifo_TxFifo_SetIpPars(AvFifo_TxFifo* Fifo,
                                               const AvFifo_IpPars* IpPars);

CDTAPI_API void AvFifo_TxFifo_SetMaxSize(AvFifo_TxFifo* Fifo, int Size);

// Starts transmitting. The FIFO checks the network, resolves the destination MAC
// address, and opens a pipe. The FIFO starts empty and its statistics are reset. The
// card schedules each frame's packets according to its time-of-day clock; video starts
// at the configured transmit offset within the frame period.
CDTAPI_API DtapiResult AvFifo_TxFifo_Start(AvFifo_TxFifo* Fifo);

// Stops transmitting. Frames not yet sent are discarded from the pipe; frames remaining
// in the FIFO stay there until Start.
CDTAPI_API DtapiResult AvFifo_TxFifo_Stop(AvFifo_TxFifo* Fifo);

CDTAPI_API DtapiResult AvFifo_TxFifo_UsesHwPipe(const AvFifo_TxFifo* Fifo,
                                                bool* UsesHwPipe);

// Queues a frame obtained from GetFromMemPool for transmission. Once the frame is sent,
// or determined to be unsendable, it is returned to the pool. NumValidBytes must match
// the configured frame format.
CDTAPI_API DtapiResult AvFifo_TxFifo_Write(AvFifo_TxFifo* Fifo, AvFifo_Frame* Frame);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Timing helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Times of day on the media clock: the grid of frame or field periods for video and of
// sample periods for audio, counted from the epoch, and the RTP timestamps of 90 kHz for
// video and of the sample rate for audio. A time on the grid, rounded to a nanosecond,
// converts to the RTP timestamp of that grid point, and an RTP timestamp converts to the
// time of its tick, truncated to a nanosecond. On the grid of a fractional frame rate a
// frame can lie a quarter, a half or three quarters of a tick after its timestamp.
//

// Converts an audio RTP timestamp at SampleRate Hz to the time of day nearest to *ToD.
// *ToD is typically the current time and resolves the wrap of the 32-bit RTP timestamp.
CDTAPI_API DtTimeOfDay Rtp2Tod_Audio(uint32_t RtpTime, const DtTimeOfDay* ToD,
                                     int SampleRate);

// Converts a video RTP timestamp to the time of day nearest to *ToD, using a 90 kHz RTP
// clock.
CDTAPI_API DtTimeOfDay Rtp2Tod_Video(uint32_t RtpTime, const DtTimeOfDay* ToD);

// Returns the point on the SampleRate-Hz audio grid nearest to *ToD.
CDTAPI_API DtTimeOfDay Tod2Grid_Audio(const DtTimeOfDay* ToD, int SampleRate);

// Returns the point on the video grid of *Rate frames or fields per second nearest to
// *ToD.
CDTAPI_API DtTimeOfDay Tod2Grid_Video(const DtTimeOfDay* ToD, const FrameRate* Rate);

// Converts *ToD to an audio RTP timestamp at SampleRate Hz, rounded to the nearest
// sample.
CDTAPI_API uint32_t Tod2Rtp_Audio(const DtTimeOfDay* ToD, int SampleRate);

// Converts *ToD to a video RTP timestamp. The conversion truncates at one eighth of an
// RTP tick after *ToD so that a time on the grid of a fractional frame rate maps to its
// corresponding timestamp.
CDTAPI_API uint32_t Tod2Rtp_Video(const DtTimeOfDay* ToD);

#ifdef __cplusplus
} // extern "C"
#endif
