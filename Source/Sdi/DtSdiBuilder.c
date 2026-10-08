// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiBuilder.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The builder: puts SDI frames together from an image, audio and ancillary data
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The builder writes a frame line by line. Each line is made stream by stream. There is
// one stream in SD, C and Y in HD and 3G, and C and Y of each of four links in 2160p. A
// stream's words are, in order:
// 1. its timing references, in HD and up with the line number and the CRC after EAV;
// 2. its horizontal blanking, with the payload ID and the program's packets in it;
// 3. its active part. This is the image or, on a line of the vertical blanking,
//    blanking with the program's packets.
// The streams are then interleaved into the raw line, and the line is written.
//
// Blanking is 200 (hex) in a C stream and 040 in a Y stream. In SD the two alternate,
// starting with Cb. The black frames of DtSdiFrame use the same values. The payload ID
// goes on the third line after each field's switching line, in the Y stream, and on
// every link in 2160p. Both match the DekTec matrix API's output. The audio that
// DtSdiEmbed makes follows the payload ID. Its control packets go in the Y stream and
// its data in the C stream, on link 1 in 2160p. The program's packets come last. The
// line CRCs and the packets' checksums are left to the transmitter unless the program
// asks for them.
//
// This is the portable version.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"      // Allocation seam.
#include "Core/DtWorkerPool.h" // The bands of lines.
#include "DtSdiAnc.h"          // The data IDs the builder writes itself.
#include "DtSdiConv.h"         // The conversions.
#include "DtSdiCrc.h"          // The line CRC.
#include "DtSdiEmbed.h"        // The audio.
#include "DtSdiImage.h"        // Reading the image.
#include "DtSdiSymbols.h"      // Writing the frame's symbols.
#include "DtSdiView.h"         // The frame a call writes.
#include "Video/DtSmpte352.h"  // The payload ID.
#include "cdtapi_sdi.h"        // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Constants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The most words one stream of a line can have. The longest stream line has 4125
// words, in 720p23.98 and 720p24.
#define DT_SDIBUILDER_MAX_STREAM_WORDS 4200

// The most symbols a raw line can have. The longest raw line is that of 2160p23.98,
// with four links of 2 x 2750.
#define DT_SDIBUILDER_MAX_LINE_SYMBOLS 22000

// The width of the widest image, 2160p, in pixels.
#define DT_SDIBUILDER_MAX_WIDTH 3840

// The most sections a frame's packets can go in. Each line has two blankings in each of
// up to eight streams.
#define DT_SDIBUILDER_MAX_SECTIONS (1125 * 2 * 8)

// The words of the payload ID packet: the flag, the IDs, the count, four bytes and the
// checksum.
#define DT_SDIBUILDER_PAYLOAD_ID_WORDS 11

// The blanking values of a C word and of a Y word.
#define DT_SDIBUILDER_BLANK_C 0x200
#define DT_SDIBUILDER_BLANK_Y 0x040

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The buffers and the state of one band of lines. One piece of a frame's job builds the
// band.
typedef struct DtSdiBuilderBand
{
    // Per stream: the words made for the current line.
    uint16_t Words[8][DT_SDIBUILDER_MAX_STREAM_WORDS];
    // The raw line, with the streams interleaved.
    uint16_t Line[DT_SDIBUILDER_MAX_LINE_SYMBOLS];
    // In 2160p: the two image lines that one raw line takes.
    uint16_t Image[2][2 * DT_SDIBUILDER_MAX_WIDTH];
    // Per stream: the CRC over the active part of the line before.
    uint32_t LastCrc[8];
    // The position reached in writing the frame's audio.
    DtSdiEmbedCursor Cursor;
} DtSdiBuilderBand;

struct DtSdiBuilder
{
    int VidStd;          // The standard of the frame built last; 0 before the first
    bool Checksums;      // True to work out line CRCs and packet checksums. False leaves
                         // them to the transmitter.
    uint32_t LastCrc[8]; // Per stream: the CRC over the active part of the last line of
                         // that frame. The next frame's first line CRC starts from it.
    uint32_t CrcTable[1024]; // The CRC-18 of each 10-bit word, from a CRC of 0
    DtSdiCrcFunc Crc;      // The function that computes the CRC over a line's active part
    const DtSdiConv* Conv; // The conversions the builder uses
    DtSdiEmbed Embed;      // The audio state: the frame being built and the cadence
    int Used[DT_SDIBUILDER_MAX_SECTIONS]; // Per section of the frame: the words its
                                          // packets take

    DtWorkerPool* Pool;      // The worker pool the program gave
    int NumThreads;          // The number of threads the program gave
    DtJobRunner Runner;      // Runs a frame's job in pieces
    int RunnerPieces;        // The pieces the runner was set up for; 0 when it must be
                             // set up again
    DtSdiBuilderBand* Bands; // A band for each piece
    int NumBands;            // The number of bands
};

// Selects which of the program's packets PutPackets writes. In SD the program's audio
// control packets must come before the builder's audio (SMPTE ST 272), so they are
// written separately from the other packets.
typedef enum PacketKind
{
    PACKETS_ALL,        // Every packet
    PACKETS_SD_CONTROL, // Only SD audio control packets
    PACKETS_REST,       // Every packet except SD audio control packets
} PacketKind;

// The frame that one job builds, band by band.
typedef struct BuildJob
{
    DtSdiBuilder* Builder;   // The builder that runs the job
    DtSdiView* Frame;        // The frame being written
    const DtSdiImage* Image; // The image, or NULL for a black image
    const DtSdiAncData* Anc; // The program's packets, or NULL
    uint32_t Vpid;           // The four bytes of the payload ID
    bool PayloadId; // The builder writes its own payload ID; false if the program has one
    int Unit;       // Bands start on a multiple of this many lines. It is 2 if every
                    // other line starts part-way through a byte, else 1.
    bool InPlace;   // The frame is room that an output channel lent: each line goes
                    // straight into its coded lines
    bool MakeRaw;   // MakeLine puts the whole line in Band->Line. In 2160p built in
                    // place it does so only when the line CRCs need it.
} BuildJob;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StreamOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns the index of the stream a packet goes in. In SD this is the one stream. In HD
// and 3G it is C or Y. In 2160p it is C or Y of the packet's link.
//
static int StreamOf(const DtSdiGeometry* Geo, const DtSdiAncPacket* Packet)
{
    if (Geo->NumStreams == 1)
        return 0;
    const int Link = Packet->VirtualInterface == 0 ? 1 : Packet->VirtualInterface;
    return 2 * (Link - 1) + (Packet->OnChroma ? 0 : 1);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Section -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the index of the section for the blanking of stream Stream on line LineIndex.
// InHanc selects the horizontal blanking, else the vertical blanking.
//
static int Section(int LineIndex, bool InHanc, int Stream)
{
    return (LineIndex * 2 + (InHanc ? 1 : 0)) * 8 + Stream;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasPayloadId -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns whether stream Stream on line LineIndex carries the payload ID. The payload
// ID goes on the third line after each field's switching line. It goes in SD's one
// stream, and above SD in every Y stream, including the Y stream of each link in 2160p.
// A DekTec card shows the payload ID of 1080p50 in the Y stream alone.
//
static bool HasPayloadId(const DtSdiGeometry* Geo, int LineIndex, int Stream)
{
    const DtFrameProps* Props = &Geo->Props;
    const int Line = LineIndex + 1;
    const bool OnLine =
        Line == Props->Fields[0].SwitchingLine + 3 ||
        (Props->NumFields == 2 && Line == Props->Fields[1].SwitchingLine + 3);
    if (!OnLine)
        return false;
    return Geo->NumStreams == 1 || !Geo->StreamIsChroma[Stream];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsSdControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns whether P is an SD audio control packet (DID EC to EF).
//
static bool IsSdControl(const DtSdiGeometry* Geo, const DtSdiAncPacket* P)
{
    return Geo->NumStreams == 1 && P->Did >= DT_SDIANC_DID_SD_CONTROL_4 &&
           P->Did <= DT_SDIANC_DID_SD_CONTROL_1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckPackets -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Checks the program's packets and that each section of the frame has room for them.
// Builder->Used receives the number of words the packets take in each section.
//
// Audio packets from the program are refused while the builder embeds its own audio
// (HasAudio). One exception: in SD the builder writes no audio control packets, so the
// program may add its own.
//
// Sets *PayloadId to whether the builder should write its own payload ID, which it does
// unless the program's packets contain one.
//
static DtapiResult CheckPackets(DtSdiBuilder* Builder, const DtSdiGeometry* Geo,
                                const DtSdiAncData* Anc, bool HasAudio, bool* PayloadId)
{
    if (Anc->NumPackets < 0 || (Anc->NumPackets > 0 && Anc->Packets == NULL))
        return DTAPI_E_INVALID_ARG;

    *PayloadId = true;
    for (int p = 0; p < Anc->NumPackets; p++)
    {
        if (Anc->Packets[p].Did == DT_SDIANC_DID_PAYLOAD_ID &&
            Anc->Packets[p].SdidOrDbn == DT_SDIANC_SDID_PAYLOAD_ID)
        {
            *PayloadId = false;
        }
    }

    const int NumLines = Geo->Layout.NumLines;
    memset(Builder->Used, 0, (size_t)NumLines * 2 * 8 * sizeof(Builder->Used[0]));
    const int MaxLink = Geo->Is4k ? 4 : 1;
    const int HancRoom = Geo->StreamHancWords - Geo->StreamEavWords - Geo->StreamSavWords;

    for (int p = 0; p < Anc->NumPackets; p++)
    {
        const DtSdiAncPacket* P = &Anc->Packets[p];
        if (P->Did > 0xFF || P->SdidOrDbn > 0xFF || P->NumWords < 0 ||
            P->NumWords > 255 || (P->NumWords > 0 && P->Words == NULL) ||
            P->VirtualInterface < 0 || P->VirtualInterface > MaxLink ||
            (Geo->NumStreams == 1 && P->OnChroma))
        {
            return DTAPI_E_INVALID_ARG;
        }
        if (HasAudio && DtSdiAnc_IsAudio((uint8_t)P->Did) && !IsSdControl(Geo, P))
            return DTAPI_E_INVALID_ARG;
        if (P->Line < 1 || P->Line > NumLines ||
            (!P->InHanc && !DtSdiGeometry_IsVanc(Geo, P->Line - 1)))
        {
            return DTAPI_E_INVALID_LINE;
        }

        const int Stream = StreamOf(Geo, P);
        int* Used = &Builder->Used[Section(P->Line - 1, P->InHanc, Stream)];
        int Room = P->InHanc ? HancRoom : Geo->StreamActiveWords;
        if (P->InHanc && *PayloadId && HasPayloadId(Geo, P->Line - 1, Stream))
            Room -= DT_SDIBUILDER_PAYLOAD_ID_WORDS;
        if (P->InHanc)
            Room -= DtSdiEmbed_HancWords(&Builder->Embed, Geo, P->Line - 1, Stream);
        *Used += 7 + P->NumWords;
        if (*Used > Room)
            return DTAPI_E_TOO_LONG;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutPackets -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the program's packets of kind Kind for one section into Words, starting at
// Pos, in the order the program gave them. Returns the position after the last one.
//
static int PutPackets(const DtSdiGeometry* Geo, const DtSdiAncData* Anc, int LineIndex,
                      bool InHanc, int Stream, PacketKind Kind, bool Checksum,
                      uint16_t* Words, int Pos)
{
    for (int p = 0; Anc != NULL && p < Anc->NumPackets; p++)
    {
        const DtSdiAncPacket* P = &Anc->Packets[p];
        if (P->Line != LineIndex + 1 || P->InHanc != InHanc || StreamOf(Geo, P) != Stream)
            continue;
        if ((Kind == PACKETS_SD_CONTROL && !IsSdControl(Geo, P)) ||
            (Kind == PACKETS_REST && IsSdControl(Geo, P)))
        {
            continue;
        }
        Pos = DtSdiAnc_Put(Words, Pos, (uint8_t)P->Did, (uint8_t)P->SdidOrDbn, P->Words,
                           P->NumWords, Checksum);
    }
    return Pos;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ImageLineOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the image line that raw line LineIndex carries, or -1 for a line of the
// vertical blanking. Use it only for standards other than 2160p.
//
static int ImageLineOf(const DtSdiGeometry* Geo, int LineIndex)
{
    for (int f = 0; f < Geo->NumFields; f++)
    {
        const int j = LineIndex - Geo->FieldFirstIndex[f];
        if (j >= 0 && j < Geo->FieldNumLines[f])
            return Geo->NumFields == 1 ? j : 2 * j + f;
    }
    return -1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutBlack -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes a black active part into raw line Line: 200 (hex) in the C words, 040 in the
// Y words.
//
static void PutBlack(const DtSdiGeometry* Geo, uint16_t* Line)
{
    const int Streams = Geo->NumStreams;
    const size_t First = (size_t)Geo->StreamHancWords * (size_t)Streams;
    const size_t Count = (size_t)Geo->StreamActiveWords * (size_t)Streams;
    if (Streams == 8)
    {
        for (size_t k = 0; k < Count; k++)
            Line[First + k] =
                (k & 4) == 0 ? DT_SDIBUILDER_BLANK_C : DT_SDIBUILDER_BLANK_Y;
        return;
    }
    for (size_t k = 0; k < Count; k++)
        Line[First + k] = (k & 1) == 0 ? DT_SDIBUILDER_BLANK_C : DT_SDIBUILDER_BLANK_Y;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetImageLines4k -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the two image lines that raw line LineIndex of a 2160p frame carries into
// Band->Image: the upper line, which links 1 and 2 carry, and the lower line, which
// links 3 and 4 carry. Without an image both lines are black.
//
static void GetImageLines4k(const BuildJob* Job, DtSdiBuilderBand* Band, int LineIndex)
{
    const DtSdiGeometry* Geo = &Job->Frame->Geo;

    if (Job->Image == NULL)
    {
        const size_t Count = 2 * (size_t)Geo->Width;
        for (size_t k = 0; k < Count; k++)
            Band->Image[0][k] = Band->Image[1][k] =
                (k & 1) == 0 ? DT_SDIBUILDER_BLANK_C : DT_SDIBUILDER_BLANK_Y;
        return;
    }
    const int k = LineIndex - Geo->PictureFirstIndex;
    DtSdiImage_GetLine(Job->Image, Geo, 2 * k, Band->Image[0], Job->Builder->Conv);
    DtSdiImage_GetLine(Job->Image, Geo, 2 * k + 1, Band->Image[1], Job->Builder->Conv);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Builds raw line LineIndex in Band->Line.
//
// The horizontal blanking is built stream by stream in Band->Words and then interleaved
// into the line. On a line of the vertical blanking the active part is built the same
// way, with the program's packets in it.
//
// On a line of the image, the active part comes from the job's image. In SD, HD and 3G
// the image is read straight into the line, because the samples are in the same order.
// In 2160p it is read into Band->Image first and then split over the links. Without an
// image the line is black.
//
// In 2160p built in place, the line goes into the coded lines from Band->Words and
// Band->Image, so the words are put in Band->Line only when the line CRCs need it.
//
static void MakeLine(const BuildJob* Job, DtSdiBuilderBand* Band, int LineIndex)
{
    const DtSdiBuilder* Builder = Job->Builder;
    const DtSdiGeometry* Geo = &Job->Frame->Geo;
    const DtSdiImage* Image = Job->Image;
    const DtSdiAncData* Anc = Job->Anc;
    const uint32_t Vpid = Job->Vpid;
    const int Line = LineIndex + 1;
    const int Streams = Geo->NumStreams;
    const int Hanc = Geo->StreamHancWords;
    const int Total = Hanc + Geo->StreamActiveWords;
    const uint32_t Eav = DtSdiFrame_Xyz(&Geo->Props, Line, true);
    const uint32_t Sav = DtSdiFrame_Xyz(&Geo->Props, Line, false);
    const bool Vanc = DtSdiGeometry_IsVanc(Geo, LineIndex);
    const int Made = Vanc ? Total : Hanc; // The words made per stream
    uint16_t* Raw = Band->Line;

    for (int s = 0; s < Streams; s++)
    {
        uint16_t* W = Band->Words[s];

        // Fills the stream with blanking first. In SD's one stream C and Y alternate,
        // starting with Cb.
        if (Streams == 1)
        {
            for (int k = 0; k < Made; k++)
                W[k] = (k & 1) == 0 ? DT_SDIBUILDER_BLANK_C : DT_SDIBUILDER_BLANK_Y;
        }
        else
        {
            const uint16_t Blank =
                Geo->StreamIsChroma[s] ? DT_SDIBUILDER_BLANK_C : DT_SDIBUILDER_BLANK_Y;
            for (int k = 0; k < Made; k++)
                W[k] = Blank;
        }

        // Writes the timing references, and in HD and up the line number after EAV.
        W[0] = 0x3FF;
        W[1] = 0x000;
        W[2] = 0x000;
        W[3] = (uint16_t)Eav;
        if (Streams > 1)
        {
            W[4] = (uint16_t)DtSdiFrame_WithParity((uint32_t)Line << 2);
            W[5] = (uint16_t)DtSdiFrame_WithParity((uint32_t)(Line >> 7) << 2 & 0x3C);
        }
        W[Hanc - 4] = 0x3FF;
        W[Hanc - 3] = 0x000;
        W[Hanc - 2] = 0x000;
        W[Hanc - 1] = (uint16_t)Sav;

        // The horizontal blanking, in this order:
        // 1. the payload ID, unless the program sends its own;
        // 2. in SD, the program's audio control packets;
        // 3. the builder's audio;
        // 4. the rest of the program's packets.
        int Pos = Geo->StreamEavWords;
        if (Job->PayloadId && HasPayloadId(Geo, LineIndex, s))
        {
            const uint16_t Bytes[4] = {DtSdiAnc_WithParity8(Vpid & 0xFF),
                                       DtSdiAnc_WithParity8(Vpid >> 8 & 0xFF),
                                       DtSdiAnc_WithParity8(Vpid >> 16 & 0xFF),
                                       DtSdiAnc_WithParity8(Vpid >> 24 & 0xFF)};
            Pos = DtSdiAnc_Put(W, Pos, DT_SDIANC_DID_PAYLOAD_ID,
                               DT_SDIANC_SDID_PAYLOAD_ID, Bytes, 4, Builder->Checksums);
        }
        Pos = PutPackets(Geo, Anc, LineIndex, true, s, PACKETS_SD_CONTROL,
                         Builder->Checksums, W, Pos);
        Pos = DtSdiEmbed_Put(&Builder->Embed, &Band->Cursor, LineIndex, s, W, Pos,
                             Builder->Checksums);
        PutPackets(Geo, Anc, LineIndex, true, s, PACKETS_REST, Builder->Checksums, W,
                   Pos);
        if (Vanc)
        {
            PutPackets(Geo, Anc, LineIndex, false, s, PACKETS_ALL, Builder->Checksums, W,
                       Hanc);
        }
    }

    // Writes the CRC words of HD and up. Each stream's CRC covers the active part of the
    // line before, then this line's EAV and line number. The CRC takes ten bits at a
    // time. Because the CRC is linear, the register's lower ten bits XORed with the word
    // select one entry of the table. The register's upper bits are shifted down and
    // XORed with that entry. When the CRC is left to the transmitter, the CRC words are
    // 200 (hex). That is a CRC of 0 with its bit 9 set.
    const uint32_t* Table = Builder->CrcTable;
    for (int s = 0; Streams > 1 && s < Streams; s++)
    {
        uint16_t* W = Band->Words[s];
        uint32_t Crc = Band->LastCrc[s];
        if (Builder->Checksums)
            for (int k = 0; k < 6; k++)
                Crc = (Crc >> 10) ^ Table[(Crc ^ W[k]) & 0x3FF];
        else
            Crc = 0;
        W[6] = (uint16_t)DtSdiFrame_WithParity(Crc);
        W[7] = (uint16_t)DtSdiFrame_WithParity(Crc >> 9);
    }

    // Interleaves the words made for each stream into the line.
    for (int s = 0; Job->MakeRaw && s < Streams; s++)
    {
        const uint16_t* W = Band->Words[s];
        uint16_t* To = Raw + Geo->StreamFirst[s];
        for (int k = 0; k < Made; k++)
            To[(size_t)k * (size_t)Streams] = W[k];
    }

    // Writes the active part of a line of the image.
    if (!Vanc)
    {
        uint16_t* Active = Raw + (size_t)Hanc * (size_t)Streams;
        if (Geo->Is4k && (Image != NULL || Job->InPlace))
        {
            GetImageLines4k(Job, Band, LineIndex);
            if (Job->MakeRaw)
                Builder->Conv->Join4k(Band->Image[0], Band->Image[1],
                                      (size_t)Geo->LinkWidth, Active);
        }
        else if (Image == NULL)
            PutBlack(Geo, Raw);
        else
            DtSdiImage_GetLine(Image, Geo, ImageLineOf(Geo, LineIndex), Active,
                               Builder->Conv);
    }

    // Computes the CRC of this line's active part for every stream. The next line
    // carries it. Crcs is indexed by a stream's position in the line, LastCrc by stream
    // number.
    if (Streams > 1 && Builder->Checksums)
    {
        uint32_t Crcs[DT_SDICRC_MAX_STREAMS];
        Builder->Crc(Raw + (size_t)Hanc * (size_t)Streams, (size_t)(Total - Hanc),
                     Streams, Table, Crcs);
        for (int s = 0; s < Streams; s++)
            Band->LastCrc[s] = Crcs[Geo->StreamFirst[s]];
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PackSection -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Packs Count symbols as 10-bit symbols into the section of Bytes bytes at Out, and
// clears the rest of the section. Count need not be a multiple of four: a HANC section
// of 720p23.98 and 720p24 is not.
//
static void PackSection(const DtSdiConv* Conv, const uint16_t* Symbols, size_t Count,
                        uint8_t* Out, size_t Bytes)
{
    const size_t Whole = Count / 4 * 4;
    size_t Byte = Whole / 4 * 5;
    uint32_t Bits = 0; // Bits not yet written, the first in the lowest bit
    int NumBits = 0;

    Conv->Pack10(Symbols, Whole, Out);
    for (size_t i = Whole; i < Count; i++)
    {
        Bits |= (uint32_t)(Symbols[i] & 0x3FF) << NumBits;
        for (NumBits += 10; NumBits >= 8; NumBits -= 8, Bits >>= 8)
            Out[Byte++] = (uint8_t)Bits;
    }
    if (NumBits > 0)
        Out[Byte++] = (uint8_t)Bits;
    memset(Out + Byte, 0, Bytes - Byte);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteCodedLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes raw line LineIndex, which MakeLine has just made, into its coded lines in the
// room that an output channel lent. The line headers are there already.
//
// Up to 3G the line's HANC part goes into the HANC section and its active part into the
// video section. In 2160p each link's C and Y words, interleaved, go into the link's
// HANC section; links 1 and 2 are in the first coded line, links 3 and 4 in the second.
// On a line of the vertical blanking each link's active part takes half of the video
// section. On a line of the picture the video section of the first coded line holds the
// upper image line, as links 1 and 2 carry it in turn, and that of the second coded line
// the lower image line. Band->Line serves as scratch in 2160p.
//
static void WriteCodedLine(const BuildJob* Job, DtSdiBuilderBand* Band, int LineIndex)
{
    const DtSdiGeometry* Geo = &Job->Frame->Geo;
    const DtSdiFrameLayout* Layout = &Geo->Layout;
    const DtSdiConv* Conv = Job->Builder->Conv;
    const size_t HancBytes = (size_t)Layout->SectionBytesHanc;
    const size_t VideoBytes = (size_t)Layout->SectionBytesActive;
    uint8_t* Coded = DtSdiView_TxCodedLines(Job->Frame, LineIndex);

    if (!Geo->Is4k)
    {
        const size_t Hanc = (size_t)Layout->LineNumSymsHanc;
        PackSection(Conv, Band->Line, Hanc, Coded, HancBytes);
        PackSection(Conv, Band->Line + Hanc, (size_t)Layout->LineNumSymsActive,
                    Coded + HancBytes, VideoBytes);
        return;
    }

    const bool Vanc = DtSdiGeometry_IsVanc(Geo, LineIndex);
    const size_t HancWords = (size_t)Geo->StreamHancWords;
    const size_t Words = HancWords + (Vanc ? (size_t)Geo->StreamActiveWords : 0);
    const size_t HalfSymbols = (size_t)Layout->SectionNumSymsActive / 2;
    const size_t HalfBytes = HalfSymbols / 4 * 5;
    uint8_t* Lines[2] = {Coded + Layout->TxLineHeaderNumBytes,
                         Coded + Layout->TxStride + Layout->TxLineHeaderNumBytes};

    // Stream 2L is the C stream of link L + 1, and stream 2L + 1 its Y stream.
    for (int Link = 0; Link < 4; Link++)
    {
        const uint16_t* C = Band->Words[2 * Link];
        const uint16_t* Y = Band->Words[2 * Link + 1];
        uint16_t* Symbols = Band->Line;
        uint8_t* Line = Lines[Link / 2];
        const size_t Side = (size_t)(Link & 1);

        for (size_t k = 0; k < Words; k++)
        {
            Symbols[2 * k] = C[k];
            Symbols[2 * k + 1] = Y[k];
        }
        PackSection(Conv, Symbols, 2 * HancWords, Line + Side * HancBytes, HancBytes);
        if (Vanc)
            PackSection(Conv, Symbols + 2 * HancWords, HalfSymbols,
                        Line + 2 * HancBytes + Side * HalfBytes, HalfBytes);
    }
    for (int h = 0; h < 2; h++)
    {
        uint8_t* Video = Lines[h] + 2 * HancBytes;
        if (Vanc)
            memset(Video + 2 * HalfBytes, 0, VideoBytes - 2 * HalfBytes);
        else
            PackSection(Conv, Band->Image[h], (size_t)Layout->SectionNumSymsActive, Video,
                        VideoBytes);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BuildBand -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Builds one piece of a frame's job. The piece makes and writes its share of the
// frame's lines, using its own band.
//
// At its first line, the band takes the audio cursor from the plan. When checksums are
// on in HD and up, the band also needs the CRC of the line before its first line. So
// unless the band starts the frame, it makes that line first, only for its CRC. The
// band that ends the frame writes the frame's padding and keeps the CRC for the next
// frame. In room that an output channel lent, each line goes straight into its coded
// lines, and the frame has no padding.
//
static void BuildBand(void* Context, int PieceIndex, int NumPieces)
{
    const BuildJob* Job = (const BuildJob*)Context;
    DtSdiBuilder* Builder = Job->Builder;
    const DtSdiView* Frame = Job->Frame;
    const DtSdiGeometry* Geo = &Frame->Geo;
    const int NumLines = Geo->Layout.NumLines;
    DtSdiBuilderBand* Band = &Builder->Bands[PieceIndex];
    int First = 0;
    int End = 0;
    DtJobRunner_Split(NumLines, PieceIndex, NumPieces, Job->Unit, &First, &End);
    if (First >= End)
        return;

    memcpy(Band->LastCrc, Builder->LastCrc, sizeof(Band->LastCrc));
    if (First > 0 && Builder->Checksums && Geo->NumStreams > 1)
    {
        DtSdiEmbed_CursorAt(&Builder->Embed, First - 1, &Band->Cursor);
        MakeLine(Job, Band, First - 1);
    }
    DtSdiEmbed_CursorAt(&Builder->Embed, First, &Band->Cursor);

    if (Job->InPlace)
    {
        for (int LineIndex = First; LineIndex < End; LineIndex++)
        {
            MakeLine(Job, Band, LineIndex);
            WriteCodedLine(Job, Band, LineIndex);
        }
        if (End == NumLines)
            memcpy(Builder->LastCrc, Band->LastCrc, sizeof(Builder->LastCrc));
        return;
    }

    const size_t LineSymbols =
        (size_t)(Geo->StreamHancWords + Geo->StreamActiveWords) * (size_t)Geo->NumStreams;
    const size_t FirstBit = (size_t)First * LineSymbols * (size_t)Frame->BitsPerSymbol;
    DtSdiSymbolWriter Writer;
    DtSdiSymbolWriter_Init(&Writer, Frame->Frame + FirstBit / 8, Frame->BitsPerSymbol,
                           Builder->Conv);
    for (int LineIndex = First; LineIndex < End; LineIndex++)
    {
        MakeLine(Job, Band, LineIndex);
        DtSdiSymbolWriter_Put(&Writer, Band->Line, LineSymbols);
    }
    if (End == NumLines)
    {
        DtSdiSymbolWriter_End(&Writer, Frame->Frame + Frame->FrameSize);
        memcpy(Builder->LastCrc, Band->LastCrc, sizeof(Builder->LastCrc));
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConfigureBands -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets up the runner and gives each piece a band. The number of pieces is the number of
// threads the program asked for. When the program asked for 0, the standard decides.
// When the pieces or their bands cannot be had, the builder works in one piece, in the
// calling thread. Returns false when not even one band can be allocated.
//
static bool ConfigureBands(DtSdiBuilder* Builder, const DtSdiGeometry* Geo)
{
    const int Pieces = Builder->NumThreads > 0 ? Builder->NumThreads
                                               : DtSdiFrame_NumJobPieces(&Geo->Layout);
    if (Pieces == Builder->RunnerPieces && Builder->Bands != NULL)
        return true;

    if (DtJobRunner_SetPool(&Builder->Runner, Builder->Pool, Pieces) != DTAPI_OK)
        DtJobRunner_SetPool(&Builder->Runner, NULL, 0);
    int Wanted = DtJobRunner_NumPieces(&Builder->Runner);
    if (Wanted != Builder->NumBands || Builder->Bands == NULL)
    {
        DtAlloc_Free(Builder->Bands);
        Builder->Bands =
            (DtSdiBuilderBand*)DtAlloc_Malloc((size_t)Wanted * sizeof(DtSdiBuilderBand));
        if (Builder->Bands == NULL && Wanted > 1)
        {
            DtJobRunner_SetPool(&Builder->Runner, NULL, 0);
            Wanted = 1;
            Builder->Bands = (DtSdiBuilderBand*)DtAlloc_Malloc(sizeof(DtSdiBuilderBand));
        }
        Builder->NumBands = Builder->Bands == NULL ? 0 : Wanted;
    }
    Builder->RunnerPieces = Pieces;
    return Builder->Bands != NULL;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Builder +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiBuilder_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtSdiBuilder* DtSdiBuilder_Alloc(void)
{
    DtSdiBuilder* Builder = (DtSdiBuilder*)DtAlloc_Malloc(sizeof(DtSdiBuilder));
    if (Builder == NULL)
        return NULL;
    memset(Builder, 0, sizeof(*Builder));
    Builder->Conv = DtSdiConv_Best();
    DtJobRunner_Init(&Builder->Runner);
    for (uint32_t i = 0; i < 1024; i++)
        Builder->CrcTable[i] = DtSdiFrame_Crc18(i, 0);
    Builder->Crc = DtSdiCrc_Best();
    return Builder;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiBuilder_Build -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Checks every argument before it touches the frame. The audio is planned first, so
// that the room it leaves for the program's packets is known. The CRC of the first line
// covers the last line of the frame built before, when that frame has the same
// standard. In the first frame, or after a change of standard, it covers only the
// line's own EAV and line number. A frame built in room that an output channel lent is
// marked built, so that the channel takes it.
//
DtapiResult DtSdiBuilder_Build(DtSdiBuilder* Builder, DtSdiView* Frame,
                               const DtSdiImage* Image, DtSdiAudio* Audio,
                               const DtSdiAncData* Anc)
{
    if (Builder == NULL || Frame == NULL)
        return DTAPI_E_INVALID_ARG;
    // An input channel's frame is read-only; room an output channel lent is not.
    if (!Frame->HasFrame || (Frame->Holder != NULL && !Frame->IsTx))
        return DTAPI_E_STATE;
    const DtSdiGeometry* Geo = &Frame->Geo;

    DtapiResult Result = DTAPI_OK;
    if (Image != NULL)
        Result = DtSdiImage_Check(Image, Geo);
    if (Result == DTAPI_OK)
    {
        DtSdiEmbed_Init(&Builder->Embed, Geo);
        Result = DtSdiEmbed_Begin(&Builder->Embed, Audio);
    }
    bool PayloadId = true;
    if (Result == DTAPI_OK && Anc != NULL)
        Result = CheckPackets(Builder, Geo, Anc, Audio != NULL, &PayloadId);
    if (Result == DTAPI_OK && !ConfigureBands(Builder, Geo))
        Result = DTAPI_E_OUT_OF_MEM;
    if (Result != DTAPI_OK)
        return Result;
    DtSdiEmbed_Start(&Builder->Embed);

    if (Builder->VidStd != Geo->VidStd)
    {
        memset(Builder->LastCrc, 0, sizeof(Builder->LastCrc));
        Builder->VidStd = Geo->VidStd;
    }
    // Makes each band start on a line that starts on a byte boundary, so that no two
    // bands write the same byte. In 10 bits, a line of 720p23.98 and 720p24 ends
    // part-way through a byte. In room that an output channel lent, every coded line
    // starts on a byte boundary.
    const size_t LineBits = (size_t)(Geo->StreamHancWords + Geo->StreamActiveWords) *
                            (size_t)Geo->NumStreams * (size_t)Frame->BitsPerSymbol;
    int Unit = 1;
    while (!Frame->IsTx && (LineBits * (size_t)Unit) % 8 != 0)
        Unit++;
    const uint32_t Vpid = DtSmpte352_Make(Geo->VidStd);
    const bool MakeRaw = !(Frame->IsTx && Geo->Is4k) || Builder->Checksums;
    BuildJob Job = {Builder,   Frame, Image,       Anc,    Vpid,
                    PayloadId, Unit,  Frame->IsTx, MakeRaw};
    DtJobRunner_Run(&Builder->Runner, BuildBand, &Job);
    DtSdiEmbed_End(&Builder->Embed);
    if (Frame->IsTx)
        Frame->IsBuilt = true;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiBuilder_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSdiBuilder_Free(DtSdiBuilder* Builder)
{
    if (Builder == NULL)
        return;
    DtJobRunner_Free(&Builder->Runner);
    DtAlloc_Free(Builder->Bands);
    DtAlloc_Free(Builder);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiBuilder_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiBuilder_Freep(DtSdiBuilder** Builder)
{
    if (Builder == NULL)
        return;
    DtSdiBuilder_Free(*Builder);
    *Builder = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiBuilder_GetNumAudioSamples -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sets up a copy of the builder's audio state for VidStd and asks the copy for the
// number of samples. The copy knows where the builder's cadence stands, and the builder
// itself is left unchanged.
//
DtapiResult DtSdiBuilder_GetNumAudioSamples(const DtSdiBuilder* Builder, int VidStd,
                                            int FrameNumber, int* NumSamples)
{
    if (Builder == NULL || NumSamples == NULL)
        return DTAPI_E_INVALID_ARG;
    *NumSamples = 0;
    DtSdiGeometry Geo;
    const DtapiResult Result = DtSdiGeometry_Init(&Geo, VidStd);
    if (Result != DTAPI_OK)
        return Result;
    DtSdiEmbed* Embed = (DtSdiEmbed*)DtAlloc_Malloc(sizeof(DtSdiEmbed));
    if (Embed == NULL)
        return DTAPI_E_OUT_OF_MEM;
    memcpy(Embed, &Builder->Embed, sizeof(*Embed));
    DtSdiEmbed_Init(Embed, &Geo);
    const DtapiResult Found = DtSdiEmbed_NumSamples(Embed, FrameNumber, NumSamples);
    DtAlloc_Free(Embed);
    return Found;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiBuilder_SetChecksums -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Turning the CRCs on starts them afresh. The CRCs of the frame before were not worked
// out, so the first line's CRC covers nothing of that frame.
//
DtapiResult DtSdiBuilder_SetChecksums(DtSdiBuilder* Builder, bool Compute)
{
    if (Builder == NULL)
        return DTAPI_E_INVALID_ARG;
    if (Compute && !Builder->Checksums)
        memset(Builder->LastCrc, 0, sizeof(Builder->LastCrc));
    Builder->Checksums = Compute;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiBuilder_SetWorkerPool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Hands the pool to the runner at once, so that the builder holds its reference from
// this call on. The pieces are set up again for the standard of the next frame.
//
DtapiResult DtSdiBuilder_SetWorkerPool(DtSdiBuilder* Builder, DtWorkerPool* Pool,
                                       int NumThreads)
{
    if (Builder == NULL || NumThreads < 0)
        return DTAPI_E_INVALID_ARG;
    const DtapiResult Result = DtJobRunner_SetPool(&Builder->Runner, Pool, NumThreads);
    if (Result != DTAPI_OK)
        return Result;
    Builder->Pool = Pool;
    Builder->NumThreads = NumThreads;
    Builder->RunnerPieces = 0;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiBuilder_UseConv -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiBuilder_UseConv(DtSdiBuilder* Builder, const DtSdiConv* Conv)
{
    Builder->Conv = Conv;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiBuilder_UseCrc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSdiBuilder_UseCrc(DtSdiBuilder* Builder, DtSdiCrcFunc Crc)
{
    Builder->Crc = Crc;
}
