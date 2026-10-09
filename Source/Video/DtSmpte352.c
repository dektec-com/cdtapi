// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSmpte352.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The fields of a SMPTE ST 352 payload identifier (VPID) - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>

// CDTAPI includes
#include "DtFrameProps.h" // The scan of a standard.
#include "DtSmpte352.h"   // Interface being implemented.
#include "DtVidStd.h"     // Frame rates and I/O standards.
#include "cdtapi.h"       // DTAPI_IOCONFIG_ codes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+= Making a payload identifier +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RateCode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns the picture rate code for a frame rate of Num / Den, as byte 2 holds it in
// bits 3..0. Returns 0 for a rate that is not in the table.
//
static uint32_t RateCode(int Num, int Den)
{
    static const struct
    {
        int Num, Den;
        uint32_t Code;
    } Codes[] = {{24000, 1001, 0x2}, {24, 1, 0x3}, {25, 1, 0x5},       {30000, 1001, 0x6},
                 {30, 1, 0x7},       {50, 1, 0x9}, {60000, 1001, 0xA}, {60, 1, 0xB}};
    for (size_t i = 0; i < sizeof(Codes) / sizeof(Codes[0]); i++)
    {
        if (Codes[i].Num == Num && Codes[i].Den == Den)
            return Codes[i].Code;
    }
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_Make -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Chooses byte 1 and the scan bits of byte 2 by payload. Bit 7 of byte 2 marks a
// progressive transport, and bit 6 a progressive picture.
// 1. SD: 81, with an interlaced transport and picture.
// 2. 720p: 84, with a progressive picture. The transport bit stays 0, because SMPTE
//    ST 292 does not mark it.
// 3. 1080 lines: 85, interlaced, PsF or progressive. PsF sets only the picture bit.
// 4. 3G level A: 89, progressive.
// 5. 3G level B: 8A, with a progressive picture on an interlaced transport (SMPTE
//    ST 372).
// 6. 2160p: C0 on 6G or CE on 12G, progressive.
// Byte 4 is 01, which means 10 bits, and link A on level B.
//
uint32_t DtSmpte352_Make(int VidStd)
{
    const DtVidStdEntry* Info = DtVidStd_Find(VidStd);
    DtFrameProps Props;
    if (Info == NULL || (Info->IsLevelB && DtVidStd_Is4k(VidStd)) ||
        !DtFrameProps_Init(&Props, VidStd))
        return 0;

    uint32_t Vpid;
    if (Info->IsLevelB)
        Vpid = 0x408A;
    else if (DtVidStd_Is4k(VidStd))
        Vpid = Info->IoStd == DTAPI_IOCONFIG_12GSDI ? 0xC0CE : 0xC0C0;
    else if (DtFrameProps_IsSd(&Props))
        Vpid = 0x0081;
    else if (Info->NumLines == 750)
        Vpid = 0x4084;
    else if (DtFrameProps_Is3g(&Props))
        Vpid = 0xC089;
    else if (DtFrameProps_IsPsF(&Props))
        Vpid = 0x4085;
    else if (DtFrameProps_IsInterlaced(&Props))
        Vpid = 0x0085;
    else
        Vpid = 0xC085;

    int Num = 0;
    int Den = 0;
    DtVidStd_FrameRate(VidStd, &Num, &Den);
    return Vpid | RateCode(Num, Den) << 8 | 0x01000000u;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Payload fields +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_PayloadId -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads byte 1, bits 7..0.
//
int DtSmpte352_PayloadId(uint32_t Vpid)
{
    return (int)(Vpid & 0xFF);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_PictureRate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Decodes the rate code in byte 2, bits 3..0.
//
void DtSmpte352_PictureRate(uint32_t Vpid, int* Num, int* Den)
{
    *Num = 0;
    *Den = 0;

    switch ((Vpid >> 8) & 0xF)
    {
    case 0x2:
        *Num = 24000;
        *Den = 1001;
        break;
    case 0x3:
        *Num = 24;
        *Den = 1;
        break;
    case 0x4:
        *Num = 48000;
        *Den = 1001;
        break;
    case 0x5:
        *Num = 25;
        *Den = 1;
        break;
    case 0x6:
        *Num = 30000;
        *Den = 1001;
        break;
    case 0x7:
        *Num = 30;
        *Den = 1;
        break;
    case 0x8:
        *Num = 48;
        *Den = 1;
        break;
    case 0x9:
        *Num = 50;
        *Den = 1;
        break;
    case 0xA:
        *Num = 60000;
        *Den = 1001;
        break;
    case 0xB:
        *Num = 60;
        *Den = 1;
        break;
    default:
        break;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_IsInterlacedTransport -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads byte 2, bit 7, which is 0 for an interlaced transport.
//
bool DtSmpte352_IsInterlacedTransport(uint32_t Vpid)
{
    return (Vpid & 0x8000) == 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_IsInterlacedStructure -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads byte 2, bit 6, which is 0 for an interlaced picture.
//
bool DtSmpte352_IsInterlacedStructure(uint32_t Vpid)
{
    return (Vpid & 0x4000) == 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_Is16x9 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads byte 3, bit 7.
//
bool DtSmpte352_Is16x9(uint32_t Vpid)
{
    return ((Vpid >> 23) & 0x1) != 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_LinkNumber -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the link number from byte 4. For 2160p on 3G level B links, byte 4 holds a
// channel number instead. Two channels share a link, so the channel is divided by two.
//
int DtSmpte352_LinkNumber(uint32_t Vpid)
{
    switch (DtSmpte352_PayloadId(Vpid))
    {
    case DT_S352_ID_S425_5_2160_A:
        return (int)((Vpid >> 30) & 0x3);
    case DT_S352_ID_S425_5_2160_B:
        return (int)((Vpid >> 29) & 0x7) / 2;
    case DT_S352_ID_S2081_2160:
    case DT_S352_ID_S2082_2160:
        return (int)((Vpid >> 29) & 0x7);
    default: // Single-link payloads, including 3G level B with 1080 lines.
        return 0;
    }
}
