// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiLevelB.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - 3G level B: between two pictures and the frame on the line
//
// SPDX-License-Identifier: BSD-3-Clause
//
// A line of a picture and a line of a link have the same words: EAV, the line number and
// the CRC, the horizontal blanking, SAV and the active part, in a C and a Y stream, C
// first. Moving a line from one to the other changes the fourth word of EAV and SAV, the
// line number and the CRC words, and nothing else. The active part stays the same, so
// the CRC of a link's line before is that of the active part of the picture's line it
// carries; that is how a band of lines starts its CRCs.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"     // Allocation seam.
#include "DtSdiLevelB.h"      // Interface being implemented.
#include "DtSdiSymbols.h"     // Reading symbols.
#include "Video/DtSdiFrame.h" // Timing references, line numbers and CRCs.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Constants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The blanking values of a C word and of a Y word, and the CRC word left for the
// transmitter.
#define DT_SDILEVELB_BLANK_C 0x200
#define DT_SDILEVELB_BLANK_Y 0x040
#define DT_SDILEVELB_CRC_FOR_TRANSMITTER 0x200

// The last interface line of field 1.
#define DT_SDILEVELB_FIELD1_END DT_SDIGEOMETRY_LEVELB_FIELD1_END

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The work of one call, which the pieces share.
typedef struct LevelBJob
{
    DtSdiLevelB* Converter;
    int Field;               // 1 or 2
    const uint8_t* Source;   // The picture put in, or the frame taken from
    int SourceBits;          // Its bits per symbol
    uint8_t* Target;         // The frame put into, or the picture taken out
    int TargetBits;          // Its bits per symbol
    int First;               // The field's first interface line
    int Last;                // The field's last interface line
    uint32_t FirstCrc[2][2]; // Put: the CRCs of the line before First
    uint32_t LastCrc[2][2];  // Put: the CRCs of line Last, from the last band
} LevelBJob;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinkWords -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the words of one line of a picture or a link: both streams.
//
static int LinkWords(const DtSdiLevelB* Converter)
{
    const DtSdiGeometry* Geo = &Converter->Geo;
    return 2 * (Geo->StreamHancWords + Geo->StreamActiveWords);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BandOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns the buffers of piece PieceIndex.
//
static DtSdiLevelBBand* BandOf(DtSdiLevelB* Converter, int PieceIndex)
{
    return PieceIndex == 0 ? &Converter->Own : &Converter->Extra[PieceIndex - 1];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads Count symbols of line LineIndex (from 0) of a raw frame whose lines are Count
// symbols of Bits bits each, into Words.
//
static void ReadLine(const DtSdiLevelB* Converter, const uint8_t* Frame, int Bits,
                     int LineIndex, size_t Count, uint16_t* Words)
{
    const size_t FirstBit = (size_t)LineIndex * Count * (size_t)Bits;
    DtSdiSymbolPtr Ptr;
    Ptr.Byte = Frame + FirstBit / 8;
    Ptr.Bit = (int)(FirstBit % 8);
    Ptr.BitsPerSymbol = Bits;
    DtSdiSymbols_Read(&Ptr, Count, Words, Converter->Conv);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the Count symbols of Words as line LineIndex (from 0) of a raw frame whose lines
// are Count symbols of Bits bits each. Every line of level B starts on a byte boundary,
// and Count is a multiple of four.
//
static void WriteLine(const DtSdiLevelB* Converter, uint8_t* Frame, int Bits,
                      int LineIndex, size_t Count, const uint16_t* Words)
{
    uint8_t* Out = Frame + (size_t)LineIndex * Count * (size_t)Bits / 8;
    if (Bits == 10)
    {
        Converter->Conv->Pack10(Words, Count, Out);
        return;
    }
    for (size_t k = 0; k < Count; k++)
    {
        Out[2 * k] = (uint8_t)(Words[k] & 0xFF);
        Out[2 * k + 1] = (uint8_t)(Words[k] >> 8);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeBlank -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Fills the line in Words with blanking and the timing references' first three words;
// SetTiming adds the rest.
//
static void MakeBlank(const DtSdiLevelB* Converter, uint16_t* Words)
{
    const int Hanc = Converter->Geo.StreamHancWords;
    const int Count = LinkWords(Converter);
    for (int k = 0; k < Count; k++)
        Words[k] = (k & 1) == 0 ? DT_SDILEVELB_BLANK_C : DT_SDILEVELB_BLANK_Y;
    for (int s = 0; s < 2; s++)
    {
        Words[s] = Words[2 * (Hanc - 4) + s] = 0x3FF;
        Words[2 + s] = Words[4 + s] = 0x000;
        Words[2 * (Hanc - 3) + s] = Words[2 * (Hanc - 2) + s] = 0x000;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetTiming -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Gives the line in Words the fourth words of EAV and SAV and the line number of line
// Line (from 1) of a frame of Props, in both streams.
//
static void SetTiming(const DtSdiLevelB* Converter, uint16_t* Words,
                      const DtFrameProps* Props, int Line)
{
    const int Hanc = Converter->Geo.StreamHancWords;
    const uint16_t Eav = (uint16_t)DtSdiFrame_Xyz(Props, Line, true);
    const uint16_t Sav = (uint16_t)DtSdiFrame_Xyz(Props, Line, false);
    for (int s = 0; s < 2; s++)
    {
        Words[6 + s] = Eav;
        Words[8 + s] = (uint16_t)DtSdiFrame_WithParity((uint32_t)Line << 2);
        Words[10 + s] =
            (uint16_t)DtSdiFrame_WithParity((uint32_t)(Line >> 7) << 2 & 0x3C);
        Words[2 * (Hanc - 1) + s] = Sav;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ActiveCrc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sets Crc[0] and Crc[1] to the CRCs of the active part of the line in Words, of its C
// and its Y stream.
//
static void ActiveCrc(const DtSdiLevelB* Converter, const uint16_t* Words, uint32_t* Crc)
{
    const DtSdiGeometry* Geo = &Converter->Geo;
    uint32_t Crcs[DT_SDICRC_MAX_STREAMS];
    Converter->Crc(Words + 2 * Geo->StreamHancWords, (size_t)Geo->StreamActiveWords, 2,
                   Converter->CrcTable, Crcs);
    Crc[0] = Crcs[0];
    Crc[1] = Crcs[1];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetCrcs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Gives the line in Words its CRC words: each stream's CRC covers the active part of the
// link's line before, whose CRCs Crc holds, then this line's EAV and line number. Then
// sets Crc to the CRCs of this line's active part, for the next line.
//
static void SetCrcs(const DtSdiLevelB* Converter, uint16_t* Words, uint32_t* Crc)
{
    const uint32_t* Table = Converter->CrcTable;
    for (int s = 0; s < 2; s++)
    {
        uint32_t Value = Crc[s];
        for (int k = 0; k < 6; k++)
            Value = (Value >> 10) ^ Table[(Value ^ Words[2 * k + s]) & 0x3FF];
        Words[12 + s] = (uint16_t)DtSdiFrame_WithParity(Value);
        Words[14 + s] = (uint16_t)DtSdiFrame_WithParity(Value >> 9);
    }
    ActiveCrc(Converter, Words, Crc);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PictureLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the line of the picture, from 1, that link Link (0 for A, 1 for B) of interface
// line Line carries; past the picture's last line, the next picture's line 1. See SMPTE
// ST 372, Figure 2.
//
static int PictureLine(int Line, int Link)
{
    if (Line <= DT_SDILEVELB_FIELD1_END)
        return 2 * Line + Link;
    return 2 * (Line - DT_SDILEVELB_FIELD1_END - 1) + 1 + Link;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinkLineOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Puts into Words the line that link Link carries on interface line Line, as it comes
// from the picture: the picture's line, or blanking past its end.
//
static void LinkLineOf(const DtSdiLevelB* Converter, const LevelBJob* Job, int Line,
                       int Link, uint16_t* Words)
{
    const int From = PictureLine(Line, Link);
    if (From > Converter->Geo.Layout.NumLines)
        MakeBlank(Converter, Words);
    else
        ReadLine(Converter, Job->Source, Job->SourceBits, From - 1,
                 (size_t)LinkWords(Converter), Words);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutBand -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Puts the piece's band of the field's interface lines into the frame. The first band
// takes the CRCs of the line before the field from the job; another band works them out
// from the picture lines that the line before it carries. The last band leaves the CRCs
// of the field's last line in the job.
//
static void PutBand(void* Context, int PieceIndex, int NumPieces)
{
    LevelBJob* Job = (LevelBJob*)Context;
    DtSdiLevelB* Converter = Job->Converter;
    DtSdiLevelBBand* Band = BandOf(Converter, PieceIndex);
    const int Words = LinkWords(Converter);
    const int Total = Job->Last - Job->First + 1;
    int Begin = 0;
    int End = 0;
    DtJobRunner_Split(Total, PieceIndex, NumPieces, 1, &Begin, &End);
    if (Begin >= End)
        return;

    uint32_t Crc[2][2];
    if (Begin == 0)
        memcpy(Crc, Job->FirstCrc, sizeof(Crc));
    else
    {
        for (int Link = 0; Link < 2; Link++)
        {
            LinkLineOf(Converter, Job, Job->First + Begin - 1, Link, Band->Link[Link]);
            ActiveCrc(Converter, Band->Link[Link], Crc[Link]);
        }
    }

    for (int Line = Job->First + Begin; Line < Job->First + End; Line++)
    {
        for (int Link = 0; Link < 2; Link++)
        {
            uint16_t* L = Band->Link[Link];
            LinkLineOf(Converter, Job, Line, Link, L);
            SetTiming(Converter, L, &Converter->Interface, Line);
            SetCrcs(Converter, L, Crc[Link]);
        }
        Converter->Conv->JoinLevelB(Band->Link[1], Band->Link[0], (size_t)Words,
                                    Band->Line);
        WriteLine(Converter, Job->Target, Job->TargetBits, Line - 1, 2 * (size_t)Words,
                  Band->Line);
    }
    if (End == Total)
        memcpy(Job->LastCrc, Crc, sizeof(Crc));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TakeBand -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Takes the piece's band of the field's interface lines out of the frame into the
// picture. The first band of field 1 also writes the picture's line 1, as blanking.
//
static void TakeBand(void* Context, int PieceIndex, int NumPieces)
{
    LevelBJob* Job = (LevelBJob*)Context;
    DtSdiLevelB* Converter = Job->Converter;
    DtSdiLevelBBand* Band = BandOf(Converter, PieceIndex);
    const DtFrameProps* Props = &Converter->Geo.Props;
    const int Words = LinkWords(Converter);
    const int NumLines = Converter->Geo.Layout.NumLines;
    int Begin = 0;
    int End = 0;
    DtJobRunner_Split(Job->Last - Job->First + 1, PieceIndex, NumPieces, 1, &Begin, &End);
    if (Begin >= End)
        return;

    for (int Line = Job->First + Begin; Line < Job->First + End; Line++)
    {
        ReadLine(Converter, Job->Source, Job->SourceBits, Line - 1, 2 * (size_t)Words,
                 Band->Line);
        Converter->Conv->SplitLevelB(Band->Line, (size_t)Words, Band->Link[1],
                                     Band->Link[0]);
        for (int Link = 0; Link < 2; Link++)
        {
            const int To = PictureLine(Line, Link);
            if (To > NumLines)
                continue;
            uint16_t* L = Band->Link[Link];
            SetTiming(Converter, L, Props, To);
            for (int s = 0; s < 4; s++)
                L[12 + s] = DT_SDILEVELB_CRC_FOR_TRANSMITTER;
            WriteLine(Converter, Job->Target, Job->TargetBits, To - 1, (size_t)Words, L);
        }
    }
    if (Job->Field == 1 && Begin == 0)
    {
        uint16_t* L = Band->Link[0];
        MakeBlank(Converter, L);
        SetTiming(Converter, L, Props, 1);
        for (int s = 0; s < 4; s++)
            L[12 + s] = DT_SDILEVELB_CRC_FOR_TRANSMITTER;
        WriteLine(Converter, Job->Target, Job->TargetBits, 0, (size_t)Words, L);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RunBands -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Runs Func over the pieces of Runner, with a band for each, or as one piece on the
// calling thread when there is no runner or no memory for the bands.
//
static void RunBands(DtSdiLevelB* Converter, const DtJobRunner* Runner, DtJobFunc Func,
                     LevelBJob* Job)
{
    const int Pieces = Runner != NULL ? DtJobRunner_NumPieces(Runner) : 1;
    if (Pieces > 1 && Converter->NumExtra < Pieces - 1)
    {
        DtAlloc_Free(Converter->Extra);
        Converter->Extra = (DtSdiLevelBBand*)DtAlloc_Malloc((size_t)(Pieces - 1) *
                                                            sizeof(DtSdiLevelBBand));
        Converter->NumExtra = Converter->Extra != NULL ? Pieces - 1 : 0;
    }
    if (Pieces > 1 && Converter->NumExtra >= Pieces - 1)
        DtJobRunner_Run(Runner, Func, Job);
    else
        Func(Job, 0, 1);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- InitJob -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sets up the job of putting or taking field Field.
//
static void InitJob(LevelBJob* Job, DtSdiLevelB* Converter, int Field,
                    const uint8_t* Source, int SourceBits, uint8_t* Target,
                    int TargetBits)
{
    memset(Job, 0, sizeof(*Job));
    Job->Converter = Converter;
    Job->Field = Field;
    Job->Source = Source;
    Job->SourceBits = SourceBits;
    Job->Target = Target;
    Job->TargetBits = TargetBits;
    Job->First = Field == 1 ? 1 : DT_SDILEVELB_FIELD1_END + 1;
    Job->Last = Field == 1 ? DT_SDILEVELB_FIELD1_END : Converter->Geo.Layout.NumLines;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Level B +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiLevelB_BlackPicture -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiLevelB_BlackPicture(DtSdiLevelB* Converter, uint8_t* Picture, int BitsPerSymbol)
{
    const int Words = LinkWords(Converter);
    const int NumLines = Converter->Geo.Layout.NumLines;
    uint16_t* L = Converter->Own.Link[0];
    for (int Line = 1; Line <= NumLines; Line++)
    {
        MakeBlank(Converter, L);
        SetTiming(Converter, L, &Converter->Geo.Props, Line);
        for (int s = 0; s < 4; s++)
            L[12 + s] = DT_SDILEVELB_CRC_FOR_TRANSMITTER;
        WriteLine(Converter, Picture, BitsPerSymbol, Line - 1, (size_t)Words, L);
    }
    const size_t End = (size_t)NumLines * (size_t)Words * (size_t)BitsPerSymbol / 8;
    memset(Picture + End, 0,
           DtSdiFrame_RawSize(&Converter->Geo.Layout, BitsPerSymbol) - End);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiLevelB_FrameSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t DtSdiLevelB_FrameSize(const DtSdiGeometry* Geo, int BitsPerSymbol)
{
    const size_t Symbols = (size_t)Geo->Layout.NumLines * 2 * 2 *
                           (size_t)(Geo->StreamHancWords + Geo->StreamActiveWords);
    return (Symbols * (size_t)BitsPerSymbol + 63) / 64 * 8;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiLevelB_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiLevelB_Free(DtSdiLevelB* Converter)
{
    DtAlloc_Free(Converter->Extra);
    Converter->Extra = NULL;
    Converter->NumExtra = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiLevelB_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSdiLevelB_Init(DtSdiLevelB* Converter, int VidStd)
{
    memset(Converter, 0, sizeof(*Converter));
    DtapiResult Result = DtSdiGeometry_Init(&Converter->Geo, VidStd);
    if (Result == DTAPI_OK && !Converter->Geo.IsLevelB)
        Result = DTAPI_E_INVALID_VIDSTD;
    if (Result == DTAPI_OK &&
        !DtFrameProps_Init(&Converter->Interface, Converter->Geo.InterfaceVidStd))
        Result = DTAPI_E_INVALID_VIDSTD;
    if (Result != DTAPI_OK)
        return Result;
    Converter->Conv = DtSdiConv_Best();
    Converter->Crc = DtSdiCrc_Best();
    for (uint32_t i = 0; i < 1024; i++)
        Converter->CrcTable[i] = DtSdiFrame_Crc18(i, 0);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiLevelB_PutField -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The CRCs of the line before the field come from the line put into a frame last when
// it was that line; else, for field 2, from line 562 of Frame, and for field 1 they
// start from 0.
//
void DtSdiLevelB_PutField(DtSdiLevelB* Converter, int Field, const uint8_t* Picture,
                          int PictureBits, uint8_t* Frame, int FrameBits,
                          const DtJobRunner* Runner)
{
    const int Words = LinkWords(Converter);
    const int NumLines = Converter->Geo.Layout.NumLines;
    LevelBJob Job;
    InitJob(&Job, Converter, Field, Picture, PictureBits, Frame, FrameBits);

    const bool Follows = Converter->CrcLine == Job.First - 1 ||
                         (Job.First == 1 && Converter->CrcLine == NumLines);
    if (Follows)
        memcpy(Job.FirstCrc, Converter->LastCrc, sizeof(Job.FirstCrc));
    else if (Job.First > 1)
    {
        DtSdiLevelBBand* Band = &Converter->Own;
        ReadLine(Converter, Frame, FrameBits, Job.First - 2, 2 * (size_t)Words,
                 Band->Line);
        Converter->Conv->SplitLevelB(Band->Line, (size_t)Words, Band->Link[1],
                                     Band->Link[0]);
        for (int Link = 0; Link < 2; Link++)
            ActiveCrc(Converter, Band->Link[Link], Job.FirstCrc[Link]);
    }

    RunBands(Converter, Runner, PutBand, &Job);
    memcpy(Converter->LastCrc, Job.LastCrc, sizeof(Converter->LastCrc));
    Converter->CrcLine = Job.Last;

    const size_t End = (size_t)NumLines * 2 * (size_t)Words * (size_t)FrameBits / 8;
    memset(Frame + End, 0, DtSdiLevelB_FrameSize(&Converter->Geo, FrameBits) - End);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiLevelB_TakeField -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSdiLevelB_TakeField(DtSdiLevelB* Converter, int Field, const uint8_t* Frame,
                           int FrameBits, uint8_t* Picture, int PictureBits,
                           const DtJobRunner* Runner)
{
    const int Words = LinkWords(Converter);
    const int NumLines = Converter->Geo.Layout.NumLines;
    LevelBJob Job;
    InitJob(&Job, Converter, Field, Frame, FrameBits, Picture, PictureBits);
    RunBands(Converter, Runner, TakeBand, &Job);

    const size_t End = (size_t)NumLines * (size_t)Words * (size_t)PictureBits / 8;
    memset(Picture + End, 0,
           DtSdiFrame_RawSize(&Converter->Geo.Layout, PictureBits) - End);
}
