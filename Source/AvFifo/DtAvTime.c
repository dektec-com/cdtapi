// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtAvTime.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Times of day on the media clock, and RTP timestamps
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>

// CDTAPI includes
#include "DtAvTime.h"      // Interface being implemented.
#include "cdtapi_avfifo.h" // The public timing helpers.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Arithmetic +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The range of a 32-bit RTP timestamp.
#define RTP_RANGE (UINT64_C(1) << 32)

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Multiply -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The 128-bit product of A and B, from 32-bit halves.
//
static void Multiply(uint64_t A, uint64_t B, uint64_t* High, uint64_t* Low)
{
    uint64_t A0 = A & 0xFFFFFFFFu;
    uint64_t A1 = A >> 32;
    uint64_t B0 = B & 0xFFFFFFFFu;
    uint64_t B1 = B >> 32;

    uint64_t P00 = A0 * B0;
    uint64_t P01 = A0 * B1;
    uint64_t P10 = A1 * B0;
    uint64_t P11 = A1 * B1;

    uint64_t Middle = (P00 >> 32) + (P01 & 0xFFFFFFFFu) + (P10 & 0xFFFFFFFFu);
    *Low = (P00 & 0xFFFFFFFFu) | (Middle << 32);
    *High = P11 + (P01 >> 32) + (P10 >> 32) + (Middle >> 32);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvTime_MulAddDiv -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A dividend that fits 64 bits divides directly; a larger one bit by bit. The remainder
// stays below Div, but shifting it can carry out of 64 bits, and then it is above Div.
//
uint64_t DtAvTime_MulAddDiv(uint64_t A, uint64_t B, uint64_t Add, uint64_t Div)
{
    uint64_t High = 0;
    uint64_t Low = 0;
    Multiply(A, B, &High, &Low);
    Low += Add;
    if (Low < Add)
        High++;
    if (High == 0)
        return Low / Div;

    uint64_t Quotient = 0;
    uint64_t Remainder = 0;
    for (int Bit = 127; Bit >= 0; Bit--)
    {
        bool Carry = (Remainder >> 63) != 0;
        uint64_t Next = Bit >= 64 ? High >> (Bit - 64) & 1 : Low >> Bit & 1;
        Remainder = Remainder << 1 | Next;
        if (Carry || Remainder >= Div)
        {
            Remainder -= Div;
            if (Bit < 64)
                Quotient |= UINT64_C(1) << Bit;
        }
    }
    return Quotient;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvTime_ToNs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint64_t DtAvTime_ToNs(const DtTimeOfDay* ToD)
{
    return (uint64_t)ToD->Seconds * DT_AV_NS_PER_SEC + ToD->Nanoseconds;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvTime_FromNs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtTimeOfDay DtAvTime_FromNs(uint64_t Ns)
{
    DtTimeOfDay ToD;

    ToD.Seconds = (uint32_t)(Ns / DT_AV_NS_PER_SEC);
    ToD.Nanoseconds = (uint32_t)(Ns % DT_AV_NS_PER_SEC);
    return ToD;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Conversions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvTime_Align -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// TimeConversion::AlignTimestamp: the number of whole periods, rounded, and their time,
// rounded.
//
uint64_t DtAvTime_Align(uint64_t Ns, int Numerator, int Denominator)
{
    if (Numerator <= 0 || Denominator <= 0)
        return Ns;
    uint64_t Period = (uint64_t)Denominator * DT_AV_NS_PER_SEC;
    uint64_t NumPeriods = DtAvTime_MulAddDiv(Ns, (uint64_t)Numerator, Period / 2, Period);
    return DtAvTime_MulAddDiv(NumPeriods, Period, (uint64_t)Numerator / 2,
                              (uint64_t)Numerator);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvTime_Tod2Rtp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// TimeConversion::Tod2Rtp. A video time on the grid of a fractional frame rate lies a
// quarter, a half or three quarters of a tick after the timestamp it has, and its
// rounding to a nanosecond moves it by half a nanosecond: adding an eighth of a tick
// before truncating gives the timestamp for each of those. Audio rounds.
//
uint32_t DtAvTime_Tod2Rtp(int RtpRate, uint64_t Ns)
{
    if (RtpRate <= 0)
        return 0;
    uint64_t Fraction = RtpRate == DT_AV_VIDEO_RTP_RATE ? 8 : 2;
    uint64_t Ticks = DtAvTime_MulAddDiv(Ns, (uint64_t)RtpRate,
                                        DT_AV_NS_PER_SEC / Fraction, DT_AV_NS_PER_SEC);
    return (uint32_t)Ticks;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvTime_Rtp2Tod -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// TimeConversion::Rtp2Tod: the wraps of the timestamp up to ApproxNs, one more or one
// less when RtpTime lies half a wrap or more from ApproxNs's own timestamp, and the time
// of the full count of ticks, truncated.
//
uint64_t DtAvTime_Rtp2Tod(int RtpRate, uint32_t RtpTime, uint64_t ApproxNs)
{
    if (RtpRate <= 0)
        return ApproxNs;
    uint64_t NumWraps =
        DtAvTime_MulAddDiv(ApproxNs, (uint64_t)RtpRate, 0, RTP_RANGE * DT_AV_NS_PER_SEC);
    int64_t Diff = (int64_t)RtpTime - (int64_t)DtAvTime_Tod2Rtp(RtpRate, ApproxNs);
    if (Diff <= -(int64_t)(RTP_RANGE / 2))
        NumWraps++;
    else if (Diff >= (int64_t)(RTP_RANGE / 2) && NumWraps > 0)
        NumWraps--;
    uint64_t Ticks = NumWraps * RTP_RANGE + RtpTime;
    return DtAvTime_MulAddDiv(Ticks, DT_AV_NS_PER_SEC, 0, (uint64_t)RtpRate);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Timing helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Tod2Grid_Audio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtTimeOfDay Tod2Grid_Audio(const DtTimeOfDay* ToD, int SampleRate)
{
    if (ToD == NULL)
        return DtAvTime_FromNs(0);
    return DtAvTime_FromNs(DtAvTime_Align(DtAvTime_ToNs(ToD), SampleRate, 1));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Tod2Grid_Video -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtTimeOfDay Tod2Grid_Video(const DtTimeOfDay* ToD, const FrameRate* Rate)
{
    if (ToD == NULL)
        return DtAvTime_FromNs(0);
    if (Rate == NULL)
        return *ToD;
    return DtAvTime_FromNs(
        DtAvTime_Align(DtAvTime_ToNs(ToD), Rate->Numerator, Rate->Denominator));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Rtp2Tod_Audio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtTimeOfDay Rtp2Tod_Audio(uint32_t RtpTime, const DtTimeOfDay* ToD, int SampleRate)
{
    if (ToD == NULL)
        return DtAvTime_FromNs(0);
    return DtAvTime_FromNs(DtAvTime_Rtp2Tod(SampleRate, RtpTime, DtAvTime_ToNs(ToD)));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Rtp2Tod_Video -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtTimeOfDay Rtp2Tod_Video(uint32_t RtpTime, const DtTimeOfDay* ToD)
{
    return Rtp2Tod_Audio(RtpTime, ToD, DT_AV_VIDEO_RTP_RATE);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Tod2Rtp_Audio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint32_t Tod2Rtp_Audio(const DtTimeOfDay* ToD, int SampleRate)
{
    if (ToD == NULL)
        return 0;
    return DtAvTime_Tod2Rtp(SampleRate, DtAvTime_ToNs(ToD));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Tod2Rtp_Video -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint32_t Tod2Rtp_Video(const DtTimeOfDay* ToD)
{
    return Tod2Rtp_Audio(ToD, DT_AV_VIDEO_RTP_RATE);
}
