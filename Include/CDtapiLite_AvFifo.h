// #*#*#*#*#*#*#*#*#*#*#*#*# CDtapiLite_AvFifo.h *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Public C API for SMPTE 2110 video and audio through the A/V FIFO
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdint.h>

// CDtapiLite includes
#include "CDtapiLite.h" // Results, devices and the time of day.

#ifdef __cplusplus
extern "C"
{
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The names, values and layouts are those of CDTAPI_AvFifo.h, so that code written for
// it compiles against this header unchanged.
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
typedef enum St2110_RxFrameFormatC
{
    St2110_RxFrameFormat_Raw,               // The pixel groups as received
    St2110_RxFrameFormat_Uyvy422_8b,        // 8-bit UYVY
    St2110_RxFrameFormat_Uyvy422_10b,       // 10-bit UYVY, packed least significant bit
                                            // first
    St2110_RxFrameFormat_Uyvy422_10b_to_8b, // 10-bit video converted to 8-bit UYVY
    St2110_RxFrameFormat_Yuv422p_8b         // 8-bit planar YUV: Y, U and V planes
} St2110_RxFrameFormat;

// The format of ST 2110-30 audio samples.
typedef enum St2110_AudioFormatC
{
    St2110_AudioFormat_L16BE, // 16-bit PCM, big endian
    St2110_AudioFormat_L24BE, // 24-bit PCM, big endian
    St2110_AudioFormat_Raw
} St2110_AudioFormat;

// Configures the reception of ST 2110-30 audio.
typedef struct St2110_RxConfigAudioC
{
    St2110_AudioFormat Format;
    int SampleRate; // In Hz
} St2110_RxConfigAudio;

// Configures the reception of ST 2110-20 video.
typedef struct St2110_RxConfigVideoC
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
    int PayloadSize; // Bytes of video per packet, or -1 for the most a packet holds
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
    int Port;
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
typedef struct AvFifo_FrameC
{
    uint32_t RtpTime; // RTP timestamp of the frame's packets
    DtTimeOfDay ToD;  // Time of day of the frame's first sample

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
    int FramesOk;         // Frames received
    int FramesIncomplete; // Frames that missed packets
    int FramesSizeError;  // Frames of an unexpected size
    int Gaps;             // Gaps in the RTP sequence numbers between frames
    int IpPacketErrors;   // Packets with a corrupt header
    int DroppedFrames;    // Frames the FIFO had no room for
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

// The video format a received frame is in.
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
CDTAPILITE_API int GetFrameProperties(const AvFifo_Frame* Frame,
                                      FrameProperties* Properties);

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
CDTAPILITE_API DtTimeOfDay Tod2Grid_Audio(const DtTimeOfDay* ToD, int SampleRate);

// The time of day on the video grid of *Rate frames or fields per second nearest to *ToD.
CDTAPILITE_API DtTimeOfDay Tod2Grid_Video(const DtTimeOfDay* ToD, const FrameRate* Rate);

// The time of day of an audio RTP timestamp of SampleRate Hz: the one nearest to the
// approximate time of day *ToD, typically the current time, of the times at which the
// 32-bit timestamp has that value.
CDTAPILITE_API DtTimeOfDay Rtp2Tod_Audio(uint32_t RtpTime, const DtTimeOfDay* ToD,
                                         int SampleRate);

// The time of day of a video RTP timestamp, as Rtp2Tod_Audio at 90 kHz.
CDTAPILITE_API DtTimeOfDay Rtp2Tod_Video(uint32_t RtpTime, const DtTimeOfDay* ToD);

// The audio RTP timestamp of SampleRate Hz of *ToD, rounded to the nearest sample.
CDTAPILITE_API uint32_t Tod2Rtp_Audio(const DtTimeOfDay* ToD, int SampleRate);

// The video RTP timestamp of *ToD. It truncates an eighth of a tick later than *ToD, so
// that a time on the grid of a fractional frame rate gives its own timestamp.
CDTAPILITE_API uint32_t Tod2Rtp_Video(const DtTimeOfDay* ToD);

#ifdef __cplusplus
} // extern "C"
#endif
