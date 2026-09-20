// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# Sim4k.h #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - How the emulated card lays 2160p over one link out, in symbols
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The 4K layout +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A 2160p signal over one 6G or 12G link is four 1080p links, and the card's ring holds
// two coded lines for each of their lines: the first the HANC sections of links 1 and 2
// and a video section, the second those of links 3 and 4. The raw line the channels
// carry is the eight streams of the four links word by word, the chrominance of links 4,
// 2, 3 and 1 and then their luminance. Of the video section each link takes whole pixel
// pairs in turn on a picture line, and its own half on a blanking line. This is the
// emulator's own reading of 0014, written from the layout rather than from the library's
// conversion, so that the suites check one against the other.
//
// Hanc is the symbols of one HANC section, Act those of one link's active part, half the
// video section's; Raw holds 4*Hanc + 2*Act symbols and each coded line 2*Hanc + 2*Act.

// Where symbol Index of the line of link Link, both from 0, lies in the raw line.
size_t Sim4k_RawAt(int Link, int Index);

// Splits a raw line into the two coded lines of the ring.
void Sim4k_Split(int Hanc, int Act, bool Blanking, const uint16_t* Raw, uint16_t* CodedA,
                 uint16_t* CodedB);

// Puts the two coded lines of the ring back together into a raw line.
void Sim4k_Merge(int Hanc, int Act, bool Blanking, const uint16_t* CodedA,
                 const uint16_t* CodedB, uint16_t* Raw);
