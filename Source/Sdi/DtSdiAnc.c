// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiAnc.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Finding and writing SMPTE ST 291 ancillary packets in a stream of SDI words
//
// SPDX-License-Identifier: BSD-3-Clause
//
// A packet is the ancillary data flag 000, 3FF, 3FF; the data ID; the secondary data ID
// or, for a data ID of 80 (hex) and up, the data block number; the data count, in its
// lower eight bits; that many user data words; and the checksum: the sum of the lower
// nine bits of the data ID through the last user data word, in nine bits, with bit 9
// the inverse of bit 8. The IDs and the count carry even parity in bit 8 and its
// inverse in bit 9.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtSdiAnc.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The words of a packet besides its user data: the flag's three, the IDs' two, the data
// count and the checksum.
#define DT_SDIANC_OVERHEAD 7

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiAnc_Find -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtSdiAnc_Find(const uint16_t* Words, int Count, int* Pos, DtSdiAncFound* Packet)
{
    for (int i = *Pos; i + DT_SDIANC_OVERHEAD <= Count; i++)
    {
        if (Words[i] != 0x000 || Words[i + 1] != 0x3FF || Words[i + 2] != 0x3FF)
            continue;

        const int NumWords = Words[i + 5] & 0xFF;
        if (i + DT_SDIANC_OVERHEAD + NumWords > Count)
            return false;

        unsigned Sum = 0;
        for (int k = i + 3; k < i + 6 + NumWords; k++)
            Sum += Words[k] & 0x1FF;
        Sum &= 0x1FF;
        const unsigned Checksum = Words[i + 6 + NumWords];

        Packet->Start = i;
        Packet->Did = (uint8_t)Words[i + 3];
        Packet->SdidOrDbn = (uint8_t)Words[i + 4];
        Packet->NumWords = NumWords;
        Packet->Words = Words + i + 6;
        Packet->ChecksumOk =
            (Checksum & 0x1FF) == Sum && ((Checksum >> 9) & 1) != ((Checksum >> 8) & 1);
        *Pos = i + DT_SDIANC_OVERHEAD + NumWords;
        return true;
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiAnc_IsAudio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtSdiAnc_IsAudio(uint8_t Did)
{
    // HD: data E4 to E7, control E0 to E3. SD: control EC to EF, data and extended data
    // F8 to FF.
    return (Did >= 0xE0 && Did <= 0xE7) || (Did >= 0xEC && Did <= 0xEF) || Did >= 0xF8;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiAnc_IsWanted -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtSdiAnc_IsWanted(const DtSdiAncFilter* Filters, int NumFilters, bool InHanc,
                       int Line)
{
    if (NumFilters == 0)
        return true;
    for (int f = 0; f < NumFilters; f++)
    {
        const DtSdiAncFilter* F = &Filters[f];
        const bool Space = F->Space == DT_SDI_ANC_SPACE_BOTH ||
                           (F->Space == DT_SDI_ANC_SPACE_HANC) == InHanc;
        if (Space && Line >= F->FirstLine && (F->LastLine == 0 || Line <= F->LastLine))
            return true;
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiAnc_IsListed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtSdiAnc_IsListed(const DtSdiAncFilter* Filters, int NumFilters, uint8_t Did,
                       uint8_t SdidOrDbn, bool InHanc, int Line)
{
    if (NumFilters == 0)
    {
        const bool PayloadId =
            Did == DT_SDIANC_DID_PAYLOAD_ID && SdidOrDbn == DT_SDIANC_SDID_PAYLOAD_ID;
        return !PayloadId && !DtSdiAnc_IsAudio(Did);
    }

    for (int f = 0; f < NumFilters; f++)
    {
        const DtSdiAncFilter* F = &Filters[f];
        if (!F->AnyDid && F->Did != Did)
            continue;
        if (!F->AnyDid && !F->AnySdid && Did < 0x80 && F->Sdid != SdidOrDbn)
            continue;
        if ((F->Space == DT_SDI_ANC_SPACE_HANC && !InHanc) ||
            (F->Space == DT_SDI_ANC_SPACE_VANC && InHanc))
        {
            continue;
        }
        if (Line < F->FirstLine || (F->LastLine != 0 && Line > F->LastLine))
            continue;
        return true;
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiAnc_Put -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtSdiAnc_Put(uint16_t* Words, int Pos, uint8_t Did, uint8_t SdidOrDbn,
                 const uint16_t* Data, int Count, bool Checksum)
{
    Words[Pos++] = 0x000;
    Words[Pos++] = 0x3FF;
    Words[Pos++] = 0x3FF;
    const int First = Pos;
    Words[Pos++] = DtSdiAnc_WithParity8(Did);
    Words[Pos++] = DtSdiAnc_WithParity8(SdidOrDbn);
    Words[Pos++] = DtSdiAnc_WithParity8((unsigned)Count);
    for (int i = 0; i < Count; i++)
        Words[Pos++] = (uint16_t)(Data[i] & 0x3FF);
    if (!Checksum)
    {
        Words[Pos++] = DT_SDIANC_NO_CHECKSUM;
        return Pos;
    }
    unsigned Sum = 0;
    for (int i = First; i < Pos; i++)
        Sum += Words[i] & 0x1FF;
    Sum &= 0x1FF;
    Words[Pos++] = (uint16_t)(Sum | (((Sum >> 8) & 1) ^ 1) << 9);
    return Pos;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiAnc_WithParity8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint16_t DtSdiAnc_WithParity8(unsigned Value)
{
    unsigned Ones = Value & 0xFF;
    Ones ^= Ones >> 4;
    Ones ^= Ones >> 2;
    Ones ^= Ones >> 1;
    const unsigned Bit8 = Ones & 1;
    return (uint16_t)((Value & 0xFF) | Bit8 << 8 | (Bit8 ^ 1) << 9);
}
