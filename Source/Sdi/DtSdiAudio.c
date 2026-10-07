// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiAudio.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The embedded audio of SDI frames: taking it out of its packets
//
// SPDX-License-Identifier: BSD-3-Clause
//
// SMPTE ST 299-1 (HD and up) puts four channels, a group, in each data packet: two
// clock words, then four words a channel, then six words of BCH code. A channel's four
// words carry the AES3 subframe from bit 4 of the first word up, eight bits a word: the
// 24 bits of audio, then V, U, C and P; bit 3 of the first word of channels 1 and 3 of
// the group is Z, the start of an AES3 block. So the lower bytes of the four words, the
// first lowest, are the subframe in the layout of DT_SDI_AUDIO_AES3.
//
// SMPTE ST 272 (SD) puts subframes of three words in a data packet, each for the
// channel of the group that bits 1 and 2 of its first word name: Z in bit 0 of the first
// word, then 20 bits of audio over the three words from bit 3 of the first, then V, U, C
// and P in bits 5 to 8 of the third. Its audio becomes the upper 20 of the 24 bits.
//
// A control packet's first user data word is the frame's place in the audio cadence.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtSdiAudio.h"     // Interface being implemented.
#include "DtSdiGeometry.h"  // The standards a channel carries.
#include "Video/DtVidStd.h" // Frame rates.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define DT_SDIAUDIO_SAMPLE_RATE 48000

// The user data words of a HD data packet before the BCH code, and the code's words.
#define DT_SDIAUDIO_HD_DATA_WORDS 18
#define DT_SDIAUDIO_HD_BCH_WORDS 6

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutSample -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the subframe Aes3 as sample of channel Channel (from 0), in the format of its
// pair, where the program wants it and has room; and counts it.
//
static void PutSample(DtSdiAudio* Audio, int Channel, uint32_t Aes3)
{
    DtSdiAudioChannel* C = &Audio->Channels[Channel];
    const DtSdiAudioFormat Format = Audio->Formats[Channel / 2];
    const int Index = C->NumSamples++;

    C->Present = true;
    if ((Aes3 & DT_SDI_AES3_V) != 0)
        C->Invalid = true;
    if (Format == DT_SDI_AUDIO_NONE || C->Samples == NULL || Index >= C->MaxSamples)
        return;

    const size_t At = (size_t)Index * (size_t)(C->Stride == 0 ? 1 : C->Stride);
    if (Format == DT_SDI_AUDIO_AES3)
        ((uint32_t*)C->Samples)[At] = Aes3;
    else
        ((int32_t*)C->Samples)[At] = (int32_t)((Aes3 << 4) & 0xFFFFFF00u);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HdBchHolds -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns whether the six BCH words of a HD data packet hold: the code over the lower
// bytes of its flag, IDs, data count and first 18 user data words, one byte a word.
//
static bool HdBchHolds(const DtSdiAncFound* Found)
{
    const uint64_t Bch = DtSdiAudio_HdBch(Found->Words - 6);
    for (int i = 0; i < DT_SDIAUDIO_HD_BCH_WORDS; i++)
    {
        if ((Found->Words[DT_SDIAUDIO_HD_DATA_WORDS + i] & 0xFF) !=
            ((Bch >> (8 * i)) & 0xFF))
            return false;
    }
    return true;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Audio +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiAudio_HdBch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Each byte goes into a register of six bytes, the code's: the byte xored with the
// register's lowest byte selects what the shift brings in, the generator of ST 299-1,
// which the multiplication spreads over the bytes it touches.
//
uint64_t DtSdiAudio_HdBch(const uint16_t* Words)
{
    uint64_t Bch = 0;
    for (int i = 0; i < 6 + DT_SDIAUDIO_HD_DATA_WORDS; i++)
        Bch = Bch >> 8 ^ (((Words[i] ^ Bch) & 0xFF) * 0x10101010001ULL);
    return Bch;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiAudio_MaxSamples -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtSdiAudio_MaxSamples(int VidStd, int* NumSamples)
{
    if (NumSamples == NULL)
        return DTAPI_E_INVALID_ARG;
    *NumSamples = 0;

    DtSdiGeometry Geo;
    DtapiResult Result = DtSdiGeometry_Init(&Geo, VidStd);
    if (Result != DTAPI_OK)
        return Result;
    int Num = 0;
    int Den = 0;
    DtVidStd_FrameRate(VidStd, &Num, &Den);
    const long long Product = (long long)DT_SDIAUDIO_SAMPLE_RATE * Den;
    *NumSamples = (int)((Product + Num - 1) / Num);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiAudio_Begin -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiAudio_Begin(DtSdiAudio* Audio)
{
    for (int c = 0; c < DT_SDI_AUDIO_MAX_CHANNELS; c++)
    {
        Audio->Channels[c].NumSamples = 0;
        Audio->Channels[c].Present = false;
        Audio->Channels[c].Invalid = false;
    }
    for (int g = 0; g < DT_SDI_AUDIO_MAX_CHANNELS / 4; g++)
        Audio->NumPacketErrors[g] = 0;
    Audio->FrameNumber = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiAudio_CadenceLength -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A frame holds 48000 Den / Num samples on average; the cadence is the denominator of
// that fraction in its lowest terms.
//
int DtSdiAudio_CadenceLength(int VidStd)
{
    int Num = 0;
    int Den = 0;
    DtVidStd_FrameRate(VidStd, &Num, &Den);
    if (Num <= 0)
        return 1;
    long long A = (long long)DT_SDIAUDIO_SAMPLE_RATE * Den;
    long long B = Num;
    while (B != 0)
    {
        const long long R = A % B;
        A = B;
        B = R;
    }
    return (int)(Num / A);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiAudio_Check -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSdiAudio_Check(const DtSdiAudio* Audio, int VidStd)
{
    int Most = 0;
    DtapiResult Result = DtSdiAudio_MaxSamples(VidStd, &Most);
    if (Result != DTAPI_OK)
        return Result;

    for (int p = 0; p < DT_SDI_AUDIO_MAX_CHANNELS / 2; p++)
    {
        const DtSdiAudioFormat Format = Audio->Formats[p];
        if (Format != DT_SDI_AUDIO_NONE && Format != DT_SDI_AUDIO_PCM &&
            Format != DT_SDI_AUDIO_AES3)
        {
            return DTAPI_E_INVALID_FORMAT;
        }
        for (int c = 2 * p; c < 2 * p + 2; c++)
        {
            const DtSdiAudioChannel* C = &Audio->Channels[c];
            if (Format == DT_SDI_AUDIO_NONE || C->Samples == NULL)
                continue;
            if (C->Stride < 0)
                return DTAPI_E_INVALID_ARG;
            if (C->MaxSamples < Most)
                return DTAPI_E_BUF_TOO_SMALL;
        }
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiAudio_TakeHd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSdiAudio_TakeHd(DtSdiAudio* Audio, const DtSdiAncFound* Found, bool Check)
{
    const uint8_t Did = Found->Did;
    if (Did >= 0xE0 && Did <= 0xE3)
    {
        // A control packet: E3 for group 1 down to E0 for group 4.
        if (Found->NumWords > 0 && Audio->FrameNumber == 0)
            Audio->FrameNumber = Found->Words[0] & 0xFF;
        return;
    }

    // A data packet: E7 for group 1 down to E4 for group 4.
    const int Group = 0xE7 - Did;
    if (Found->NumWords < DT_SDIAUDIO_HD_DATA_WORDS + DT_SDIAUDIO_HD_BCH_WORDS)
    {
        Audio->NumPacketErrors[Group]++;
        return;
    }
    if (Check && (!Found->ChecksumOk || !HdBchHolds(Found)))
        Audio->NumPacketErrors[Group]++;

    for (int c = 0; c < 4; c++)
    {
        const uint16_t* W = Found->Words + 2 + 4 * c;
        const uint32_t Aes3 = (uint32_t)(W[0] & 0xFF) | (uint32_t)(W[1] & 0xFF) << 8 |
                              (uint32_t)(W[2] & 0xFF) << 16 |
                              (uint32_t)(W[3] & 0xFF) << 24;
        PutSample(Audio, 4 * Group + c, Aes3);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiAudio_TakeSd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSdiAudio_TakeSd(DtSdiAudio* Audio, const DtSdiAncFound* Found, bool Check)
{
    const uint8_t Did = Found->Did;
    if (Did >= 0xEC && Did <= 0xEF)
    {
        // A control packet: EF for group 1 down to EC for group 4.
        if (Found->NumWords > 0 && Audio->FrameNumber == 0)
            Audio->FrameNumber = Found->Words[0] & 0xFF;
        return;
    }
    if ((Did & 1) == 0)
        return; // Extended data, the four bits below the 20, which this leaves out

    // A data packet: FF for group 1, FD, FB, F9 for group 4.
    const int Group = (0xFF - Did) / 2;
    if (Check && !Found->ChecksumOk)
        Audio->NumPacketErrors[Group]++;

    for (int i = 0; i + 3 <= Found->NumWords; i += 3)
    {
        const uint32_t X = Found->Words[i];
        const uint32_t Y = Found->Words[i + 1];
        const uint32_t Z = Found->Words[i + 2];
        const uint32_t Aes3 =
            (X & 0x1) << 3 | (X & 0x1F8) << 5 | (Y & 0x1FF) << 14 | (Z & 0x1FF) << 23;
        PutSample(Audio, 4 * Group + (int)((X >> 1) & 3), Aes3);
    }
}
