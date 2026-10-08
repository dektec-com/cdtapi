// #*#*#*#*#*#*#*#*#*#*#*#*#*#* cdtapi_sdi.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Public C API that takes SDI frames apart and puts them together
//
// SPDX-License-Identifier: BSD-3-Clause
//
// An SDI frame carries more than its image. It also carries timing references, line
// numbers, CRCs, embedded audio and other ancillary data. A parser takes a frame apart
// into the image, the audio and the ancillary packets. A builder puts a frame together
// from these parts and adds everything else the frame needs. Both handle SD, HD,
// 3G level A, and 2160p over one 6G or 12G link.
//
// Neither the parser nor the builder needs a card. Both work on a view of a frame. A
// view records where the lines of the frame are, and creating it copies nothing. A view
// can describe one of two things:
//   - A raw frame in the program's memory. DtSdiView_SetRawFrame() points a view at it.
//     This is the layout that DtInpChannel_ReadFrame() reads and
//     DtOutpChannel_WriteFrame() writes, and that an .sdi file holds after its header.
//   - A frame in the receive buffer of an input channel, read where the card wrote it.
//     DtInpChannel_AcquireFrame(), in cdtapi.h, points a view at it.
//   - Room for a frame in the transmit buffer of an output channel, for the builder to
//     build the frame in place. DtOutpChannel_AcquireFrame(), in cdtapi.h, points a view
//     at it.
//
// To take frames apart, a program takes these steps:
// 1. It creates a parser with DtSdiParser_Alloc() and a view with DtSdiView_Alloc().
//    Optionally, it gives the parser a worker pool.
// 2. For each frame, it points the view at the frame and calls DtSdiParser_Parse(). It
//    passes the parts it wants: an image, audio, ancillary packets, or any mix of these.
// 3. It frees the parser and the view.
//
// To put frames together, a program takes these steps:
// 1. It creates a builder with DtSdiBuilder_Alloc() and a view with DtSdiView_Alloc().
// 2. For each frame, it calls DtSdiBuilder_GetNumAudioSamples() to learn how many audio
//    samples per channel the frame takes. It points the view at its frame buffer and
//    calls DtSdiBuilder_Build() with the image, the audio and the ancillary packets.
// 3. It frees the builder and the view.
//
// The program supplies every buffer, both for input and for results. A framework's
// buffer pool can therefore lend them. A call writes each result once, in its final
// place.
//
// A parser or builder keeps state from one frame to the next, such as the audio cadence.
// Each stream of frames therefore needs its own parser or builder, and only one thread
// at a time may use it.
//
// Every function that takes a parser, builder, view or other pointer returns
// DTAPI_E_INVALID_ARG when that pointer is NULL, unless its comment says otherwise.

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
// A view describes where an SDI frame lies in memory. It holds the frame's video
// standard, the number of bits per symbol, and where each line starts. Pointing a view
// at a frame copies nothing. The view stays valid only as long as the memory it
// describes.
//
// A program creates a view once and points it at one frame after another. No frame then
// costs an allocation.
//

// A view of one SDI frame. A parser reads the frame through it, and a builder writes the
// frame through it.
typedef struct DtSdiView DtSdiView;

// Points at a run of SDI symbols where they lie in a frame, so that a program can read
// them one by one. A symbol takes one of two forms:
//   - 10 bits, packed least significant bit first.
//   - 16 bits, a little-endian word with the value in its lower 10 bits.
// A run of 10-bit symbols need not start on a byte boundary. The pointer therefore names
// the bit as well as the byte.
typedef struct DtSdiSymbolPtr
{
    const uint8_t* Byte; // The byte that holds the first bit of symbol 0
    int Bit;             // That bit within Byte, from 0 (least significant) to 7
    int BitsPerSymbol;   // The size of a symbol in bits: 10 or 16
} DtSdiSymbolPtr;

// Returns symbol Index of the run that Ptr points at. Index counts from 0. The result is
// a value from 0 to 1023. The function reads only the bytes that hold the symbol.
//
// It is meant for reading a few samples here and there, for example to check or
// analyse them. To convert a whole image, use DtSdiParser_Parse(), which is much faster.
static inline uint16_t DtSdiSymbolPtr_Get(const DtSdiSymbolPtr* Ptr, size_t Index)
{
    if (Ptr->BitsPerSymbol == 16)
    {
        const uint8_t* Word = Ptr->Byte + 2 * Index;
        return (uint16_t)((Word[0] | Word[1] << 8) & 0x3FF);
    }

    const size_t FirstBit = (size_t)Ptr->Bit + 10 * Index;
    const uint8_t* Bytes = Ptr->Byte + FirstBit / 8;
    const unsigned Shift = (unsigned)(FirstBit % 8);
    uint32_t Bits = (uint32_t)Bytes[0] | (uint32_t)Bytes[1] << 8;
    if (Shift > 6)
        Bits |= (uint32_t)Bytes[2] << 16;
    return (uint16_t)(Bits >> Shift & 0x3FF);
}

// Creates a view that does not yet describe a frame. Returns NULL when there is not
// enough memory.
CDTAPI_API DtSdiView* DtSdiView_Alloc(void);

// Frees View. The frame it describes is left untouched. NULL does nothing.
CDTAPI_API void DtSdiView_Free(DtSdiView* View);

// Frees *View, as DtSdiView_Free() does, and sets *View to NULL. NULL does nothing.
CDTAPI_API void DtSdiView_Freep(DtSdiView** View);

// Finds where one line of the image lies in the frame, without copying it. Line is a
// line of the woven image, counted from 0, top to bottom. The function sets *Symbols to
// point at the line's symbols.
//
// The symbols are in the frame's own order: Cb, Y, Cr, Y and so on. So
// DtSdiSymbolPtr_Get(Symbols, 0) is the first Cb of the line. A line has twice as many
// symbols as the image is wide in pixels.
//
// With this function a program reads the image without any copy. Keep in mind:
//   - Lines are not evenly spaced. The fields lie apart, and in the buffer of an input
//     channel the frame may wrap around.
//   - A line need not start on a byte boundary. In a 10-bit raw frame of 720p23.98 or
//     720p24, every other line starts part-way through a byte. The symbol pointer
//     handles this.
//   - When Symbols->Bit is 0, Symbols->Byte is where the line's packed symbols start.
//     The program can then read them in bulk, for example to copy or upload them as
//     they are. Bit is 0 for every line except half of the lines of 720p23.98 and
//     720p24 in 10 bits.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_STATE           the view does not describe a frame, or describes a frame
//                           that an output channel lent, which is still being built
//   DTAPI_E_INVALID_LINE    Line is not a line of the image
//   DTAPI_E_NOT_SUPPORTED   the frame is 2160p, whose lines are spread over its links
CDTAPI_API DtapiResult DtSdiView_GetActiveLine(const DtSdiView* View, int Line,
                                               DtSdiSymbolPtr* Symbols);

// Returns the format of the frame that View describes. *VidStd is set to its video
// standard, a DTAPI_VIDSTD_ code. *BitsPerSymbol is set to its number of bits per
// symbol, 10 or 16. Either pointer may be NULL.
//
// A program needs this for a frame of an input channel, because the channel detected
// that frame's standard.
//
// Returns DTAPI_OK, or DTAPI_E_STATE when the view does not describe a frame.
CDTAPI_API DtapiResult DtSdiView_GetFormat(const DtSdiView* View, int* VidStd,
                                           int* BitsPerSymbol);

// Reads the SMPTE ST 352 payload ID of the frame into *PayloadId. The four bytes of the
// payload ID are packed with the first byte in the most significant position. In 2160p
// it is the payload ID of the first link.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_STATE           the view does not describe a frame
//   DTAPI_E_NOT_FOUND       the frame carries no payload ID; *PayloadId is then 0
//   DTAPI_E_OUT_OF_MEM      no memory to read the frame's lines with
CDTAPI_API DtapiResult DtSdiView_GetPayloadId(const DtSdiView* View, uint32_t* PayloadId);

// Computes the size in bytes of a raw frame, as DtInpChannel_ReadFrame() delivers it.
// The frame has video standard VidStd and BitsPerSymbol bits per symbol. The size
// includes the padding at the end of the frame and is returned in *Size.
//
// Returns DTAPI_OK, DTAPI_E_INVALID_VIDSTD or DTAPI_E_INVALID_ARG. These occur in the
// same cases as for DtSdiView_SetRawFrame().
CDTAPI_API DtapiResult DtSdiView_RawFrameSize(int VidStd, int BitsPerSymbol,
                                              size_t* Size);

// Points View at a raw frame in the program's memory. Frame holds the frame, of video
// standard VidStd. BitsPerSymbol says how its symbols are stored:
//   10   packed least significant bit first
//   16   each symbol in a 16-bit little-endian word
// Frame must hold the whole frame, so Size must be what DtSdiView_RawFrameSize() gives.
// The function copies nothing and does not check the contents of the frame.
//
// The same view serves a parser, which only reads the frame, and a builder, which
// writes all of it. If the view describes a frame of an input channel, release that
// frame first with DtInpChannel_ReleaseFrame().
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_VIDSTD  VidStd is not a standard of SD, HD, 3G level A, or 2160p
//                           over one link
//   DTAPI_E_INVALID_ARG     Frame is NULL, or BitsPerSymbol is not 10 or 16
//   DTAPI_E_INVALID_SIZE    Size is not the size of such a frame
//   DTAPI_E_IN_USE          View describes a frame that an input channel holds
// After any failure except DTAPI_E_IN_USE, the view does not describe a frame.
CDTAPI_API DtapiResult DtSdiView_SetRawFrame(DtSdiView* View, void* Frame, size_t Size,
                                             int VidStd, int BitsPerSymbol);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Images +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// An image is the active video of one whole frame, in 4:2:2 as SDI carries it. The image
// of an interlaced frame holds both fields. One field alone is called a field, never an
// image.
//
// The program chooses how the fields lie in the image, with DtSdiFields. It must always
// give this choice, so that the layout is never a guess.
//
// A woven image has the size of its standard:
//   - 720 x 487 for 525 lines, with field 1 on top
//   - 720 x 576 for 625 lines
//   - 1280 x 720, 1920 x 1080 and 3840 x 2160
// A progressive frame has no fields to arrange, and neither has a segmented frame (PsF).
// For these, every choice of DtSdiFields gives the same image.
//

// How the two fields of an interlaced frame lie in an image.
typedef enum DtSdiFields
{
    DT_SDI_FIELDS_NONE,     // Not given; a call refuses it
    DT_SDI_FIELDS_WOVEN,    // Woven into one image, the lines of the fields alternating
    DT_SDI_FIELDS_SEPARATE, // Apart: all lines of field 1, then all lines of field 2.
                            // Not yet supported
} DtSdiFields;

// The pixel formats of an image.
typedef enum DtSdiPixelFormat
{
    DT_SDI_PIXFMT_NONE,        // Not given; a call refuses it
    DT_SDI_PIXFMT_UYVY_10B,    // Cb Y Cr Y, 10-bit samples packed least significant bit
                               // first, as SDI carries them. One plane
    DT_SDI_PIXFMT_UYVY_8B,     // Cb Y Cr Y, 8 bits per sample, with the lowest two bits
                               // dropped. One plane
    DT_SDI_PIXFMT_V210,        // 10-bit 4:2:2 in 32-bit words, three samples per word and
                               // six pixels in 16 bytes. Each line is padded to a
                               // multiple of 128 bytes. One plane
    DT_SDI_PIXFMT_Y210,        // Y Cb Y Cr, each sample in a 16-bit little-endian word
                               // with the 10 bits at the top. One plane
    DT_SDI_PIXFMT_YUV422P_10B, // Planes of Y, Cb and Cr, each sample in a 16-bit
                               // little-endian word with the 10 bits at the bottom
    DT_SDI_PIXFMT_YUV422P_8B,  // Planes of Y, Cb and Cr, 8 bits per sample
} DtSdiPixelFormat;

// An image in the program's memory. The parser writes into it, and the builder reads
// from it.
//
// Sample values are converted as follows:
//   - From 10 to 8 bits, the lowest two bits are dropped.
//   - From 8 to 10 bits, the builder appends two zero bits.
//   - The builder limits every sample to the range 4 to 1019. SDI reserves the values
//     0 to 3 and 1020 to 1023 for timing references.
typedef struct DtSdiImage
{
    DtSdiPixelFormat Format; // The pixel format of the image
    DtSdiFields Fields;      // How the fields of an interlaced frame lie in the image
    uint8_t* Planes[3];      // The first line of each plane. For the planar formats these
                             // are Y, Cb and Cr; the other formats use only Planes[0]
    int Strides[3];          // The distance in bytes from the start of one line of a
                             // plane to the next. It is at least the minimum stride
                             // that DtSdiImage_GetSize() gives
} DtSdiImage;

// Returns the size of an image of video standard VidStd in pixel format Format. It sets
// *Width and *Height to the size in pixels. It sets MinStrides[] to the minimum stride
// of each plane in bytes, or 0 for a plane that the format does not have. The size does
// not depend on how the fields are arranged. Width, Height and MinStrides may each be
// NULL.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_VIDSTD  as for DtSdiView_SetRawFrame()
//   DTAPI_E_INVALID_FORMAT  Format is not a known pixel format
CDTAPI_API DtapiResult DtSdiImage_GetSize(int VidStd, DtSdiPixelFormat Format, int* Width,
                                          int* Height, int MinStrides[3]);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Audio +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// SDI carries up to 16 channels of embedded audio at 48 kHz. The channels form four
// groups of four: channels 1 to 4 are group 1, and so on. AES3 combines them in pairs:
// channels 1 and 2 are the first pair.
//
// A frame holds the audio samples of its own frame period. At the 1001 rates, the number
// of samples varies from frame to frame, for example 1601 or 1602 at 29.97 Hz.
//
// Each channel has its own buffer and stride. The same structure therefore describes
// both layouts of audio:
//   - Interleaved: all channels in one buffer, with Stride the number of channels.
//   - Planar: a buffer per channel, with Stride 1.
//

// The number of audio channels that DtSdiAudio has room for.
//
// The parser and the builder handle channels 1 to 16: four groups of four, as SMPTE
// ST 299-1 and ST 272 define them. Channels 17 to 32 are reserved for groups 5 to 8 of
// SMPTE ST 299-2, so that supporting them later does not change this structure. The
// parser leaves them empty. The builder returns DTAPI_E_NOT_SUPPORTED when a program
// offers samples on them.
#define DT_SDI_AUDIO_MAX_CHANNELS 32

// The formats of audio samples. The program chooses one for each pair of channels.
typedef enum DtSdiAudioFormat
{
    DT_SDI_AUDIO_NONE, // The pair is not wanted, or not sent
    DT_SDI_AUDIO_PCM,  // PCM: one int32_t per sample, with the 24 bits at the top
    DT_SDI_AUDIO_AES3, // The AES3 subframe as it is, for Dolby E and other data. One
                       // uint32_t per sample, laid out as the DT_SDI_AES3_ masks say
} DtSdiAudioFormat;

// The bits of a sample in format DT_SDI_AUDIO_AES3. Bits 0 to 2 are 0.
#define DT_SDI_AES3_Z 0x00000008u     // Set in the first subframe of an AES3 block of 192
#define DT_SDI_AES3_AUDIO 0x0FFFFFF0u // The 24 bits of audio or data, the lowest at bit 4
#define DT_SDI_AES3_V 0x10000000u     // Validity: 1 means the sample is not valid audio
#define DT_SDI_AES3_U 0x20000000u     // User data
#define DT_SDI_AES3_C 0x40000000u     // Channel status
#define DT_SDI_AES3_P 0x80000000u     // Parity

// The samples of one audio channel.
typedef struct DtSdiAudioChannel
{
    void* Samples;  // The first sample. NULL for a channel that the program does not
                    // want or does not send
    int Stride;     // The distance in samples from one sample of this channel to the
                    // next. 0 means 1
    int MaxSamples; // Parser: the number of samples that fit in Samples. It must be at
                    // least what DtSdiAudio_MaxSamples() gives
    int NumSamples; // Parser: set to the number of samples the frame held.
                    // Builder: the number of samples ready in Samples. It must be at
                    // least the number the frame takes
    bool Present;   // Parser: set when the frame carried this channel
    bool Invalid;   // Parser: set when the V bit of a sample marked it as not valid.
                    // The sample is still delivered as it arrived
} DtSdiAudioChannel;

// The embedded audio of a frame.
typedef struct DtSdiAudio
{
    // The format of each pair of channels. [0] is for channels 1 and 2, and so on.
    DtSdiAudioFormat Formats[DT_SDI_AUDIO_MAX_CHANNELS / 2];

    // The channels. [0] is channel 1.
    DtSdiAudioChannel Channels[DT_SDI_AUDIO_MAX_CHANNELS];

    // Parser: the number of packets per group whose BCH code or checksum failed. [0] is
    // group 1. Counted only after DtSdiParser_SetAudioChecks() turns the checks on.
    int NumPacketErrors[DT_SDI_AUDIO_MAX_CHANNELS / 4];

    // The frame's place in the audio cadence of a 1001 rate, counted from 1. For example,
    // it runs from 1 to 5 at 29.97 Hz. It is 0 for a rate without a cadence.
    //   - Parser: set to the frame's place in the cadence.
    //   - Builder: 0 makes the builder follow its own cadence. Another value puts the
    //     frame at that place, and the cadence continues from there.
    // To pass received audio on frame for frame, give the builder the parser's
    // FrameNumber.
    int FrameNumber;

    // Builder: set to the number of samples per channel that the frame took from the
    // start of each channel's Samples. The remaining samples stay with the program, for
    // the next frame.
    int NumSamplesUsed;
} DtSdiAudio;

// Returns in *NumSamples the largest number of audio samples that one channel can have
// in one frame of video standard VidStd. A channel buffer of this size is always large
// enough.
//
// Returns DTAPI_OK or DTAPI_E_INVALID_VIDSTD.
CDTAPI_API DtapiResult DtSdiAudio_MaxSamples(int VidStd, int* NumSamples);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Ancillary data +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Ancillary packets (SMPTE ST 291) carry timecode, captions, AFD and much else. They lie
// in the horizontal blanking (HANC) or the vertical blanking (VANC). The parser and the
// builder pass packets on as they are, without interpreting them. Two kinds of packet
// take another route:
//   - Audio and its control packets go through DtSdiAudio.
//   - The payload ID goes through DtSdiView_GetPayloadId().
//
// By default the parser lists every packet except the audio packets and the payload ID.
// DtSdiParser_SetAncFilter() makes it list only the packets a program asks for, for
// example the captions. A filter can also name the audio packets and the payload ID.
// The list is then a full table of contents of the frame.
//
// A filter that names only some lines, or only the vertical blanking, also saves work.
// When the program wants no audio from the frame, the parser then skips reading the
// rest of the blanking.
//

// The blanking that an ancillary packet lies in.
typedef enum DtSdiAncSpace
{
    DT_SDI_ANC_SPACE_NONE, // Not given; a call refuses it
    DT_SDI_ANC_SPACE_HANC, // The horizontal blanking
    DT_SDI_ANC_SPACE_VANC, // The vertical blanking
    DT_SDI_ANC_SPACE_BOTH, // Either blanking
} DtSdiAncSpace;

// Selects a kind of ancillary packet for the parser to list. A packet matches when it
// has this DID and SDID, lies in this blanking, and is on one of these lines. DID and
// SDID are the 8-bit values, without parity bits.
typedef struct DtSdiAncFilter
{
    bool AnyDid;         // Matches every DID, including those of audio and the payload
                         // ID. Did and Sdid are then not used
    uint8_t Did;         // The data ID
    bool AnySdid;        // Matches every SDID of Did
    uint8_t Sdid;        // The secondary data ID. Not used for a DID of 0x80 and up,
                         // whose packets carry a data block number instead
    DtSdiAncSpace Space; // The blanking
    int FirstLine;       // The first line, numbered as DtSdiAncPacket's Line. 0 means
                         // the frame's first line
    int LastLine;        // The last line. 0 means the frame's last line
} DtSdiAncFilter;

// One ancillary packet. Each word is a 10-bit value in a 16-bit word, with the parity
// bits included, as the frame carries it.
typedef struct DtSdiAncPacket
{
    int Line;              // The SDI line number, counted from 1 as the frame numbers its
                           // lines. In 525 lines, the first active line of field 1 is
                           // line 17
    bool InHanc;           // True in the horizontal blanking, false in the vertical
                           // blanking
    bool OnChroma;         // HD and up: true in the chroma stream, false in the luma
                           // stream. Always false in SD
    int VirtualInterface;  // 3G and 2160p: the virtual interface, counted from 1.
                           // Builder: 0 means the first
    uint16_t Did;          // The data ID, 8 bits without parity. The builder adds the
                           // parity
    uint16_t SdidOrDbn;    // The secondary data ID or, for a DID of 0x80 and up, the
                           // data block number. 8 bits, as for Did
    int NumWords;          // The number of user data words, 0 to 255
    const uint16_t* Words; // The user data words
    bool ChecksumOk;       // Parser: true when the packet's checksum is correct
} DtSdiAncPacket;

// The ancillary packets of a frame.
//
// The parser writes the packets into Packets and their user data words into Words. It
// points each packet's Words into that buffer. When Words is NULL, the parser only lists
// the packets and copies none of their words. Each packet's Words is then NULL, and its
// NumWords is still set.
//
// A packet that does not fit is counted in NumLost. The parser still takes apart the
// rest of the frame, and then returns DTAPI_E_BUF_TOO_SMALL.
//
// The builder reads Packets[0] to Packets[NumPackets - 1], each with its own Words. It
// does not use the other fields of this structure.
typedef struct DtSdiAncData
{
    DtSdiAncPacket* Packets; // The packets
    int MaxPackets;          // Parser: the number of packets that fit in Packets
    int NumPackets;          // Parser: set to the number of packets listed.
                             // Builder: the number of packets to write
    uint16_t* Words;         // Parser: the buffer for the packets' user data words, or
                             // NULL to list the packets without their words
    int MaxWords;            // Parser: the number of words that fit in Words
    int NumWords;            // Parser: set to the number of words written into Words
    int NumLost;             // Parser: set to the number of packets that did not fit
} DtSdiAncData;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtSdiParser +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A parser takes frames apart into an image, audio and ancillary packets.
//

// A parser of SDI frames.
typedef struct DtSdiParser DtSdiParser;

// Creates a parser. Returns NULL when there is not enough memory.
CDTAPI_API DtSdiParser* DtSdiParser_Alloc(void);

// Frees Parser. NULL does nothing.
CDTAPI_API void DtSdiParser_Free(DtSdiParser* Parser);

// Frees *Parser, as DtSdiParser_Free() does, and sets *Parser to NULL. NULL does
// nothing.
CDTAPI_API void DtSdiParser_Freep(DtSdiParser** Parser);

// Takes the frame that the view Frame describes apart into an image, audio and
// ancillary packets. It reads each line of the frame once. Image, Audio and Anc say
// what the program wants. Each may be NULL when that part is not wanted.
//   Image    The parser writes the active video into it, in its Format.
//   Audio    For each pair whose format is not DT_SDI_AUDIO_NONE, the parser writes the
//            samples of the channels that have a buffer.
//   Anc      The parser lists the ancillary packets. These are the packets that
//            DtSdiParser_SetAncFilter() selects, or by default every packet except the
//            audio packets and the payload ID.
//
// When a frame has a different video standard than the frame before, the parser starts
// the audio afresh.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_STATE           Frame does not describe a frame, or describes a frame that
//                           an output channel lent, which is still being built
//   DTAPI_E_INVALID_FORMAT  Image->Format, Image->Fields or a format in Audio is not a
//                           value of its enum, or is _NONE where a value is required
//   DTAPI_E_INVALID_ARG     a plane that the format needs is NULL, or its stride is too
//                           small
//   DTAPI_E_BUF_TOO_SMALL   an audio channel's MaxSamples is below
//                           DtSdiAudio_MaxSamples(), or Anc has no room for every packet
//   DTAPI_E_NOT_SUPPORTED   Image->Fields is DT_SDI_FIELDS_SEPARATE, which is not yet
//                           supported
//   DTAPI_E_OUT_OF_MEM      there is no memory for the buffers of even one band of the
//                           image's lines
//
// The parser checks the arguments first. After a failed check it has written nothing.
//
// Whether Anc has room for every packet becomes clear only while the parser reads the
// frame. When the room runs out, the parser still writes the image and the audio. It
// lists the packets that fit and counts the others in Anc->NumLost. It then returns
// DTAPI_E_BUF_TOO_SMALL. So when the result is DTAPI_E_BUF_TOO_SMALL and Anc->NumLost is
// above 0, the frame was taken apart and only packets were lost.
CDTAPI_API DtapiResult DtSdiParser_Parse(DtSdiParser* Parser, const DtSdiView* Frame,
                                         DtSdiImage* Image, DtSdiAudio* Audio,
                                         DtSdiAncData* Anc);

// Selects the ancillary packets that the parser lists. The parser then lists only the
// packets that match at least one of the NumFilters filters in Filters. Passing NULL
// with NumFilters 0 restores the default: every packet except the audio packets and the
// payload ID. The parser keeps its own copy of the filters.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG     Parser is NULL, NumFilters is negative, Filters is NULL
//                           while NumFilters is above 0, a filter's Space is _NONE or
//                           unknown, or a filter's LastLine comes before its FirstLine
//   DTAPI_E_OUT_OF_MEM      there is no memory for the copy. The filters stay as they
//                           were
CDTAPI_API DtapiResult DtSdiParser_SetAncFilter(DtSdiParser* Parser,
                                                const DtSdiAncFilter* Filters,
                                                int NumFilters);

// Turns checking of the audio packets on or off. When Check is true, the parser checks
// the BCH code and the checksum of every audio packet. It counts the packets that fail
// in DtSdiAudio's NumPacketErrors. The samples are the same either way.
//
// Checking is off by default, because the BCH code needs a calculation over every word
// of every packet.
//
// Returns DTAPI_OK, or DTAPI_E_INVALID_ARG for a NULL Parser.
CDTAPI_API DtapiResult DtSdiParser_SetAudioChecks(DtSdiParser* Parser, bool Check);

// Makes the parser divide the lines of a frame's image over the threads of Pool, at
// most NumThreads at once. With NumThreads 0, the parser chooses the number by the video
// standard, as an input channel does (see DtInpChannel_SetWorkerPool()). With Pool
// NULL, the parser does all work in the calling thread. This is the default.
//
// The parser always reads the audio and the ancillary packets in the calling thread,
// because they run through the frame in order. The results are the same either way.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG  Parser is NULL, or NumThreads is negative
//   DTAPI_E_OUT_OF_MEM   there is no memory for waiting on the pool's threads
CDTAPI_API DtapiResult DtSdiParser_SetWorkerPool(DtSdiParser* Parser, DtWorkerPool* Pool,
                                                 int NumThreads);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtSdiBuilder +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A builder puts frames together from an image, audio and ancillary packets. It writes
// the whole frame, which includes:
//   - the timing references, line numbers and CRCs
//   - the payload ID
//   - the audio with its control packets
//   - the program's ancillary packets
//
// The builder gives the PCM audio channels professional AES3 channel status for 48 kHz.
// Pairs in format DT_SDI_AUDIO_AES3 carry the program's own V, U and C bits and its own
// AES3 block. For those pairs the builder computes only the P bit.
//

// A builder of SDI frames.
typedef struct DtSdiBuilder DtSdiBuilder;

// Creates a builder. Returns NULL when there is not enough memory.
CDTAPI_API DtSdiBuilder* DtSdiBuilder_Alloc(void);

// Puts a frame together from an image, audio and ancillary packets, and writes all of it
// into the frame that the view Frame describes. Image, Audio and Anc may each be NULL.
// Without an image the frame is black. Without audio the frame carries none.
//
// Audio samples:
//   - Each audio channel that the program sends offers NumSamples samples. This must be
//     at least the number the frame takes. DtSdiBuilder_GetNumAudioSamples() gives that
//     number, and DtSdiAudio_MaxSamples() is always enough.
//   - The builder takes the samples it needs from the start of each channel's Samples,
//     and sets Audio->NumSamplesUsed to their number. The remaining samples stay with
//     the program, for the next frame.
//   - The builder takes exactly 48 kHz, locked to the video. Audio from a source with
//     its own clock therefore makes the program's store of samples slowly grow or
//     shrink. The program has to correct for this.
//
// CRCs and checksums:
//   - In HD and up, each line carries a CRC after its line number (SMPTE ST 292).
//   - Each ancillary packet ends in a checksum.
//   - By default the builder leaves both to the transmitter, which computes them while
//     it sends. The builder writes a legal word in their place. After
//     DtSdiBuilder_SetChecksums() the builder computes them itself.
//   - The builder always computes the BCH code of the audio packets in HD and up,
//     because no transmitter fills it in.
//
// Ancillary packets:
//   - The builder writes each of the program's packets on the line, in the blanking,
//     in the stream and in the virtual interface that the packet names.
//   - In the horizontal blanking, the program's packets follow the builder's own
//     packets of that line. These are the payload ID, the audio control packets and
//     the audio.
//   - The builder keeps audio off the line after the switching point. The program's
//     packets may go on any line.
//
// A program can also supply the payload ID and the audio packets itself, for example to
// pass on all the packets that the parser listed:
//   - Payload ID: when the program's packets contain one, the builder writes none of
//     its own.
//   - Audio: the builder accepts audio packets from the program only when Audio is NULL.
//     The program is then responsible for them. This includes the BCH codes in HD and
//     up, and keeping the packets off the line after the switching point. The builder
//     only checks that they fit.
//   - SD audio control packets: the builder never writes them, because SMPTE ST 272
//     makes them optional at 48 kHz. A program that wants one adds its own, on the
//     second line after a switching line. The builder accepts that packet even when it
//     embeds the audio itself. It places the packet before the audio, as the standard
//     requires.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_STATE           Frame does not describe a frame, or describes a frame that
//                           an input channel lent, which is read-only. A frame that an
//                           output channel lent is what the builder writes into
//   DTAPI_E_INVALID_FORMAT  as for DtSdiParser_Parse()
//   DTAPI_E_INVALID_ARG     as for DtSdiParser_Parse(). Also when Audio->FrameNumber is
//                           not a place in the cadence of the frame's rate, or when a
//                           packet has an audio DID while Audio is not NULL. SD audio
//                           control packets are an exception to the latter
//   DTAPI_E_BUF_TOO_SMALL   an audio channel offers fewer samples than the frame takes.
//                           Audio->NumSamplesUsed is then set to the number it takes
//   DTAPI_E_NOT_SUPPORTED   a channel from 17 to 32 has samples; see
//                           DT_SDI_AUDIO_MAX_CHANNELS
//   DTAPI_E_INVALID_LINE    a packet's line is not in the blanking that it names
//   DTAPI_E_TOO_LONG        the packets of a line do not fit in its blanking
//   DTAPI_E_OUT_OF_MEM      there is no memory for the buffers of even one band of
//                           lines
// The builder makes all checks first. After a failure, it has not written the frame and
// the audio cadence has not moved on.
CDTAPI_API DtapiResult DtSdiBuilder_Build(DtSdiBuilder* Builder, DtSdiView* Frame,
                                          const DtSdiImage* Image, DtSdiAudio* Audio,
                                          const DtSdiAncData* Anc);

// Frees Builder. NULL does nothing.
CDTAPI_API void DtSdiBuilder_Free(DtSdiBuilder* Builder);

// Frees *Builder, as DtSdiBuilder_Free() does, and sets *Builder to NULL. NULL does
// nothing.
CDTAPI_API void DtSdiBuilder_Freep(DtSdiBuilder** Builder);

// Returns in *NumSamples how many audio samples per channel the builder's next frame
// takes. VidStd is the video standard of that frame. FrameNumber is the value the
// program will give in DtSdiAudio's FrameNumber: 0 for the builder's own cadence, or
// the frame's place in the cadence.
//
// At the 1001 rates the number follows the cadence, for example 1602, 1601, 1602, 1601,
// 1602 at 29.97 Hz.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_VIDSTD  as for DtSdiView_SetRawFrame()
//   DTAPI_E_INVALID_ARG     FrameNumber is not a place in the cadence of the standard's
//                           rate
//   DTAPI_E_OUT_OF_MEM      there is not enough memory for the calculation
CDTAPI_API DtapiResult DtSdiBuilder_GetNumAudioSamples(const DtSdiBuilder* Builder,
                                                       int VidStd, int FrameNumber,
                                                       int* NumSamples);

// Turns the computation of CRCs and checksums on or off. When Compute is true, the
// builder computes the CRC of each line in HD and up and the checksum of each ancillary
// packet. A frame is then correct without help from a transmitter. This is needed for a
// frame that goes into a file, or to a transmitter that does not fill these words in.
//
// It is off by default, for two reasons. The CRC needs a calculation over every word of
// the frame. And a DekTec transmitter fills in both words while it sends. When off, the
// builder writes 200 (hex) in each CRC word and 0CC (hex) in each checksum. These are
// legal words, which the transmitter replaces.
//
// The CRC of a frame's first line also covers the last line of the frame built before.
// It does so only when that frame had the same standard and was built with CRCs.
//
// Returns DTAPI_OK, or DTAPI_E_INVALID_ARG for a NULL Builder.
CDTAPI_API DtapiResult DtSdiBuilder_SetChecksums(DtSdiBuilder* Builder, bool Compute);

// Makes the builder divide the lines of a frame over the threads of Pool, at most
// NumThreads at once. With NumThreads 0, the builder chooses the number by the video
// standard, as a parser does. Each band of lines works out the state of the audio and
// the CRCs at its own first line. The frames are therefore the same either way.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG  Builder is NULL, or NumThreads is negative
//   DTAPI_E_OUT_OF_MEM   there is no memory for waiting on the pool's threads
CDTAPI_API DtapiResult DtSdiBuilder_SetWorkerPool(DtSdiBuilder* Builder,
                                                  DtWorkerPool* Pool, int NumThreads);

#ifdef __cplusplus
}
#endif
