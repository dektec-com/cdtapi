// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSmpte352.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The fields of a SMPTE ST 352 payload identifier (VPID) - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <string.h>

// CDTAPI includes
#include "DtFrameProps.h" // The scan of a standard.
#include "DtSmpte352.h"   // Interface being implemented.
#include "DtVidStd.h"     // Frame rates and I/O standards.
#include "cdtapi.h"       // DTAPI_IOCONFIG_ codes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Signalling layout +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Where a payload puts the fields of the signalling. The standards differ:
//   - SD (SMPTE ST 352 Annex B.1) carries only the aspect ratio, 4:3 or 16:9, in byte 3
//     bit 7.
//   - 720p on HD (SMPTE ST 292-1 section 9.4) carries none of the fields.
//   - The others carry the transfer characteristic in byte 2 bits 5..4, ICtCp in byte 4
//     bit 4 and the range in byte 4's bit depth code. They put the colorimetry and the
//     aspect ratio in one of two ways:
//       - Split: 1080 lines on HD (ST 292-1 section 9.5), on 3G level B (ST 372
//         section 7.1) and 2160p on four level-B links (ST 425-5 section 7.2.3) put the
//         colorimetry's two bits in byte 3 bits 7 and 4, and the aspect ratio in bit 5.
//       - Plain: 1080 lines on 3G level A (ST 425-1 section 4.1.6.2), 2160p on 6G and 12G
//         (ST 2081-10 and ST 2082-10 section 4.3) and on four level-A links (ST 425-5
//         section 6.2.3) put the colorimetry in byte 3 bits 5..4, and the aspect ratio
//         in bit 7.
//

typedef enum Layout
{
    LAYOUT_UNKNOWN, // A payload the library does not know
    LAYOUT_SD,      // Only the aspect ratio
    LAYOUT_720,     // None of the fields
    LAYOUT_SPLIT,   // Colorimetry in byte 3 bits 7 and 4, aspect ratio in bit 5
    LAYOUT_PLAIN,   // Colorimetry in byte 3 bits 5..4, aspect ratio in bit 7
} Layout;

// The bits of the fields, in a VPID with byte 1 in the least significant bits.
#define TRANSFER_SHIFT 12       // Byte 2 bits 5..4
#define BYTE3_BIT(b) (16 + (b)) // Bit b of byte 3
#define ICTCP_BIT 0x10000000u   // Byte 4 bit 4
#define DEPTH_SHIFT 24          // Byte 4 bits 1..0, the bit depth
#define DEPTH_MASK 0x03000000u  // Those two bits
#define DEPTH_10 0x1u           // 10 bits, narrow range
#define DEPTH_10_FULL_HD 0x3u   // 10 bits, full range, in ST 292-1
#define DEPTH_10_FULL 0x0u      // 10 bits, full range, in the others
#define DEPTH_12_FULL 0x3u      // 12 bits, full range, in the others
#define COLOUR_VANC_CODE                                                                 \
    0x1u // Colorimetry code 1h: the Color VANC packet in ST                             \
         // 2081-10, ST 2082-10 and ST 425-5; reserved elsewhere

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LayoutOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns where payload identifier PayloadId puts the fields of the signalling.
//
static Layout LayoutOf(int PayloadId)
{
    switch (PayloadId)
    {
    case DT_S352_ID_S259:
        return LAYOUT_SD;
    case DT_S352_ID_S292_720:
        return LAYOUT_720;
    case DT_S352_ID_S292_1080:
    case DT_S352_ID_S425_1080_B:
    case DT_S352_ID_S425_5_2160_B:
        return LAYOUT_SPLIT;
    case DT_S352_ID_S425_1080_A:
    case DT_S352_ID_S425_5_2160_A:
    case DT_S352_ID_S2081_2160:
    case DT_S352_ID_S2082_2160:
        return LAYOUT_PLAIN;
    default:
        return LAYOUT_UNKNOWN;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Is2160 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns whether PayloadId is of 2160 lines. There colorimetry code 1h means the Color
// VANC packet, and the default colorimetry is BT.2020.
//
static bool Is2160(int PayloadId)
{
    return PayloadId == DT_S352_ID_S425_5_2160_A ||
           PayloadId == DT_S352_ID_S425_5_2160_B || PayloadId == DT_S352_ID_S2081_2160 ||
           PayloadId == DT_S352_ID_S2082_2160;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasFields -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns whether a layout carries all five fields.
//
static bool HasFields(Layout Where)
{
    return Where == LAYOUT_SPLIT || Where == LAYOUT_PLAIN;
}

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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Structure -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the VPID of VidStd without its signalling: byte 1, the scan bits and picture
// rate of byte 2, and the bit depth of byte 4. Bit 7 of byte 2 marks a progressive
// transport, and bit 6 a progressive picture.
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
static uint32_t Structure(int VidStd)
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
    return Vpid | RateCode(Num, Den) << 8 | DEPTH_10 << DEPTH_SHIFT;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_Make -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint32_t DtSmpte352_Make(int VidStd)
{
    DtSdiVideoSignalling Signalling;
    DtSmpte352_DefaultSignalling(VidStd, &Signalling);
    return DtSmpte352_MakeSignalling(VidStd, &Signalling);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_MakeSignalling -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Adds the signalling's bits to the structure, on the bits of the payload's layout. A
// field the layout does not carry adds nothing.
//
uint32_t DtSmpte352_MakeSignalling(int VidStd, const DtSdiVideoSignalling* Signalling)
{
    uint32_t Vpid = Structure(VidStd);
    const int PayloadId = DtSmpte352_PayloadId(Vpid);
    const Layout Where = LayoutOf(PayloadId);
    if (Vpid == 0 || Where == LAYOUT_720)
        return Vpid;
    if (Where == LAYOUT_SD)
        return Signalling->Aspect == DT_SDI_ASPECT_16X9 ? Vpid | 1u << BYTE3_BIT(7)
                                                        : Vpid;

    uint32_t Transfer = 0;
    if (Signalling->Transfer == DT_SDI_TRANSFER_HLG)
        Transfer = 1;
    else if (Signalling->Transfer == DT_SDI_TRANSFER_PQ)
        Transfer = 2;
    else if (Signalling->Transfer == DT_SDI_TRANSFER_UNSPECIFIED)
        Transfer = 3;
    Vpid |= Transfer << TRANSFER_SHIFT;

    uint32_t Colour = 0;
    if (Signalling->Colorimetry == DT_SDI_COLORIMETRY_BT2020)
        Colour = 2;
    else if (Signalling->Colorimetry == DT_SDI_COLORIMETRY_UNKNOWN)
        Colour = 3;
    const uint32_t Wide = Signalling->Aspect == DT_SDI_ASPECT_16X9 ? 1u : 0u;
    if (Where == LAYOUT_SPLIT)
        Vpid |= (Colour >> 1) << BYTE3_BIT(7) | (Colour & 1) << BYTE3_BIT(4) |
                Wide << BYTE3_BIT(5);
    else
        Vpid |= Colour << BYTE3_BIT(4) | Wide << BYTE3_BIT(7);

    if (Signalling->LumaChroma == DT_SDI_LUMACHROMA_ICTCP)
        Vpid |= ICTCP_BIT;
    if (Signalling->Range == DT_SDI_RANGE_FULL)
    {
        const uint32_t Full =
            PayloadId == DT_S352_ID_S292_1080 ? DEPTH_10_FULL_HD : DEPTH_10_FULL;
        Vpid = (Vpid & ~DEPTH_MASK) | Full << DEPTH_SHIFT;
    }
    return Vpid;
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_AspectRatio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtAspectRatio DtSmpte352_AspectRatio(uint32_t Vpid)
{
    DtSdiVideoSignalling Signalling;
    if (!DtSmpte352_ReadSignalling(Vpid, &Signalling))
        return DT_AR_UNKNOWN;
    if (Signalling.Aspect == DT_SDI_ASPECT_4X3)
        return DT_AR_4_3;
    return Signalling.Aspect == DT_SDI_ASPECT_16X9 ? DT_AR_16_9 : DT_AR_UNKNOWN;
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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Signalling +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The sizes at which a DtSdiVideoSignalling ends after each of its fields.
static const size_t FieldEnds[] = {
    offsetof(DtSdiVideoSignalling, Transfer) + sizeof(DtSdiTransfer),
    offsetof(DtSdiVideoSignalling, Colorimetry) + sizeof(DtSdiColorimetry),
    offsetof(DtSdiVideoSignalling, Aspect) + sizeof(DtSdiAspect),
    offsetof(DtSdiVideoSignalling, LumaChroma) + sizeof(DtSdiLumaChroma),
    offsetof(DtSdiVideoSignalling, Range) + sizeof(DtSdiRange),
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_IsSignallingSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtSmpte352_IsSignallingSize(int Size)
{
    for (size_t i = 0; i < sizeof(FieldEnds) / sizeof(FieldEnds[0]); i++)
    {
        if (Size > 0 && (size_t)Size == FieldEnds[i])
            return true;
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_TakeSignalling -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Copies the covered bytes over a whole signalling of _DEFAULT fields, and checks the
// copy. The enums are compared as int, so that a value outside them is seen.
//
DtapiResult DtSmpte352_TakeSignalling(const DtSdiVideoSignalling* In,
                                      DtSdiVideoSignalling* Out)
{
    DtSdiVideoSignalling Signalling;
    memset(&Signalling, 0, sizeof(Signalling));
    Signalling.Size = (int)sizeof(Signalling);
    if (In != NULL && In->Size != 0)
    {
        if (!DtSmpte352_IsSignallingSize(In->Size))
            return DTAPI_E_INVALID_ARG;
        const size_t First = offsetof(DtSdiVideoSignalling, Transfer);
        memcpy((uint8_t*)&Signalling + First, (const uint8_t*)In + First,
               (size_t)In->Size - First);
    }

    if ((int)Signalling.Transfer < (int)DT_SDI_TRANSFER_DEFAULT ||
        (int)Signalling.Transfer > (int)DT_SDI_TRANSFER_UNSPECIFIED ||
        (int)Signalling.Colorimetry < (int)DT_SDI_COLORIMETRY_DEFAULT ||
        (int)Signalling.Colorimetry > (int)DT_SDI_COLORIMETRY_UNKNOWN ||
        (int)Signalling.Aspect < (int)DT_SDI_ASPECT_DEFAULT ||
        (int)Signalling.Aspect > (int)DT_SDI_ASPECT_UNKNOWN ||
        (int)Signalling.LumaChroma < (int)DT_SDI_LUMACHROMA_DEFAULT ||
        (int)Signalling.LumaChroma > (int)DT_SDI_LUMACHROMA_ICTCP ||
        (int)Signalling.Range < (int)DT_SDI_RANGE_DEFAULT ||
        (int)Signalling.Range > (int)DT_SDI_RANGE_FULL)
        return DTAPI_E_INVALID_ARG;
    if (Signalling.Colorimetry == DT_SDI_COLORIMETRY_COLOR_VANC)
        return DTAPI_E_NOT_SUPPORTED;
    *Out = Signalling;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_GiveSignalling -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSmpte352_GiveSignalling(const DtSdiVideoSignalling* Signalling,
                               DtSdiVideoSignalling* Out)
{
    const size_t First = offsetof(DtSdiVideoSignalling, Transfer);
    memcpy((uint8_t*)Out + First, (const uint8_t*)Signalling + First,
           (size_t)Out->Size - First);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_DefaultSignalling -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The defaults follow the frame size, as CDTAPI's ST 2110 flows take them: BT.601 in SD,
// BT.709 in HD and 3G, BT.2020 in 2160p; SDR, Y'CbCr and narrow range everywhere; 4:3 in
// SD and 16:9 above it.
//
void DtSmpte352_DefaultSignalling(int VidStd, DtSdiVideoSignalling* Signalling)
{
    const int PayloadId = DtSmpte352_PayloadId(Structure(VidStd));
    memset(Signalling, 0, sizeof(*Signalling));
    Signalling->Size = (int)sizeof(*Signalling);
    Signalling->Transfer = DT_SDI_TRANSFER_SDR;
    Signalling->LumaChroma = DT_SDI_LUMACHROMA_YCBCR;
    Signalling->Range = DT_SDI_RANGE_NARROW;
    if (PayloadId == DT_S352_ID_S259)
    {
        Signalling->Colorimetry = DT_SDI_COLORIMETRY_BT601;
        Signalling->Aspect = DT_SDI_ASPECT_4X3;
        return;
    }
    Signalling->Aspect = DT_SDI_ASPECT_16X9;
    Signalling->Colorimetry =
        Is2160(PayloadId) ? DT_SDI_COLORIMETRY_BT2020 : DT_SDI_COLORIMETRY_BT709;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Carries... -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether a payload of layout Where can carry a value of a field. _DEFAULT and the value
// of the standard's own signalling, Default, always fit, also where the layout has no
// field for it and implies the value.
//
static bool CarriesTransfer(Layout Where, DtSdiTransfer Value,
                            const DtSdiVideoSignalling* Default)
{
    return Value == DT_SDI_TRANSFER_DEFAULT || Value == Default->Transfer ||
           HasFields(Where);
}

static bool CarriesColorimetry(Layout Where, DtSdiColorimetry Value,
                               const DtSdiVideoSignalling* Default)
{
    if (Value == DT_SDI_COLORIMETRY_DEFAULT || Value == Default->Colorimetry)
        return true;
    return HasFields(Where) &&
           (Value == DT_SDI_COLORIMETRY_BT709 || Value == DT_SDI_COLORIMETRY_BT2020 ||
            Value == DT_SDI_COLORIMETRY_UNKNOWN);
}

static bool CarriesAspect(Layout Where, DtSdiAspect Value,
                          const DtSdiVideoSignalling* Default)
{
    if (Value == DT_SDI_ASPECT_DEFAULT || Value == Default->Aspect)
        return true;
    if (Where == LAYOUT_SD)
        return Value == DT_SDI_ASPECT_16X9;
    return HasFields(Where) && Value == DT_SDI_ASPECT_UNKNOWN;
}

static bool CarriesOther(Layout Where, int Value, int DefaultValue)
{
    return Value == 0 || Value == DefaultValue || HasFields(Where);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_ResolveSignalling -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Each field goes the same way: Frame's value when set, which must fit; else Builder's
// when set and it fits; else the default.
//
DtapiResult DtSmpte352_ResolveSignalling(int VidStd, const DtSdiVideoSignalling* Frame,
                                         const DtSdiVideoSignalling* Builder,
                                         DtSdiVideoSignalling* Out)
{
    DtSdiVideoSignalling Default;
    DtSmpte352_DefaultSignalling(VidStd, &Default);
    const Layout Where = LayoutOf(DtSmpte352_PayloadId(Structure(VidStd)));

    if (!CarriesTransfer(Where, Frame->Transfer, &Default) ||
        !CarriesColorimetry(Where, Frame->Colorimetry, &Default) ||
        !CarriesAspect(Where, Frame->Aspect, &Default) ||
        !CarriesOther(Where, (int)Frame->LumaChroma, (int)Default.LumaChroma) ||
        !CarriesOther(Where, (int)Frame->Range, (int)Default.Range))
        return DTAPI_E_NOT_SUPPORTED;

    DtSdiVideoSignalling Signalling = Default;
    if (Frame->Transfer != DT_SDI_TRANSFER_DEFAULT)
        Signalling.Transfer = Frame->Transfer;
    else if (Builder->Transfer != DT_SDI_TRANSFER_DEFAULT &&
             CarriesTransfer(Where, Builder->Transfer, &Default))
        Signalling.Transfer = Builder->Transfer;

    if (Frame->Colorimetry != DT_SDI_COLORIMETRY_DEFAULT)
        Signalling.Colorimetry = Frame->Colorimetry;
    else if (Builder->Colorimetry != DT_SDI_COLORIMETRY_DEFAULT &&
             CarriesColorimetry(Where, Builder->Colorimetry, &Default))
        Signalling.Colorimetry = Builder->Colorimetry;

    if (Frame->Aspect != DT_SDI_ASPECT_DEFAULT)
        Signalling.Aspect = Frame->Aspect;
    else if (Builder->Aspect != DT_SDI_ASPECT_DEFAULT &&
             CarriesAspect(Where, Builder->Aspect, &Default))
        Signalling.Aspect = Builder->Aspect;

    if (Frame->LumaChroma != DT_SDI_LUMACHROMA_DEFAULT)
        Signalling.LumaChroma = Frame->LumaChroma;
    else if (Builder->LumaChroma != DT_SDI_LUMACHROMA_DEFAULT &&
             CarriesOther(Where, (int)Builder->LumaChroma, (int)Default.LumaChroma))
        Signalling.LumaChroma = Builder->LumaChroma;

    if (Frame->Range != DT_SDI_RANGE_DEFAULT)
        Signalling.Range = Frame->Range;
    else if (Builder->Range != DT_SDI_RANGE_DEFAULT &&
             CarriesOther(Where, (int)Builder->Range, (int)Default.Range))
        Signalling.Range = Builder->Range;

    *Out = Signalling;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSmpte352_ReadSignalling -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads each field from the bits of the payload's layout. Colorimetry code 1h is the
// Color VANC packet in 2160p, and unknown in 1080 lines, where it is reserved. The bit
// depth codes of ST 292-1 are 8 bits, 10 bits, reserved and 10 bits full range; those
// of the others are 10 bits full range, 10 bits, 12 bits and 12 bits full range.
//
bool DtSmpte352_ReadSignalling(uint32_t Vpid, DtSdiVideoSignalling* Signalling)
{
    const int PayloadId = DtSmpte352_PayloadId(Vpid);
    const Layout Where = LayoutOf(PayloadId);
    memset(Signalling, 0, sizeof(*Signalling));
    Signalling->Size = (int)sizeof(*Signalling);
    Signalling->LumaChroma = DT_SDI_LUMACHROMA_YCBCR;
    Signalling->Range = DT_SDI_RANGE_NARROW;
    if (Where == LAYOUT_UNKNOWN)
    {
        Signalling->Transfer = DT_SDI_TRANSFER_UNSPECIFIED;
        Signalling->Colorimetry = DT_SDI_COLORIMETRY_UNKNOWN;
        Signalling->Aspect = DT_SDI_ASPECT_UNKNOWN;
        return false;
    }

    Signalling->Transfer = DT_SDI_TRANSFER_SDR;
    if (Where == LAYOUT_SD)
    {
        Signalling->Colorimetry = DT_SDI_COLORIMETRY_BT601;
        Signalling->Aspect =
            (Vpid >> BYTE3_BIT(7) & 1) != 0 ? DT_SDI_ASPECT_16X9 : DT_SDI_ASPECT_4X3;
        return true;
    }
    if (Where == LAYOUT_720)
    {
        Signalling->Colorimetry = DT_SDI_COLORIMETRY_BT709;
        Signalling->Aspect = DT_SDI_ASPECT_16X9;
        return true;
    }

    static const DtSdiTransfer Transfers[4] = {DT_SDI_TRANSFER_SDR, DT_SDI_TRANSFER_HLG,
                                               DT_SDI_TRANSFER_PQ,
                                               DT_SDI_TRANSFER_UNSPECIFIED};
    Signalling->Transfer = Transfers[Vpid >> TRANSFER_SHIFT & 0x3];

    uint32_t Colour;
    bool Wide;
    if (Where == LAYOUT_SPLIT)
    {
        Colour = (Vpid >> BYTE3_BIT(7) & 1) << 1 | (Vpid >> BYTE3_BIT(4) & 1);
        Wide = (Vpid >> BYTE3_BIT(5) & 1) != 0;
    }
    else
    {
        Colour = Vpid >> BYTE3_BIT(4) & 0x3;
        Wide = (Vpid >> BYTE3_BIT(7) & 1) != 0;
    }
    if (Colour == 0)
        Signalling->Colorimetry = DT_SDI_COLORIMETRY_BT709;
    else if (Colour == 2)
        Signalling->Colorimetry = DT_SDI_COLORIMETRY_BT2020;
    else if (Colour == COLOUR_VANC_CODE && Is2160(PayloadId))
        Signalling->Colorimetry = DT_SDI_COLORIMETRY_COLOR_VANC;
    else
        Signalling->Colorimetry = DT_SDI_COLORIMETRY_UNKNOWN;
    Signalling->Aspect = Wide ? DT_SDI_ASPECT_16X9 : DT_SDI_ASPECT_UNKNOWN;

    if ((Vpid & ICTCP_BIT) != 0)
        Signalling->LumaChroma = DT_SDI_LUMACHROMA_ICTCP;
    const uint32_t Depth = (Vpid & DEPTH_MASK) >> DEPTH_SHIFT;
    const bool Full = PayloadId == DT_S352_ID_S292_1080
                          ? Depth == DEPTH_10_FULL_HD
                          : Depth == DEPTH_10_FULL || Depth == DEPTH_12_FULL;
    if (Full)
        Signalling->Range = DT_SDI_RANGE_FULL;
    return true;
}
