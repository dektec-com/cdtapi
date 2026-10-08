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
#include "DtSdiConv.h"     // The conversions.
#include "DtSdiGeometry.h" // The image's size.
#include "cdtapi_sdi.h"    // The image.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Images +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Checks that Image can hold an image of Geo. The format and the arrangement of the
// fields must be known, and every plane the format needs must be there. Each of those
// planes must have a stride large enough for a line.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_FORMAT  the format or the fields are _NONE or unknown
//   DTAPI_E_NOT_SUPPORTED   the fields are DT_SDI_FIELDS_SEPARATE
//   DTAPI_E_INVALID_ARG     a plane is NULL, or its stride is too small
DtapiResult DtSdiImage_Check(const DtSdiImage* Image, const DtSdiGeometry* Geo);

// Converts line Line (from 0) of Image, an image of Geo, from its pixel format into
// symbols. Symbols receives 2 * Geo->Width symbols, in the order Cb, Y, Cr, Y and so on.
// Each symbol is limited to 4..1019, because timing references keep 0 to 3 and 1020 to
// 1023. An 8-bit sample gets two zero bits below it. Image must have passed
// DtSdiImage_Check. Conv does the conversion.
void DtSdiImage_GetLine(const DtSdiImage* Image, const DtSdiGeometry* Geo, int Line,
                        uint16_t* Symbols, const DtSdiConv* Conv);

// Converts 2 * Geo->Width symbols, in the order Cb, Y, Cr, Y and so on, into line Line
// (from 0) of Image, an image of Geo, in its pixel format. Each symbol is a value from 0
// to 1023. In v210, the bytes after the line's words are set to 0, up to the least
// stride. Image must have passed DtSdiImage_Check. Conv does the conversion.
void DtSdiImage_PutLine(const DtSdiImage* Image, const DtSdiGeometry* Geo, int Line,
                        const uint16_t* Symbols, const DtSdiConv* Conv);
