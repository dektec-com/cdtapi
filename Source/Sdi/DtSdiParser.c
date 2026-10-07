// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiParser.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The parser: takes SDI frames apart into an image, audio and ancillary data
//
// SPDX-License-Identifier: BSD-3-Clause
//
// For now a stub: the parser exists and keeps its settings, and parsing returns
// DTAPI_E_NOT_SUPPORTED; plan 0032 fills it in.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h" // Allocation seam.
#include "DtSdiView.h"    // The frame a call reads or writes.
#include "cdtapi_sdi.h"   // Interface being implemented.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- State -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

struct DtSdiParser
{
    bool AudioChecks;           // Check the BCH code and checksum of the audio packets
    DtSdiAncFilter* AncFilters; // The packets to list; NULL for the default
    int NumAncFilters;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiParser_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtSdiParser* DtSdiParser_Alloc(void)
{
    DtSdiParser* Parser = (DtSdiParser*)DtAlloc_Malloc(sizeof(DtSdiParser));
    if (Parser == NULL)
        return NULL;
    memset(Parser, 0, sizeof(*Parser));
    return Parser;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiParser_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiParser_Free(DtSdiParser* Parser)
{
    if (Parser == NULL)
        return;
    DtAlloc_Free(Parser->AncFilters);
    DtAlloc_Free(Parser);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiParser_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSdiParser_Freep(DtSdiParser** Parser)
{
    if (Parser == NULL)
        return;
    DtSdiParser_Free(*Parser);
    *Parser = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiParser_Parse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtSdiParser_Parse(DtSdiParser* Parser, const DtSdiView* Frame,
                              DtSdiImage* Image, DtSdiAudio* Audio, DtSdiAncData* Anc)
{
    (void)Image;
    (void)Audio;
    (void)Anc;
    if (Parser == NULL || Frame == NULL)
        return DTAPI_E_INVALID_ARG;
    if (!Frame->HasFrame)
        return DTAPI_E_STATE;
    return DTAPI_E_NOT_SUPPORTED;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiParser_SetAncFilter -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Checks every filter before it replaces the old ones, so that a failure leaves the
// parser as it was.
//
DtapiResult DtSdiParser_SetAncFilter(DtSdiParser* Parser, const DtSdiAncFilter* Filters,
                                     int NumFilters)
{
    if (Parser == NULL || NumFilters < 0 || (Filters == NULL && NumFilters > 0))
        return DTAPI_E_INVALID_ARG;
    for (int i = 0; i < NumFilters; i++)
    {
        const DtSdiAncFilter* Filter = &Filters[i];
        if (Filter->Space != DT_SDI_ANC_SPACE_HANC &&
            Filter->Space != DT_SDI_ANC_SPACE_VANC &&
            Filter->Space != DT_SDI_ANC_SPACE_BOTH)
        {
            return DTAPI_E_INVALID_ARG;
        }
        if (Filter->FirstLine < 0 || Filter->LastLine < 0 ||
            (Filter->LastLine != 0 && Filter->LastLine < Filter->FirstLine))
        {
            return DTAPI_E_INVALID_ARG;
        }
    }

    DtSdiAncFilter* Copy = NULL;
    if (NumFilters > 0)
    {
        Copy = (DtSdiAncFilter*)DtAlloc_Malloc((size_t)NumFilters * sizeof(*Copy));
        if (Copy == NULL)
            return DTAPI_E_OUT_OF_MEM;
        memcpy(Copy, Filters, (size_t)NumFilters * sizeof(*Copy));
    }
    DtAlloc_Free(Parser->AncFilters);
    Parser->AncFilters = Copy;
    Parser->NumAncFilters = NumFilters;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiParser_SetAudioChecks -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSdiParser_SetAudioChecks(DtSdiParser* Parser, bool Check)
{
    if (Parser == NULL)
        return DTAPI_E_INVALID_ARG;
    Parser->AudioChecks = Check;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiParser_SetWorkerPool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtSdiParser_SetWorkerPool(DtSdiParser* Parser, DtWorkerPool* Pool,
                                      int NumThreads)
{
    (void)Pool;
    if (Parser == NULL || NumThreads < 0)
        return DTAPI_E_INVALID_ARG;
    return DTAPI_E_NOT_SUPPORTED;
}
