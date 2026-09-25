// #*#*#*#*#*#*#*#*#*#*#*#*#*#* SimChSdiRx.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The emulated SDI receive channels, their frame source and test controls
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"       // Allocation seam.
#include "DtPcieAbi.h"          // The driver ABI the emulator answers in.
#include "OAL/OsThread.h"       // The clock a source can follow.
#include "Sim4k.h"              // How 2160p over one link lies in the ring.
#include "SimChSdiRx.h"         // Interface being implemented.
#include "SimDtPcie.h"          // Port counts.
#include "Video/DtFrameProps.h" // Frame geometry of the source.
#include "Video/DtSdiFrame.h"   // The format the source writes.
#include "Video/DtVidStd.h"     // Which standards are 4K.
#include "cdtapi.h"             // DTAPI_VIDSTD_ codes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The users one channel accepts, as many as the driver's SDI receive channel does.
#define SIM_RX_MAX_USERS 8

// The largest ring the driver allocates.
#define SIM_RX_MAX_RING (256 * 1024 * 1024)

// The faults SimRxFault numbers.
#define SIM_RX_FAULT_COUNT 4

// The format events one read of the write offset delivers at most, on the clock: two
// frames. A receiver that falls further behind drops the rest, and goes on from then.
#define SIM_RX_MAX_DUE_EVENTS 8

typedef struct SimRxUser
{
    void* Handle; // NULL for a free slot
    bool Exclusive;
    int OpMode; // DT_FUNC_OPMODE_ value
    uint32_t ReadOffset;
    bool Mapped; // The handle has the ring's address
} SimRxUser;

typedef struct SimRxChannel
{
    SimRxUser Users[SIM_RX_MAX_USERS];
    int NumUsers;

    bool Configured;
    DtIoctlChSdiRxCmdConfigureInput Config;
    uint8_t* Ring;
    size_t RingSize;
    size_t MaxLoad;
    uint32_t WriteOffset;
    bool Running;

    int SourceVidStd;
    uint32_t NextFrame;

    // Frames from a file in place of the ones SimChSdiRx_Line makes; NULL for none.
    uint8_t* FileData;
    size_t FileFrameCount;
    size_t FileFrameBytes; // A frame in the file, with its padding
    int FileLineSyms;      // The symbols of a line, from its EAV on
    bool Faults[SIM_RX_FAULT_COUNT];

    // The frame being written.
    bool InFrame;
    uint32_t FrameNumber;
    int SeqNumber;     // The quarter the next event reports
    bool FrameInSync;  // The source matches the configuration
    bool FrameDropped; // Part of the frame did not fit
    int LinesWritten;  // Lines of the frame in the ring
    DtSdiFrameLayout Layout;

    // On the clock, when the next format event is due; 0 for at once.
    double NextEventMs;
} SimRxChannel;

static struct
{
    bool Initialised;
    SimRxChannel Channels[SIM_SDI_PORT_COUNT];
    size_t RingLimit;
    bool RealTime;
    int StreamAlignment;
    bool MapAsLinux;
    int FailCmd;
    uint32_t FailStatus;
    int SlowCmd;
    int SlowMs;
} g_Rx;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EnsureRx -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void EnsureRx(void)
{
    if (!g_Rx.Initialised)
        SimChSdiRx_Reset();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindUser -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static SimRxUser* FindUser(SimRxChannel* Channel, void* Handle)
{
    for (int i = 0; i < SIM_RX_MAX_USERS; i++)
    {
        if (Channel->Users[i].Handle == Handle)
            return &Channel->Users[i];
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Unconfigure -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Unconfigure(SimRxChannel* Channel)
{
    DtAlloc_Free(Channel->Ring);
    Channel->Ring = NULL;
    Channel->RingSize = 0;
    Channel->MaxLoad = 0;
    Channel->WriteOffset = 0;
    Channel->Configured = false;
    Channel->Running = false;
    Channel->InFrame = false;
    for (int i = 0; i < SIM_RX_MAX_USERS; i++)
    {
        Channel->Users[i].Mapped = false;
        Channel->Users[i].ReadOffset = 0;
        Channel->Users[i].OpMode = DT_FUNC_OPMODE_IDLE;
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame source +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Crc18 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// SMPTE 292's CRC-18, x^18 + x^5 + x^4 + 1, over one 10-bit word, least significant bit
// first.
//
static uint32_t Crc18(uint32_t Crc, uint32_t Word)
{
    for (int Bit = 0; Bit < 10; Bit++)
    {
        uint32_t Feedback = (Crc ^ (Word >> Bit)) & 1;

        Crc >>= 1;
        if (Feedback != 0)
            Crc ^= 0x23000;
    }
    return Crc;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DataSymbol -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A symbol that is not a timing reference, between 040 and 3BF.
//
static uint32_t DataSymbol(uint32_t FrameNumber, int Line, int Index)
{
    uint32_t Hash = FrameNumber * 0x9E3779B1u ^ (uint32_t)Line * 0x85EBCA77u ^
                    (uint32_t)Index * 0xC2B2AE3Du;

    Hash ^= Hash >> 15;
    Hash *= 0x2C1B3C6Du;
    Hash ^= Hash >> 12;
    return 0x040 + Hash % 0x380;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Xyz -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The fourth word of a timing reference: field, vertical blanking, EAV or SAV, and the
// protection bits over those three.
//
static uint32_t Xyz(const DtFrameProps* Props, int Line, bool Eav)
{
    uint32_t F = Props->NumFields == 2 && Line >= Props->Fields[1].StartLine ? 1 : 0;
    const DtFieldProps* Field = &Props->Fields[F];
    uint32_t V = Line < Field->ActiveStartLine || Line > Field->ActiveEndLine ? 1 : 0;
    uint32_t H = Eav ? 1 : 0;

    return 0x200 | F << 8 | V << 7 | H << 6 | (V ^ H) << 5 | (F ^ H) << 4 | (F ^ V) << 3 |
           (F ^ V ^ H) << 2;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WithParity -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Nine bits with bit 9 the inverse of bit 8, as line numbers and CRC words carry them.
//
static uint32_t WithParity(uint32_t Nine)
{
    Nine &= 0x1FF;
    return Nine | ((Nine >> 8) ^ 1) << 9;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Line4k -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The raw line of a 4K standard: the lines of the four links, each of a frame number of
// its own so that a test sees which link a symbol came from, interleaved word by word.
// Returns the number of symbols, or 0 for a standard of level-B links or a line the
// frame does not have.
//
static int Line4k(int VidStd, uint32_t FrameNumber, int Line, uint16_t* Symbols)
{
    const DtVidStdEntry* Info = DtVidStd_Find(VidStd);
    uint16_t Link[SIM_RX_MAX_LINE_SYMBOLS / 4];
    int Count = 0;

    if (Info == NULL || Info->IsLevelB)
        return 0;
    for (int L = 0; L < 4; L++)
    {
        Count =
            SimChSdiRx_Line(Info->OneLinkVidStd, FrameNumber + (uint32_t)L, Line, Link);
        if (Count == 0 || Count > SIM_RX_MAX_LINE_SYMBOLS / 4)
            return 0;
        for (int i = 0; i < Count; i++)
            Symbols[Sim4k_RawAt(L, i)] = Link[i];
    }
    return 4 * Count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimChSdiRx_Line -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int SimChSdiRx_Line(int VidStd, uint32_t FrameNumber, int Line, uint16_t* Symbols)
{
    DtFrameProps Props;

    if (DtVidStd_Is4k(VidStd))
        return Line4k(VidStd, FrameNumber, Line, Symbols);
    if (!DtFrameProps_Init(&Props, VidStd))
        return 0;

    int NumLines = DtFrameProps_NumLines(&Props);
    int HancSyms = DtFrameProps_LineNumSymHancInclTiming(&Props);
    int Total = HancSyms + Props.LineNumSymActive;
    if (Line < 1 || Line > NumLines || Total > SIM_RX_MAX_LINE_SYMBOLS)
        return 0;

    int i;
    for (i = 0; i < Total; i++)
        Symbols[i] = (uint16_t)DataSymbol(FrameNumber, Line, i);

    if (Props.LineNumSymEav == 4)
    {
        const uint32_t Eav[4] = {0x3FF, 0x000, 0x000, Xyz(&Props, Line, true)};
        const uint32_t Sav[4] = {0x3FF, 0x000, 0x000, Xyz(&Props, Line, false)};

        for (i = 0; i < 4; i++)
        {
            Symbols[i] = (uint16_t)Eav[i];
            Symbols[HancSyms - 4 + i] = (uint16_t)Sav[i];
        }
        return Total;
    }

    // HD and 3G: each word once for each channel, C first.
    {
        uint32_t PrevFrame = Line == 1 ? FrameNumber - 1 : FrameNumber;
        int PrevLine = Line == 1 ? NumLines : Line - 1;
        uint32_t Words[8] = {0x3FF,
                             0x000,
                             0x000,
                             Xyz(&Props, Line, true),
                             WithParity((uint32_t)Line << 2),
                             WithParity((uint32_t)(Line >> 7) << 2 & 0x3C),
                             0,
                             0};
        int Channel;

        for (Channel = 0; Channel < 2; Channel++)
        {
            uint32_t Crc = 0;
            int j;

            for (j = Channel; j < Props.LineNumSymActive; j += 2)
                Crc = Crc18(Crc, DataSymbol(PrevFrame, PrevLine, HancSyms + j));
            for (j = 0; j < 6; j++)
                Crc = Crc18(Crc, Words[j]);

            Words[6] = WithParity(Crc);
            Words[7] = WithParity(Crc >> 9);
            for (j = 0; j < 8; j++)
                Symbols[2 * j + Channel] = (uint16_t)Words[j];
            for (j = 0; j < 4; j++)
            {
                uint32_t Sav[4] = {0x3FF, 0x000, 0x000, Xyz(&Props, Line, false)};
                Symbols[HancSyms - 8 + 2 * j + Channel] = (uint16_t)Sav[j];
            }
        }
    }
    return Total;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LineFromFile -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Fills Symbols with line Line, from 1, of frame FrameNumber of the channel's file, the
// first frame again after the last.
//
static void LineFromFile(const SimRxChannel* Channel, uint32_t FrameNumber, int Line,
                         uint16_t* Symbols)
{
    const uint8_t* Frame = Channel->FileData + (FrameNumber % Channel->FileFrameCount) *
                                                   Channel->FileFrameBytes;
    size_t Bit = (size_t)(Line - 1) * (size_t)Channel->FileLineSyms * 10;
    const uint8_t* In = Frame + Bit / 8;
    uint32_t Accu = (uint32_t)(*In++ >> (Bit % 8));
    int Have = 8 - (int)(Bit % 8);

    for (int i = 0; i < Channel->FileLineSyms; i++)
    {
        while (Have < 10)
        {
            Accu |= (uint32_t)*In++ << Have;
            Have += 8;
        }
        Symbols[i] = (uint16_t)(Accu & 0x3FF);
        Accu >>= 10;
        Have -= 10;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PackSection -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Packs Count symbols, least significant bit first, into Bytes bytes, padding with zero.
//
static void PackSection(const uint16_t* Symbols, int Count, uint8_t* Out, int Bytes)
{
    uint64_t Accu = 0;
    int Have = 0;
    int Byte = 0;

    memset(Out, 0, (size_t)Bytes);
    for (int i = 0; i < Count; i++)
    {
        Accu |= (uint64_t)(Symbols[i] & 0x3FF) << Have;
        Have += 10;
        while (Have >= 8)
        {
            Out[Byte++] = (uint8_t)Accu;
            Accu >>= 8;
            Have -= 8;
        }
    }
    if (Have > 0)
        Out[Byte] = (uint8_t)Accu;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RingFreeBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// What can still be written: the ring less the data word kept free, less the load of the
// running user that is furthest behind.
//
static size_t RingFreeBytes(const SimRxChannel* Channel)
{
    size_t Load = 0;

    for (int i = 0; i < SIM_RX_MAX_USERS; i++)
    {
        const SimRxUser* User = &Channel->Users[i];

        if (User->Handle == NULL || User->OpMode != DT_FUNC_OPMODE_RUN)
            continue;
        size_t UserLoad = ((size_t)Channel->WriteOffset + Channel->RingSize -
                           (size_t)User->ReadOffset % Channel->RingSize) %
                          Channel->RingSize;
        if (UserLoad > Load)
            Load = UserLoad;
    }
    return Load >= Channel->MaxLoad ? 0 : Channel->MaxLoad - Load;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RingWrite -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes Size bytes at the write offset, across the end of the ring where needed. Returns
// false, writing nothing, when they do not fit.
//
static bool RingWrite(SimRxChannel* Channel, const uint8_t* Data, size_t Size)
{
    size_t Offset = Channel->WriteOffset;
    size_t First = Channel->RingSize - Offset;

    if (Size > RingFreeBytes(Channel))
        return false;

    if (First > Size)
        First = Size;
    memcpy(Channel->Ring + Offset, Data, First);
    memcpy(Channel->Ring, Data + First, Size - First);
    Channel->WriteOffset = (uint32_t)((Offset + Size) % Channel->RingSize);
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StartFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Begins the next frame: its number, whether the source matches the configuration, and
// its header.
//
static void StartFrame(SimRxChannel* Channel)
{
    const DtIoctlChSdiRxCmdConfigureInput* Config = &Channel->Config;
    DtSdiFrameLayout* Layout = &Channel->Layout;

    Channel->FrameNumber = Channel->NextFrame;
    if (Channel->Faults[SIM_RX_FAULT_SKIP_FRAME])
    {
        Channel->FrameNumber++;
        Channel->Faults[SIM_RX_FAULT_SKIP_FRAME] = false;
    }
    Channel->NextFrame = Channel->FrameNumber + 1;
    Channel->InFrame = true;
    Channel->SeqNumber = 0;
    Channel->FrameDropped = false;
    Channel->LinesWritten = 0;

    uint8_t Header[64];
    Channel->FrameInSync =
        DtSdiFrame_LayoutInit(Layout, Channel->SourceVidStd, g_Rx.StreamAlignment) &&
        Layout->NumLines == Config->m_FrameProps.m_NumLines &&
        Layout->LineNumSymsHanc == Config->m_FrameProps.m_NumSymsHanc &&
        Layout->LineNumSymsActive == Config->m_FrameProps.m_NumSymsVidVanc &&
        Layout->RxHeaderNumBytes <= (int)sizeof(Header);
    if (Channel->Faults[SIM_RX_FAULT_OUT_OF_SYNC])
    {
        Channel->FrameInSync = false;
        Channel->Faults[SIM_RX_FAULT_OUT_OF_SYNC] = false;
    }
    if (!Channel->FrameInSync)
        return;

    {
        DtSdiFrameRxHeader Fields;

        Fields.SyncWord = DT_SDIFRAME_SYNC_WORD;
        Fields.ProtocolVersion = 0;
        Fields.Format = Layout->Format;
        Fields.FrameId = (int)(Channel->FrameNumber & 0xFFFF);
        Fields.PtpSeconds = Channel->FrameNumber;
        Fields.PtpNanoseconds = 0;
        if (Channel->Faults[SIM_RX_FAULT_SYNC_WORD])
            Fields.SyncWord ^= 0x100;
        if (Channel->Faults[SIM_RX_FAULT_FORMAT])
        {
            Fields.Format = Layout->Is4k ? DT_SDIFRAME_FORMAT_UNCOMPRESSED
                                         : DT_SDIFRAME_FORMAT_UNCOMPRESSED_4K;
        }
        Channel->Faults[SIM_RX_FAULT_SYNC_WORD] = false;
        Channel->Faults[SIM_RX_FAULT_FORMAT] = false;

        memset(Header, 0, sizeof(Header));
        DtSdiFrame_EncodeRxHeader(&Fields, Header);
        if (!RingWrite(Channel, Header, (size_t)Layout->RxHeaderNumBytes))
            Channel->FrameDropped = true;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EncodeLines4k -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Packs raw line Line, from 1, of a 4K frame into the two coded lines the ring holds,
// through Sections, which takes both coded lines in symbols.
//
static void EncodeLines4k(const DtSdiFrameLayout* Layout, const uint16_t* Raw, int Line,
                          uint16_t* Sections, uint8_t* Coded)
{
    const int Hanc = Layout->SectionNumSymsHanc;
    const int Video = Layout->SectionNumSymsActive;
    uint16_t* A = Sections;
    uint16_t* B = Sections + 2 * Hanc + Video;

    Sim4k_Split(Hanc, Video / 2, DtSdiFrame_IsBlankingLine(Layout, Line - 1), Raw, A, B);
    for (int i = 0; i < 2; i++)
    {
        const uint16_t* Line2 = i == 0 ? A : B;
        uint8_t* Out = Coded + (size_t)i * (size_t)Layout->RxStride;

        PackSection(Line2, Hanc, Out, Layout->SectionBytesHanc);
        PackSection(Line2 + Hanc, Hanc, Out + Layout->SectionBytesHanc,
                    Layout->SectionBytesHanc);
        PackSection(Line2 + 2 * Hanc, Video, Out + 2 * (size_t)Layout->SectionBytesHanc,
                    Layout->SectionBytesActive);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteLines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the lines of the current frame up to Upto, stopping at the first that does not
// fit.
//
static void WriteLines(SimRxChannel* Channel, int Upto)
{
    const DtSdiFrameLayout* Layout = &Channel->Layout;

    if (!Channel->FrameInSync || Channel->FrameDropped || Channel->LinesWritten >= Upto)
        return;

    const int PerLine = Layout->NumCodedLines / Layout->NumLines;
    uint8_t* Coded = (uint8_t*)DtAlloc_Malloc((size_t)PerLine * (size_t)Layout->RxStride);
    uint16_t* Symbols =
        (uint16_t*)DtAlloc_Malloc(SIM_RX_MAX_LINE_SYMBOLS * sizeof(uint16_t));
    uint16_t* Sections =
        Layout->Is4k ? (uint16_t*)DtAlloc_Malloc((size_t)PerLine *
                                                 (size_t)(2 * Layout->SectionNumSymsHanc +
                                                          Layout->SectionNumSymsActive) *
                                                 sizeof(uint16_t))
                     : NULL;
    if (Coded == NULL || Symbols == NULL || (Layout->Is4k && Sections == NULL))
    {
        DtAlloc_Free(Coded);
        DtAlloc_Free(Symbols);
        DtAlloc_Free(Sections);
        Channel->FrameDropped = true;
        return;
    }

    while (Channel->LinesWritten < Upto)
    {
        int Line = Channel->LinesWritten + 1;

        if (Channel->FileData != NULL)
            LineFromFile(Channel, Channel->FrameNumber, Line, Symbols);
        else
            SimChSdiRx_Line(Layout->VidStd, Channel->FrameNumber, Line, Symbols);
        if (Layout->Is4k)
            EncodeLines4k(Layout, Symbols, Line, Sections, Coded);
        else
        {
            PackSection(Symbols, Layout->LineNumSymsHanc, Coded,
                        Layout->SectionBytesHanc);
            PackSection(Symbols + Layout->LineNumSymsHanc, Layout->LineNumSymsActive,
                        Coded + Layout->SectionBytesHanc, Layout->SectionBytesActive);
        }
        if (!RingWrite(Channel, Coded, (size_t)PerLine * (size_t)Layout->RxStride))
        {
            Channel->FrameDropped = true;
            break;
        }
        Channel->LinesWritten++;
    }
    DtAlloc_Free(Coded);
    DtAlloc_Free(Symbols);
    DtAlloc_Free(Sections);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NextEvent -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The next quarter of the current frame, starting a frame where needed.
//
static void NextEvent(SimRxChannel* Channel,
                      DtIoctlChSdiRxCmdWaitForFmtEventOutput* Event)
{
    if (!Channel->InFrame)
        StartFrame(Channel);

    int Quarter = Channel->SeqNumber;
    if (Channel->FrameInSync)
        WriteLines(Channel, (Channel->Layout.NumLines * (Quarter + 1) + 3) / 4);

    Event->m_FrameId = (Int)(Channel->FrameNumber & 0xFFFF);
    Event->m_SeqNumber = Quarter;
    Event->m_InSync = Channel->FrameInSync && !Channel->FrameDropped ? 1 : 0;

    Channel->SeqNumber++;
    if (Channel->SeqNumber == 4)
        Channel->InFrame = false;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The sizes the driver checks a command against before carrying it out.
static bool Fits(size_t InSize, size_t InNeeded, const void* Out, const size_t* OutSize,
                 size_t OutNeeded)
{
    if (InSize < InNeeded)
        return false;
    if (OutNeeded == 0)
        return true;
    return Out != NULL && OutSize != NULL && *OutSize >= OutNeeded;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Attach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// An exclusive user excludes every other, and exclusive access is refused while any
// other user is attached, as the DTA-2178 answered both.
//
static uint32_t Attach(SimRxChannel* Channel, void* Handle,
                       const DtIoctlChSdiRxCmdAttachInput* Request)
{
    if (FindUser(Channel, Handle) != NULL)
        return DT_STATUS_IN_USE;

    for (int i = 0; i < SIM_RX_MAX_USERS; i++)
    {
        if (Channel->Users[i].Handle != NULL &&
            (Channel->Users[i].Exclusive || Request->m_ReqExclusiveAccess != 0))
        {
            return DT_STATUS_IN_USE;
        }
    }

    SimRxUser* Slot = FindUser(Channel, NULL);
    if (Slot == NULL)
        return DT_STATUS_IN_USE;

    memset(Slot, 0, sizeof(*Slot));
    Slot->Handle = Handle;
    Slot->Exclusive = Request->m_ReqExclusiveAccess != 0;
    Slot->OpMode = DT_FUNC_OPMODE_IDLE;
    Channel->NumUsers++;
    return DT_STATUS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The last user takes the configuration with it, as it does in the driver.
//
static void Detach(SimRxChannel* Channel, SimRxUser* User)
{
    memset(User, 0, sizeof(*User));
    Channel->NumUsers--;
    if (Channel->NumUsers == 0)
    {
        Unconfigure(Channel);
        return;
    }

    Channel->Running = false;
    for (int i = 0; i < SIM_RX_MAX_USERS; i++)
    {
        if (Channel->Users[i].Handle != NULL &&
            Channel->Users[i].OpMode == DT_FUNC_OPMODE_RUN)
        {
            Channel->Running = true;
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Configure -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A size out of range, or quad link, which the emulator does not model, leaves the
// channel unconfigured, as a refused configuration did on the card. The same
// configuration again keeps the ring.
//
static uint32_t Configure(SimRxChannel* Channel,
                          const DtIoctlChSdiRxCmdConfigureInput* In)
{
    const size_t Unit = SIM_RX_PREFETCH_PAGES * 4096;

    if (In->m_DmaBuf.m_MinSize < (Int)Unit || In->m_DmaBuf.m_MinSize > SIM_RX_MAX_RING)
    {
        Unconfigure(Channel);
        return DT_STATUS_INVALID_PARAMETER;
    }
    if (In->m_NumPhysicalPorts != 1)
    {
        Unconfigure(Channel);
        return DT_STATUS_NOT_SUPPORTED;
    }

    size_t Size = ((size_t)In->m_DmaBuf.m_MinSize + Unit - 1) / Unit * Unit;
    if (g_Rx.RingLimit > 0 && Size > g_Rx.RingLimit)
        Size = g_Rx.RingLimit < Unit ? Unit : g_Rx.RingLimit / Unit * Unit;

    if (Channel->Configured && Size == Channel->RingSize &&
        memcmp((const char*)In + sizeof(In->m_CmdHdr),
               (const char*)&Channel->Config + sizeof(In->m_CmdHdr),
               sizeof(*In) - sizeof(In->m_CmdHdr)) == 0)
    {
        return DT_STATUS_OK;
    }

    Unconfigure(Channel);
    uint8_t* Ring = (uint8_t*)DtAlloc_Malloc(Size);
    if (Ring == NULL)
        return DT_STATUS_OUT_OF_MEMORY;
    memset(Ring, 0, Size);

    Channel->Ring = Ring;
    Channel->RingSize = Size;
    Channel->MaxLoad = Size - SIM_RX_PCIE_DATA_WIDTH / 8;
    Channel->Config = *In;
    Channel->Configured = true;
    return DT_STATUS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A channel that starts running writes from the start of its ring, at the start of a
// frame.
//
static uint32_t SetOpMode(SimRxChannel* Channel, SimRxUser* User, int OpMode)
{
    bool WasRunning = Channel->Running;

    if (OpMode != DT_FUNC_OPMODE_IDLE && OpMode != DT_FUNC_OPMODE_STANDBY &&
        OpMode != DT_FUNC_OPMODE_RUN)
    {
        return DT_STATUS_INVALID_PARAMETER;
    }
    if (OpMode != DT_FUNC_OPMODE_IDLE && !Channel->Configured)
        return DT_STATUS_NOT_INITIALISED;

    User->OpMode = OpMode;
    Channel->Running = false;
    for (int i = 0; i < SIM_RX_MAX_USERS; i++)
    {
        if (Channel->Users[i].Handle != NULL &&
            Channel->Users[i].OpMode == DT_FUNC_OPMODE_RUN)
        {
            Channel->Running = true;
        }
    }

    if (Channel->Running && !WasRunning)
    {
        Channel->WriteOffset = 0;
        Channel->InFrame = false;
        Channel->NextEventMs = 0;
    }
    return DT_STATUS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EventPeriodMs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// On the clock, the time between two format events of the channel, a quarter of its
// source's frame; 0 off the clock or without a source.
//
static double EventPeriodMs(const SimRxChannel* Channel)
{
    DtFrameProps Props;

    if (!g_Rx.RealTime || !DtFrameProps_Init(&Props, Channel->SourceVidStd) ||
        Props.FpsNum <= 0)
    {
        return 0;
    }
    return 1000.0 * Props.FpsDen / Props.FpsNum / 4;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteDueEvents -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// On the clock, the format events whose time has come, as a card's receiver writes its
// frames whether or not anyone waits: a program that only looks at the write offset sees
// the frames arrive.
//
static void WriteDueEvents(SimRxChannel* Channel)
{
    double PeriodMs = EventPeriodMs(Channel);
    if (PeriodMs <= 0 || !Channel->Running)
        return;

    double NowMs = (double)OsTime_MonotonicMs();
    DtIoctlChSdiRxCmdWaitForFmtEventOutput Event;
    for (int i = 0; i < SIM_RX_MAX_DUE_EVENTS && Channel->NextEventMs <= NowMs; i++)
    {
        NextEvent(Channel, &Event);
        Channel->NextEventMs =
            (Channel->NextEventMs > 0 ? Channel->NextEventMs : NowMs) + PeriodMs;
    }
    if (Channel->NextEventMs <= NowMs)
        Channel->NextEventMs = NowMs + PeriodMs;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DispatchCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// One command of a channel; SimChSdiRx_Cmd without the slowing down.
//
static uint32_t DispatchCmd(void* Handle, int PortIndex, int Cmd, const void* In,
                            size_t InSize, void* Out, size_t* OutSize, int* SleepMs)
{
    EnsureRx();
    *SleepMs = 0;
    if (PortIndex < 0 || PortIndex >= SIM_SDI_PORT_COUNT)
        return DT_STATUS_NOT_SUPPORTED;
    SimRxChannel* Channel = &g_Rx.Channels[PortIndex];

    if (g_Rx.FailCmd == Cmd && g_Rx.FailStatus != 0)
        return g_Rx.FailStatus;

    switch (Cmd)
    {
    case DT_CHSDIRX_CMD_ATTACH:
        if (!Fits(InSize, sizeof(DtIoctlChSdiRxCmdAttachInput), NULL, NULL, 0))
            return DT_STATUS_INVALID_PARAMETER;
        return Attach(Channel, Handle, (const DtIoctlChSdiRxCmdAttachInput*)In);
    case DT_CHSDIRX_CMD_DETACH:
    case DT_CHSDIRX_CMD_CONFIGURE:
    case DT_CHSDIRX_CMD_GET_OPERATIONAL_MODE:
    case DT_CHSDIRX_CMD_SET_OPERATIONAL_MODE:
    case DT_CHSDIRX_CMD_WAIT_FOR_FMT_EVENT:
    case DT_CHSDIRX_CMD_GET_WRITE_OFFSET:
    case DT_CHSDIRX_CMD_SET_READ_OFFSET:
    case DT_CHSDIRX_CMD_MAP_DMA_BUF_TO_USER:
        break;
    case DT_CHSDIRX_CMD_GET_PROPS:
    {
        DtIoctlChSdiRxCmdGetPropsOutput* Props = (DtIoctlChSdiRxCmdGetPropsOutput*)Out;

        if (!Fits(InSize, sizeof(DtIoctlChSdiRxCmdGetPropsInput), Out, OutSize,
                  sizeof(*Props)))
        {
            return DT_STATUS_INVALID_PARAMETER;
        }
        memset(Props, 0, sizeof(*Props));
        Props->m_Dma.m_Caps = DT_CDMAC_CAP_RX | DT_CDMAC_CAP_TX;
        Props->m_Dma.m_PrefetchSize = SIM_RX_PREFETCH_PAGES;
        Props->m_Dma.m_PcieDataWidth = SIM_RX_PCIE_DATA_WIDTH;
        Props->m_Dma.m_ReorderBufSize = SIM_RX_REORDER_BUF_SIZE;
        Props->m_StreamAlignment = g_Rx.StreamAlignment;
        *OutSize = sizeof(*Props);
        return DT_STATUS_OK;
    }
    default:
        return DT_STATUS_NOT_SUPPORTED;
    }

    SimRxUser* User = FindUser(Channel, Handle);
    if (Handle == NULL || User == NULL)
        return DT_STATUS_NOT_FOUND;

    switch (Cmd)
    {
    case DT_CHSDIRX_CMD_DETACH:
        Detach(Channel, User);
        return DT_STATUS_OK;

    case DT_CHSDIRX_CMD_CONFIGURE:
        if (!Fits(InSize, sizeof(DtIoctlChSdiRxCmdConfigureInput), NULL, NULL, 0))
            return DT_STATUS_INVALID_PARAMETER;
        return Configure(Channel, (const DtIoctlChSdiRxCmdConfigureInput*)In);

    case DT_CHSDIRX_CMD_GET_OPERATIONAL_MODE:
        if (!Fits(InSize, sizeof(DtIoctlChSdiRxCmdGetOpModeInput), Out, OutSize,
                  sizeof(DtIoctlChSdiRxCmdGetOpModeOutput)))
        {
            return DT_STATUS_INVALID_PARAMETER;
        }
        ((DtIoctlChSdiRxCmdGetOpModeOutput*)Out)->m_OpMode = User->OpMode;
        *OutSize = sizeof(DtIoctlChSdiRxCmdGetOpModeOutput);
        return DT_STATUS_OK;

    case DT_CHSDIRX_CMD_SET_OPERATIONAL_MODE:
        if (!Fits(InSize, sizeof(DtIoctlChSdiRxCmdSetOpModeInput), NULL, NULL, 0))
            return DT_STATUS_INVALID_PARAMETER;
        return SetOpMode(Channel, User,
                         ((const DtIoctlChSdiRxCmdSetOpModeInput*)In)->m_OpMode);

    case DT_CHSDIRX_CMD_WAIT_FOR_FMT_EVENT:
    {
        const DtIoctlChSdiRxCmdWaitForFmtEventInput* Request =
            (const DtIoctlChSdiRxCmdWaitForFmtEventInput*)In;
        DtIoctlChSdiRxCmdWaitForFmtEventOutput* Event =
            (DtIoctlChSdiRxCmdWaitForFmtEventOutput*)Out;

        if (!Fits(InSize, sizeof(*Request), Out, OutSize, sizeof(*Event)))
            return DT_STATUS_INVALID_PARAMETER;
        if (!Channel->Configured)
            return DT_STATUS_NOT_INITIALISED;
        if (!Channel->Running)
        {
            *SleepMs = Request->m_Timeout > 0 ? Request->m_Timeout : 0;
            return DT_STATUS_TIMEOUT;
        }

        // On the clock, a wait before the next event is due returns when it is, or
        // times out without it; a late event comes at once.
        double PeriodMs = EventPeriodMs(Channel);
        double NowMs = (double)OsTime_MonotonicMs();
        int Delay = 0;
        if (PeriodMs > 0 && Channel->NextEventMs > NowMs)
        {
            Delay = (int)(Channel->NextEventMs - NowMs + 0.999);
            if (Request->m_Timeout >= 0 && Delay > Request->m_Timeout)
            {
                *SleepMs = Request->m_Timeout;
                return DT_STATUS_TIMEOUT;
            }
        }
        if (PeriodMs > 0)
            Channel->NextEventMs =
                (Channel->NextEventMs > NowMs ? Channel->NextEventMs : NowMs) + PeriodMs;

        memset(Event, 0, sizeof(*Event));
        NextEvent(Channel, Event);
        *OutSize = sizeof(*Event);
        *SleepMs = Delay;
        if (Channel->SourceVidStd == DTAPI_VIDSTD_UNKNOWN && Request->m_Timeout > 0)
            *SleepMs = Request->m_Timeout < 10 ? Request->m_Timeout : 10;
        return DT_STATUS_OK;
    }

    case DT_CHSDIRX_CMD_GET_WRITE_OFFSET:
        if (!Fits(InSize, sizeof(DtIoctlChSdiRxCmdGetWrOffsetInput), Out, OutSize,
                  sizeof(DtIoctlChSdiRxCmdGetWrOffsetOutput)))
        {
            return DT_STATUS_INVALID_PARAMETER;
        }
        if (!Channel->Configured)
            return DT_STATUS_NOT_INITIALISED;
        WriteDueEvents(Channel);
        ((DtIoctlChSdiRxCmdGetWrOffsetOutput*)Out)->m_WriteOffset = Channel->WriteOffset;
        *OutSize = sizeof(DtIoctlChSdiRxCmdGetWrOffsetOutput);
        return DT_STATUS_OK;

    case DT_CHSDIRX_CMD_SET_READ_OFFSET:
        if (!Fits(InSize, sizeof(DtIoctlChSdiRxCmdSetRdOffsetInput), NULL, NULL, 0))
            return DT_STATUS_INVALID_PARAMETER;
        User->ReadOffset = ((const DtIoctlChSdiRxCmdSetRdOffsetInput*)In)->m_ReadOffset;
        return DT_STATUS_OK;

    default: // DT_CHSDIRX_CMD_MAP_DMA_BUF_TO_USER
    {
        DtIoctlChSdiRxCmdMapDmaBufToUserOutput* Map =
            (DtIoctlChSdiRxCmdMapDmaBufToUserOutput*)Out;

        if (!Fits(InSize, sizeof(DtIoctlChSdiRxCmdMapDmaBufToUserInput), Out, OutSize,
                  sizeof(*Map)))
        {
            return DT_STATUS_INVALID_PARAMETER;
        }
        if (!Channel->Configured)
            return DT_STATUS_NOT_INITIALISED;

        memset(Map, 0, sizeof(*Map));
        if (!g_Rx.MapAsLinux)
            User->Mapped = true;
        Map->m_BufferAddr = User->Mapped ? (uint64_t)(uintptr_t)Channel->Ring : 0;
        Map->m_BufSize = (Int)Channel->RingSize;
        Map->m_MaxLoad = (Int)Channel->MaxLoad;
        *OutSize = sizeof(*Map);
        return DT_STATUS_OK;
    }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimChSdiRx_Cmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint32_t SimChSdiRx_Cmd(void* Handle, int PortIndex, int Cmd, const void* In,
                        size_t InSize, void* Out, size_t* OutSize, int* SleepMs)
{
    uint32_t Status =
        DispatchCmd(Handle, PortIndex, Cmd, In, InSize, Out, OutSize, SleepMs);

    if (g_Rx.SlowCmd == Cmd)
        *SleepMs += g_Rx.SlowMs;
    return Status;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimChSdiRx_Map -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The Linux driver's segments: 256 MB per port, after one for the device.
//
void* SimChSdiRx_Map(void* Handle, uint64_t Offset, size_t Size)
{
    const uint64_t Segment = 256ull * 1024 * 1024;

    EnsureRx();
    if (Offset % Segment != 0 || Offset / Segment < 1)
        return NULL;
    uint64_t Port = Offset / Segment - 1;
    if (Port >= SIM_SDI_PORT_COUNT)
        return NULL;

    SimRxChannel* Channel = &g_Rx.Channels[Port];
    SimRxUser* User = FindUser(Channel, Handle);
    if (Handle == NULL || User == NULL || !Channel->Configured ||
        Size != Channel->RingSize)
        return NULL;

    User->Mapped = true;
    return Channel->Ring;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimChSdiRx_CloseHandle -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimChSdiRx_CloseHandle(void* Handle)
{
    EnsureRx();
    for (int Port = 0; Port < SIM_SDI_PORT_COUNT; Port++)
    {
        SimRxChannel* Channel = &g_Rx.Channels[Port];
        SimRxUser* User = FindUser(Channel, Handle);

        if (Handle != NULL && User != NULL)
            Detach(Channel, User);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimChSdiRx_Reset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimChSdiRx_Reset(void)
{
    for (int Port = 0; Port < SIM_SDI_PORT_COUNT; Port++)
    {
        SimRxChannel* Channel = &g_Rx.Channels[Port];

        if (g_Rx.Initialised)
        {
            DtAlloc_Free(Channel->Ring);
            DtAlloc_Free(Channel->FileData);
        }
        memset(Channel, 0, sizeof(*Channel));
        Channel->SourceVidStd = DTAPI_VIDSTD_UNKNOWN;
    }
    g_Rx.RingLimit = 0;
    g_Rx.RealTime = false;
    g_Rx.StreamAlignment = SIM_RX_STREAM_ALIGNMENT;
    g_Rx.MapAsLinux = false;
    g_Rx.FailCmd = -1;
    g_Rx.FailStatus = 0;
    g_Rx.SlowCmd = -1;
    g_Rx.SlowMs = 0;
    g_Rx.Initialised = true;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test controls +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_SetRxSource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_SetRxSource(int PortIndex, int VidStd)
{
    EnsureRx();
    if (PortIndex < 0 || PortIndex >= SIM_SDI_PORT_COUNT)
        return;
    SimRxChannel* Channel = &g_Rx.Channels[PortIndex];
    Channel->SourceVidStd = VidStd;
    DtAlloc_Free(Channel->FileData);
    Channel->FileData = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_SetRxRealTime -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_SetRxRealTime(bool RealTime)
{
    EnsureRx();
    g_Rx.RealTime = RealTime;
    for (int i = 0; i < SIM_SDI_PORT_COUNT; i++)
        g_Rx.Channels[i].NextEventMs = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimChSdiRx_SetFileSource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool SimChSdiRx_SetFileSource(int PortIndex, int VidStd, const char* Path)
{
    DtSdiFrameLayout Layout;

    EnsureRx();
    if (PortIndex < 0 || PortIndex >= SIM_SDI_PORT_COUNT || Path == NULL ||
        !DtSdiFrame_LayoutInit(&Layout, VidStd, g_Rx.StreamAlignment))
    {
        return false;
    }
    const int LineSyms = Layout.LineNumSymsHanc + Layout.LineNumSymsActive;
    const size_t Bits = (size_t)Layout.NumLines * (size_t)LineSyms * 10;
    const size_t FrameNumBytes = ((Bits + 7) / 8 + 7) / 8 * 8;
    if (LineSyms > SIM_RX_MAX_LINE_SYMBOLS)
        return false;

    FILE* File = SimDtPcie_OpenFile(Path, "rb");
    if (File == NULL)
        return false;
    uint8_t* Data = NULL;
    size_t Size = 0;
    bool Failed = false;
    for (;;)
    {
        uint8_t* More = (uint8_t*)DtAlloc_Realloc(Data, Size + FrameNumBytes);
        if (More == NULL)
        {
            Failed = true;
            break;
        }
        Data = More;
        size_t Got = fread(Data + Size, 1, FrameNumBytes, File);
        Size += Got;
        if (Got < FrameNumBytes)
            break;
    }
    Failed = Failed || ferror(File) != 0;
    fclose(File);
    if (Failed || Size == 0 || Size % FrameNumBytes != 0)
    {
        DtAlloc_Free(Data);
        return false;
    }

    SimRxChannel* Channel = &g_Rx.Channels[PortIndex];
    DtAlloc_Free(Channel->FileData);
    Channel->FileData = Data;
    Channel->FileFrameCount = Size / FrameNumBytes;
    Channel->FileFrameBytes = FrameNumBytes;
    Channel->FileLineSyms = LineSyms;
    Channel->SourceVidStd = VidStd;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_RunRxEvents -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_RunRxEvents(int PortIndex, int Events)
{
    EnsureRx();
    if (PortIndex < 0 || PortIndex >= SIM_SDI_PORT_COUNT)
        return;
    DtIoctlChSdiRxCmdWaitForFmtEventOutput Event;
    for (int i = 0; i < Events && g_Rx.Channels[PortIndex].Running; i++)
        NextEvent(&g_Rx.Channels[PortIndex], &Event);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_InjectRxFault -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_InjectRxFault(int PortIndex, SimRxFault Fault)
{
    EnsureRx();
    if (PortIndex >= 0 && PortIndex < SIM_SDI_PORT_COUNT && (int)Fault >= 0 &&
        (int)Fault < SIM_RX_FAULT_COUNT)
    {
        g_Rx.Channels[PortIndex].Faults[Fault] = true;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_LimitRxRing -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_LimitRxRing(size_t Size)
{
    EnsureRx();
    g_Rx.RingLimit = Size;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_SetRxAlignment -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcie_SetRxAlignment(int AlignmentInBits)
{
    EnsureRx();
    g_Rx.StreamAlignment = AlignmentInBits;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_MapRxRingAsLinux -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcie_MapRxRingAsLinux(bool AsLinux)
{
    EnsureRx();
    g_Rx.MapAsLinux = AsLinux;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_FailRxCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_FailRxCmd(int Cmd, uint32_t Status)
{
    EnsureRx();
    g_Rx.FailCmd = Cmd;
    g_Rx.FailStatus = Status;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_SlowRxCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_SlowRxCmd(int Cmd, int Ms)
{
    EnsureRx();
    g_Rx.SlowCmd = Ms > 0 ? Cmd : -1;
    g_Rx.SlowMs = Ms > 0 ? Ms : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_GetRxState -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcie_GetRxState(int PortIndex, SimRxState* State)
{
    EnsureRx();
    memset(State, 0, sizeof(*State));
    if (PortIndex < 0 || PortIndex >= SIM_SDI_PORT_COUNT)
        return;

    State->Configured = g_Rx.Channels[PortIndex].Configured;
    State->RingSize = g_Rx.Channels[PortIndex].RingSize;
    State->NumUsers = g_Rx.Channels[PortIndex].NumUsers;
    State->NextFrame = g_Rx.Channels[PortIndex].NextFrame;
    State->WriteOffset = g_Rx.Channels[PortIndex].WriteOffset;
}
