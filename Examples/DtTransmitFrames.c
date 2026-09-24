// #*#*#*#*#*#*#*#*#*#*#*#*# DtTransmitFrames.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Example: transmits raw SDI frames on an output channel
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Attaches an output channel to the port and transmits --count frames in the transmit
// mode's symbol size. The frames come from the files <in>0.raw, <in>1.raw and so on, as
// DtReceiveFrames --out writes them, in turn and from the first again when --count asks
// for more; or, without --in, they are a generated test pattern of the video standard
// --vidstd: grey bars and a white bar that moves one step each frame, with legal values,
// line numbers and line CRCs. --vidstd also sets the port's I/O standard through the
// channel, with --linkstd for a 4K standard, which says how it is carried. The program
// prints one line per frame as DtReceiveFrames does, so that the hashes of what one port
// sends and another receives can be compared:
//
//     9217800001:5  frame 0  7425000 bytes  hash 3C0F2E6D89A1B437
//
// With --flags it then prints the channel's latched flags. It detaches when every frame
// is written, waiting until the card has sent them. The port must be an output;
// DtConfigPort makes it one. Exits with 0 when every frame is written, 2 when there is no
// SDI output, and 1 when a call fails or the command line is wrong.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// MSVC deprecates fopen in favour of fopen_s, which other C libraries do not have. Asked
// for before any header.
#ifdef _MSC_VER
    #define _CRT_SECURE_NO_WARNINGS
#endif

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Example includes
#include "Common/ExampleCommon.h" // The API and what the examples share.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The most frames --in reads.
#define MAX_FILES 1000

static const ExampleOption g_Options[] = {
    {"--serial", true, "The device's serial number; the first device with an SDI output"},
    {"--port", true, "The port number; the first SDI output"},
    {"--vidstd", true,
     "Set the I/O standard and the pattern for this video standard, such as 1080I50"},
    {"--linkstd", true,
     "How 4K is carried: 0 or 1 four 3G links, 2 6G, 3 12G; default -1"},
    {"--txmode", true, "8B, 10B or 16B symbols; 10B without it"},
    {"--in", true, "Transmit the frames in <in>0.raw, <in>1.raw and so on"},
    {"--count", true, "The number of frames to transmit; without it one, or every file"},
    {"--flags", false, "Print the latched flags after the last frame"},
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsSdiOutput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool IsSdiOutput(const DtHwFuncDesc* Port)
{
    return Port->IsSdi && Port->IsOutput;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Fnv1a64 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The hash DtReceiveFrames prints.
//
static uint64_t Fnv1a64(const char* Data, int Size)
{
    uint64_t Hash = 0xCBF29CE484222325ull;

    for (int i = 0; i < Size; i++)
    {
        Hash ^= (uint8_t)Data[i];
        Hash *= 0x100000001B3ull;
    }
    return Hash;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TxModeFrom -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The transmit mode and symbol size for a name on the command line. False for another
// name.
//
static bool TxModeFrom(const char* Name, int* TxMode, int* BitsPerSymbol)
{
    *TxMode = DTAPI_TXMODE_SDI_FULL;
    if (Name == NULL || strcmp(Name, "10B") == 0)
    {
        *TxMode |= DTAPI_TXMODE_SDI_10B;
        *BitsPerSymbol = 10;
    }
    else if (strcmp(Name, "8B") == 0)
        *BitsPerSymbol = 8;
    else if (strcmp(Name, "16B") == 0)
    {
        *TxMode |= DTAPI_TXMODE_SDI_16B;
        *BitsPerSymbol = 16;
    }
    else
        return false;
    return true;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame files +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct Frames
{
    int Count;
    char* Data[MAX_FILES];
    int Size[MAX_FILES];
} Frames;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadFile -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads a whole file into a new buffer. Returns NULL when it cannot be opened or read, or
// when it is empty.
//
static char* ReadFile(const char* Path, int* Size)
{
    FILE* File = fopen(Path, "rb");
    if (File == NULL)
        return NULL;

    char* Data = NULL;
    long Length = -1;
    if (fseek(File, 0, SEEK_END) == 0)
        Length = ftell(File);
    if (Length > 0 && Length <= 0x7FFFFFFF && fseek(File, 0, SEEK_SET) == 0)
        Data = (char*)malloc((size_t)Length);
    if (Data != NULL && fread(Data, 1, (size_t)Length, File) != (size_t)Length)
    {
        free(Data);
        Data = NULL;
    }
    fclose(File);
    *Size = (int)Length;
    return Data;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadFrames -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads <Prefix>0.raw, <Prefix>1.raw and so on, up to the first that does not exist.
// False, having printed why, when there is none.
//
static bool ReadFrames(const char* Prefix, Frames* Out)
{
    Out->Count = 0;
    while (Out->Count < MAX_FILES)
    {
        char Path[1024];

        snprintf(Path, sizeof(Path), "%s%d.raw", Prefix, Out->Count);
        int Size = 0;
        char* Data = ReadFile(Path, &Size);
        if (Data == NULL)
            break;
        Out->Data[Out->Count] = Data;
        Out->Size[Out->Count] = Size;
        Out->Count++;
    }
    if (Out->Count == 0)
        printf("Cannot read %s0.raw\n", Prefix);
    return Out->Count > 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FreeFrames -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void FreeFrames(Frames* Set)
{
    for (int i = 0; i < Set->Count; i++)
        free(Set->Data[i]);
    Set->Count = 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test pattern +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A raw frame holds every line, EAV first, in the order the lines are numbered: in HD
// each word once for the colour difference and once for the luma, colour difference
// first, and after the EAV the line number and the line's CRC; in SD the samples as
// SMPTE 259 orders them. Symbols are packed as the transmit mode says, 10-bit ones least
// significant bit first, and the frame is padded with zeros to a multiple of 8 bytes.
//

// The layout of a video standard's lines.
typedef struct Geometry
{
    int VidStd;
    int HancSymbols; // Between EAV and SAV
    bool TwoFields;  // Interlaced and PsF
} Geometry;

static const Geometry g_Geometries[] = {
    {DTAPI_VIDSTD_525I59_94, 268, true},    {DTAPI_VIDSTD_625I50, 280, true},
    {DTAPI_VIDSTD_720P23_98, 5666, false},  {DTAPI_VIDSTD_720P24, 5666, false},
    {DTAPI_VIDSTD_720P25, 5336, false},     {DTAPI_VIDSTD_720P29_97, 4016, false},
    {DTAPI_VIDSTD_720P30, 4016, false},     {DTAPI_VIDSTD_720P50, 1376, false},
    {DTAPI_VIDSTD_720P59_94, 716, false},   {DTAPI_VIDSTD_720P60, 716, false},
    {DTAPI_VIDSTD_1080P23_98, 1636, false}, {DTAPI_VIDSTD_1080P24, 1636, false},
    {DTAPI_VIDSTD_1080P25, 1416, false},    {DTAPI_VIDSTD_1080P29_97, 536, false},
    {DTAPI_VIDSTD_1080P30, 536, false},     {DTAPI_VIDSTD_1080PSF23_98, 1636, true},
    {DTAPI_VIDSTD_1080PSF24, 1636, true},   {DTAPI_VIDSTD_1080PSF25, 1416, true},
    {DTAPI_VIDSTD_1080PSF29_97, 536, true}, {DTAPI_VIDSTD_1080PSF30, 536, true},
    {DTAPI_VIDSTD_1080I50, 1416, true},     {DTAPI_VIDSTD_1080I59_94, 536, true},
    {DTAPI_VIDSTD_1080I60, 536, true},      {DTAPI_VIDSTD_1080P50, 1416, false},
    {DTAPI_VIDSTD_1080P50B, 1416, false},   {DTAPI_VIDSTD_1080P59_94, 536, false},
    {DTAPI_VIDSTD_1080P59_94B, 536, false}, {DTAPI_VIDSTD_1080P60, 536, false},
    {DTAPI_VIDSTD_1080P60B, 536, false},
};

// Blanking and the grey bars' and white bar's levels.
#define BLANK_C 0x200
#define BLANK_Y 0x040
#define WHITE_Y 0x3AC

typedef struct Pattern
{
    int NumLines;
    int ActiveSymbols;
    int EavSymbols; // With the line number and CRC in HD
    int SavSymbols;
    int LineSymbols;
    int Fields[2][4]; // Start line, end line, first and last active line of each field
    bool TwoFields;
    int BitsPerSymbol;
    int FrameSize;
    uint16_t* Line;  // The line being made
    uint16_t* Video; // The active part of the current frame's video lines
    uint32_t ActiveCrc[2]
                      [2]; // CRC over a blanking and a video line's active part, C and Y
} Pattern;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PatternInit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The layout of the pattern for VidStd, from the geometry of the video standard. False
// for a standard the pattern does not know or when memory runs out.
//
static bool PatternInit(Pattern* Pat, int VidStd, int BitsPerSymbol)
{
    const Geometry* Geo = NULL;

    memset(Pat, 0, sizeof(*Pat));
    for (size_t i = 0; i < sizeof(g_Geometries) / sizeof(g_Geometries[0]); i++)
    {
        if (g_Geometries[i].VidStd == VidStd)
            Geo = &g_Geometries[i];
    }
    if (Geo == NULL)
        return false;

    const char* Name = Example_VidStdName(VidStd);
    static const int Fields525[2][4] = {{1, 262, 17, 260}, {263, 525, 280, 522}};
    static const int Fields625[2][4] = {{1, 312, 23, 310}, {313, 625, 336, 623}};
    static const int Fields750[2][4] = {{1, 750, 26, 745}, {0, 0, 0, 0}};
    static const int Fields1125P[2][4] = {{1, 1125, 42, 1121}, {0, 0, 0, 0}};
    static const int Fields1125I[2][4] = {{1, 563, 21, 560}, {564, 1125, 584, 1123}};
    const int(*Fields)[4];
    if (strncmp(Name, "525", 3) == 0)
    {
        Fields = Fields525;
        Pat->NumLines = 525;
        Pat->ActiveSymbols = 1440;
    }
    else if (strncmp(Name, "625", 3) == 0)
    {
        Fields = Fields625;
        Pat->NumLines = 625;
        Pat->ActiveSymbols = 1440;
    }
    else if (strncmp(Name, "720", 3) == 0)
    {
        Fields = Fields750;
        Pat->NumLines = 750;
        Pat->ActiveSymbols = 2560;
    }
    else
    {
        Fields = Geo->TwoFields ? Fields1125I : Fields1125P;
        Pat->NumLines = 1125;
        Pat->ActiveSymbols = 3840;
    }
    memcpy(Pat->Fields, Fields, sizeof(Pat->Fields));

    bool IsSd = Pat->NumLines < 750;
    Pat->EavSymbols = IsSd ? 4 : 16;
    Pat->SavSymbols = IsSd ? 4 : 8;
    Pat->LineSymbols =
        Pat->EavSymbols + Geo->HancSymbols + Pat->SavSymbols + Pat->ActiveSymbols;
    Pat->TwoFields = Geo->TwoFields;
    Pat->BitsPerSymbol = BitsPerSymbol;
    int64_t Bits = (int64_t)Pat->NumLines * Pat->LineSymbols * BitsPerSymbol;
    Pat->FrameSize = (int)((Bits + 63) / 64 * 8);
    Pat->Line = (uint16_t*)malloc((size_t)Pat->LineSymbols * sizeof(uint16_t));
    Pat->Video = (uint16_t*)malloc((size_t)Pat->ActiveSymbols * sizeof(uint16_t));
    return Pat->Line != NULL && Pat->Video != NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PatternFree -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void PatternFree(Pattern* Pat)
{
    free(Pat->Line);
    free(Pat->Video);
    Pat->Line = NULL;
    Pat->Video = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Crc18 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// SMPTE 292's line CRC, x^18 + x^5 + x^4 + 1, over one 10-bit word, least significant
// bit first.
//
static uint32_t Crc18(uint32_t Crc, uint32_t Word)
{
    for (int Bit = 0; Bit < 10; Bit++)
    {
        uint32_t Feedback = (Crc ^ (Word >> Bit)) & 1;

        Crc >>= 1;
        if (Feedback != 0)
            Crc ^= 0x23000;
    }
    return Crc;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WithParity -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Nine bits with bit 9 the inverse of bit 8, as line numbers and CRC words carry them.
//
static uint32_t WithParity(uint32_t Nine)
{
    Nine &= 0x1FF;
    return Nine | ((Nine >> 8) ^ 1) << 9;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Xyz -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The fourth word of a timing reference of Line: field, vertical blanking, EAV or SAV,
// and the protection bits.
//
static uint32_t Xyz(const Pattern* Pat, int Line, bool Eav)
{
    uint32_t F = Pat->TwoFields && Line >= Pat->Fields[1][0] ? 1 : 0;
    const int* Field = Pat->Fields[F];
    uint32_t V = Line < Field[2] || Line > Field[3] ? 1 : 0;
    uint32_t H = Eav ? 1 : 0;

    return 0x200 | F << 8 | V << 7 | H << 6 | (V ^ H) << 5 | (F ^ H) << 4 | (F ^ V) << 3 |
           (F ^ V ^ H) << 2;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsVideoLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool IsVideoLine(const Pattern* Pat, int Line)
{
    uint32_t F = Pat->TwoFields && Line >= Pat->Fields[1][0] ? 1 : 0;

    return Line >= Pat->Fields[F][2] && Line <= Pat->Fields[F][3];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Line into Pat->Line: blanking, the active part of a video line where the line has
// video, and the timing references, in HD with the line number and the CRC over the
// active part of the line before, which is a video line when PrevIsVideo.
//
static void MakeLine(Pattern* Pat, int Line, bool PrevIsVideo)
{
    uint16_t* Out = Pat->Line;
    int ActiveStart = Pat->LineSymbols - Pat->ActiveSymbols;

    for (int i = 0; i < Pat->LineSymbols; i++)
        Out[i] = (uint16_t)(i % 2 == 0 ? BLANK_C : BLANK_Y);
    if (IsVideoLine(Pat, Line))
        memcpy(Out + ActiveStart, Pat->Video,
               (size_t)Pat->ActiveSymbols * sizeof(uint16_t));

    uint32_t Words[8] = {0x3FF,
                         0x000,
                         0x000,
                         Xyz(Pat, Line, true),
                         WithParity((uint32_t)Line << 2),
                         WithParity((uint32_t)(Line >> 7) << 2 & 0x3C),
                         0,
                         0};
    uint32_t Sav[4] = {0x3FF, 0x000, 0x000, Xyz(Pat, Line, false)};
    if (Pat->EavSymbols == 4)
    {
        for (int i = 0; i < 4; i++)
        {
            Out[i] = (uint16_t)Words[i];
            Out[ActiveStart - 4 + i] = (uint16_t)Sav[i];
        }
        return;
    }
    for (int Channel = 0; Channel < 2; Channel++)
    {
        uint32_t Crc = Pat->ActiveCrc[PrevIsVideo ? 1 : 0][Channel];

        for (int j = 0; j < 6; j++)
            Crc = Crc18(Crc, Words[j]);
        Words[6] = WithParity(Crc);
        Words[7] = WithParity(Crc >> 9);
        for (int j = 0; j < 8; j++)
            Out[2 * j + Channel] = (uint16_t)Words[j];
        for (int j = 0; j < 4; j++)
            Out[ActiveStart - 8 + 2 * j + Channel] = (uint16_t)Sav[j];
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeVideo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The active part of the video lines of frame FrameNumber: eight grey bars, and a white
// bar a sixteenth of the width wide that moves right by an eighth of its width each
// frame. Also the CRCs over it and over a blanking line's active part, for each channel.
//
static void MakeVideo(Pattern* Pat, int64_t FrameNumber)
{
    int Samples = Pat->ActiveSymbols / 2;
    int BarWidth = Samples / 16;
    int64_t Step = BarWidth / 8 > 0 ? BarWidth / 8 : 1;
    int WhiteAt = (int)(FrameNumber * Step % (Samples - BarWidth));

    for (int Sample = 0; Sample < Samples; Sample++)
    {
        int Luma = BLANK_Y + (WHITE_Y - BLANK_Y) * (Sample * 8 / Samples) / 7;

        if (Sample >= WhiteAt && Sample < WhiteAt + BarWidth)
            Luma = WHITE_Y;
        Pat->Video[2 * Sample] = BLANK_C;
        Pat->Video[2 * Sample + 1] = (uint16_t)Luma;
    }

    for (int Channel = 0; Channel < 2; Channel++)
    {
        uint32_t Blank = 0;
        uint32_t Video = 0;

        for (int j = Channel; j < Pat->ActiveSymbols; j += 2)
        {
            Blank = Crc18(Blank, Channel == 0 ? BLANK_C : BLANK_Y);
            Video = Crc18(Video, Pat->Video[j]);
        }
        Pat->ActiveCrc[0][Channel] = Blank;
        Pat->ActiveCrc[1][Channel] = Video;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Frame FrameNumber of the pattern into Frame, which holds Pat->FrameSize bytes. The line
// before line 1 is the last line of a frame, which is blanking in every standard.
//
static void MakeFrame(Pattern* Pat, int64_t FrameNumber, char* Frame)
{
    uint8_t* Out = (uint8_t*)Frame;
    uint64_t Accu = 0;
    int Have = 0;

    memset(Frame, 0, (size_t)Pat->FrameSize);
    MakeVideo(Pat, FrameNumber);
    for (int Line = 1; Line <= Pat->NumLines; Line++)
    {
        MakeLine(Pat, Line, Line > 1 && IsVideoLine(Pat, Line - 1));
        for (int i = 0; i < Pat->LineSymbols; i++)
        {
            uint32_t Symbol = Pat->Line[i];

            if (Pat->BitsPerSymbol == 16)
            {
                *Out++ = (uint8_t)Symbol;
                *Out++ = (uint8_t)(Symbol >> 8);
            }
            else if (Pat->BitsPerSymbol == 8)
                *Out++ = (uint8_t)(Symbol >> 2);
            else
            {
                Accu |= (uint64_t)Symbol << Have;
                for (Have += 10; Have >= 8; Have -= 8)
                {
                    *Out++ = (uint8_t)Accu;
                    Accu >>= 8;
                }
            }
        }
    }
    if (Have > 0)
        *Out = (uint8_t)Accu;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmit +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

typedef struct Source
{
    Frames Files;    // With --in
    Pattern Pat;     // Otherwise
    char* Generated; // Pat.FrameSize bytes
} Source;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteNext -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes frame Number and prints its line. Returns the exit code.
//
static int WriteNext(DtOutpChannel* Channel, const DtHwFuncDesc* Port, Source* Src,
                     int64_t Number)
{
    char* Frame = Src->Generated;
    int Size = Src->Pat.FrameSize;

    if (Src->Files.Count > 0)
    {
        Frame = Src->Files.Data[Number % Src->Files.Count];
        Size = Src->Files.Size[Number % Src->Files.Count];
    }
    else
        MakeFrame(&Src->Pat, Number, Frame);

    unsigned int Result = DtOutpChannel_Write(Channel, Frame, Size);
    printf("%s  ", Port->DeviceName);
    if (Result != DTAPI_OK)
        return Example_Failed("DtOutpChannel_Write", Result);
    printf("frame %lld  %d bytes  hash %016llX\n", (long long)Number, Size,
           (unsigned long long)Fnv1a64(Frame, Size));
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Transmit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Holds, writes the first frame, sends, and writes the others. Returns the exit code.
//
static int Transmit(DtOutpChannel* Channel, const DtHwFuncDesc* Port, Source* Src,
                    int64_t Count, bool Flags)
{
    unsigned int Result = DtOutpChannel_SetTxControl(Channel, DTAPI_TXCTRL_HOLD);
    if (Result != DTAPI_OK)
    {
        printf("%s  ", Port->DeviceName);
        return Example_Failed("DtOutpChannel_SetTxControl", Result);
    }

    for (int64_t i = 0; i < Count; i++)
    {
        int Exit = WriteNext(Channel, Port, Src, i);
        if (Exit != EXAMPLE_OK)
            return Exit;
        if (i == 0)
        {
            Result = DtOutpChannel_SetTxControl(Channel, DTAPI_TXCTRL_SEND);
            if (Result != DTAPI_OK)
            {
                printf("%s  ", Port->DeviceName);
                return Example_Failed("DtOutpChannel_SetTxControl", Result);
            }
        }
    }

    if (Flags)
    {
        int Status = 0;
        int Latched = 0;

        Result = DtOutpChannel_GetFlags(Channel, &Status, &Latched);
        printf("%s  ", Port->DeviceName);
        if (Result != DTAPI_OK)
            return Example_Failed("DtOutpChannel_GetFlags", Result);
        printf("latched%s%s%s\n", (Latched & DTAPI_TX_FIFO_UFL) != 0 ? " FIFO_UFL" : "",
               (Latched & DTAPI_TX_DMA_UFL) != 0 ? " DMA_UFL" : "",
               (Latched & (DTAPI_TX_FIFO_UFL | DTAPI_TX_DMA_UFL)) == 0 ? " none" : "");
    }
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AttachAndTransmit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Attaches the device and the channel to Port, sets the I/O standard when asked and then
// the transmit mode, in that order, since a standard that crosses between SDI and ASI
// gives the channel that side's default transmit mode, and transmits. Returns the exit
// code.
//
static int AttachAndTransmit(DtDevice* Device, DtOutpChannel* Channel,
                             const DtHwFuncDesc* Port, int TxMode, int VidStd,
                             int LinkStd, Source* Src, int64_t Count, bool Flags)
{
    unsigned int Result = DtDevice_AttachToSerial(Device, Port->SerialNumber);
    if (!Example_Succeeded(Result))
        return Example_Failed("DtDevice_AttachToSerial", Result);
    Result = DtOutpChannel_AttachToPort(Channel, Device, Port->Port);
    if (!Example_Succeeded(Result))
    {
        printf("%s  ", Port->DeviceName);
        return Example_Failed("DtOutpChannel_AttachToPort", Result);
    }

    const char* What = "DtOutpChannel_SetTxMode";
    Result = DTAPI_OK;
    if (VidStd != DTAPI_VIDSTD_UNKNOWN)
    {
        int Value = -1;
        int SubValue = -1;

        What = "DtapiVidStd2IoStd";
        Result = DtapiVidStd2IoStd(VidStd, LinkStd, &Value, &SubValue);
        if (Result == DTAPI_OK)
        {
            What = "DtOutpChannel_SetIoConfig";
            Result = DtOutpChannel_SetIoConfig(Channel, DTAPI_IOCONFIG_IOSTD, Value,
                                               SubValue, -1, -1);
        }
    }
    if (Result == DTAPI_OK)
    {
        What = "DtOutpChannel_SetTxMode";
        Result = DtOutpChannel_SetTxMode(Channel, TxMode, 0);
    }
    if (Result != DTAPI_OK)
    {
        printf("%s  ", Port->DeviceName);
        Example_Failed(What, Result);
        DtOutpChannel_Detach(Channel, DTAPI_INSTANT_DETACH);
        return EXAMPLE_FAILED;
    }

    int Exit = Transmit(Channel, Port, Src, Count, Flags);
    Result = DtOutpChannel_Detach(Channel, Exit == EXAMPLE_OK ? DTAPI_WAIT_UNTIL_SENT
                                                              : DTAPI_INSTANT_DETACH);
    if (Exit == EXAMPLE_OK && Result != DTAPI_OK)
    {
        printf("%s  ", Port->DeviceName);
        Exit = Example_Failed("DtOutpChannel_Detach", Result);
    }
    return Exit;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LoadSource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the files, or prepares the pattern. False, having printed why, when that fails.
//
static bool LoadSource(const char* In, int VidStd, int BitsPerSymbol, Source* Src)
{
    memset(Src, 0, sizeof(*Src));
    if (In != NULL)
        return ReadFrames(In, &Src->Files);

    if (VidStd == DTAPI_VIDSTD_UNKNOWN)
    {
        printf("Give --in, or --vidstd for the test pattern\n");
        return false;
    }
    if (!PatternInit(&Src->Pat, VidStd, BitsPerSymbol))
    {
        printf("No test pattern for video standard %s\n", Example_VidStdName(VidStd));
        return false;
    }
    Src->Generated = (char*)malloc((size_t)Src->Pat.FrameSize);
    if (Src->Generated == NULL)
    {
        printf("Allocating: DTAPI_E_OUT_OF_MEM\n");
        return false;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main(int Argc, char** Argv)
{
    int64_t Serial = 0;
    int64_t PortNumber = 0;
    int64_t Count = -1;
    int64_t LinkStd = -1;
    if (!Example_CheckArguments(Argc, Argv, "Transmits raw SDI frames on an SDI output.",
                                g_Options,
                                (int)(sizeof(g_Options) / sizeof(g_Options[0]))) ||
        !Example_Int64(Argc, Argv, "--serial", &Serial) ||
        !Example_Int64(Argc, Argv, "--port", &PortNumber) ||
        !Example_Int64(Argc, Argv, "--count", &Count) ||
        !Example_Int64(Argc, Argv, "--linkstd", &LinkStd))
    {
        return EXAMPLE_FAILED;
    }
    int TxMode = 0;
    int BitsPerSymbol = 0;
    if (!TxModeFrom(Example_Value(Argc, Argv, "--txmode"), &TxMode, &BitsPerSymbol))
    {
        printf("Unknown transmit mode: %s; 8B, 10B or 16B\n",
               Example_Value(Argc, Argv, "--txmode"));
        return EXAMPLE_FAILED;
    }
    int VidStd = DTAPI_VIDSTD_UNKNOWN;
    const char* VidStdName = Example_Value(Argc, Argv, "--vidstd");
    if (VidStdName != NULL && !Example_VidStdFromName(VidStdName, &VidStd))
    {
        printf("Unknown video standard: %s\n", VidStdName);
        return EXAMPLE_FAILED;
    }

    Source Src;
    if (!LoadSource(Example_Value(Argc, Argv, "--in"), VidStd, BitsPerSymbol, &Src))
    {
        FreeFrames(&Src.Files);
        PatternFree(&Src.Pat);
        return EXAMPLE_FAILED;
    }
    if (Count < 0)
        Count = Src.Files.Count > 0 ? Src.Files.Count : 1;

    DtHwFuncDesc Port;
    int Exit = EXAMPLE_FAILED;
    unsigned int Result = Example_FindPort(Serial, (int)PortNumber, IsSdiOutput, &Port);
    DtDevice* Device = DtDevice_Alloc();
    DtOutpChannel* Channel = DtOutpChannel_Alloc();
    if (Result == DTAPI_E_NOT_FOUND)
    {
        printf("No SDI output that suits\n");
        Exit = EXAMPLE_NOTHING;
    }
    else if (Result != DTAPI_OK)
        Exit = Example_Failed("DtapiHwFuncScan", Result);
    else if (Device == NULL || Channel == NULL)
        Exit = Example_Failed("Allocating", DTAPI_E_OUT_OF_MEM);
    else
        Exit = AttachAndTransmit(Device, Channel, &Port, TxMode, VidStd, (int)LinkStd,
                                 &Src, Count, Example_HasFlag(Argc, Argv, "--flags"));

    DtOutpChannel_Free(Channel);
    DtDevice_Free(Device);
    FreeFrames(&Src.Files);
    PatternFree(&Src.Pat);
    free(Src.Generated);
    return Exit;
}
