// #*#*#*#*#*#*#*#*#*#*#*#*#*# cdtapi_avfifo.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Public C API for SMPTE 2110 video and audio through the A/V FIFO
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
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
// The names, values and layouts are DTAPI's, as the rest of the interface is.
//

// An exact ratio.
typedef struct Ratio
{
    int Numerator;
    int Denominator;
} Ratio, FrameRate;

// The dimensions of active video in pixels.
typedef struct VideoSize
{
    int Width;
    int Height;
} VideoSize;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= SMPTE 2110 receive +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The pixel format the frames of received ST 2110-20 video get.
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
    int SampleRate; // In Hz
} St2110_RxConfigAudio;

// Configures the reception of ST 2110-20 video.
typedef struct St2110_RxConfigVideo
{
    St2110_RxFrameFormat Format;
} St2110_RxConfigVideo;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= SMPTE 2110 transmit +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The scanning of video.
typedef enum St2110_VideoScanning
{
    St2110_VideoScanning_Progressive,
    St2110_VideoScanning_Interlaced,
    St2110_VideoScanning_PsF,
} St2110_VideoScanning;

// The pixel format of the frames the application transmits.
typedef enum St2110_TxFrameFormat
{
    St2110_TxFrameFormat_Uyvy422_8b, // 8-bit UYVY
    St2110_TxFrameFormat_Uyvy422_10b // 10-bit UYVY, packed least significant bit first
} St2110_TxFrameFormat;

// How video is packed into packets.
typedef enum St2110_PackingMode
{
    St2110_PackingMode_General, // Whole pixel groups only
    St2110_PackingMode_Block    // Payloads of a multiple of 180 bytes
} St2110_PackingMode;

// How the packets of a frame are spread over time.
typedef enum St2110_Scheduling
{
    St2110_Scheduling_Linear,
    St2110_Scheduling_Gapped
} St2110_Scheduling;

// The packing of video.
typedef struct St2110_VideoPacking
{
    int OneLinePerPacket;
    St2110_PackingMode PackingMode;
    int PayloadSize; // Bytes of video per packet, whole pixel groups; -1 for the most a
                     // standard-size packet holds. Checked when the FIFO starts.
} St2110_VideoPacking;

// Configures the transmission of ST 2110-40 ancillary data.
typedef struct St2110_TxConfigAnc
{
    St2110_VideoScanning VideoScanning;
} St2110_TxConfigAnc;

// Configures the transmission of ST 2110-30 audio.
typedef struct St2110_TxConfigAudio
{
    St2110_AudioFormat Format;
    int NumChannels;
    int NumSamplesPerIpPacket;
    int SampleRate; // In Hz
} St2110_TxConfigAudio;

// The timing of transmitted video.
typedef struct St2110_VideoTiming
{
    FrameRate Rate; // The frame rate of progressive video, the field rate otherwise
    St2110_Scheduling Scheduling;
    St2110_VideoScanning VideoScanning;
} St2110_VideoTiming;

// Configures the transmission of ST 2110-20 video in every detail.
typedef struct St2110_TxConfigRawVideo
{
    Ratio ActiveVideo;           // The active part of the frame period
    int Is420;                   // 4:2:0 chroma subsampling
    int NumRows;                 // Rows in a frame
    St2110_VideoPacking Packing; // Packing of the packets
    struct
    {
        int NumBytes;  // Bytes in a pixel group
        int NumPixels; // Pixels in a pixel group
    } PGroup;
    int RowSize;               // Bytes in a row
    St2110_VideoTiming Timing; // Timing
    int TrOffset;              // Nanoseconds from the start of a frame period, a whole
                               // number of frame periods since the epoch, to its first
                               // packet
} St2110_TxConfigRawVideo;

// Configures the transmission of ST 2110-20 video.
typedef struct St2110_TxConfigVideo
{
    St2110_TxFrameFormat Format;
    St2110_VideoPacking Packing; // Packing of the packets
    VideoSize Resolution;        // Size of the active video
    St2110_VideoTiming Timing;   // Timing
} St2110_TxConfigVideo;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= AvFifo_IpPars +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// IPv4 or IPv6.
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

// A source of source-specific multicast.
typedef struct IpSrcFlt
{
    uint8_t IpAddr[16]; // 4 bytes for IPv4, 16 for IPv6
    int Port;           // The source's UDP port, or -1 for any
} IpSrcFlt;

// An IP end point.
typedef struct AvFifo_IpPars
{
    uint8_t IpAddr[16]; // 4 bytes for IPv4, 16 for IPv6
    IpProtocolVersion IpVersion;
    int Port;
    IpSrcFlt* SrcFlt; // Sources of source-specific multicast, or NULL
    int NSrcFlt;      // Number of sources

    int DiffServ;        // Differentiated services field
    uint8_t Gateway[16]; // A gateway to use, or all zero
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

// A frame of video, a field of interlaced video, or audio samples.
typedef struct AvFifo_Frame
{
    uint32_t RtpTime; // RTP timestamp of the frame's first sample
    DtTimeOfDay ToD;  // Transmit: when the frame starts; receive: when its first packet
                      // arrived

    uint8_t* Data;     // The frame's bytes
    int Field;         // Interlaced and PsF video: the field, 0 or 1
    int NumValidBytes; // Bytes of Data in use
    size_t Size;       // Bytes Data holds

    // Received ST 2110-20 video only.
    int Is420;   // 4:2:0 chroma subsampling
    int NumRows; // Rows in the frame
} AvFifo_Frame;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Statistics +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// What a receive FIFO counted.
typedef struct RxStatistics
{
    int FramesOk;         // Frames received whole, those the FIFO had no room for too
    int FramesIncomplete; // Frames that missed packets
    int FramesSizeError;  // Frames of an unexpected size
    int Gaps;             // Gaps in the RTP sequence numbers between frames
    int IpPacketErrors;   // Packets with a corrupt header
    int DroppedFrames;    // Frames the FIFO or the frame pool had no room for
    int SyncErrors;       // Losses of synchronisation
} RxStatistics;

// What a transmit FIFO counted.
typedef struct TxStatistics
{
    int FramesOk; // Frames transmitted
} TxStatistics;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame properties +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

typedef enum ChromaSubsampling
{
    ChromaSubsampling_Key,
    ChromaSubsampling_420,
    ChromaSubsampling_422,
    ChromaSubsampling_444,
} ChromaSubsampling;

// The video format a received frame is in. For interlaced video the frame is a field:
// NLines and BytesPerFrame are the field's, Height the picture's.
typedef struct FrameProperties
{
    int Is420;
    int NLines;
    int BytesPerLine;
    int BytesPerFrame;

    int Width;
    int Height;
    int IsInterlaced;
    ChromaSubsampling Subsampling;
    int BitDepth;
} FrameProperties;

// Finds the video format of a received frame by its valid bytes, rows and 4:2:0
// subsampling, among 720x480 and 720x576 interlaced, 1920x1080 interlaced, 1280x720,
// 1920x1080, 2048x1080 and 3840x2160 progressive, all 4:2:2 in 8, 10, 12 and 16 bits.
// Returns 1 and fills *Properties when found, -1 otherwise.
CDTAPI_API int GetFrameProperties(const AvFifo_Frame* Frame, FrameProperties* Properties);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The FIFOs +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A receive FIFO takes one SMPTE 2110 stream from an IP port of a DtPcie card that
// reports DT_CAP_AVFIFO, a transmit FIFO sends one; each has a thread of its own, a pipe
// of the card and a pool of frames. The order of use:
//
//   Alloc, Attach, Configure (audio or video), SetIpPars, Start,
//   Read and ReturnToMemPool, or GetFromMemPool and Write, ..., Stop, Detach, Free
//
// The port of Attach counts from 1, as DTAPI's AV FIFO, DtInpChannel and DtOutpChannel
// count it.
//
// Every function that returns a result gives DTAPI_OK or a failure, and a failure also
// sets the text GetLastException gives on the calling thread. GetFromMemPool and
// SetMaxSize, which return no result, set the text when they fail. The failures:
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
// and the driver's own result for a command that fails. A failed Start leaves the FIFO
// attached and stopped.
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

// Returns the text of the calling thread's last failure, or an empty string. The text
// stays until the next failure on that thread.
CDTAPI_API const char* GetLastException(void);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Receiving -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

typedef struct AvFifo_RxFifoC AvFifo_RxFifo;

// Makes a receive FIFO, or returns NULL when there is no memory; frees it, detaching it
// first; and frees it and sets *Fifo to NULL.
CDTAPI_API AvFifo_RxFifo* AvFifo_RxFifo_Alloc(void);
CDTAPI_API void AvFifo_RxFifo_Free(AvFifo_RxFifo* Fifo);
CDTAPI_API void AvFifo_RxFifo_Freep(AvFifo_RxFifo** Fifo);

// Attaches the FIFO to a port, counted from 1, with HwOrSwPipe_Auto or a pipe
// preference. The FIFO opens its own handle to the device.
CDTAPI_API DtapiResult AvFifo_RxFifo_Attach(AvFifo_RxFifo* Fifo, const DtDevice* Device,
                                            int Port);
CDTAPI_API DtapiResult AvFifo_RxFifo_Attach2(AvFifo_RxFifo* Fifo, const DtDevice* Device,
                                             int Port, HwOrSwPipe Pipe);

// Stops and detaches the FIFO. Frames the application holds stay valid until Free.
CDTAPI_API DtapiResult AvFifo_RxFifo_Detach(AvFifo_RxFifo* Fifo);

// Returns the frames in the FIFO to the pool.
CDTAPI_API DtapiResult AvFifo_RxFifo_Clear(AvFifo_RxFifo* Fifo);

// Starts receiving: checks the network, opens a pipe, programs its filter and joins a
// multicast group. The FIFO starts empty and the statistics from zero.
CDTAPI_API DtapiResult AvFifo_RxFifo_Start(AvFifo_RxFifo* Fifo);

// Stops receiving and gives up the pipe; the frames in the FIFO stay until Start.
CDTAPI_API DtapiResult AvFifo_RxFifo_Stop(AvFifo_RxFifo* Fifo);

// Configures the FIFO for audio, with a FIFO of 400 frames unless its size was set, or
// for video.
CDTAPI_API DtapiResult AvFifo_RxFifo_ConfigureAudio(AvFifo_RxFifo* Fifo,
                                                    const St2110_RxConfigAudio* Config);
CDTAPI_API DtapiResult AvFifo_RxFifo_ConfigureVideo(AvFifo_RxFifo* Fifo,
                                                    const St2110_RxConfigVideo* Config);

// Sets the stream to receive. The sources are copied; at most three, of one address.
CDTAPI_API DtapiResult AvFifo_RxFifo_SetIpPars(AvFifo_RxFifo* Fifo,
                                               const AvFifo_IpPars* IpPars);

// The frames in the FIFO, 0 for a NULL FIFO.
CDTAPI_API int AvFifo_RxFifo_GetFifoLoad(const AvFifo_RxFifo* Fifo);

// Takes the oldest frame, or returns NULL when there is none. The frame is the
// application's until it returns it to the pool.
CDTAPI_API AvFifo_Frame* AvFifo_RxFifo_Read(AvFifo_RxFifo* Fifo);

// Returns a frame Read gave.
CDTAPI_API DtapiResult AvFifo_RxFifo_ReturnToMemPool(AvFifo_RxFifo* Fifo,
                                                     AvFifo_Frame* Frame);

// The most frames the FIFO holds: 4, or 400 once configured for audio, unless set; and
// setting it while stopped. A size below 1, a started FIFO or a lack of memory keeps the
// size and sets GetLastException's text.
CDTAPI_API int AvFifo_RxFifo_GetMaxSize(const AvFifo_RxFifo* Fifo);
CDTAPI_API void AvFifo_RxFifo_SetMaxSize(AvFifo_RxFifo* Fifo, int Size);

// What the FIFO counted since Start.
CDTAPI_API RxStatistics AvFifo_RxFifo_GetStatistics(const AvFifo_RxFifo* Fifo);

// Whether the FIFO receives through a hardware pipe: known once started, and before for
// HwOrSwPipe_ForceHwPipe and HwOrSwPipe_UseSwPipe.
CDTAPI_API DtapiResult AvFifo_RxFifo_UsesHwPipe(const AvFifo_RxFifo* Fifo,
                                                int* UsesHwPipe);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Transmitting -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

typedef struct AvFifo_TxFifoC AvFifo_TxFifo;

// As for the receive FIFO.
CDTAPI_API AvFifo_TxFifo* AvFifo_TxFifo_Alloc(void);
CDTAPI_API void AvFifo_TxFifo_Free(AvFifo_TxFifo* Fifo);
CDTAPI_API void AvFifo_TxFifo_Freep(AvFifo_TxFifo** Fifo);
CDTAPI_API DtapiResult AvFifo_TxFifo_Attach(AvFifo_TxFifo* Fifo, const DtDevice* Device,
                                            int Port);
CDTAPI_API DtapiResult AvFifo_TxFifo_Attach2(AvFifo_TxFifo* Fifo, const DtDevice* Device,
                                             int Port, HwOrSwPipe Pipe);
CDTAPI_API DtapiResult AvFifo_TxFifo_Detach(AvFifo_TxFifo* Fifo);
CDTAPI_API DtapiResult AvFifo_TxFifo_Clear(AvFifo_TxFifo* Fifo);

// Starts transmitting: checks the network, resolves the destination's MAC address and
// opens a pipe. The FIFO starts empty and the statistics from zero. The card sends each
// frame's packets from its time of day on, as its clock has it; video starts one
// transmit offset into the frame period.
CDTAPI_API DtapiResult AvFifo_TxFifo_Start(AvFifo_TxFifo* Fifo);

// Stops transmitting; frames not yet sent are dropped from the pipe, and those in the
// FIFO stay until Start.
CDTAPI_API DtapiResult AvFifo_TxFifo_Stop(AvFifo_TxFifo* Fifo);

// As for the receive FIFO; the transmit FIFO also checks the configuration at once,
// DTAPI_E_INVALID_ARG when it does not hold.
CDTAPI_API DtapiResult AvFifo_TxFifo_ConfigureAudio(AvFifo_TxFifo* Fifo,
                                                    const St2110_TxConfigAudio* Config);
CDTAPI_API DtapiResult AvFifo_TxFifo_ConfigureVideo(AvFifo_TxFifo* Fifo,
                                                    const St2110_TxConfigVideo* Config);

// Sets the stream to send: destination, RTP payload type from 0 to 127 and port from 0
// to 65535. The transport protocol is not used: the streams are RTP, as in DTAPI.
CDTAPI_API DtapiResult AvFifo_TxFifo_SetIpPars(AvFifo_TxFifo* Fifo,
                                               const AvFifo_IpPars* IpPars);

CDTAPI_API int AvFifo_TxFifo_GetFifoLoad(const AvFifo_TxFifo* Fifo);

// Queues a frame from GetFromMemPool for sending; once sent, or found unsendable, it
// returns to the pool. The frame's valid bytes must be those of the configuration.
CDTAPI_API DtapiResult AvFifo_TxFifo_Write(AvFifo_TxFifo* Fifo, AvFifo_Frame* Frame);

// A frame of Size bytes to fill, or NULL before Configure or without memory.
CDTAPI_API AvFifo_Frame* AvFifo_TxFifo_GetFromMemPool(AvFifo_TxFifo* Fifo, int Size);

// As for the receive FIFO; the statistics count the frames sent since Start.
CDTAPI_API int AvFifo_TxFifo_GetMaxSize(const AvFifo_TxFifo* Fifo);
CDTAPI_API void AvFifo_TxFifo_SetMaxSize(AvFifo_TxFifo* Fifo, int Size);
CDTAPI_API TxStatistics AvFifo_TxFifo_GetStatistics(const AvFifo_TxFifo* Fifo);
CDTAPI_API DtapiResult AvFifo_TxFifo_UsesHwPipe(const AvFifo_TxFifo* Fifo,
                                                int* UsesHwPipe);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Timing helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Times of day on the media clock: the grid of frame or field periods for video and of
// sample periods for audio, counted from the epoch, and the RTP timestamps of 90 kHz for
// video and of the sample rate for audio. A time on the grid, rounded to a nanosecond,
// converts to the RTP timestamp of that grid point, and an RTP timestamp converts to the
// time of its tick, truncated to a nanosecond. On the grid of a fractional frame rate a
// frame can lie a quarter, a half or three quarters of a tick after its timestamp.
//

// The time of day on the audio grid of SampleRate Hz nearest to *ToD.
CDTAPI_API DtTimeOfDay Tod2Grid_Audio(const DtTimeOfDay* ToD, int SampleRate);

// The time of day on the video grid of *Rate frames or fields per second nearest to *ToD.
CDTAPI_API DtTimeOfDay Tod2Grid_Video(const DtTimeOfDay* ToD, const FrameRate* Rate);

// The time of day of an audio RTP timestamp of SampleRate Hz: the one nearest to the
// approximate time of day *ToD, typically the current time, of the times at which the
// 32-bit timestamp has that value.
CDTAPI_API DtTimeOfDay Rtp2Tod_Audio(uint32_t RtpTime, const DtTimeOfDay* ToD,
                                     int SampleRate);

// The time of day of a video RTP timestamp, as Rtp2Tod_Audio at 90 kHz.
CDTAPI_API DtTimeOfDay Rtp2Tod_Video(uint32_t RtpTime, const DtTimeOfDay* ToD);

// The audio RTP timestamp of SampleRate Hz of *ToD, rounded to the nearest sample.
CDTAPI_API uint32_t Tod2Rtp_Audio(const DtTimeOfDay* ToD, int SampleRate);

// The video RTP timestamp of *ToD. It truncates an eighth of a tick later than *ToD, so
// that a time on the grid of a fractional frame rate gives its own timestamp.
CDTAPI_API uint32_t Tod2Rtp_Video(const DtTimeOfDay* ToD);

#ifdef __cplusplus
} // extern "C"
#endif
