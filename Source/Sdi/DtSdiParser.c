// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiParser.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The parser: takes SDI frames apart into an image, audio and ancillary data
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The parser reads a frame line by line: each line's symbols into a buffer of its own,
// one value a word, and from there into the image. This is the portable version, the
// reference that the vector versions of plan 0032's step F must equal. Audio and
// ancillary data follow in step C, and the worker pool in step F.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h" // Allocation seam.
#include "DtSdiAnc.h"     // Finding the ancillary packets.
#include "DtSdiAudio.h"   // Taking the audio out of its packets.
#include "DtSdiImage.h"   // Writing the image.
#include "DtSdiSymbols.h" // Reading the frame's symbols.
#include "DtSdiView.h"    // The frame a call reads or writes.
#include "cdtapi_sdi.h"   // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Constants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The widest image, 2160p, in pixels: a line has twice as many symbols.
#define DT_SDIPARSER_MAX_WIDTH 3840

// The symbols of the active parts of one raw 2160p line: the four links' 1920 pixels.
#define DT_SDIPARSER_MAX_RAW_ACTIVE (4 * DT_SDIPARSER_MAX_WIDTH)

// Where each link's words lie in a group of eight words of a raw 2160p line: the C words
// of links 4, 2, 3 and 1, then their Y words. g_LinkPlace[L] is the place of link L + 1.
static const int g_LinkPlace[4] = {3, 1, 2, 0};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

struct DtSdiParser
{
    bool AudioChecks;           // Check the BCH code and checksum of the audio packets
    DtSdiAncFilter* AncFilters; // The packets to list; NULL for the default
    int NumAncFilters;

    // The symbols of the lines being read: two image lines, as a 2160p line holds two;
    // the active parts of a raw 2160p line, or a section of any raw line; and one
    // stream's words of such a section.
    uint16_t Lines[2][2 * DT_SDIPARSER_MAX_WIDTH];
    uint16_t RawActive[DT_SDIPARSER_MAX_RAW_ACTIVE];
    uint16_t StreamWords[DT_SDIPARSER_MAX_WIDTH];
};

// Where a section of a line lies, which the parser searches for packets.
typedef struct Section
{
    int LineIndex; // The raw line, from 0
    bool InHanc;   // The horizontal blanking; else the active part of a blanking line
} Section;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ParseImage -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the image of a frame up to 3G into Image, line by line.
//
static void ParseImage(DtSdiParser* Parser, const DtSdiView* Frame,
                       const DtSdiImage* Image)
{
    const DtSdiGeometry* Geo = &Frame->Geo;
    const size_t NumSymbols = 2 * (size_t)Geo->Width;

    for (int y = 0; y < Geo->Height; y++)
    {
        const DtSdiSymbolPtr Active = DtSdiView_RawSymbols(
            Frame, DtSdiGeometry_RawLine(Geo, y), (size_t)Geo->Layout.LineNumSymsHanc);
        DtSdiSymbols_Read(&Active, NumSymbols, Parser->Lines[0]);
        DtSdiImage_PutLine(Image, Geo, y, Parser->Lines[0]);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ParseImage4k -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the image of a 2160p frame into Image, two lines from each raw line of the
// picture. In a raw line, the active parts of the four links follow their horizontal
// blanking, word by word: word n of each link's C stream and then of its Y stream, in
// groups of eight. Pixel x of a link is the C and Y word n = x of its active part. Link
// 1 and 2 carry the pixel pairs of the upper image line in turn, link 3 and 4 those of
// the lower one.
//
static void ParseImage4k(DtSdiParser* Parser, const DtSdiView* Frame,
                         const DtSdiImage* Image)
{
    const DtSdiGeometry* Geo = &Frame->Geo;
    const size_t HancWords = (size_t)Geo->Layout.SectionNumSymsHanc / 2;
    const int LinkWidth = Geo->LinkWidth;

    for (int k = 0; k < Geo->Height / 2; k++)
    {
        const DtSdiSymbolPtr Active =
            DtSdiView_RawSymbols(Frame, Geo->PictureFirstIndex + k, 8 * HancWords);
        DtSdiSymbols_Read(&Active, 8 * (size_t)LinkWidth, Parser->RawActive);

        for (int Link = 0; Link < 4; Link++)
        {
            uint16_t* Line = Parser->Lines[Link >> 1];
            const int Place = g_LinkPlace[Link];
            for (int x = 0; x < LinkWidth; x++)
            {
                // Pixel x of the link is pixel X of the image line: its pixel pair
                // x / 2 is the image's pair 2 * (x / 2) + 0 for links 1 and 3, + 1 for
                // links 2 and 4.
                const int X = 2 * (2 * (x / 2) + (Link & 1)) + (x & 1);
                Line[2 * X] = Parser->RawActive[8 * x + Place];
                Line[2 * X + 1] = Parser->RawActive[8 * x + 4 + Place];
            }
        }
        DtSdiImage_PutLine(Image, Geo, 2 * k, Parser->Lines[0]);
        DtSdiImage_PutLine(Image, Geo, 2 * k + 1, Parser->Lines[1]);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ListPacket -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Adds Found, in stream Stream of Where, to Anc if it has room, or counts it lost.
//
static void ListPacket(const DtSdiGeometry* Geo, const Section* Where, int Stream,
                       const DtSdiAncFound* Found, DtSdiAncData* Anc)
{
    const bool WordsFit =
        Anc->Words == NULL || Anc->NumWords + Found->NumWords <= Anc->MaxWords;
    if (Anc->NumPackets >= Anc->MaxPackets || !WordsFit)
    {
        Anc->NumLost++;
        return;
    }

    DtSdiAncPacket* Packet = &Anc->Packets[Anc->NumPackets++];
    memset(Packet, 0, sizeof(*Packet));
    Packet->Line = Where->LineIndex + 1;
    Packet->InHanc = Where->InHanc;
    Packet->OnChroma = Geo->StreamIsChroma[Stream];
    Packet->VirtualInterface = Geo->StreamLink[Stream];
    Packet->Did = Found->Did;
    Packet->SdidOrDbn = Found->SdidOrDbn;
    Packet->NumWords = Found->NumWords;
    Packet->ChecksumOk = Found->ChecksumOk;
    if (Anc->Words != NULL)
    {
        uint16_t* Words = Anc->Words + Anc->NumWords;
        memcpy(Words, Found->Words, (size_t)Found->NumWords * sizeof(*Words));
        Packet->Words = Words;
        Anc->NumWords += Found->NumWords;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ScanSection -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads a section of a line, splits it into its streams, and takes the packets in each:
// audio into Audio, when the program wants it and the packet is link 1's in the
// horizontal blanking; and the packets the parser's filters want into Anc. Either may be
// NULL.
//
static void ScanSection(DtSdiParser* Parser, const DtSdiView* Frame, const Section* Where,
                        DtSdiAudio* Audio, DtSdiAncData* Anc)
{
    const DtSdiGeometry* Geo = &Frame->Geo;
    const int Streams = Geo->NumStreams;
    const int SectionWords =
        Where->InHanc ? Geo->StreamHancWords : Geo->StreamActiveWords;
    const size_t FirstSymbol =
        Where->InHanc ? 0 : (size_t)Geo->StreamHancWords * (size_t)Streams;

    // The words to search: those between the timing references, or the whole active
    // part.
    const int First = Where->InHanc ? Geo->StreamEavWords : 0;
    const int End = Where->InHanc ? SectionWords - Geo->StreamSavWords : SectionWords;

    const DtSdiSymbolPtr Symbols =
        DtSdiView_RawSymbols(Frame, Where->LineIndex, FirstSymbol);
    DtSdiSymbols_Read(&Symbols, (size_t)SectionWords * (size_t)Streams,
                      Parser->RawActive);

    for (int s = 0; s < Streams; s++)
    {
        for (int k = First; k < End; k++)
            Parser->StreamWords[k - First] =
                Parser->RawActive[Geo->StreamFirst[s] + k * Streams];

        int Pos = 0;
        DtSdiAncFound Found;
        while (DtSdiAnc_Find(Parser->StreamWords, End - First, &Pos, &Found))
        {
            if (Audio != NULL && Where->InHanc && Geo->StreamLink[s] == 1 &&
                DtSdiAnc_IsAudio(Found.Did))
            {
                if (Streams == 1)
                    DtSdiAudio_TakeSd(Audio, &Found, Parser->AudioChecks);
                else
                    DtSdiAudio_TakeHd(Audio, &Found, Parser->AudioChecks);
            }
            if (Anc != NULL &&
                DtSdiAnc_IsListed(Parser->AncFilters, Parser->NumAncFilters, Found.Did,
                                  Found.SdidOrDbn, Where->InHanc, Where->LineIndex + 1))
            {
                ListPacket(Geo, Where, s, &Found, Anc);
            }
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ParseBlanking -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Takes the audio and lists the ancillary packets of the frame, line by line: in each
// line's horizontal blanking, then in the active part of a blanking line. A section that
// neither audio nor a filter needs is not read.
//
static void ParseBlanking(DtSdiParser* Parser, const DtSdiView* Frame, DtSdiAudio* Audio,
                          DtSdiAncData* Anc)
{
    const DtSdiGeometry* Geo = &Frame->Geo;
    for (int Line = 0; Line < Geo->Layout.NumLines; Line++)
    {
        const Section Hanc = {Line, true};
        const Section Vanc = {Line, false};
        const bool HancListed =
            Anc != NULL &&
            DtSdiAnc_IsWanted(Parser->AncFilters, Parser->NumAncFilters, true, Line + 1);
        const bool VancListed =
            Anc != NULL && DtSdiGeometry_IsVanc(Geo, Line) &&
            DtSdiAnc_IsWanted(Parser->AncFilters, Parser->NumAncFilters, false, Line + 1);

        if (Audio != NULL || HancListed)
            ScanSection(Parser, Frame, &Hanc, Audio, HancListed ? Anc : NULL);
        if (VancListed)
            ScanSection(Parser, Frame, &Vanc, NULL, Anc);
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Parser +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiParser_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtSdiParser* DtSdiParser_Alloc(void)
{
    DtSdiParser* Parser = (DtSdiParser*)DtAlloc_Malloc(sizeof(DtSdiParser));
    if (Parser == NULL)
        return NULL;
    memset(Parser, 0, sizeof(*Parser));
    return Parser;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiParser_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiParser_Free(DtSdiParser* Parser)
{
    if (Parser == NULL)
        return;
    DtAlloc_Free(Parser->AncFilters);
    DtAlloc_Free(Parser);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiParser_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSdiParser_Freep(DtSdiParser** Parser)
{
    if (Parser == NULL)
        return;
    DtSdiParser_Free(*Parser);
    *Parser = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiParser_Parse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The arguments are checked before anything is written. The image is read first, then
// the blanking of every line, for the audio and the ancillary packets together.
//
DtapiResult DtSdiParser_Parse(DtSdiParser* Parser, const DtSdiView* Frame,
                              DtSdiImage* Image, DtSdiAudio* Audio, DtSdiAncData* Anc)
{
    if (Parser == NULL || Frame == NULL)
        return DTAPI_E_INVALID_ARG;
    if (!Frame->HasFrame)
        return DTAPI_E_STATE;
    DtapiResult Result = DTAPI_OK;
    if (Image != NULL)
        Result = DtSdiImage_Check(Image, &Frame->Geo);
    if (Result == DTAPI_OK && Audio != NULL)
        Result = DtSdiAudio_Check(Audio, Frame->Geo.VidStd);
    if (Result != DTAPI_OK)
        return Result;

    if (Image != NULL)
    {
        if (Frame->Geo.Is4k)
            ParseImage4k(Parser, Frame, Image);
        else
            ParseImage(Parser, Frame, Image);
    }
    if (Audio != NULL)
        DtSdiAudio_Begin(Audio);
    if (Anc != NULL)
    {
        Anc->NumPackets = 0;
        Anc->NumWords = 0;
        Anc->NumLost = 0;
    }
    if (Audio != NULL || Anc != NULL)
        ParseBlanking(Parser, Frame, Audio, Anc);

    // A rate without a cadence has no place in one, whatever the control packet says.
    if (Audio != NULL && DtSdiAudio_CadenceLength(Frame->Geo.VidStd) == 1)
        Audio->FrameNumber = 0;
    return Anc != NULL && Anc->NumLost > 0 ? DTAPI_E_BUF_TOO_SMALL : DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiParser_SetAncFilter -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Checks every filter before it replaces the old ones, so that a failure leaves the
// parser as it was.
//
DtapiResult DtSdiParser_SetAncFilter(DtSdiParser* Parser, const DtSdiAncFilter* Filters,
                                     int NumFilters)
{
    if (Parser == NULL || NumFilters < 0 || (Filters == NULL && NumFilters > 0))
        return DTAPI_E_INVALID_ARG;
    for (int i = 0; i < NumFilters; i++)
    {
        const DtSdiAncFilter* Filter = &Filters[i];
        if (Filter->Space != DT_SDI_ANC_SPACE_HANC &&
            Filter->Space != DT_SDI_ANC_SPACE_VANC &&
            Filter->Space != DT_SDI_ANC_SPACE_BOTH)
        {
            return DTAPI_E_INVALID_ARG;
        }
        if (Filter->FirstLine < 0 || Filter->LastLine < 0 ||
            (Filter->LastLine != 0 && Filter->LastLine < Filter->FirstLine))
        {
            return DTAPI_E_INVALID_ARG;
        }
    }

    DtSdiAncFilter* Copy = NULL;
    if (NumFilters > 0)
    {
        Copy = (DtSdiAncFilter*)DtAlloc_Malloc((size_t)NumFilters * sizeof(*Copy));
        if (Copy == NULL)
            return DTAPI_E_OUT_OF_MEM;
        memcpy(Copy, Filters, (size_t)NumFilters * sizeof(*Copy));
    }
    DtAlloc_Free(Parser->AncFilters);
    Parser->AncFilters = Copy;
    Parser->NumAncFilters = NumFilters;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiParser_SetAudioChecks -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSdiParser_SetAudioChecks(DtSdiParser* Parser, bool Check)
{
    if (Parser == NULL)
        return DTAPI_E_INVALID_ARG;
    Parser->AudioChecks = Check;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiParser_SetWorkerPool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtSdiParser_SetWorkerPool(DtSdiParser* Parser, DtWorkerPool* Pool,
                                      int NumThreads)
{
    (void)Pool;
    if (Parser == NULL || NumThreads < 0)
        return DTAPI_E_INVALID_ARG;
    return DTAPI_E_NOT_SUPPORTED;
}
