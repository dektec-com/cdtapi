// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiParser.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The parser: takes SDI frames apart into an image, audio and ancillary data
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The parser reads a frame line by line. It first reads a line's symbols into a buffer,
// one value per word, and then writes them into the image. The image lines are divided
// into bands that run on a worker pool, and each band has buffers of its own. After the
// image, the calling thread reads the blanking line by line. It does this in one thread
// because the audio samples and the list of packets must come out in the frame's order.
// This is the portable version. The vector versions must give the same results.

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

// The width in pixels of the widest image, 2160p. A line has twice as many symbols.
#define DT_SDIPARSER_MAX_WIDTH 3840

// The number of symbols in the active parts of one raw 2160p line. Each of the four
// links carries 1920 pixels.
#define DT_SDIPARSER_MAX_RAW_ACTIVE (4 * DT_SDIPARSER_MAX_WIDTH)

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Holds the buffers that one band of the image works with.
typedef struct DtSdiParserBand
{
    // Two image lines, because one raw 2160p line holds two
    uint16_t Lines[2][2 * DT_SDIPARSER_MAX_WIDTH];
    // The active parts of one raw 2160p line
    uint16_t RawActive[DT_SDIPARSER_MAX_RAW_ACTIVE];
    DtSdiLineScratch Scratch; // A decoded 2160p line of a frame in a ring
} DtSdiParserBand;

struct DtSdiParser
{
    const DtSdiConv* Conv;      // The conversions
    bool AudioChecks;           // Check the BCH code and checksum of the audio packets
    DtSdiAncFilter* AncFilters; // The packets to list; NULL for the default
    int NumAncFilters;          // The number of filters in AncFilters

    DtWorkerPool* Pool; // The worker pool the program gave
    int NumThreads;     // The pieces the program asked for; 0 for the standard's number
    DtJobRunner Runner; // Runs the bands of the image on the pool
    int RunnerPieces;   // The pieces Runner was set up for; 0 to set it up again
    DtSdiParserBand* Bands; // The buffers of each piece
    int NumBands;           // The number of entries in Bands

    // Buffers for reading the blanking: the symbols of one section of a line, the words
    // of one stream of it, and a decoded 2160p line of a frame in a ring.
    DtSdiLineScratch Scratch;
    uint16_t RawActive[DT_SDIPARSER_MAX_RAW_ACTIVE];
    uint16_t StreamWords[DT_SDIPARSER_MAX_WIDTH];
};

// Holds what the bands of one image job share.
typedef struct ImageJob
{
    DtSdiParser* Parser;     // The parser, which holds the bands' buffers
    const DtSdiView* Frame;  // The frame to read
    const DtSdiImage* Image; // The image to write
} ImageJob;

// Names a section of a line that the parser searches for packets.
typedef struct Section
{
    int LineIndex; // The raw line, from 0
    bool InHanc;   // The horizontal blanking; else the active part of a blanking line
} Section;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ParseImage -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes image lines First up to End into Image, for a frame of a standard up to 3G. It
// reads each line's active part into the buffers of Band and converts it from there.
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
// Writes the image of a 2160p frame into Image, for raw picture lines First up to End.
// Each raw line of the picture gives two lines of the image.
//
// In a raw line, the active parts of the four links follow their horizontal blanking.
// They are interleaved word by word in groups of eight. Group n holds word n of each
// link's C stream and then word n of each link's Y stream. Pixel x of a link is C and
// Y word x of its active part. Links 1 and 2 take turns carrying the pixel pairs of the
// upper image line. Links 3 and 4 do the same for the lower line. Split4k separates the
// two lines.
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
// Parses one piece of the image job, using the buffers of the piece's own band. The
// piece takes its share of the image lines. In 2160p it takes its share of the raw lines
// of the picture instead.
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
// Sets up the runner and gives each of its pieces a band's buffers. The number of pieces
// is what the program asked for. When the program asked for 0, it is the number the
// standard calls for. If the runner or the buffers cannot be set up for that number, the
// parser works in one piece, in the calling thread. Returns false when not even one
// band's buffers can be allocated.
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
// Adds the packet Found to the list in Anc. Stream and Where tell where the packet was
// found. When Anc has no room for the packet or its words, the packet is counted as lost.
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
// Reads a section of a line, splits it into its streams, and searches each stream for
// packets. An audio packet goes into Audio when it is in the horizontal blanking of link
// 1. A packet that the parser's filters want goes into Anc. Audio and Anc may each be
// NULL, and then that kind of packet is skipped.
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

    // Search the words between the timing references in the horizontal blanking, or the
    // whole active part.
    const int First = Where->InHanc ? Geo->StreamEavWords : 0;
    const int End = Where->InHanc ? SectionWords - Geo->StreamSavWords : SectionWords;

    // Audio is only in link 1, so without a packet list the other links are skipped.
    // For a 2160p frame in a ring, each link's horizontal blanking is a separate section
    // with C and Y words alternating. It is read directly from there instead of decoding
    // the whole line.
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
// Takes the audio out of the frame and lists its ancillary packets, line by line. For
// each line it searches the horizontal blanking first. Then, on a line of vertical
// blanking, it searches the active part. A section that neither the audio nor a filter
// needs is not read.
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
// Checks all arguments before it writes anything. Then it reads the image. After that it
// reads the blanking of every line, for the audio and the ancillary packets together.
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

    // Clean up FrameNumber, the frame's place in the audio cadence:
    // - At a rate without a cadence it is always 0, whatever a control packet says.
    // - In SD only the lowest bits of the audio frame number give the place (as many as
    //   the cadence length needs); the higher bits may hold a frame counter (SMPTE ST
    //   272, 14.4). A place beyond the cadence length means none, so 0.
    const int Length = DtSdiAudio_CadenceLength(Frame->Geo.VidStd);
    if (Audio != NULL && Length == 1)
        Audio->FrameNumber = 0;
    if (Audio != NULL && Length > 1 && Frame->Geo.NumStreams == 1)
    {
        int Mask = 1;
        while (Mask < Length)
            Mask = Mask << 1 | 1;
        Audio->FrameNumber &= Mask;
        if (Audio->FrameNumber > Length)
            Audio->FrameNumber = 0;
    }
    return Anc != NULL && Anc->NumLost > 0 ? DTAPI_E_BUF_TOO_SMALL : DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiParser_SetAncFilter -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Checks every filter before it replaces the old ones. A failure therefore leaves the
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
// Gives the pool to the runner at once, so that the parser holds a reference to it from
// this call on. Clearing RunnerPieces makes the next frame set up the pieces again, for
// that frame's standard.
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
