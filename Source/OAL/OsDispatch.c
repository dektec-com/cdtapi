// #*#*#*#*#*#*#*#*#*#*#*#*#*#* OsDispatch.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Chooses between the real driver and the emulated device
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "Core/DtAlloc.h"       // Allocation seam.
#include "OsAbstractionLayer.h" // Interface being implemented.
#include "OsBackend.h"          // Backend interface.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Handle +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

struct OsDrv
{
    const OsBackend* Backend;
    void* State;
    bool IsEmulated;
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Device +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsDrv_Open -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
OsDrv* OsDrv_Open(int Index)
{
    const OsBackend* Backend;
    bool Emulated;
    void* State;
    OsDrv* Drv;

    if (Index < 0 || Index >= DT_MAX_DEVICES)
        return NULL;

    // The emulator is checked before real hardware is enumerated, so that an application
    // needs no change and no new call to use it.
    Emulated = OsSim_IsRequested();
    Backend = Emulated ? OsSim_Backend() : OsPlatform_Backend();

    // A build without the platform backend, asked for real hardware.
    if (Backend == NULL)
        return NULL;

    State = Backend->Open(Index);
    if (State == NULL)
        return NULL;

    Drv = (OsDrv*)DtAlloc_Malloc(sizeof(OsDrv));
    if (Drv == NULL)
    {
        Backend->Close(State);
        return NULL;
    }

    Drv->Backend = Backend;
    Drv->State = State;
    Drv->IsEmulated = Emulated;
    return Drv;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsDrv_Close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void OsDrv_Close(OsDrv* Drv)
{
    if (Drv == NULL)
        return;

    Drv->Backend->Close(Drv->State);
    DtAlloc_Free(Drv);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsDrv_IsEmulated -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool OsDrv_IsEmulated(const OsDrv* Drv)
{
    return Drv != NULL && Drv->IsEmulated;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Control +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsDrv_IoCtl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int OsDrv_IoCtl(OsDrv* Drv, uint32_t Code, const void* In, size_t InSize, void* Out,
                size_t* OutSize, uint32_t* DrvStatus)
{
    uint32_t Ignored;

    if (DrvStatus == NULL)
        DrvStatus = &Ignored;

    *DrvStatus = 0;

    // Every command starts with a header, so a request without input never reaches a
    // driver.
    if (Drv == NULL || In == NULL || InSize == 0)
        return OS_IOCTL_COMMUNICATION;

    return Drv->Backend->IoCtl(Drv->State, Code, In, InSize, Out, OutSize, DrvStatus);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Memory +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsDrv_MapMemory -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void* OsDrv_MapMemory(OsDrv* Drv, uint64_t Offset, size_t Size)
{
    if (Drv == NULL || Size == 0 || Drv->Backend->MapMemory == NULL)
        return NULL;

    return Drv->Backend->MapMemory(Drv->State, Offset, Size);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsDrv_UnmapMemory -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void OsDrv_UnmapMemory(OsDrv* Drv, void* Address, size_t Size)
{
    if (Drv == NULL || Address == NULL || Drv->Backend->UnmapMemory == NULL)
        return;

    Drv->Backend->UnmapMemory(Drv->State, Address, Size);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsDrv_LastError -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint32_t OsDrv_LastError(const OsDrv* Drv)
{
    if (Drv == NULL)
        return 0;

    return Drv->Backend->LastError(Drv->State);
}
