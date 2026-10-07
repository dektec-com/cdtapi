// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiImage.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The images of SDI frames: their sizes in each pixel format
//
// SPDX-License-Identifier: BSD-3-Clause
//
// For now a stub that checks its arguments and returns DTAPI_E_NOT_SUPPORTED; plan 0032
// fills it in.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "cdtapi_sdi.h" // Interface being implemented.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiImage_GetSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSdiImage_GetSize(int VidStd, DtSdiPixelFormat Format, int* Width,
                               int* Height, int MinStrides[3])
{
    (void)VidStd;
    (void)Format;
    if (Width != NULL)
        *Width = 0;
    if (Height != NULL)
        *Height = 0;
    if (MinStrides != NULL)
        MinStrides[0] = MinStrides[1] = MinStrides[2] = 0;
    return DTAPI_E_NOT_SUPPORTED;
}
