// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiBuilder.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The builder: puts SDI frames together from an image, audio and ancillary data
//
// SPDX-License-Identifier: BSD-3-Clause
//
// For now a stub: the builder exists, and building returns DTAPI_E_NOT_SUPPORTED; plan
// 0032 fills it in.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h" // Allocation seam.
#include "DtSdiView.h"    // The frame a call reads or writes.
#include "cdtapi_sdi.h"   // Interface being implemented.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- State -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

struct DtSdiBuilder
{
    int FrameCount; // Frames built, for the audio cadence
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiBuilder_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtSdiBuilder* DtSdiBuilder_Alloc(void)
{
    DtSdiBuilder* Builder = (DtSdiBuilder*)DtAlloc_Malloc(sizeof(DtSdiBuilder));
    if (Builder == NULL)
        return NULL;
    memset(Builder, 0, sizeof(*Builder));
    return Builder;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiBuilder_Build -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSdiBuilder_Build(DtSdiBuilder* Builder, DtSdiView* Frame,
                               const DtSdiImage* Image, DtSdiAudio* Audio,
                               const DtSdiAncData* Anc)
{
    (void)Image;
    (void)Audio;
    (void)Anc;
    if (Builder == NULL || Frame == NULL)
        return DTAPI_E_INVALID_ARG;
    if (!Frame->HasFrame || Frame->Holder != NULL)
        return DTAPI_E_STATE;
    return DTAPI_E_NOT_SUPPORTED;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiBuilder_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSdiBuilder_Free(DtSdiBuilder* Builder)
{
    DtAlloc_Free(Builder);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiBuilder_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiBuilder_Freep(DtSdiBuilder** Builder)
{
    if (Builder == NULL)
        return;
    DtSdiBuilder_Free(*Builder);
    *Builder = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiBuilder_GetNumAudioSamples -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtSdiBuilder_GetNumAudioSamples(const DtSdiBuilder* Builder, int VidStd,
                                            int FrameNumber, int* NumSamples)
{
    (void)VidStd;
    (void)FrameNumber;
    if (Builder == NULL || NumSamples == NULL)
        return DTAPI_E_INVALID_ARG;
    *NumSamples = 0;
    return DTAPI_E_NOT_SUPPORTED;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiBuilder_SetWorkerPool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSdiBuilder_SetWorkerPool(DtSdiBuilder* Builder, DtWorkerPool* Pool,
                                       int NumThreads)
{
    (void)Pool;
    if (Builder == NULL || NumThreads < 0)
        return DTAPI_E_INVALID_ARG;
    return DTAPI_E_NOT_SUPPORTED;
}
