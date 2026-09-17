// #*#*#*#*#*#*#*#*#*#*#*#*#*#* SimChSdiRx.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - The emulated SDI receive channels, their frame source and test controls
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
#include "CDtapiLite.h"         // DTAPI_VIDSTD_ codes.
#include "Core/DtAlloc.h"       // Allocation seam.
#include "DtDrvAbi.h"           // The driver ABI the emulator answers in.
#include "SimChSdiRx.h"         // Interface being implemented.
#include "SimDtPcie.h"          // Port counts.
#include "Video/DtFrameProps.h" // Frame geometry of the source.
#include "Video/DtSdiFrame.h"   // The format the source writes.
#include "Video/DtVidStd.h"     // Which standards are 4K.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The users one channel accepts, as DtDfChSdiRx's MaxNumUsersSupported.
#define SIM_RX_MAX_USERS 8

// The largest ring the driver allocates.
#define SIM_RX_MAX_RING (256 * 1024 * 1024)

// The faults SimRxFault numbers.
#define SIM_RX_FAULT_COUNT 4

// Symbols in the longest line the source writes: 720p at 23.98 Hz.
#define SIM_RX_MAX_LINE_SYMBOLS 8250

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
    bool Faults[SIM_RX_FAULT_COUNT];

    // The frame being written.
    bool InFrame;
    uint32_t FrameNumber;
    int SeqNumber;    // The quarter the next event reports
    bool FrameInSync; // The source matches the configuration
    bool Dropped;     // Part of the frame did not fit
    int LinesWritten; // Lines of the frame in the ring
    DtSdiFrameLayout Layout;
} SimRxChannel;

static struct
{
    bool Initialised;
    SimRxChannel Channels[SIM_SDI_PORT_COUNT];
    size_t RingLimit;
    int Alignment;
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
        SimChSdiRxReset();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindUser -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static SimRxUser* FindUser(SimRxChannel* Channel, void* Handle)
{
    int i;

    for (i = 0; i < SIM_RX_MAX_USERS; i++)
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
    int i;

    DtFree(Channel->Ring);
    Channel->Ring = NULL;
    Channel->RingSize = 0;
    Channel->MaxLoad = 0;
    Channel->WriteOffset = 0;
    Channel->Configured = false;
    Channel->Running = false;
    Channel->InFrame = false;
    for (i = 0; i < SIM_RX_MAX_USERS; i++)
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
static uint32_t Crc18(uint32_t Crc, unsigned Word)
{
    int Bit;

    for (Bit = 0; Bit < 10; Bit++)
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
// A symbol that is no timing reference, between 040 and 3BF.
//
static unsigned DataSymbol(uint32_t FrameNumber, int Line, int Index)
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
static unsigned Xyz(const DtFrameProps* Props, int Line, bool Eav)
{
    unsigned F = Props->NumFields == 2 && Line >= Props->Fields[1].StartLine ? 1 : 0;
    const DtFieldProps* Field = &Props->Fields[F];
    unsigned V = Line < Field->VidStartLine || Line > Field->VidEndLine ? 1 : 0;
    unsigned H = Eav ? 1 : 0;

    return 0x200 | F << 8 | V << 7 | H << 6 | (V ^ H) << 5 | (F ^ H) << 4 | (F ^ V) << 3 |
           (F ^ V ^ H) << 2;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WithParity -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Nine bits with bit 9 the inverse of bit 8, as line numbers and CRC words carry them.
//
static unsigned WithParity(unsigned Nine)
{
    Nine &= 0x1FF;
    return Nine | ((Nine >> 8) ^ 1) << 9;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimChSdiRxLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int SimChSdiRxLine(int VidStd, uint32_t FrameNumber, int Line, uint16_t* Symbols)
{
    DtFrameProps Props;
    int Blank, Total, NumLines, i;

    if (DtVidStdIs4k(VidStd) || !DtFramePropsInit(&Props, VidStd))
        return 0;

    NumLines = DtFramePropsNumLines(&Props);
    Blank = DtFramePropsLineSymbolsHanc(&Props);
    Total = Blank + Props.LineNumSymVanc;
    if (Line < 1 || Line > NumLines || Total > SIM_RX_MAX_LINE_SYMBOLS)
        return 0;

    for (i = 0; i < Total; i++)
        Symbols[i] = (uint16_t)DataSymbol(FrameNumber, Line, i);

    if (Props.LineNumSymEav == 4)
    {
        const unsigned Eav[4] = {0x3FF, 0x000, 0x000, Xyz(&Props, Line, true)};
        const unsigned Sav[4] = {0x3FF, 0x000, 0x000, Xyz(&Props, Line, false)};

        for (i = 0; i < 4; i++)
        {
            Symbols[i] = (uint16_t)Eav[i];
            Symbols[Blank - 4 + i] = (uint16_t)Sav[i];
        }
        return Total;
    }

    // HD and 3G: each word once for each channel, C first.
    {
        uint32_t PrevFrame = Line == 1 ? FrameNumber - 1 : FrameNumber;
        int PrevLine = Line == 1 ? NumLines : Line - 1;
        unsigned Words[8] = {0x3FF,
                             0x000,
                             0x000,
                             Xyz(&Props, Line, true),
                             WithParity((unsigned)Line << 2),
                             WithParity((unsigned)(Line >> 7) << 2 & 0x3C),
                             0,
                             0};
        int Channel;

        for (Channel = 0; Channel < 2; Channel++)
        {
            uint32_t Crc = 0;
            int j;

            for (j = Channel; j < Props.LineNumSymVanc; j += 2)
                Crc = Crc18(Crc, DataSymbol(PrevFrame, PrevLine, Blank + j));
            for (j = 0; j < 6; j++)
                Crc = Crc18(Crc, Words[j]);

            Words[6] = WithParity(Crc);
            Words[7] = WithParity(Crc >> 9);
            for (j = 0; j < 8; j++)
                Symbols[2 * j + Channel] = (uint16_t)Words[j];
            for (j = 0; j < 4; j++)
            {
                unsigned Sav[4] = {0x3FF, 0x000, 0x000, Xyz(&Props, Line, false)};
                Symbols[Blank - 8 + 2 * j + Channel] = (uint16_t)Sav[j];
            }
        }
    }
    return Total;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PackSection -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Packs Count symbols, least significant bit first, into Bytes bytes, padding with zero.
//
static void PackSection(const uint16_t* Symbols, int Count, uint8_t* Out, int Bytes)
{
    uint64_t Accu = 0;
    int Have = 0, i, Byte = 0;

    memset(Out, 0, (size_t)Bytes);
    for (i = 0; i < Count; i++)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RingFree -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// What can still be written: the ring less the data word kept free, less the load of the
// running user that is furthest behind.
//
static size_t RingFree(const SimRxChannel* Channel)
{
    size_t Load = 0;
    int i;

    for (i = 0; i < SIM_RX_MAX_USERS; i++)
    {
        const SimRxUser* User = &Channel->Users[i];
        size_t UserLoad;

        if (User->Handle == NULL || User->OpMode != DT_FUNC_OPMODE_RUN)
            continue;
        UserLoad = ((size_t)Channel->WriteOffset + Channel->RingSize -
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

    if (Size > RingFree(Channel))
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
    uint8_t Header[64];

    Channel->FrameNumber = Channel->NextFrame;
    if (Channel->Faults[SIM_RX_FAULT_SKIP_FRAME])
    {
        Channel->FrameNumber++;
        Channel->Faults[SIM_RX_FAULT_SKIP_FRAME] = false;
    }
    Channel->NextFrame = Channel->FrameNumber + 1;
    Channel->InFrame = true;
    Channel->SeqNumber = 0;
    Channel->Dropped = false;
    Channel->LinesWritten = 0;

    Channel->FrameInSync =
        DtSdiFrameLayoutInit(Layout, Channel->SourceVidStd, g_Rx.Alignment) &&
        Layout->NumLines == Config->m_FrameProps.m_NumLines &&
        Layout->LineSymsHanc == Config->m_FrameProps.m_NumSymsHanc &&
        Layout->LineSymsVideo == Config->m_FrameProps.m_NumSymsVidVanc &&
        Layout->HeaderBytes <= (int)sizeof(Header);
    if (Channel->Faults[SIM_RX_FAULT_OUT_OF_SYNC])
    {
        Channel->FrameInSync = false;
        Channel->Faults[SIM_RX_FAULT_OUT_OF_SYNC] = false;
    }
    if (!Channel->FrameInSync)
        return;

    {
        DtSdiFrameHeader Fields;

        Fields.SyncWord = DT_SDIFRAME_SYNC_WORD;
        Fields.ProtocolVersion = 0;
        Fields.Format = DT_SDIFRAME_FORMAT_UNCOMPRESSED;
        Fields.FrameId = (int)(Channel->FrameNumber & 0xFFFF);
        Fields.PtpSeconds = Channel->FrameNumber;
        Fields.PtpNanoseconds = 0;
        if (Channel->Faults[SIM_RX_FAULT_SYNC_WORD])
            Fields.SyncWord ^= 0x100;
        if (Channel->Faults[SIM_RX_FAULT_FORMAT])
            Fields.Format = DT_SDIFRAME_FORMAT_UNCOMPRESSED_4K;
        Channel->Faults[SIM_RX_FAULT_SYNC_WORD] = false;
        Channel->Faults[SIM_RX_FAULT_FORMAT] = false;

        memset(Header, 0, sizeof(Header));
        DtSdiFrameEncodeHeader(&Fields, Header);
        if (!RingWrite(Channel, Header, (size_t)Layout->HeaderBytes))
            Channel->Dropped = true;
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
    uint16_t Symbols[SIM_RX_MAX_LINE_SYMBOLS];
    uint8_t* Coded;

    if (!Channel->FrameInSync || Channel->Dropped || Channel->LinesWritten >= Upto)
        return;

    Coded = (uint8_t*)DtMalloc((size_t)Layout->Stride);
    if (Coded == NULL)
    {
        Channel->Dropped = true;
        return;
    }

    while (Channel->LinesWritten < Upto)
    {
        int Line = Channel->LinesWritten + 1;

        SimChSdiRxLine(Layout->VidStd, Channel->FrameNumber, Line, Symbols);
        PackSection(Symbols, Layout->LineSymsHanc, Coded, Layout->LineBytesHanc);
        PackSection(Symbols + Layout->LineSymsHanc, Layout->LineSymsVideo,
                    Coded + Layout->LineBytesHanc, Layout->LineBytesVideo);
        if (!RingWrite(Channel, Coded, (size_t)Layout->Stride))
        {
            Channel->Dropped = true;
            break;
        }
        Channel->LinesWritten++;
    }
    DtFree(Coded);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NextEvent -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The next quarter of the current frame, starting a frame where needed.
//
static void NextEvent(SimRxChannel* Channel,
                      DtIoctlChSdiRxCmdWaitForFmtEventOutput* Event)
{
    int Quarter;

    if (!Channel->InFrame)
        StartFrame(Channel);

    Quarter = Channel->SeqNumber;
    if (Channel->FrameInSync)
        WriteLines(Channel, (Channel->Layout.NumLines * (Quarter + 1) + 3) / 4);

    Event->m_FrameId = (Int)(Channel->FrameNumber & 0xFFFF);
    Event->m_SeqNumber = Quarter;
    Event->m_InSync = Channel->FrameInSync && !Channel->Dropped ? 1 : 0;

    Channel->SeqNumber++;
    if (Channel->SeqNumber == 4)
        Channel->InFrame = false;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The sizes a command needs, as the driver's I/O stub checks them.
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
    SimRxUser* Slot;
    int i;

    if (FindUser(Channel, Handle) != NULL)
        return DT_STATUS_IN_USE;

    for (i = 0; i < SIM_RX_MAX_USERS; i++)
    {
        if (Channel->Users[i].Handle != NULL &&
            (Channel->Users[i].Exclusive || Request->m_ReqExclusiveAccess != 0))
        {
            return DT_STATUS_IN_USE;
        }
    }

    Slot = FindUser(Channel, NULL);
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
// The last user takes the configuration with it (DtDfChSdiRx_OnDetachLastUser).
//
static void Detach(SimRxChannel* Channel, SimRxUser* User)
{
    int i;

    memset(User, 0, sizeof(*User));
    Channel->NumUsers--;
    if (Channel->NumUsers == 0)
    {
        Unconfigure(Channel);
        return;
    }

    Channel->Running = false;
    for (i = 0; i < SIM_RX_MAX_USERS; i++)
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
    size_t Size;
    uint8_t* Ring;

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

    Size = ((size_t)In->m_DmaBuf.m_MinSize + Unit - 1) / Unit * Unit;
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
    Ring = (uint8_t*)DtMalloc(Size);
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
    int i;

    if (OpMode != DT_FUNC_OPMODE_IDLE && OpMode != DT_FUNC_OPMODE_STANDBY &&
        OpMode != DT_FUNC_OPMODE_RUN)
    {
        return DT_STATUS_INVALID_PARAMETER;
    }
    if (OpMode != DT_FUNC_OPMODE_IDLE && !Channel->Configured)
        return DT_STATUS_NOT_INITIALISED;

    User->OpMode = OpMode;
    Channel->Running = false;
    for (i = 0; i < SIM_RX_MAX_USERS; i++)
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
    }
    return DT_STATUS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RunCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// One command of a channel; SimChSdiRxCmd without the slowing down.
//
static uint32_t RunCmd(void* Handle, int PortIndex, int Cmd, const void* In,
                       size_t InSize, void* Out, size_t* OutSize, int* SleepMs)
{
    SimRxChannel* Channel;
    SimRxUser* User;

    EnsureRx();
    *SleepMs = 0;
    if (PortIndex < 0 || PortIndex >= SIM_SDI_PORT_COUNT)
        return DT_STATUS_NOT_SUPPORTED;
    Channel = &g_Rx.Channels[PortIndex];

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
        Props->m_StreamAlignment = g_Rx.Alignment;
        *OutSize = sizeof(*Props);
        return DT_STATUS_OK;
    }
    default:
        return DT_STATUS_NOT_SUPPORTED;
    }

    User = FindUser(Channel, Handle);
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

        memset(Event, 0, sizeof(*Event));
        NextEvent(Channel, Event);
        *OutSize = sizeof(*Event);
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimChSdiRxCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint32_t SimChSdiRxCmd(void* Handle, int PortIndex, int Cmd, const void* In,
                       size_t InSize, void* Out, size_t* OutSize, int* SleepMs)
{
    uint32_t Status = RunCmd(Handle, PortIndex, Cmd, In, InSize, Out, OutSize, SleepMs);

    if (g_Rx.SlowCmd == Cmd)
        *SleepMs += g_Rx.SlowMs;
    return Status;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimChSdiRxMap -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The Linux driver's segments: 256 MB per port, after one for the device.
//
void* SimChSdiRxMap(void* Handle, uint64_t Offset, size_t Size)
{
    const uint64_t Segment = 256ull * 1024 * 1024;
    SimRxChannel* Channel;
    SimRxUser* User;
    uint64_t Port;

    EnsureRx();
    if (Offset % Segment != 0 || Offset / Segment < 1)
        return NULL;
    Port = Offset / Segment - 1;
    if (Port >= SIM_SDI_PORT_COUNT)
        return NULL;

    Channel = &g_Rx.Channels[Port];
    User = FindUser(Channel, Handle);
    if (Handle == NULL || User == NULL || !Channel->Configured ||
        Size != Channel->RingSize)
        return NULL;

    User->Mapped = true;
    return Channel->Ring;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimChSdiRxCloseHandle -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimChSdiRxCloseHandle(void* Handle)
{
    int Port;

    EnsureRx();
    for (Port = 0; Port < SIM_SDI_PORT_COUNT; Port++)
    {
        SimRxChannel* Channel = &g_Rx.Channels[Port];
        SimRxUser* User = FindUser(Channel, Handle);

        if (Handle != NULL && User != NULL)
            Detach(Channel, User);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimChSdiRxReset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimChSdiRxReset(void)
{
    int Port;

    for (Port = 0; Port < SIM_SDI_PORT_COUNT; Port++)
    {
        SimRxChannel* Channel = &g_Rx.Channels[Port];

        if (g_Rx.Initialised)
            DtFree(Channel->Ring);
        memset(Channel, 0, sizeof(*Channel));
        Channel->SourceVidStd = DTAPI_VIDSTD_UNKNOWN;
    }
    g_Rx.RingLimit = 0;
    g_Rx.Alignment = SIM_RX_STREAM_ALIGNMENT;
    g_Rx.MapAsLinux = false;
    g_Rx.FailCmd = -1;
    g_Rx.FailStatus = 0;
    g_Rx.SlowCmd = -1;
    g_Rx.SlowMs = 0;
    g_Rx.Initialised = true;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test controls +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieSetRxSource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcieSetRxSource(int PortIndex, int VidStd)
{
    EnsureRx();
    if (PortIndex >= 0 && PortIndex < SIM_SDI_PORT_COUNT)
        g_Rx.Channels[PortIndex].SourceVidStd = VidStd;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieRunRxEvents -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcieRunRxEvents(int PortIndex, int Events)
{
    DtIoctlChSdiRxCmdWaitForFmtEventOutput Event;
    int i;

    EnsureRx();
    if (PortIndex < 0 || PortIndex >= SIM_SDI_PORT_COUNT)
        return;
    for (i = 0; i < Events && g_Rx.Channels[PortIndex].Running; i++)
        NextEvent(&g_Rx.Channels[PortIndex], &Event);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieInjectRxFault -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcieInjectRxFault(int PortIndex, SimRxFault Fault)
{
    EnsureRx();
    if (PortIndex >= 0 && PortIndex < SIM_SDI_PORT_COUNT && (int)Fault >= 0 &&
        (int)Fault < SIM_RX_FAULT_COUNT)
    {
        g_Rx.Channels[PortIndex].Faults[Fault] = true;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieLimitRxRing -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcieLimitRxRing(size_t Size)
{
    EnsureRx();
    g_Rx.RingLimit = Size;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieSetRxAlignment -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcieSetRxAlignment(int AlignmentBits)
{
    EnsureRx();
    g_Rx.Alignment = AlignmentBits;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieMapRxRingAsLinux -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcieMapRxRingAsLinux(bool AsLinux)
{
    EnsureRx();
    g_Rx.MapAsLinux = AsLinux;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieFailRxCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcieFailRxCmd(int Cmd, uint32_t Status)
{
    EnsureRx();
    g_Rx.FailCmd = Cmd;
    g_Rx.FailStatus = Status;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieSlowRxCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcieSlowRxCmd(int Cmd, int Ms)
{
    EnsureRx();
    g_Rx.SlowCmd = Ms > 0 ? Cmd : -1;
    g_Rx.SlowMs = Ms > 0 ? Ms : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieGetRxState -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcieGetRxState(int PortIndex, SimRxState* State)
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
