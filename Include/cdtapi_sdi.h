// #*#*#*#*#*#*#*#*#*#*#*#*#*#* cdtapi_sdi.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Public C API that takes SDI frames apart and puts them together
//
// SPDX-License-Identifier: BSD-3-Clause
//
// An SDI frame carries more than its image: timing references, line numbers, CRCs,
// embedded audio and other ancillary data. A parser takes a frame apart into the image,
// the audio and the ancillary packets; a builder puts a frame together from them, and
// adds everything else the frame needs. They handle SD, HD, 3G level A, and 2160p over
// one 6G or 12G link.
//
// Neither needs a card. They work on a view of a frame, which says where the frame's
// lines are, and copy nothing to make it. A view can describe:
//   - a raw frame in the program's memory, as DtInpChannel_ReadFrame() reads it and
//     DtOutpChannel_WriteFrame() writes it, or as an .sdi file holds it after its header:
//     DtSdiView_SetRawFrame();
//   - a frame in the receive buffer of an input channel, read where the card wrote it:
//     DtInpChannel_AcquireFrame(), in cdtapi.h.
//
// To take frames apart, a program:
// 1. creates a parser with DtSdiParser_Alloc(), and optionally gives it a worker pool,
//    and creates a view with DtSdiView_Alloc();
// 2. for each frame, points the view at it and calls DtSdiParser_Parse() with what it
//    wants from the frame: an image, audio, ancillary packets, or any of these;
// 3. frees the parser and the view.
//
// To put frames together, a program:
// 1. creates a builder with DtSdiBuilder_Alloc(), and a view with DtSdiView_Alloc();
// 2. for each frame, asks DtSdiBuilder_GetNumAudioSamples() how many audio samples per
//    channel the frame takes, points the view at its frame buffer, and calls
//    DtSdiBuilder_Build() with the image, the audio and the ancillary packets;
// 3. frees the builder and the view.
//
// The program gives the buffers that results go into and that input comes from, so that
// a framework's buffer pool can lend them: a call writes each result once, in its final
// place. A parser or builder keeps state from one frame to the next, such as the audio
// cadence, so each stream of frames needs its own, used by one thread at a time.
//
// Every function that takes a parser, builder, view or other pointer returns
// DTAPI_E_INVALID_ARG when it is NULL, unless its comment says otherwise.

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "cdtapi.h" // Results, video standards and worker pools.

#ifdef __cplusplus
extern "C"
{
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Views +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A view describes where an SDI frame lies in memory: its video standard, how many bits
// a symbol has, and where each of its lines starts. Pointing a view at a frame copies
// nothing, and the view is valid only as long as the memory it describes. A program
// creates a view once and points it at one frame after another, so no frame costs an
// allocation.
//

// A view of one SDI frame, which a parser reads or a builder writes.
typedef struct DtSdiView DtSdiView;

// Creates a view that describes no frame yet. Returns NULL when there is not enough
// memory.
CDTAPI_API DtSdiView* DtSdiView_Alloc(void);

// Frees View. The frame it describes is not touched. NULL does nothing.
CDTAPI_API void DtSdiView_Free(DtSdiView* View);

// Frees *View, as DtSdiView_Free() does, and sets *View to NULL. NULL does nothing.
CDTAPI_API void DtSdiView_Freep(DtSdiView** View);

// Returns in *Data where line Line of the frame's woven image (counted from 0, top to
// bottom) lies in the frame, without copying it. The samples are in
// the frame's own format: Cb, Y, Cr, Y and so on, in 10-bit symbols packed least
// significant bit first or in 16-bit words, as DtSdiView_GetFormat() says. The line
// returned starts on a byte boundary. In a 10-bit raw frame of 720p23.98 or 720p24,
// every other line starts half-way through a byte; for those lines there is no pointer,
// and DtSdiParser_Parse() into DT_SDI_PIXFMT_UYVY_10B gives the image with one copy.
//
// This is how a program reads the image with no copy at all. Lines are not evenly
// spaced: the fields lie apart, and in an input channel's buffer the frame may wrap.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_STATE           the view describes no frame
//   DTAPI_E_INVALID_LINE    Line is not a line of the image
//   DTAPI_E_NOT_SUPPORTED   the frame is 2160p, whose lines are spread over its links,
//                           or the line does not start on a byte boundary
CDTAPI_API DtapiResult DtSdiView_GetActiveLine(const DtSdiView* View, int Line,
                                               const void** Data);

// Returns the video standard of the frame View describes, a DTAPI_VIDSTD_ code, in
// *VidStd, and its bits a symbol, 10 or 16, in *BitsPerSymbol. Either may be NULL. A
// program needs it for a frame of an input channel, whose standard the channel found.
//
// Returns DTAPI_OK, or DTAPI_E_STATE when the view describes no frame.
CDTAPI_API DtapiResult DtSdiView_GetFormat(const DtSdiView* View, int* VidStd,
                                           int* BitsPerSymbol);

// Reads the SMPTE ST 352 payload ID of the frame into *PayloadId, as its four bytes with
// the first in the most significant byte. In 2160p it is the payload ID of the first
// link.
//
// Returns DTAPI_OK, DTAPI_E_STATE when the view describes no frame, or DTAPI_E_NOT_FOUND
// when the frame carries none; *PayloadId is then 0.
CDTAPI_API DtapiResult DtSdiView_GetPayloadId(const DtSdiView* View, uint32_t* PayloadId);

// Returns in *Size the size in bytes of a raw frame of video standard VidStd with
// BitsPerSymbol bits a symbol, including the padding at its end, as
// DtInpChannel_ReadFrame() delivers it.
//
// Returns DTAPI_OK, DTAPI_E_INVALID_VIDSTD or DTAPI_E_INVALID_ARG, as for
// DtSdiView_SetRawFrame().
CDTAPI_API DtapiResult DtSdiView_RawFrameSize(int VidStd, int BitsPerSymbol,
                                              size_t* Size);

// Points View at the raw frame in Frame, of video standard VidStd and with BitsPerSymbol
// bits a symbol: 10, packed least significant bit first, or 16, each symbol in a 16-bit
// little-endian word. Frame must hold the whole frame: Size must be what
// DtSdiView_RawFrameSize() gives. Nothing is copied or checked in the frame itself.
//
// The same view serves a parser, which only reads the frame, and a builder, which
// writes all of it. A view that describes a frame of an input channel must have been
// released first (DtInpChannel_ReleaseFrame()).
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_VIDSTD  VidStd is not a standard of SD, HD, 3G level A or 2160p
//                           over one link
//   DTAPI_E_INVALID_ARG     Frame is NULL, or BitsPerSymbol is not 10 or 16
//   DTAPI_E_INVALID_SIZE    Size is not the size of such a frame
//   DTAPI_E_IN_USE          View describes a frame an input channel holds
// After a failure the view describes no frame.
CDTAPI_API DtapiResult DtSdiView_SetRawFrame(DtSdiView* View, void* Frame, size_t Size,
                                             int VidStd, int BitsPerSymbol);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Images +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// An image is the active video of one whole frame, 4:2:2 as SDI carries it. Of an
// interlaced frame it holds both fields; one field alone is never called an image, but a
// field. How the fields lie in the image is the program's choice, DtSdiFields, and must
// always be given, so that it is never a guess.
//
// Woven, an image has the standard's size: 720 x 487 for 525 lines, with field 1 on top;
// 720 x 576 for 625 lines; 1280 x 720, 1920 x 1080 and 3840 x 2160. A progressive frame,
// and a segmented one (PsF), has no fields to arrange: every choice gives the same image.
//

// How the two fields of an interlaced frame lie in an image.
typedef enum DtSdiFields
{
    DT_SDI_FIELDS_NONE,     // Not given; refused
    DT_SDI_FIELDS_WOVEN,    // Woven into one image, their lines taking turns
    DT_SDI_FIELDS_SEPARATE, // Apart: all lines of field 1, then all lines of field 2.
                            // Not yet supported
} DtSdiFields;

// The pixel formats of an image.
typedef enum DtSdiPixelFormat
{
    DT_SDI_PIXFMT_NONE,        // Not given; refused
    DT_SDI_PIXFMT_UYVY_10B,    // Cb Y Cr Y, 10-bit samples packed least significant bit
                               // first, as SDI carries them; one plane
    DT_SDI_PIXFMT_UYVY_8B,     // Cb Y Cr Y, 8 bits a sample, the lowest two bits dropped;
                               // one plane
    DT_SDI_PIXFMT_V210,        // 10-bit 4:2:2 in 32-bit words, three samples a word, six
                               // pixels in 16 bytes, lines padded to 128 bytes; one plane
    DT_SDI_PIXFMT_Y210,        // Y Cb Y Cr, each sample in a 16-bit little-endian word
                               // with the 10 bits at the top; one plane
    DT_SDI_PIXFMT_YUV422P_10B, // Planes of Y, Cb and Cr, each sample in a 16-bit
                               // little-endian word with the 10 bits at the bottom
    DT_SDI_PIXFMT_YUV422P_8B,  // Planes of Y, Cb and Cr, 8 bits a sample
} DtSdiPixelFormat;

// An image in the program's memory. The parser writes into it; the builder reads it.
//
// Converting from 10 to 8 bits drops the lowest two bits; from 8 to 10 bits, the builder
// adds two zero bits. The builder limits every sample to 4..1019, since 0 to 3 and 1020
// to 1023 are reserved for timing references in SDI.
typedef struct DtSdiImage
{
    DtSdiPixelFormat Format;
    DtSdiFields Fields; // How the fields of an interlaced frame lie in the image
    uint8_t* Planes[3]; // The first line of each plane: Y, Cb and Cr for the planar
                        // formats, Planes[0] only for the others
    int Strides[3];     // Bytes from the start of one line of a plane to the next; at
                        // least what DtSdiImage_GetSize() gives
} DtSdiImage;

// Returns the size of an image of video standard VidStd in Format: its width and height
// in pixels, and the least stride of each plane in bytes, 0 for a plane the format does
// not have. The size is the same with either arrangement of the fields. Width, Height and
// MinStrides may each be NULL.
//
// Returns DTAPI_OK, DTAPI_E_INVALID_VIDSTD as for DtSdiView_SetRawFrame(), or
// DTAPI_E_INVALID_FORMAT for an unknown Format.
CDTAPI_API DtapiResult DtSdiImage_GetSize(int VidStd, DtSdiPixelFormat Format, int* Width,
                                          int* Height, int MinStrides[3]);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Audio +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// SDI carries up to 16 channels of embedded audio, at 48 kHz, in four groups of four:
// channels 1 to 4 are group 1, and so on. AES3 pairs them: channels 1 and 2 are one pair.
// A frame holds the samples of its own period. At 1001 rates the number varies from
// frame to frame, e.g. 1601 or 1602 at 29.97 Hz.
//
// Each channel has a buffer of its own, with a stride, so that the same structure
// describes interleaved audio (all channels in one buffer, Stride the number of
// channels) and planar audio (a buffer per channel, Stride 1).
//

#define DT_SDI_AUDIO_MAX_CHANNELS 16

// The formats of audio samples, chosen per pair of channels.
typedef enum DtSdiAudioFormat
{
    DT_SDI_AUDIO_NONE, // Not wanted, or not sent
    DT_SDI_AUDIO_PCM,  // PCM: an int32_t a sample, the 24 bits at the top
    DT_SDI_AUDIO_AES3, // The AES3 subframe as it is, for Dolby E and other data: a
                       // uint32_t a sample, laid out as the DT_SDI_AES3_ masks say
} DtSdiAudioFormat;

// The bits of a sample in DT_SDI_AUDIO_AES3. Bits 0 to 2 are 0.
#define DT_SDI_AES3_Z 0x00000008u     // The first subframe of an AES3 block of 192
#define DT_SDI_AES3_AUDIO 0x0FFFFFF0u // The 24 bits of audio or data, the lowest at bit 4
#define DT_SDI_AES3_V 0x10000000u     // Validity: 1 means not valid as audio
#define DT_SDI_AES3_U 0x20000000u     // User data
#define DT_SDI_AES3_C 0x40000000u     // Channel status
#define DT_SDI_AES3_P 0x80000000u     // Parity

// The samples of one channel.
typedef struct DtSdiAudioChannel
{
    void* Samples;  // The first sample; NULL for a channel the program does not want or
                    // does not send
    int Stride;     // Samples from one sample of this channel to its next; 0 means 1
    int MaxSamples; // Parser: room for this many; at least DtSdiAudio_MaxSamples()
    int NumSamples; // Parser: set to the samples the frame held. Builder: the samples
                    // ready in Samples, at least as many as the frame takes
    bool Present;   // Parser: set when the frame carried this channel
    bool Invalid;   // Parser: set when the V bit of a sample said it was not valid; the
                    // sample is given as it arrived all the same
} DtSdiAudioChannel;

// The embedded audio of a frame.
typedef struct DtSdiAudio
{
    // The format of each pair of channels: [0] for channels 1 and 2, and so on.
    DtSdiAudioFormat Formats[DT_SDI_AUDIO_MAX_CHANNELS / 2];

    // The channels: [0] is channel 1.
    DtSdiAudioChannel Channels[DT_SDI_AUDIO_MAX_CHANNELS];

    // Parser, with DtSdiParser_SetAudioChecks(): per group, the packets whose BCH code
    // or checksum failed. [0] is group 1.
    int NumPacketErrors[DT_SDI_AUDIO_MAX_CHANNELS / 4];

    // The frame's place in the audio cadence of a 1001 rate, from 1, e.g. 1 to 5 at
    // 29.97 Hz; 0 for a rate without a cadence. Parser: set to the place the frame
    // has. Builder: 0 follows the builder's own cadence; another value puts the frame
    // at that place, and the cadence goes on from there. To pass received audio on
    // frame for frame, give the builder the parser's FrameNumber.
    int FrameNumber;

    // Builder: set to the samples per channel the frame took from each channel's
    // Samples. The rest stays the program's, for the next frame.
    int NumSamplesUsed;
} DtSdiAudio;

// Returns in *NumSamples the most audio samples a channel can have in one frame of video
// standard VidStd: the size of a channel's buffer that is always large enough.
//
// Returns DTAPI_OK or DTAPI_E_INVALID_VIDSTD.
CDTAPI_API DtapiResult DtSdiAudio_MaxSamples(int VidStd, int* NumSamples);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Ancillary data +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Ancillary packets (SMPTE ST 291) carry timecode, captions, AFD and much else, in the
// horizontal blanking (HANC) or the vertical blanking (VANC). The parser and builder
// pass them as they are, without interpreting them. Audio and its control packets go
// through DtSdiAudio instead, and the payload ID through DtSdiView_GetPayloadId().
//
// By default the parser lists every packet but those of the audio and the payload ID.
// With DtSdiParser_SetAncFilter() it lists only the packets a program asks for, e.g. the
// captions; with a filter that names them, the audio's and the payload ID's packets too,
// so that the list is a full table of contents of the frame. A filter that names only
// some lines or only the vertical blanking also spares the parser reading the rest,
// when the program wants no audio from the frame.
//

// Which blanking an ancillary packet is in.
typedef enum DtSdiAncSpace
{
    DT_SDI_ANC_SPACE_NONE, // Not given; refused
    DT_SDI_ANC_SPACE_HANC, // The horizontal blanking
    DT_SDI_ANC_SPACE_VANC, // The vertical blanking
    DT_SDI_ANC_SPACE_BOTH, // Either
} DtSdiAncSpace;

// A kind of ancillary packet the parser lists: those with this DID and SDID, in this
// blanking and on these lines. DID and SDID are the 8-bit values, without parity bits.
typedef struct DtSdiAncFilter
{
    bool AnyDid;         // Every DID, the audio's and the payload ID's included; Did and
                         // Sdid are then not used
    uint8_t Did;         // The data ID
    bool AnySdid;        // Every SDID of the DID
    uint8_t Sdid;        // The secondary data ID; not used for a DID of 0x80 and up,
                         // whose packets carry a data block number instead
    DtSdiAncSpace Space; // The blanking
    int FirstLine;       // The first line, counted as DtSdiAncPacket's Line; 0 for the
                         // frame's first
    int LastLine;        // The last line; 0 for the frame's last
} DtSdiAncFilter;

// One ancillary packet. Words are 10-bit values in 16-bit words, parity bits included,
// as the frame carries them.
typedef struct DtSdiAncPacket
{
    int Line;              // The SDI line, counted from 1 as the frame's line numbers
                           // count; in 525 lines, the first active line of field 1 is
                           // line 17
    bool InHanc;           // In the horizontal blanking; else in the vertical blanking
    bool OnChroma;         // HD and up: in the chroma stream; else in the luma stream.
                           // Always false in SD
    int VirtualInterface;  // 3G and 2160p: the virtual interface, counted from 1.
                           // Builder: 0 means the first
    uint16_t Did;          // Data ID
    uint16_t SdidOrDbn;    // Secondary data ID, or for a DID of 0x80 and up the data
                           // block number
    int NumWords;          // User data words, 0 to 255
    const uint16_t* Words; // The user data words
    bool ChecksumOk;       // Parser: whether the packet's checksum held
} DtSdiAncPacket;

// The ancillary packets of a frame.
//
// The parser writes the packets into Packets and their words into Words, and points
// each packet's Words into it. With Words NULL it lists the packets only, and copies
// none of their words: each packet's Words is then NULL, its NumWords still set. What
// does not fit it counts in NumLost, and returns DTAPI_OK all the same: the rest of the
// frame is not lost for it.
//
// The builder reads Packets[0] to Packets[NumPackets - 1], with each packet's own Words;
// it does not use the other fields.
typedef struct DtSdiAncData
{
    DtSdiAncPacket* Packets;
    int MaxPackets; // Parser: room in Packets
    int NumPackets; // Parser: set. Builder: given
    uint16_t* Words;
    int MaxWords; // Parser: room in Words
    int NumWords; // Parser: set
    int NumLost;  // Parser: set to the packets that did not fit
} DtSdiAncData;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtSdiParser +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A parser takes frames apart into an image, audio and ancillary packets.
//

typedef struct DtSdiParser DtSdiParser;

// Creates a parser. Returns NULL when there is not enough memory.
CDTAPI_API DtSdiParser* DtSdiParser_Alloc(void);

// Frees Parser. NULL does nothing.
CDTAPI_API void DtSdiParser_Free(DtSdiParser* Parser);

// Frees *Parser, as DtSdiParser_Free() does, and sets *Parser to NULL. NULL does
// nothing.
CDTAPI_API void DtSdiParser_Freep(DtSdiParser** Parser);

// Takes the frame in Frame apart, reading each of its lines once. Image, Audio and Anc
// say what the program wants; each may be NULL for not wanted.
//   Image    The active video is written into it, in its Format.
//   Audio    Each pair whose format is not DT_SDI_AUDIO_NONE has its channels' samples
//            written, as far as they have a buffer.
//   Anc      The ancillary packets are listed: those of DtSdiParser_SetAncFilter(), or
//            every packet but the audio's and the payload ID's.
//
// A frame of another video standard than the one before starts the audio afresh.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_STATE           Frame describes no frame
//   DTAPI_E_INVALID_FORMAT  Image->Format, Image->Fields or a format in Audio is not one
//                           of its enum, or is _NONE where it must be given
//   DTAPI_E_INVALID_ARG     a plane the format needs is NULL, or its stride too small
//   DTAPI_E_BUF_TOO_SMALL   an audio channel's MaxSamples is below
//                           DtSdiAudio_MaxSamples(); or Anc has no room for every packet
//   DTAPI_E_NOT_SUPPORTED   Image->Fields is DT_SDI_FIELDS_SEPARATE, which is not yet
//                           supported
// The checks of the arguments come first: after such a failure, nothing has been
// written. Room for the ancillary packets is found only while reading the frame: when
// it runs out, the call writes the image and the audio all the same, lists the packets
// that fit, counts the others in Anc->NumLost, and then returns DTAPI_E_BUF_TOO_SMALL.
// So after DTAPI_E_BUF_TOO_SMALL, Anc->NumLost above 0 means that the frame was taken
// apart and only packets were lost.
CDTAPI_API DtapiResult DtSdiParser_Parse(DtSdiParser* Parser, const DtSdiView* Frame,
                                         DtSdiImage* Image, DtSdiAudio* Audio,
                                         DtSdiAncData* Anc);

// Makes the parser list only the ancillary packets that match one of the NumFilters
// filters in Filters. NULL, with NumFilters 0, lists every packet but the audio's and
// the payload ID's again, the default. The parser keeps its own copy of the filters.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG     a NULL Parser, a negative NumFilters, a NULL Filters with
//                           NumFilters above 0, a filter's Space of _NONE or unknown, or
//                           its LastLine before its FirstLine
//   DTAPI_E_OUT_OF_MEM      no memory for the copy; the filters are as before
CDTAPI_API DtapiResult DtSdiParser_SetAncFilter(DtSdiParser* Parser,
                                                const DtSdiAncFilter* Filters,
                                                int NumFilters);

// Makes the parser check the BCH code and checksum of every audio packet, and count the
// packets that fail in DtSdiAudio's NumPacketErrors. Off by default: the BCH code takes
// a calculation over every word of every packet. The samples are the same either way.
CDTAPI_API DtapiResult DtSdiParser_SetAudioChecks(DtSdiParser* Parser, bool Check);

// Makes the parser divide the lines of a frame over the threads of Pool, at most
// NumThreads at once; 0 lets the parser choose by the video standard, as an input
// channel does (see DtInpChannel_SetWorkerPool()). NULL for Pool does all work in the
// calling thread, the default. The results are the same either way.
//
// Returns DTAPI_OK, or DTAPI_E_INVALID_ARG for a NULL Parser or a negative NumThreads.
CDTAPI_API DtapiResult DtSdiParser_SetWorkerPool(DtSdiParser* Parser, DtWorkerPool* Pool,
                                                 int NumThreads);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtSdiBuilder +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A builder puts frames together from an image, audio and ancillary packets. It writes
// the whole frame: the timing references, line numbers and CRCs, the payload ID, the
// audio with its control packets, and the program's ancillary packets.
//
// The audio's PCM channels get professional AES3 channel status for 48 kHz. Pairs in
// DT_SDI_AUDIO_AES3 carry the program's own V, U and C bits and AES3 block, and the
// builder computes only their P bit.
//

typedef struct DtSdiBuilder DtSdiBuilder;

// Creates a builder. Returns NULL when there is not enough memory.
CDTAPI_API DtSdiBuilder* DtSdiBuilder_Alloc(void);

// Puts a frame together in Frame, writing all of it. Image, Audio and Anc may each be
// NULL: without an image the frame is black, without audio it carries none.
//
// Each audio channel the program sends offers NumSamples samples, at least as many as
// the frame takes: DtSdiBuilder_GetNumAudioSamples() says how many that is, and
// DtSdiAudio_MaxSamples() is always enough. The builder takes what the frame needs from
// the start of each channel's Samples and sets Audio->NumSamplesUsed to that number;
// the rest stays the program's, for the next frame. The builder takes 48 kHz exactly, in
// step with the video: audio from a source with a clock of its own makes the program's
// store of samples grow or shrink slowly, which the program has to correct.
//
// The builder writes the program's ancillary packets on the line, in the blanking, the
// stream and the virtual interface each packet names. In the horizontal blanking they
// follow the builder's own packets of that line: the payload ID, the audio control
// packets and the audio. The builder keeps audio off the line after the switching
// point; the program's packets may go on any line.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_STATE           Frame describes no frame, or a frame of an input channel,
//                           which is read-only
//   DTAPI_E_INVALID_FORMAT  as for DtSdiParser_Parse()
//   DTAPI_E_INVALID_ARG     as for DtSdiParser_Parse(); or Audio->FrameNumber is not a
//                           place in the cadence of the frame's rate; or a packet has
//                           the DID of audio or of a payload ID
//   DTAPI_E_BUF_TOO_SMALL   an audio channel offers fewer samples than the frame takes;
//                           Audio->NumSamplesUsed is then set to the number it takes
//   DTAPI_E_INVALID_LINE    a packet's line is not in the blanking it names
//   DTAPI_E_TOO_LONG        the packets of a line do not fit in its blanking
// The checks come first: after a failure, the frame has not been written and the
// cadence has not moved on.
CDTAPI_API DtapiResult DtSdiBuilder_Build(DtSdiBuilder* Builder, DtSdiView* Frame,
                                          const DtSdiImage* Image, DtSdiAudio* Audio,
                                          const DtSdiAncData* Anc);

// Frees Builder. NULL does nothing.
CDTAPI_API void DtSdiBuilder_Free(DtSdiBuilder* Builder);

// Frees *Builder, as DtSdiBuilder_Free() does, and sets *Builder to NULL. NULL does
// nothing.
CDTAPI_API void DtSdiBuilder_Freep(DtSdiBuilder** Builder);

// Returns in *NumSamples how many audio samples per channel the next frame of video
// standard VidStd that the builder puts together takes. At 1001 rates it follows the
// cadence, e.g. 1602, 1601, 1602, 1601, 1602 at 29.97 Hz. FrameNumber is what the
// program will give in DtSdiAudio's FrameNumber: 0 for the builder's own cadence, or the
// frame's place in it.
//
// Returns DTAPI_OK, DTAPI_E_INVALID_VIDSTD, or DTAPI_E_INVALID_ARG for a FrameNumber
// that is not a place in the cadence of the standard's rate.
CDTAPI_API DtapiResult DtSdiBuilder_GetNumAudioSamples(const DtSdiBuilder* Builder,
                                                       int VidStd, int FrameNumber,
                                                       int* NumSamples);

// Makes the builder divide the lines of a frame over the threads of Pool, as
// DtSdiParser_SetWorkerPool() does for a parser.
CDTAPI_API DtapiResult DtSdiBuilder_SetWorkerPool(DtSdiBuilder* Builder,
                                                  DtWorkerPool* Pool, int NumThreads);

#ifdef __cplusplus
}
#endif
