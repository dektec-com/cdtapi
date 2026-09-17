// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSmpte352.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - The fields of a SMPTE ST 352 payload identifier (VPID) - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "DtSmpte352.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Payload fields +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_PayloadId -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Byte 1, bits 7..0.
//
int DtSmpte352_PayloadId(uint32_t Vpid)
{
    return (int)(Vpid & 0xFF);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_PictureRate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Byte 2, bits 3..0.
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
// Byte 2, bit 7: 0 for an interlaced transport.
//
bool DtSmpte352_IsInterlacedTransport(uint32_t Vpid)
{
    return (Vpid & 0x8000) == 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_IsInterlacedStructure -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Byte 2, bit 6: 0 for an interlaced picture.
//
bool DtSmpte352_IsInterlacedStructure(uint32_t Vpid)
{
    return (Vpid & 0x4000) == 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_Is16x9 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Byte 3, bit 7.
//
bool DtSmpte352_Is16x9(uint32_t Vpid)
{
    return ((Vpid >> 23) & 0x1) != 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_LinkNumber -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Byte 4 holds the link, or for 3G level B the channel, of which two share a link.
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
    default: // Single-link payloads, 3G level B at 1080 lines included.
        return 0;
    }
}
