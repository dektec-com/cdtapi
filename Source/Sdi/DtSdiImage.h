// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiImage.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The images of SDI frames: what the library uses besides the API
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>

// CDTAPI includes
#include "DtSdiGeometry.h" // The image's size.
#include "DtSdiVec.h"      // The conversions.
#include "cdtapi_sdi.h"    // The image.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Images +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Checks that Image can take an image of Geo: a known format and arrangement of the
// fields, every plane the format needs, and strides large enough.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_FORMAT  the format or the fields are _NONE or unknown
//   DTAPI_E_NOT_SUPPORTED   the fields are DT_SDI_FIELDS_SEPARATE
//   DTAPI_E_INVALID_ARG     a plane is NULL, or its stride too small
DtapiResult DtSdiImage_Check(const DtSdiImage* Image, const DtSdiGeometry* Geo);

// Reads line Line (from 0) of an image of Geo from Image, in its format, into Symbols:
// Cb, Y, Cr, Y and so on, 2 * Geo->Width of them, each limited to 4..1019, as timing
// references keep 0 to 3 and 1020 to 1023. 8-bit samples get two zero bits below.
// Image must have passed DtSdiImage_Check. Vec converts.
void DtSdiImage_GetLine(const DtSdiImage* Image, const DtSdiGeometry* Geo, int Line,
                        uint16_t* Symbols, const DtSdiVec* Vec);

// Writes line Line (from 0) of an image of Geo into Image, in its format, from the
// line's symbols: Cb, Y, Cr, Y and so on, 2 * Geo->Width of them, each a value from 0 to
// 1023. Image must have passed DtSdiImage_Check. Vec converts.
void DtSdiImage_PutLine(const DtSdiImage* Image, const DtSdiGeometry* Geo, int Line,
                        const uint16_t* Symbols, const DtSdiVec* Vec);
