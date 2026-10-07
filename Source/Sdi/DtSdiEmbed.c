// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiEmbed.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Embedding audio in SDI frames: the builder's audio
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The clock counts the line's samples, TicksPerLine to a line, and a frame's audio
// sample n has its moment at PhaseAtStart plus n times Increment, counted in the cadence
// from its first frame. Each line takes the samples whose moment, rounded, lies before
// the line's own start, at most MaxPerLine of them; the line after a switching line
// takes none. The clock is kept in a double and stepped by adding, as the matrix does,
// so that the rounding and so the clock phase words come out the same.
//
// A PCM sample becomes an AES3 subframe: its upper 24 bits, V 0, U 0, C the next bit of
// the channel status, P even parity over bits 4 to 30, and Z on channels 1 and 3 of a
// group at the start of each block of 192. The channel status is professional, 48 kHz,
// stereo, 24 bits, with its CRC. A channel its group lacks carries silence with V set,
// and the status the matrix gives such a channel: professional, 48 kHz, 16 bits. An AES3
// subframe of the program keeps its bits but P, which is worked out again.
//
// The channel status is kept as DTAPI keeps it: bit n of the block, in the order it is
// sent, is bit 7 - n % 8 of byte n / 8.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "DtSdiAnc.h"         // Writing packets.
#include "DtSdiAudio.h"       // The BCH code.
#include "DtSdiEmbed.h"       // Interface being implemented.
#include "Video/DtSdiFrame.h" // Nine bits with their parity.
#include "Video/DtVidStd.h"   // Frame rates.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define DT_SDIEMBED_SAMPLE_RATE 48000

// The user data words of a HD data packet and of a control packet, and the most
// samples an SD packet carries.
#define DT_SDIEMBED_HD_DATA_WORDS 24
#define DT_SDIEMBED_HD_CONTROL_WORDS 11
#define DT_SDIEMBED_SD_MAX_PER_PACKET 4

// The words of a packet besides its user data: the flag's three, the IDs' two, the data
// count and the checksum.
#define DT_SDIEMBED_PACKET_OVERHEAD 7

// Bit 13 of a sample's clock word: the sample is more than a line late, ST 299-1's MPF.
#define DT_SDIEMBED_MPF 0x2000

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Crc8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The CRC of a channel status block over its first 23 bytes, as DTAPI keeps the block:
// x^8 + x^4 + x^3 + x^2 + 1, starting from all ones, the highest bit of each byte first.
//
static uint8_t Crc8(const uint8_t* Bytes)
{
    unsigned Crc = 0xFF;
    for (int i = 0; i < 23; i++)
    {
        Crc ^= Bytes[i];
        for (int b = 0; b < 8; b++)
            Crc = (Crc & 0x80) != 0 ? (Crc << 1 ^ 0x1D) & 0xFF : Crc << 1 & 0xFF;
    }
    return (uint8_t)Crc;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Gcd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static long long Gcd(long long A, long long B)
{
    while (B != 0)
    {
        const long long R = A % B;
        A = B;
        B = R;
    }
    return A;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsSwitchingLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool IsSwitchingLine(const DtSdiEmbed* Embed, int Line)
{
    const DtFrameProps* Props = &Embed->Props;
    return Line == Props->Fields[0].SwitchingLine ||
           (Props->NumFields == 2 && Line == Props->Fields[1].SwitchingLine);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Parity -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// 1 when Bits has an odd number of ones.
//
static uint32_t Parity(uint32_t Bits)
{
    Bits ^= Bits >> 16;
    Bits ^= Bits >> 8;
    Bits ^= Bits >> 4;
    Bits ^= Bits >> 2;
    Bits ^= Bits >> 1;
    return Bits & 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Aes3Of -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The AES3 subframe of channel Channel's sample Index, and for a channel whose
// subframes the builder makes, the channel status moved on a bit.
//
static uint32_t Aes3Of(DtSdiEmbed* Embed, int Channel, int Index)
{
    const DtSdiEmbedSource Source = Embed->Source[Channel];
    const uint8_t* At = Embed->Samples[Channel] + (size_t)Index * Embed->Stride[Channel];
    uint32_t Word;

    if (Source == DT_SDIEMBED_AES3)
    {
        memcpy(&Word, At, sizeof(Word));
        Word &= ~DT_SDI_AES3_P;
        return Word | Parity(Word & 0x7FFFFFF0u) << 31;
    }

    const uint8_t* Status = Embed->Status[0];
    if (Source == DT_SDIEMBED_PCM)
    {
        int32_t Sample;
        memcpy(&Sample, At, sizeof(Sample));
        Word = (uint32_t)Sample >> 4 & DT_SDI_AES3_AUDIO;
    }
    else
    {
        Word = DT_SDI_AES3_V;
        Status = Embed->Status[1];
    }
    const int Bit = Embed->StatusBit[Channel];
    if ((Status[Bit / 8] >> (7 - Bit % 8) & 1) != 0)
        Word |= DT_SDI_AES3_C;
    Word |= Parity(Word) << 31;
    if (Bit == 0 && Channel % 2 == 0)
        Word |= DT_SDI_AES3_Z;
    Embed->StatusBit[Channel] = (Bit + 1) % 192;
    return Word;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutHdData -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes a data packet of SMPTE ST 299-1 of group Group with sample Index: the clock
// phase, each channel's subframe a byte a word, and the BCH code. Returns the word after
// it.
//
static int PutHdData(DtSdiEmbed* Embed, int Group, int Index, uint16_t* Words, int Pos,
                     bool Checksum)
{
    // The packet's first 24 words as the BCH code takes them: flag, IDs, count, then
    // the user data words.
    uint16_t Coded[6 + DT_SDIEMBED_HD_DATA_WORDS];
    uint16_t* Data = Coded + 6;
    const uint8_t Did = (uint8_t)(DT_SDIANC_DID_HD_AUDIO_DATA_G1 - Group);
    const uint8_t Dbn = Embed->Dbn[Group];

    const unsigned Clock = Embed->Clock[Index];
    const unsigned Phase = Clock & 0x1FFF;
    Data[0] = DtSdiAnc_WithParity8(Phase & 0xFF);
    Data[1] =
        DtSdiAnc_WithParity8((Phase >> 8 & 0xF) | ((Phase & 0x1000) != 0 ? 0x20 : 0) |
                             ((Clock & DT_SDIEMBED_MPF) != 0 ? 0x10 : 0));
    for (int c = 0; c < 4; c++)
    {
        const uint32_t Word = Aes3Of(Embed, 4 * Group + c, Index);
        for (int b = 0; b < 4; b++)
            Data[2 + 4 * c + b] = DtSdiAnc_WithParity8(Word >> (8 * b) & 0xFF);
    }

    Coded[0] = 0x000;
    Coded[1] = 0x3FF;
    Coded[2] = 0x3FF;
    Coded[3] = DtSdiAnc_WithParity8(Did);
    Coded[4] = DtSdiAnc_WithParity8(Dbn);
    Coded[5] = DtSdiAnc_WithParity8(DT_SDIEMBED_HD_DATA_WORDS);
    const uint64_t Bch = DtSdiAudio_HdBch(Coded);
    for (int i = 0; i < 6; i++)
        Data[18 + i] = DtSdiAnc_WithParity8((unsigned)(Bch >> (8 * i)) & 0xFF);

    Embed->Dbn[Group] = (uint8_t)(Dbn == 255 ? 1 : Dbn + 1);
    return DtSdiAnc_Put(Words, Pos, Did, Dbn, Data, DT_SDIEMBED_HD_DATA_WORDS, Checksum);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutHdControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the control packet of SMPTE ST 299-1 of group Group: the frame's place in the
// cadence, 48 kHz, the channels the program sends, and no delays. Returns the word
// after it.
//
static int PutHdControl(const DtSdiEmbed* Embed, int Group, uint16_t* Words, int Pos,
                        bool Checksum)
{
    uint16_t Data[DT_SDIEMBED_HD_CONTROL_WORDS];
    Data[0] = (uint16_t)DtSdiFrame_WithParity((uint32_t)Embed->FrameNumber);
    Data[1] = 0x200; // 48 kHz, synchronous
    Data[2] = DtSdiAnc_WithParity8(Embed->Active[Group]);
    for (int i = 3; i < DT_SDIEMBED_HD_CONTROL_WORDS; i++)
        Data[i] = 0x200; // Delays and reserved words
    const uint8_t Did = (uint8_t)(DT_SDIANC_DID_HD_AUDIO_CONTROL_G1 - Group);
    return DtSdiAnc_Put(Words, Pos, Did, 0, Data, DT_SDIEMBED_HD_CONTROL_WORDS, Checksum);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutSdData -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes a data packet of SMPTE ST 272 of group Group with Count samples from sample
// First on: for each sample a subframe of three words per channel. Z is set on every
// channel of a sample from the first whose subframe has it. Returns the word after it.
//
static int PutSdData(DtSdiEmbed* Embed, int Group, int First, int Count, uint16_t* Words,
                     int Pos, bool Checksum)
{
    uint16_t Data[3 * 4 * DT_SDIEMBED_SD_MAX_PER_PACKET];
    const uint8_t Did = (uint8_t)(DT_SDIANC_DID_SD_AUDIO_DATA_G1 - 2 * Group);
    const uint8_t Dbn = Embed->Dbn[Group];
    int i = 0;
    for (int n = 0; n < Count; n++)
    {
        uint32_t Z = 0;
        for (int c = 0; c < 4; c++)
        {
            const uint32_t Word = Aes3Of(Embed, 4 * Group + c, First + n);
            uint32_t Out = (Word & 0x7FFFFF00u) >> 5 | (uint32_t)c << 1;
            if ((Word & DT_SDI_AES3_Z) != 0)
                Z = 1;
            Out |= Z;
            Out |= Parity(Out) << 26;
            Data[i++] = (uint16_t)DtSdiFrame_WithParity(Out);
            Data[i++] = (uint16_t)DtSdiFrame_WithParity(Out >> 9);
            Data[i++] = (uint16_t)DtSdiFrame_WithParity(Out >> 18);
        }
    }
    Embed->Dbn[Group] = (uint8_t)(Dbn == 255 ? 1 : Dbn + 1);
    return DtSdiAnc_Put(Words, Pos, Did, Dbn, Data, i, Checksum);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Plan -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Works out how many samples each line carries, and in HD each sample's clock word.
//
static void Plan(DtSdiEmbed* Embed)
{
    const int Place = Embed->FrameNumber - 1;
    const int Ticks = Embed->TicksPerLine;
    int Base = 1;
    for (int i = 0; i < Place; i++)
        Base += Embed->SamplesInFrame[i];

    double Phase = Embed->PhaseAtStart;
    Phase += (Base - 1) * Embed->Increment;
    int LineStart = Ticks + Place * Embed->NumLines * Ticks;
    int Left = Embed->NumSamples;
    int Index = 0;

    for (int Line = 1; Line <= Embed->NumLines; Line++)
    {
        int Count = 0;
        if (!IsSwitchingLine(Embed, Line - 1))
        {
            int Max = Embed->MaxPerLine[Place];
            if (Max > Left)
                Max = Left;
            int Due = 0;
            if (Phase < (double)LineStart)
                Due = 1 + (int)(((double)LineStart - Phase - 1E-12) / Embed->Increment);
            if (Due > Max)
                Due = Max;

            if (Embed->Sd)
            {
                // A packet's samples step the clock at once.
                Count = Due;
                for (int Left2 = Due; Left2 > 0; Left2 -= DT_SDIEMBED_SD_MAX_PER_PACKET)
                {
                    const int InPacket = Left2 < DT_SDIEMBED_SD_MAX_PER_PACKET
                                             ? Left2
                                             : DT_SDIEMBED_SD_MAX_PER_PACKET;
                    Phase += Embed->Increment * InPacket;
                }
            }
            else
            {
                while (Count < Due && (int)(Phase + 0.5) < LineStart)
                {
                    const int Rounded = (int)(Phase + 0.5);
                    Embed->Clock[Index++] =
                        (uint16_t)(Rounded % Ticks |
                                   (LineStart - Ticks > Rounded ? DT_SDIEMBED_MPF : 0));
                    Phase += Embed->Increment;
                    Count++;
                }
            }
            Left -= Count;
        }
        Embed->Count[Line - 1] = (uint8_t)Count;
        LineStart += Ticks;
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Embedding +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiEmbed_Begin -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSdiEmbed_Begin(DtSdiEmbed* Embed, DtSdiAudio* Audio)
{
    int FrameNumber = Embed->StateVidStd == Embed->VidStd ? Embed->NextFrameNumber : 1;
    Embed->HasAudio = false;
    memset(Embed->Group, 0, sizeof(Embed->Group));
    memset(Embed->Active, 0, sizeof(Embed->Active));
    memset(Embed->Count, 0, sizeof(Embed->Count));
    for (int c = 0; c < DT_SDI_AUDIO_MAX_CHANNELS; c++)
        Embed->Source[c] = DT_SDIEMBED_NONE;
    Embed->Next = 0;

    if (Audio == NULL)
    {
        Embed->FrameNumber = FrameNumber;
        Embed->NumSamples = Embed->SamplesInFrame[FrameNumber - 1];
        return DTAPI_OK;
    }
    if (Audio->FrameNumber < 0 || Audio->FrameNumber > Embed->CadenceLength)
        return DTAPI_E_INVALID_ARG;
    if (Audio->FrameNumber > 0)
        FrameNumber = Audio->FrameNumber;
    for (int p = 0; p < DT_SDI_AUDIO_MAX_CHANNELS / 2; p++)
    {
        if (Audio->Formats[p] != DT_SDI_AUDIO_NONE &&
            Audio->Formats[p] != DT_SDI_AUDIO_PCM &&
            Audio->Formats[p] != DT_SDI_AUDIO_AES3)
            return DTAPI_E_INVALID_FORMAT;
    }

    const int NumSamples = Embed->SamplesInFrame[FrameNumber - 1];
    bool Short = false;
    for (int c = 0; c < DT_SDI_AUDIO_MAX_CHANNELS; c++)
    {
        const DtSdiAudioChannel* C = &Audio->Channels[c];
        const DtSdiAudioFormat Format = Audio->Formats[c / 2];
        if (Format == DT_SDI_AUDIO_NONE || C->Samples == NULL)
            continue;
        if (C->Stride < 0)
            return DTAPI_E_INVALID_ARG;
        if (C->NumSamples < NumSamples)
            Short = true;
        Embed->Source[c] =
            Format == DT_SDI_AUDIO_PCM ? DT_SDIEMBED_PCM : DT_SDIEMBED_AES3;
        Embed->Samples[c] = (const uint8_t*)C->Samples;
        Embed->Stride[c] = (size_t)(C->Stride == 0 ? 1 : C->Stride) * sizeof(uint32_t);
        Embed->Group[c / 4] = true;
        Embed->Active[c / 4] |= 1u << (c % 4);
        Embed->HasAudio = true;
    }
    Audio->NumSamplesUsed = Embed->HasAudio ? NumSamples : 0;
    if (Short)
        return DTAPI_E_BUF_TOO_SMALL;

    // A group carries all four of its channels: those the program does not send as
    // silence.
    for (int c = 0; c < DT_SDI_AUDIO_MAX_CHANNELS; c++)
    {
        if (Embed->Group[c / 4] && Embed->Source[c] == DT_SDIEMBED_NONE)
        {
            Embed->Source[c] = DT_SDIEMBED_MUTE;
            Embed->Samples[c] = NULL;
            Embed->Stride[c] = 0;
        }
    }
    Embed->FrameNumber = FrameNumber;
    Embed->NumSamples = NumSamples;
    if (Embed->HasAudio)
        Plan(Embed);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiEmbed_End -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiEmbed_End(DtSdiEmbed* Embed)
{
    Embed->StateVidStd = Embed->VidStd;
    Embed->NextFrameNumber = Embed->FrameNumber % Embed->CadenceLength + 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiEmbed_HancWords -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtSdiEmbed_HancWords(const DtSdiEmbed* Embed, const DtSdiGeometry* Geo, int LineIndex,
                         int Stream)
{
    (void)Geo;
    if (!Embed->HasAudio || Stream > 1)
        return 0;
    int Groups = 0;
    for (int g = 0; g < 4; g++)
        Groups += Embed->Group[g] ? 1 : 0;
    const int Count = Embed->Count[LineIndex];

    if (Embed->Sd)
    {
        const int Full = Count / DT_SDIEMBED_SD_MAX_PER_PACKET;
        const int Rest = Count % DT_SDIEMBED_SD_MAX_PER_PACKET;
        const int PerGroup =
            Full * (DT_SDIEMBED_PACKET_OVERHEAD + 12 * DT_SDIEMBED_SD_MAX_PER_PACKET) +
            (Rest > 0 ? DT_SDIEMBED_PACKET_OVERHEAD + 12 * Rest : 0);
        return Groups * PerGroup;
    }
    if (Stream == 0)
        return Groups * Count * (DT_SDIEMBED_PACKET_OVERHEAD + DT_SDIEMBED_HD_DATA_WORDS);
    return IsSwitchingLine(Embed, LineIndex + 1 - 2)
               ? Groups * (DT_SDIEMBED_PACKET_OVERHEAD + DT_SDIEMBED_HD_CONTROL_WORDS)
               : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiEmbed_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The clock's increment is a fraction, the line's samples of a frame times the frame
// rate over 48 kHz, which one division of two exact integers makes the nearest double,
// as the matrix's fraction becomes. The cadence gives its odd places the most samples
// and its even places the fewest, unless the rest of the cadence would then not add up.
//
void DtSdiEmbed_Init(DtSdiEmbed* Embed, const DtSdiGeometry* Geo)
{
    if (Embed->VidStd == Geo->VidStd)
        return;
    Embed->VidStd = Geo->VidStd;
    Embed->Sd = Geo->NumStreams == 1;
    Embed->Props = Geo->Props;
    Embed->NumLines = Geo->Layout.NumLines;
    const int StreamWords = Geo->StreamHancWords + Geo->StreamActiveWords;
    Embed->TicksPerLine = Embed->Sd ? StreamWords / 2 : StreamWords;

    int Num = 0;
    int Den = 0;
    DtVidStd_FrameRate(Geo->VidStd, &Num, &Den);
    const long long TicksInFrame = (long long)Embed->TicksPerLine * Embed->NumLines;
    Embed->Increment =
        (double)(TicksInFrame * Num) / (double)((long long)Den * DT_SDIEMBED_SAMPLE_RATE);

    // The cadence: 48000 Den / Num samples a frame on average.
    const long long PerFrameNum = (long long)DT_SDIEMBED_SAMPLE_RATE * Den;
    const long long Length = Num / Gcd(PerFrameNum, Num);
    Embed->CadenceLength = (int)Length;
    const int Most = (int)((PerFrameNum + Num - 1) / Num);
    const int Fewest = (int)(PerFrameNum / Num);
    int Left = (int)(PerFrameNum * Length / Num);
    const int Usable = Embed->NumLines - Embed->Props.NumFields;
    for (int i = 0; i < Embed->CadenceLength; i++)
    {
        int InFrame = Most;
        if ((i + 1) % 2 == 0 && (Embed->CadenceLength - i) * Most > Left)
            InFrame = Fewest;
        Embed->SamplesInFrame[i] = InFrame;
        Embed->MaxPerLine[i] = (InFrame + Usable - 1) / Usable;
        Left -= InFrame;
    }

    // The clock's start, such that the cadence's first frame takes all its samples.
    const double Audio = (Embed->SamplesInFrame[0] - 1) * Embed->Increment;
    double Start = (double)TicksInFrame - Audio;
    Embed->PhaseAtStart = Start > 1.0 ? Start - 1.0 : 0.0;

    // The channel status: of PCM, professional, 48 kHz, stereo, 24 bits; of a channel
    // its group lacks, professional, 48 kHz, 16 bits.
    memset(Embed->Status, 0, sizeof(Embed->Status));
    Embed->Status[0][0] = 0x81;
    Embed->Status[0][1] = 0x40;
    Embed->Status[0][2] = 0x34;
    Embed->Status[1][0] = 0x81;
    Embed->Status[1][2] = 0x10;
    for (int s = 0; s < 2; s++)
        Embed->Status[s][23] = Crc8(Embed->Status[s]);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiEmbed_NumSamples -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtSdiEmbed_NumSamples(const DtSdiEmbed* Embed, int FrameNumber,
                                  int* NumSamples)
{
    if (FrameNumber < 0 || FrameNumber > Embed->CadenceLength)
        return DTAPI_E_INVALID_ARG;
    if (FrameNumber == 0)
        FrameNumber = Embed->StateVidStd == Embed->VidStd ? Embed->NextFrameNumber : 1;
    *NumSamples = Embed->SamplesInFrame[FrameNumber - 1];
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiEmbed_Put -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// In HD the control packets go in the Y stream, and the data packets in the C stream
// one sample of each group in turn; in SD each group's samples of the line go in
// packets of up to four, group after group.
//
int DtSdiEmbed_Put(DtSdiEmbed* Embed, const DtSdiGeometry* Geo, int LineIndex, int Stream,
                   uint16_t* Words, int Pos, bool Checksum)
{
    (void)Geo;
    if (!Embed->HasAudio || Stream > 1)
        return Pos;
    const int Count = Embed->Count[LineIndex];

    if (Embed->Sd)
    {
        for (int g = 0; g < 4; g++)
        {
            if (!Embed->Group[g])
                continue;
            for (int n = 0; n < Count; n += DT_SDIEMBED_SD_MAX_PER_PACKET)
            {
                const int InPacket = Count - n < DT_SDIEMBED_SD_MAX_PER_PACKET
                                         ? Count - n
                                         : DT_SDIEMBED_SD_MAX_PER_PACKET;
                Pos =
                    PutSdData(Embed, g, Embed->Next + n, InPacket, Words, Pos, Checksum);
            }
        }
        Embed->Next += Count;
        return Pos;
    }

    if (Stream == 1)
    {
        if (!IsSwitchingLine(Embed, LineIndex + 1 - 2))
            return Pos;
        for (int g = 0; g < 4; g++)
            if (Embed->Group[g])
                Pos = PutHdControl(Embed, g, Words, Pos, Checksum);
        return Pos;
    }
    for (int n = 0; n < Count; n++)
        for (int g = 0; g < 4; g++)
            if (Embed->Group[g])
                Pos = PutHdData(Embed, g, Embed->Next + n, Words, Pos, Checksum);
    Embed->Next += Count;
    return Pos;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiEmbed_Start -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiEmbed_Start(DtSdiEmbed* Embed)
{
    if (Embed->StateVidStd == Embed->VidStd)
        return;
    for (int g = 0; g < 4; g++)
        Embed->Dbn[g] = 1;
    memset(Embed->StatusBit, 0, sizeof(Embed->StatusBit));
    Embed->NextFrameNumber = 1;
}
