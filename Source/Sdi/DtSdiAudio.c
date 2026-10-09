// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiAudio.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The embedded audio of SDI frames: taking it out of its packets
//
// SPDX-License-Identifier: BSD-3-Clause
//
// This file takes audio samples out of the audio packets of SMPTE ST 299-1 and ST 272.
//
// In SMPTE ST 299-1 (HD and up), each data packet carries one group of four channels.
// The packet's user data words are laid out as follows:
//   1. Two clock words.
//   2. Four words for each channel, eight bits per word. They carry the AES3 subframe
//      from bit 4 of the first word up: the 24 bits of audio, then V, U, C and P.
//   3. Six words of BCH code.
// Bit 3 of the first word of channels 1 and 3 of the group is Z. It marks the start of an
// AES3 block. So the lower bytes of a channel's four words, the first word in the lowest
// bits, form the subframe in the layout of DT_SDI_AUDIO_AES3.
//
// In SMPTE ST 272 (SD), a data packet carries subframes of three words each. Bits 1 and
// 2 of a subframe's first word name its channel within the group. The other bits are:
//   1. Z, in bit 0 of the first word.
//   2. 20 bits of audio, from bit 3 of the first word through the three words.
//   3. V, U, C and P, in bits 5 to 8 of the third word.
// The 20 bits of audio become the upper 20 of the 24 bits of a sample.
//
// The first user data word of a control packet is the frame's place in the audio
// cadence.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtSdiAudio.h"     // Interface being implemented.
#include "DtSdiGeometry.h"  // The standards a channel carries.
#include "Video/DtVidStd.h" // Frame rates.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The audio sample rate in Hz.
#define DT_SDIAUDIO_SAMPLE_RATE 48000

// The number of user data words in an HD data packet before the BCH code, and the
// number of words of the code.
#define DT_SDIAUDIO_HD_DATA_WORDS 18
#define DT_SDIAUDIO_HD_BCH_WORDS 6

// The most samples per channel by which a 3G level-B picture can go beyond half of its
// interface frame's samples.
#define DT_SDIAUDIO_LEVELB_SPREAD 8

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutSample -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Stores the AES3 subframe Aes3 as the next sample of channel Channel, counting from 0.
// The sample is written in the format chosen for the channel's pair. It is written only
// when the program wants that channel and its buffer has room. Whether or not it is
// written, the sample is counted. It also marks the channel present, and marks it
// invalid when V is set.
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
// Returns whether the six BCH words of an HD data packet are correct. They are correct
// when their lower bytes equal the code that DtSdiAudio_HdBch() works out over the
// packet.
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
// Works out the code byte by byte in a register of six bytes, the size of the code. For
// each word, the loop does three things:
//   1. It xors the word's lower byte with the register's lowest byte.
//   2. It shifts the register down by one byte.
//   3. It xors the result of step 1 into each byte that the generator of SMPTE ST 299-1
//      touches. Multiplying by the constant copies the byte into each of them.
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
    DtVidStd_FrameRate(Geo.IsLevelB ? Geo.InterfaceVidStd : VidStd, &Num, &Den);
    const long long Product = (long long)DT_SDIAUDIO_SAMPLE_RATE * Den;
    *NumSamples = (int)((Product + Num - 1) / Num);
    // A 3G level-B picture carries the samples of half of an interface frame. The halves
    // differ by a few samples, as the field ends where the clock puts them.
    if (Geo.IsLevelB)
        *NumSamples = (*NumSamples + 1) / 2 + DT_SDIAUDIO_LEVELB_SPREAD;
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
// Works out the cadence from the frame rate Num / Den. A frame holds
// DT_SDIAUDIO_SAMPLE_RATE * Den / Num samples on average. The cadence length is the
// denominator of that fraction in its lowest terms.
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
// A packet with a DID of SD audio is not HD audio, and is left alone: its group would
// lie outside the four.
//
void DtSdiAudio_TakeHd(DtSdiAudio* Audio, const DtSdiAncFound* Found, bool Check)
{
    const uint8_t Did = Found->Did;
    if (Did < 0xE0 || Did > 0xE7)
        return;
    if (Did <= 0xE3)
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
// A packet with a DID of HD audio is not SD audio, and is left alone: its group would
// lie outside the four.
//
void DtSdiAudio_TakeSd(DtSdiAudio* Audio, const DtSdiAncFound* Found, bool Check)
{
    const uint8_t Did = Found->Did;
    if ((Did < 0xEC || Did > 0xEF) && Did < 0xF8)
        return;
    if (Did <= 0xEF)
    {
        // A control packet: EF for group 1 down to EC for group 4.
        if (Found->NumWords > 0 && Audio->FrameNumber == 0)
            Audio->FrameNumber = Found->Words[0] & 0xFF;
        return;
    }
    if ((Did & 1) == 0)
        return; // Skip extended data, the four bits below the 20 bits of audio

    // A data packet: FF, FD, FB and F9 for groups 1 to 4.
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
