// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtlVidStd.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Video standard knowledge shared inside the library
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DTL_VID_STD_H
#define CDTAPILITE_DTL_VID_STD_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Link standards +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// How a 4K picture is carried. CDTAPI.h does not define these, so its users pass the
// numbers directly; the values are DTAPI's (DTAPI.h.tpl:4380-4384) and are kept private
// here rather than added to the public header.
//

#define DTL_VIDLNK_NONE -1        // Not a multi-link standard
#define DTL_VIDLNK_4K_SMPTE425 0  // Four 3G links, SMPTE 425 level A
#define DTL_VIDLNK_4K_SMPTE425B 1 // Four 3G links, SMPTE 425 annex B
#define DTL_VIDLNK_4K_SMPTE2081 2 // One 6G link
#define DTL_VIDLNK_4K_SMPTE2082 3 // One 12G link

// True for the eleven 2160p standards, as HdSdiUtil::Is4k in DTAPI.
bool DtlVidStdIs4k(int VidStd);

#endif // CDTAPILITE_DTL_VID_STD_H
