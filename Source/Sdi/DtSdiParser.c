// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiParser.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The parser: takes SDI frames apart into an image, audio and ancillary data
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The parser reads a frame line by line: each line's symbols into a buffer of its own,
// one value a word, and from there into the image. The image's lines divide into bands
// over a worker pool, each band with buffers of its own; the blanking is read in the
// calling thread after them, line by line, as the audio and the list of packets run
// through the frame in order. This is the portable version, the reference that the
// vector versions must equal.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"      // Allocation seam.
#include "Core/DtWorkerPool.h" // The bands of the image.
#include "DtSdiAnc.h"          // Finding the ancillary packets.
#include "DtSdiAudio.h"        // Taking the audio out of its packets.
#include "DtSdiConv.h"         // The conversions.
#include "DtSdiImage.h"        // Writing the image.
#include "DtSdiSymbols.h"      // Reading the frame's symbols.
#include "DtSdiView.h"         // The frame a call reads or writes.
#include "cdtapi_sdi.h"        // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Constants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The widest image, 2160p, in pixels: a line has twice as many symbols.
#define DT_SDIPARSER_MAX_WIDTH 3840

// The symbols of the active parts of one raw 2160p line: the four links' 1920 pixels.
#define DT_SDIPARSER_MAX_RAW_ACTIVE (4 * DT_SDIPARSER_MAX_WIDTH)

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The buffers of a band of the image: two image lines, as a 2160p line holds two, and
// the active parts of a raw 2160p line.
typedef struct DtSdiParserBand
{
    uint16_t Lines[2][2 * DT_SDIPARSER_MAX_WIDTH];
    uint16_t RawActive[DT_SDIPARSER_MAX_RAW_ACTIVE];
    DtSdiLineScratch Scratch; // A 2160p line of a frame in a ring, decoded
} DtSdiParserBand;

struct DtSdiParser
{
    const DtSdiConv* Conv;      // The conversions
    bool AudioChecks;           // Check the BCH code and checksum of the audio packets
    DtSdiAncFilter* AncFilters; // The packets to list; NULL for the default
    int NumAncFilters;

    // The worker pool and the threads the program gave, the pieces the runner was set
    // up for (0 when it must be set up again), and a band's buffers for each piece.
    DtWorkerPool* Pool;
    int NumThreads;
    DtJobRunner Runner;
    int RunnerPieces;
    DtSdiParserBand* Bands;
    int NumBands;

    // The blanking's symbols: a section of a raw line, and one stream's words of it; and
    // a 2160p line of a frame in a ring, decoded.
    DtSdiLineScratch Scratch;
    uint16_t RawActive[DT_SDIPARSER_MAX_RAW_ACTIVE];
    uint16_t StreamWords[DT_SDIPARSER_MAX_WIDTH];
};

// The image a job of bands writes.
typedef struct ImageJob
{
    DtSdiParser* Parser;
    const DtSdiView* Frame;
    const DtSdiImage* Image;
} ImageJob;

// Where a section of a line lies, which the parser searches for packets.
typedef struct Section
{
    int LineIndex; // The raw line, from 0
    bool InHanc;   // The horizontal blanking; else the active part of a blanking line
} Section;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ParseImage -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes image lines First up to End of a frame up to 3G into Image, line by line, with
// the buffers of Band.
//
static void ParseImage(DtSdiParserBand* Band, const DtSdiView* Frame,
                       const DtSdiImage* Image, int First, int End, const DtSdiConv* Conv)
{
    const DtSdiGeometry* Geo = &Frame->Geo;
    const size_t NumSymbols = 2 * (size_t)Geo->Width;

    for (int y = First; y < End; y++)
    {
        const DtSdiSymbolPtr Active =
            DtSdiView_LineSymbols(Frame, DtSdiGeometry_RawLine(Geo, y),
                                  (size_t)Geo->Layout.LineNumSymsHanc, NULL);
        DtSdiSymbols_Read(&Active, NumSymbols, Band->Lines[0], Conv);
        DtSdiImage_PutLine(Image, Geo, y, Band->Lines[0], Conv);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ParseImage4k -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the image of a 2160p frame into Image, two lines from each raw line of the
// picture. In a raw line, the active parts of the four links follow their horizontal
// blanking, word by word: word n of each link's C stream and then of its Y stream, in
// groups of eight. Pixel x of a link is the C and Y word n = x of its active part. Link
// 1 and 2 carry the pixel pairs of the upper image line in turn, link 3 and 4 those of
// the lower one: Split4k takes them apart.
//
static void ParseImage4k(DtSdiParserBand* Band, const DtSdiView* Frame,
                         const DtSdiImage* Image, int First, int End,
                         const DtSdiConv* Conv)
{
    const DtSdiGeometry* Geo = &Frame->Geo;
    const size_t HancWords = (size_t)Geo->Layout.SectionNumSymsHanc / 2;
    const int LinkWidth = Geo->LinkWidth;

    for (int k = First; k < End; k++)
    {
        const DtSdiSymbolPtr Active = DtSdiView_LineSymbols(
            Frame, Geo->PictureFirstIndex + k, 8 * HancWords, &Band->Scratch);
        DtSdiSymbols_Read(&Active, 8 * (size_t)LinkWidth, Band->RawActive, Conv);
        Conv->Split4k(Band->RawActive, (size_t)LinkWidth, Band->Lines[0], Band->Lines[1]);
        DtSdiImage_PutLine(Image, Geo, 2 * k, Band->Lines[0], Conv);
        DtSdiImage_PutLine(Image, Geo, 2 * k + 1, Band->Lines[1], Conv);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ImageBand -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A piece of the image's job: its share of the image lines, or in 2160p of the raw
// lines of the picture, with the buffers of its own band.
//
static void ImageBand(void* Context, int PieceIndex, int NumPieces)
{
    const ImageJob* Job = (const ImageJob*)Context;
    const DtSdiGeometry* Geo = &Job->Frame->Geo;
    DtSdiParserBand* Band = &Job->Parser->Bands[PieceIndex];
    int First = 0;
    int End = 0;
    if (Geo->Is4k)
    {
        DtJobRunner_Split(Geo->Height / 2, PieceIndex, NumPieces, 1, &First, &End);
        ParseImage4k(Band, Job->Frame, Job->Image, First, End, Job->Parser->Conv);
    }
    else
    {
        DtJobRunner_Split(Geo->Height, PieceIndex, NumPieces, 1, &First, &End);
        ParseImage(Band, Job->Frame, Job->Image, First, End, Job->Parser->Conv);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConfigureBands -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets the runner up for the pieces the program asked for or, with 0, those the
// standard calls for, and gives each piece a band's buffers. When those cannot be had,
// the parser works in one piece, in the calling thread. Returns false when not even one
// band's buffers can be had.
//
static bool ConfigureBands(DtSdiParser* Parser, const DtSdiGeometry* Geo)
{
    const int Pieces = Parser->NumThreads > 0 ? Parser->NumThreads
                                              : DtSdiFrame_NumJobPieces(&Geo->Layout);
    if (Pieces == Parser->RunnerPieces && Parser->Bands != NULL)
        return true;

    if (DtJobRunner_SetPool(&Parser->Runner, Parser->Pool, Pieces) != DTAPI_OK)
        DtJobRunner_SetPool(&Parser->Runner, NULL, 0);
    int Wanted = DtJobRunner_NumPieces(&Parser->Runner);
    if (Wanted != Parser->NumBands || Parser->Bands == NULL)
    {
        DtAlloc_Free(Parser->Bands);
        Parser->Bands =
            (DtSdiParserBand*)DtAlloc_Malloc((size_t)Wanted * sizeof(DtSdiParserBand));
        if (Parser->Bands == NULL && Wanted > 1)
        {
            DtJobRunner_SetPool(&Parser->Runner, NULL, 0);
            Wanted = 1;
            Parser->Bands = (DtSdiParserBand*)DtAlloc_Malloc(sizeof(DtSdiParserBand));
        }
        Parser->NumBands = Parser->Bands == NULL ? 0 : Wanted;
    }
    Parser->RunnerPieces = Pieces;
    return Parser->Bands != NULL;
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

    // Audio alone is in link 1: the other links need no reading. The horizontal blanking
    // of a 2160p frame in a ring is a section of each link's own, its C and Y words in
    // turn, read where it lies rather than from the whole line decoded.
    const bool AllLinks = Anc != NULL;
    const bool PerLink = Geo->Is4k && Frame->RingBase != NULL && Where->InHanc;
    if (!PerLink)
    {
        const DtSdiSymbolPtr Symbols =
            DtSdiView_LineSymbols(Frame, Where->LineIndex, FirstSymbol, &Parser->Scratch);
        DtSdiSymbols_Read(&Symbols, (size_t)SectionWords * (size_t)Streams,
                          Parser->RawActive, Parser->Conv);
    }

    for (int s = 0; s < Streams; s++)
    {
        if (!AllLinks && Geo->StreamLink[s] != 1)
            continue;
        if (PerLink && (s & 1) == 0)
        {
            const DtSdiSymbolPtr Symbols =
                DtSdiView_LinkHanc(Frame, Where->LineIndex, Geo->StreamLink[s]);
            DtSdiSymbols_Read(&Symbols, 2 * (size_t)SectionWords, Parser->RawActive,
                              Parser->Conv);
        }
        if (PerLink)
        {
            const int Y = Geo->StreamIsChroma[s] ? 0 : 1;
            for (int k = First; k < End; k++)
                Parser->StreamWords[k - First] = Parser->RawActive[2 * k + Y];
        }
        else
        {
            for (int k = First; k < End; k++)
                Parser->StreamWords[k - First] =
                    Parser->RawActive[Geo->StreamFirst[s] + k * Streams];
        }

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
    Parser->Conv = DtSdiConv_Best();
    DtJobRunner_Init(&Parser->Runner);
    return Parser;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiParser_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiParser_Free(DtSdiParser* Parser)
{
    if (Parser == NULL)
        return;
    DtJobRunner_Free(&Parser->Runner);
    DtAlloc_Free(Parser->Bands);
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
        if (!ConfigureBands(Parser, &Frame->Geo))
            return DTAPI_E_OUT_OF_MEM;
        ImageJob Job = {Parser, Frame, Image};
        bool Scratch = true;
        for (int b = 0; b < Parser->NumBands; b++)
            Scratch = DtSdiLineScratch_Alloc(&Parser->Bands[b].Scratch, Frame) && Scratch;
        if (Scratch)
            DtJobRunner_Run(&Parser->Runner, ImageBand, &Job);
        for (int b = 0; b < Parser->NumBands; b++)
            DtSdiLineScratch_Free(&Parser->Bands[b].Scratch);
        if (!Scratch)
            return DTAPI_E_OUT_OF_MEM;
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
    {
        if (!DtSdiLineScratch_Alloc(&Parser->Scratch, Frame))
            return DTAPI_E_OUT_OF_MEM;
        ParseBlanking(Parser, Frame, Audio, Anc);
        DtSdiLineScratch_Free(&Parser->Scratch);
    }

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
// The runner takes the pool at once, so that the parser holds its reference from here
// on; the pieces follow the standard of the next frame.
//
DtapiResult DtSdiParser_SetWorkerPool(DtSdiParser* Parser, DtWorkerPool* Pool,
                                      int NumThreads)
{
    if (Parser == NULL || NumThreads < 0)
        return DTAPI_E_INVALID_ARG;
    const DtapiResult Result = DtJobRunner_SetPool(&Parser->Runner, Pool, NumThreads);
    if (Result != DTAPI_OK)
        return Result;
    Parser->Pool = Pool;
    Parser->NumThreads = NumThreads;
    Parser->RunnerPieces = 0;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiParser_UseConv -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSdiParser_UseConv(DtSdiParser* Parser, const DtSdiConv* Conv)
{
    Parser->Conv = Conv;
}
