// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiBuilder.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The builder: puts SDI frames together from an image, audio and ancillary data
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The builder writes a frame line by line. Each line is made stream by stream: one in
// SD, C and Y in HD and 3G, C and Y of four links in 2160p. A stream's words are its
// timing references (in HD and up with the line number and CRC after EAV), its
// horizontal blanking with the payload ID and the program's packets in it, and its
// active part: image, or on a line of the vertical blanking, blanking with the
// program's packets. Then the streams are woven into the raw line and written.
//
// Blanking is 200 (hex) in a C stream and 040 in a Y stream, alternating from Cb in SD,
// as DTAPI's matrix and the black frames of DtSdiFrame have it. The payload ID is DTAPI's
// matrix's: on the line three after each field's switching line, in the Y stream, of
// every link in 2160p. The audio follows it, as DtSdiEmbed makes it:
// its control packets in the Y stream and its data in the C stream, in 2160p of link 1.
// The program's packets come last. The line CRCs and the packets' checksums are left to
// the transmitter unless the program asks for them. This is the portable version.

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

// The most words of one stream of a line: 4125 in 720p23.98 and 720p24.
#define DT_SDIBUILDER_MAX_STREAM_WORDS 4200

// The most symbols of a raw line: 2160p23.98's, four links of 2 x 2750.
#define DT_SDIBUILDER_MAX_LINE_SYMBOLS 22000

// The widest image, 2160p, in pixels.
#define DT_SDIBUILDER_MAX_WIDTH 3840

// The sections a frame's packets can go in: each line's two blankings, eight streams.
#define DT_SDIBUILDER_MAX_SECTIONS (1125 * 2 * 8)

// The words of the payload ID packet: flag, IDs, count, four bytes, checksum.
#define DT_SDIBUILDER_PAYLOAD_ID_WORDS 11

// The blanking of a C and of a Y word.
#define DT_SDIBUILDER_BLANK_C 0x200
#define DT_SDIBUILDER_BLANK_Y 0x040

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The buffers and the state of a band of lines: the words of each stream made for a
// line, the line woven, the image lines a 2160p line takes; the CRC over the active
// part of each stream of the line before; and where the audio stands.
typedef struct DtSdiBuilderBand
{
    uint16_t Words[8][DT_SDIBUILDER_MAX_STREAM_WORDS];
    uint16_t Line[DT_SDIBUILDER_MAX_LINE_SYMBOLS];
    uint16_t Image[2][2 * DT_SDIBUILDER_MAX_WIDTH];
    uint32_t LastCrc[8];
    DtSdiEmbedCursor Cursor;
} DtSdiBuilderBand;

struct DtSdiBuilder
{
    int VidStd;          // The standard of the frame built last; 0 before the first
    bool Checksums;      // Work out line CRCs and packet checksums; else leave them to
                         // the transmitter
    uint32_t LastCrc[8]; // Per stream: the CRC over the active part of that frame's last
                         // line, where the first line's CRC starts
    uint32_t CrcTable[1024]; // The CRC-18 of each 10-bit word, from a CRC of 0
    DtSdiCrcFunc Crc;        // The version of the CRC over a line's active part
    const DtSdiConv* Conv;   // The conversions
    DtSdiEmbed Embed;        // The audio: of the frame being built, and its cadence
    int Used[DT_SDIBUILDER_MAX_SECTIONS]; // Per section of the frame: its packets' words

    // The worker pool and the threads the program gave, the pieces the runner was set
    // up for (0 when it must be set up again), and a band for each piece.
    DtWorkerPool* Pool;
    int NumThreads;
    DtJobRunner Runner;
    int RunnerPieces;
    DtSdiBuilderBand* Bands;
    int NumBands;
};

// Which of the program's packets of a section PutPackets writes: in SD the audio control
// packets go before the builder's audio, as SMPTE ST 272 has them, the rest after it.
typedef enum PacketKind
{
    PACKETS_ALL,
    PACKETS_SD_CONTROL,
    PACKETS_REST,
} PacketKind;

// A frame a job of bands builds.
typedef struct BuildJob
{
    DtSdiBuilder* Builder;
    DtSdiView* Frame;
    const DtSdiImage* Image;
    const DtSdiAncData* Anc;
    uint32_t Vpid;
    bool PayloadId; // The builder writes the payload ID: the program's packets hold none
    int Unit;       // Lines that a band boundary does not split: 2 where every other
                    // line starts half-way through a byte
} BuildJob;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StreamOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The stream a packet names: SD's one; C or Y in HD and 3G; C or Y of a link in 2160p.
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
// The index of a section: the blanking of line LineIndex, horizontal or vertical, of
// stream Stream.
//
static int Section(int LineIndex, bool InHanc, int Stream)
{
    return (LineIndex * 2 + (InHanc ? 1 : 0)) * 8 + Stream;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasPayloadId -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns whether stream Stream of line LineIndex carries the payload ID: three lines
// after a field's switching line, in SD's one stream and in every Y stream above SD,
// those of each link of 2160p too. That is where DTAPI's matrix puts it: its comment
// has 3G and up in every stream, but its code tells HD by "not SD", and the card shows
// the payload ID of 1080p50 in the Y stream alone.
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
// Returns whether P is an audio control packet of SD, EC to EF.
//
static bool IsSdControl(const DtSdiGeometry* Geo, const DtSdiAncPacket* P)
{
    return Geo->NumStreams == 1 && P->Did >= DT_SDIANC_DID_SD_CONTROL_4 &&
           P->Did <= DT_SDIANC_DID_SD_CONTROL_1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckPackets -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Checks the program's packets and that each section has room for them, counting in
// Builder->Used the words each section's packets take. Packets with the DID of audio
// are the program's to send only when the builder embeds no audio of its own, Audio
// being NULL; in SD, where the builder writes no audio control packet, the program may
// send its own beside the builder's audio. *PayloadId is set to whether the builder
// writes the payload ID: not when the program's packets hold one.
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
// Writes the program's packets of kind Kind for one section into Words from Pos on, in
// the order the program gave them. Returns where the next word goes.
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
// vertical blanking; for a standard that is not 2160p.
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Makes raw line LineIndex in Band->Line. Its horizontal blanking is made stream by
// stream in Band->Words and then woven into the line; so is its active part on a
// line of the vertical blanking, with the program's packets. The active part of a line
// of the image is the job's image, read straight into the line in SD, HD and 3G, where
// the image's order of samples is the line's, and into Band->Image in 2160p; without
// an image, it is black.
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

        // Blanking first: C and Y alternate in SD's one stream, from Cb.
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

        // The timing references, and in HD and up the line number after EAV.
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

        // The horizontal blanking: the payload ID first, unless the program sends its
        // own, then in SD the program's audio control packets, then the audio, then the
        // rest of the program's packets.
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

    // The CRC words of HD and up: each stream's covers the active part of the line
    // before, then this line's EAV and line number. Ten bits at a time: the CRC is
    // linear, so the register's lower ten bits and the word select one entry of the
    // table, and the bits above are shifted in on it. Left to the transmitter, they are
    // 200 (hex), a CRC of 0 with its bit 9.
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

    // Weave what was made per stream into the line.
    for (int s = 0; s < Streams; s++)
    {
        const uint16_t* W = Band->Words[s];
        uint16_t* To = Raw + Geo->StreamFirst[s];
        for (int k = 0; k < Made; k++)
            To[(size_t)k * (size_t)Streams] = W[k];
    }

    // The active part of a line of the image.
    if (!Vanc)
    {
        uint16_t* Active = Raw + (size_t)Hanc * (size_t)Streams;
        if (Image == NULL)
            PutBlack(Geo, Raw);
        else if (Geo->Is4k)
        {
            const int k = LineIndex - Geo->PictureFirstIndex;
            DtSdiImage_GetLine(Image, Geo, 2 * k, Band->Image[0], Builder->Conv);
            DtSdiImage_GetLine(Image, Geo, 2 * k + 1, Band->Image[1], Builder->Conv);
            Builder->Conv->Join4k(Band->Image[0], Band->Image[1], (size_t)Geo->LinkWidth,
                                  Active);
        }
        else
            DtSdiImage_GetLine(Image, Geo, ImageLineOf(Geo, LineIndex), Active,
                               Builder->Conv);
    }

    // The CRC over this line's active part, for the next line's: of every stream at
    // once, by its place in the line.
    if (Streams > 1 && Builder->Checksums)
    {
        uint32_t Crcs[DT_SDICRC_MAX_STREAMS];
        Builder->Crc(Raw + (size_t)Hanc * (size_t)Streams, (size_t)(Total - Hanc),
                     Streams, Table, Crcs);
        for (int s = 0; s < Streams; s++)
            Band->LastCrc[s] = Crcs[Geo->StreamFirst[s]];
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BuildBand -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A piece of a frame's job: its share of the frame's lines, made and written with its
// own band. Where the band starts it takes the audio's cursor from the plan, and with
// the checksums on the CRC of the line before, which it makes for that, unless the band
// starts the frame. The band that ends the frame writes its padding and keeps the CRC
// for the next frame.
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
// Sets the runner up for the pieces the program asked for or, with 0, those the
// standard calls for, and gives each piece a band. When those cannot be had, the
// builder works in one piece, in the calling thread. Returns false when not even one
// band can be had.
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
// Every argument is checked before the frame is touched, the audio planned first so
// that the room it leaves for the program's packets is known. The CRC of the first line
// covers the last line of the frame built before, of the same standard; of the first
// frame, or after a change of standard, nothing but its own EAV and line number.
//
DtapiResult DtSdiBuilder_Build(DtSdiBuilder* Builder, DtSdiView* Frame,
                               const DtSdiImage* Image, DtSdiAudio* Audio,
                               const DtSdiAncData* Anc)
{
    if (Builder == NULL || Frame == NULL)
        return DTAPI_E_INVALID_ARG;
    if (!Frame->HasFrame || Frame->Holder != NULL)
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
    // A band starts on a line that starts on a byte, so that no two bands write the
    // same byte: in 10 bits a line of 720p23.98 and 720p24 ends half-way through one.
    const size_t LineBits = (size_t)(Geo->StreamHancWords + Geo->StreamActiveWords) *
                            (size_t)Geo->NumStreams * (size_t)Frame->BitsPerSymbol;
    int Unit = 1;
    while ((LineBits * (size_t)Unit) % 8 != 0)
        Unit++;
    const uint32_t Vpid = DtSmpte352_Make(Geo->VidStd);
    BuildJob Job = {Builder, Frame, Image, Anc, Vpid, PayloadId, Unit};
    DtJobRunner_Run(&Builder->Runner, BuildBand, &Job);
    DtSdiEmbed_End(&Builder->Embed);
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
// The builder's own audio state, set up for VidStd in a copy, says where its cadence
// stands.
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
// Turning the CRCs on starts afresh: the first line's CRC covers nothing of the frame
// before, whose CRCs were not worked out.
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
// The runner takes the pool at once, so that the builder holds its reference from here
// on; the pieces follow the standard of the next frame.
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
