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
// matrix's: on the line three after each field's switching line, in the Y stream only in
// HD and in every stream in 3G and 2160p. The line CRCs and the packets' checksums are
// left to the transmitter unless the program asks for them. This is the portable
// version; audio comes with plan 0032's step E.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"     // Allocation seam.
#include "DtSdiAnc.h"         // The data IDs the builder writes itself.
#include "DtSdiImage.h"       // Reading the image.
#include "DtSdiSymbols.h"     // Writing the frame's symbols.
#include "DtSdiView.h"        // The frame a call writes.
#include "Video/DtSmpte352.h" // The payload ID.
#include "cdtapi_sdi.h"       // Interface being implemented.

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

// What stands in for a packet's checksum that the transmitter fills in: a legal word,
// which some transmitters need before they replace it.
#define DT_SDIBUILDER_NO_CHECKSUM 0x0CC

// The blanking of a C and of a Y word.
#define DT_SDIBUILDER_BLANK_C 0x200
#define DT_SDIBUILDER_BLANK_Y 0x040

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

struct DtSdiBuilder
{
    int VidStd;          // The standard of the frame built last; 0 before the first
    bool Checksums;      // Work out line CRCs and packet checksums; else leave them to
                         // the transmitter
    uint32_t LastCrc[8]; // Per stream: the CRC over the active part of that frame's last
                         // line, where the first line's CRC starts
    uint32_t CrcTable[1024]; // The CRC-18 of each 10-bit word, from a CRC of 0

    // The words of each stream of the line being made, the line woven, the image lines
    // it takes, and per section of the frame the words its packets take.
    uint16_t Words[8][DT_SDIBUILDER_MAX_STREAM_WORDS];
    uint16_t Line[DT_SDIBUILDER_MAX_LINE_SYMBOLS];
    uint16_t Image[2][2 * DT_SDIBUILDER_MAX_WIDTH];
    int Used[DT_SDIBUILDER_MAX_SECTIONS];
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WithParity8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// An 8-bit value with even parity in bit 8 and its inverse in bit 9, as the IDs, count
// and payload ID bytes of a packet carry it.
//
static uint16_t WithParity8(unsigned Value)
{
    unsigned Ones = Value & 0xFF;
    Ones ^= Ones >> 4;
    Ones ^= Ones >> 2;
    Ones ^= Ones >> 1;
    const unsigned Bit8 = Ones & 1;
    return (uint16_t)((Value & 0xFF) | Bit8 << 8 | (Bit8 ^ 1) << 9);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutPacket -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes a packet into Words from Pos on: the flag, Did and Sdid, the count, Count user
// data words and the checksum, worked out when Checksum is true, else 0CC (hex), a legal
// word for the transmitter to replace. Returns the word after it.
//
static int PutPacket(uint16_t* Words, int Pos, unsigned Did, unsigned Sdid,
                     const uint16_t* Data, int Count, bool Checksum)
{
    Words[Pos++] = 0x000;
    Words[Pos++] = 0x3FF;
    Words[Pos++] = 0x3FF;
    const int First = Pos;
    Words[Pos++] = WithParity8(Did);
    Words[Pos++] = WithParity8(Sdid);
    Words[Pos++] = WithParity8((unsigned)Count);
    for (int i = 0; i < Count; i++)
        Words[Pos++] = (uint16_t)(Data[i] & 0x3FF);
    if (!Checksum)
    {
        Words[Pos++] = DT_SDIBUILDER_NO_CHECKSUM;
        return Pos;
    }
    unsigned Sum = 0;
    for (int i = First; i < Pos; i++)
        Sum += Words[i] & 0x1FF;
    Sum &= 0x1FF;
    Words[Pos++] = (uint16_t)(Sum | (((Sum >> 8) & 1) ^ 1) << 9);
    return Pos;
}

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
// after a field's switching line, in SD's one stream, HD's Y stream, and every stream
// of 3G and 2160p.
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
    if (Geo->NumStreams == 1 || Geo->Is4k || Geo->Layout.SdiRate == DT_SDIRATE_3G)
        return true;
    return !Geo->StreamIsChroma[Stream];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckPackets -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Checks the program's packets and that each section has room for them, counting in
// Builder->Used the words each section's packets take.
//
static DtapiResult CheckPackets(DtSdiBuilder* Builder, const DtSdiGeometry* Geo,
                                const DtSdiAncData* Anc)
{
    if (Anc->NumPackets < 0 || (Anc->NumPackets > 0 && Anc->Packets == NULL))
        return DTAPI_E_INVALID_ARG;

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
        const uint8_t Did = (uint8_t)P->Did;
        if (DtSdiAnc_IsAudio(Did) || (Did == DT_SDIANC_DID_PAYLOAD_ID &&
                                      P->SdidOrDbn == DT_SDIANC_SDID_PAYLOAD_ID))
        {
            return DTAPI_E_INVALID_ARG;
        }
        if (P->Line < 1 || P->Line > NumLines ||
            (!P->InHanc && !DtSdiGeometry_IsVanc(Geo, P->Line - 1)))
        {
            return DTAPI_E_INVALID_LINE;
        }

        const int Stream = StreamOf(Geo, P);
        int* Used = &Builder->Used[Section(P->Line - 1, P->InHanc, Stream)];
        int Room = P->InHanc ? HancRoom : Geo->StreamActiveWords;
        if (P->InHanc && HasPayloadId(Geo, P->Line - 1, Stream))
            Room -= DT_SDIBUILDER_PAYLOAD_ID_WORDS;
        *Used += 7 + P->NumWords;
        if (*Used > Room)
            return DTAPI_E_TOO_LONG;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutPackets -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the program's packets for one section into Words from Pos on, in the order
// the program gave them.
//
static void PutPackets(const DtSdiGeometry* Geo, const DtSdiAncData* Anc, int LineIndex,
                       bool InHanc, int Stream, bool Checksum, uint16_t* Words, int Pos)
{
    for (int p = 0; Anc != NULL && p < Anc->NumPackets; p++)
    {
        const DtSdiAncPacket* P = &Anc->Packets[p];
        if (P->Line == LineIndex + 1 && P->InHanc == InHanc && StreamOf(Geo, P) == Stream)
            Pos = PutPacket(Words, Pos, P->Did, P->SdidOrDbn, P->Words, P->NumWords,
                            Checksum);
    }
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutActive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the image's samples for raw line LineIndex into the active part of each
// stream, from Builder->Image: one image line, or two in 2160p.
//
static void PutActive(DtSdiBuilder* Builder, const DtSdiGeometry* Geo)
{
    const int First = Geo->StreamHancWords;
    if (Geo->Is4k)
    {
        // Link 1 and 2 carry the pixel pairs of the upper image line in turn, link 3
        // and 4 those of the lower one. Pixel x of a link is word x of its active part.
        for (int Link = 0; Link < 4; Link++)
        {
            const uint16_t* Image = Builder->Image[Link >> 1];
            uint16_t* C = Builder->Words[2 * Link] + First;
            uint16_t* Y = Builder->Words[2 * Link + 1] + First;
            for (int x = 0; x < Geo->LinkWidth; x++)
            {
                const int X = 2 * (2 * (x / 2) + (Link & 1)) + (x & 1);
                C[x] = Image[2 * X];
                Y[x] = Image[2 * X + 1];
            }
        }
        return;
    }

    const uint16_t* Image = Builder->Image[0];
    if (Geo->NumStreams == 1)
    {
        memcpy(Builder->Words[0] + First, Image,
               (size_t)Geo->StreamActiveWords * sizeof(*Image));
        return;
    }
    for (int x = 0; x < Geo->Width; x++)
    {
        Builder->Words[0][First + x] = Image[2 * x];
        Builder->Words[1][First + x] = Image[2 * x + 1];
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Makes the words of every stream of raw line LineIndex in Builder->Words. HasImage
// says whether Builder->Image holds the line's image; without, its active part is
// black.
//
static void MakeLine(DtSdiBuilder* Builder, const DtSdiGeometry* Geo,
                     const DtSdiAncData* Anc, uint32_t Vpid, int LineIndex, bool HasImage)
{
    const int Line = LineIndex + 1;
    const int Hanc = Geo->StreamHancWords;
    const int Total = Hanc + Geo->StreamActiveWords;
    const uint32_t Eav = DtSdiFrame_Xyz(&Geo->Props, Line, true);
    const uint32_t Sav = DtSdiFrame_Xyz(&Geo->Props, Line, false);
    const bool Vanc = DtSdiGeometry_IsVanc(Geo, LineIndex);

    for (int s = 0; s < Geo->NumStreams; s++)
    {
        uint16_t* W = Builder->Words[s];

        // Blanking everywhere first: C and Y alternate in SD's one stream, from Cb.
        if (Geo->NumStreams == 1)
        {
            for (int k = 0; k < Total; k++)
                W[k] = (k & 1) == 0 ? DT_SDIBUILDER_BLANK_C : DT_SDIBUILDER_BLANK_Y;
        }
        else
        {
            const uint16_t Blank =
                Geo->StreamIsChroma[s] ? DT_SDIBUILDER_BLANK_C : DT_SDIBUILDER_BLANK_Y;
            for (int k = 0; k < Total; k++)
                W[k] = Blank;
        }

        // The timing references, and in HD and up the line number after EAV.
        W[0] = 0x3FF;
        W[1] = 0x000;
        W[2] = 0x000;
        W[3] = (uint16_t)Eav;
        if (Geo->NumStreams > 1)
        {
            W[4] = (uint16_t)DtSdiFrame_WithParity((uint32_t)Line << 2);
            W[5] = (uint16_t)DtSdiFrame_WithParity((uint32_t)(Line >> 7) << 2 & 0x3C);
        }
        W[Hanc - 4] = 0x3FF;
        W[Hanc - 3] = 0x000;
        W[Hanc - 2] = 0x000;
        W[Hanc - 1] = (uint16_t)Sav;

        // The horizontal blanking: the payload ID first, then the program's packets.
        int Pos = Geo->StreamEavWords;
        if (HasPayloadId(Geo, LineIndex, s))
        {
            const uint16_t Bytes[4] = {
                WithParity8(Vpid & 0xFF), WithParity8(Vpid >> 8 & 0xFF),
                WithParity8(Vpid >> 16 & 0xFF), WithParity8(Vpid >> 24 & 0xFF)};
            Pos = PutPacket(W, Pos, DT_SDIANC_DID_PAYLOAD_ID, DT_SDIANC_SDID_PAYLOAD_ID,
                            Bytes, 4, Builder->Checksums);
        }
        PutPackets(Geo, Anc, LineIndex, true, s, Builder->Checksums, W, Pos);
        if (Vanc)
            PutPackets(Geo, Anc, LineIndex, false, s, Builder->Checksums, W, Hanc);
    }

    if (HasImage)
        PutActive(Builder, Geo);

    // The CRCs of HD and up: each stream's covers the active part of the line before,
    // then this line's EAV and line number. Ten bits at a time: the CRC is linear, so the
    // register's lower ten bits and the word select one entry of the table, and the bits
    // above are shifted in on it. Left to the transmitter, the CRC words are 200 (hex),
    // a CRC of 0 with its bit 9.
    if (Geo->NumStreams == 1)
        return;
    const uint32_t* Table = Builder->CrcTable;
    for (int s = 0; s < Geo->NumStreams; s++)
    {
        uint16_t* W = Builder->Words[s];
        if (!Builder->Checksums)
        {
            W[6] = (uint16_t)DtSdiFrame_WithParity(0);
            W[7] = (uint16_t)DtSdiFrame_WithParity(0);
            continue;
        }
        uint32_t Crc = Builder->LastCrc[s];
        for (int k = 0; k < 6; k++)
            Crc = (Crc >> 10) ^ Table[(Crc ^ W[k]) & 0x3FF];
        W[6] = (uint16_t)DtSdiFrame_WithParity(Crc);
        W[7] = (uint16_t)DtSdiFrame_WithParity(Crc >> 9);

        Crc = 0;
        for (int k = Hanc; k < Total; k++)
            Crc = (Crc >> 10) ^ Table[(Crc ^ W[k]) & 0x3FF];
        Builder->LastCrc[s] = Crc;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Weaves the streams' words into the raw line and writes it.
//
static void WriteLine(DtSdiBuilder* Builder, const DtSdiGeometry* Geo,
                      DtSdiSymbolWriter* Writer)
{
    const int Streams = Geo->NumStreams;
    const int Total = Geo->StreamHancWords + Geo->StreamActiveWords;
    for (int s = 0; s < Streams; s++)
    {
        const uint16_t* W = Builder->Words[s];
        for (int k = 0; k < Total; k++)
            Builder->Line[Geo->StreamFirst[s] + k * Streams] = W[k];
    }
    DtSdiSymbolWriter_Put(Writer, Builder->Line, (size_t)Total * (size_t)Streams);
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
    for (uint32_t i = 0; i < 1024; i++)
        Builder->CrcTable[i] = DtSdiFrame_Crc18(i, 0);
    return Builder;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiBuilder_Build -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Every argument is checked before the frame is touched. The CRC of the first line
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
    if (Result == DTAPI_OK && Anc != NULL)
        Result = CheckPackets(Builder, Geo, Anc);
    if (Result == DTAPI_OK && Audio != NULL)
    {
        // Audio is plan 0032's step E: until then a frame carries none.
        for (int c = 0; c < DT_SDI_AUDIO_MAX_CHANNELS; c++)
        {
            if (Audio->Formats[c / 2] != DT_SDI_AUDIO_NONE &&
                Audio->Channels[c].Samples != NULL)
            {
                Result = DTAPI_E_NOT_SUPPORTED;
            }
        }
    }
    if (Result != DTAPI_OK)
        return Result;

    if (Builder->VidStd != Geo->VidStd)
    {
        memset(Builder->LastCrc, 0, sizeof(Builder->LastCrc));
        Builder->VidStd = Geo->VidStd;
    }
    const uint32_t Vpid = DtSmpte352_Make(Geo->VidStd);

    DtSdiSymbolWriter Writer;
    DtSdiSymbolWriter_Init(&Writer, Frame->Frame, Frame->BitsPerSymbol);
    for (int LineIndex = 0; LineIndex < Geo->Layout.NumLines; LineIndex++)
    {
        bool HasImage = false;
        if (Image != NULL && Geo->Is4k)
        {
            const int k = LineIndex - Geo->PictureFirstIndex;
            if (k >= 0 && k < Geo->Height / 2)
            {
                DtSdiImage_GetLine(Image, Geo, 2 * k, Builder->Image[0]);
                DtSdiImage_GetLine(Image, Geo, 2 * k + 1, Builder->Image[1]);
                HasImage = true;
            }
        }
        else if (Image != NULL)
        {
            const int y = ImageLineOf(Geo, LineIndex);
            if (y >= 0)
            {
                DtSdiImage_GetLine(Image, Geo, y, Builder->Image[0]);
                HasImage = true;
            }
        }
        MakeLine(Builder, Geo, Anc, Vpid, LineIndex, HasImage);
        WriteLine(Builder, Geo, &Writer);
    }
    DtSdiSymbolWriter_End(&Writer, Frame->Frame + Frame->FrameSize);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiBuilder_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSdiBuilder_Free(DtSdiBuilder* Builder)
{
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
// Audio is plan 0032's step E.
//
DtapiResult DtSdiBuilder_GetNumAudioSamples(const DtSdiBuilder* Builder, int VidStd,
                                            int FrameNumber, int* NumSamples)
{
    (void)VidStd;
    (void)FrameNumber;
    if (Builder == NULL || NumSamples == NULL)
        return DTAPI_E_INVALID_ARG;
    *NumSamples = 0;
    return DTAPI_E_NOT_SUPPORTED;
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
// The worker pool is plan 0032's step F.
//
DtapiResult DtSdiBuilder_SetWorkerPool(DtSdiBuilder* Builder, DtWorkerPool* Pool,
                                       int NumThreads)
{
    (void)Pool;
    if (Builder == NULL || NumThreads < 0)
        return DTAPI_E_INVALID_ARG;
    return DTAPI_E_NOT_SUPPORTED;
}
