// #*#*#*#*#*#*#*#*#*#*#*#*#* TestSimSdiFiles.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The emulated card's SDI source and sink through files
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPI_SIM=1. A file of frames made here from the emulator's own
// pattern plays on an input port, and an input channel reads it back byte for byte; what
// an output channel sends lands in a file, frame for frame. The files are the ones
// FFmpeg's sdi format holds without its header, which is what CDTAPI_SIM_SDI_SOURCE and
// CDTAPI_SIM_SDI_SINK take; a last case leaves such a file for a CTest run of an example
// with the variable set.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"           // Live allocations.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // Direct handles.
#include "OAL/OsThread.h"           // Waiting for frames on the clock.
#include "OAL/Sim/SimChSdiRx.h"     // The emulated source's frames.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.
#include "Video/DtSdiFrame.h"       // Frame sizes.
#include "Video/DtVidStd.h"         // Which standards are 4K.
#include "cdtapi.h"                 // Public API under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Port 1, an input by default, and port 2, an output by default.
#define PORT 1
#define PORT_OUTPUT 2

// Large enough for a 1080-line frame with 10-bit symbols.
#define BUFFER_SIZE (8 * 1024 * 1024)

// Symbols in the longest line the emulated source makes: a raw 2160p50 line.
#define SIM_LINE_SYMBOLS 21120

// The files the cases write, in the directory CTest runs them in.
#define SOURCE_FILE "SimSdiFiles_source.sdi"
#define SINK_FILE "SimSdiFiles_sink.sdi"
#define EXAMPLE_FILE "SimSdiFiles_1080i50.sdi"

typedef struct Fixture
{
    int Live;
    DtDevice* Device;
    DtInpChannel* In;   // The case's input channel, or NULL
    DtOutpChannel* Out; // The case's output channel, or NULL
    DtWorkerPool* Pool; // The case's pool, until the channels hold it, or NULL
    char* Buffer;       // BUFFER_SIZE bytes
    OsThread* Thread;   // A thread of the case that writes to Out, or NULL
} Fixture;

// Frees what Start and the case made when an assertion fails before FINISH does. The
// channels go first: freeing a channel detaches it and ends a write on the case's
// thread, which is then joined while the case's frame, where the thread's context
// lives, still stands.
static void Cleanup(void* Context)
{
    Fixture* Fix = (Fixture*)Context;

    DtInpChannel_Free(Fix->In);
    DtOutpChannel_Free(Fix->Out);
    OsThread_Join(Fix->Thread);
    DtWorkerPool_Free(Fix->Pool);
    DtDevice_Free(Fix->Device);
    free(Fix->Buffer);
    Fix->In = NULL;
    Fix->Out = NULL;
    Fix->Thread = NULL;
    Fix->Pool = NULL;
    Fix->Device = NULL;
    Fix->Buffer = NULL;
}

// Resets the emulator and attaches a device object. Returns false, having recorded a
// failure, when that is not possible.
static bool Start(Fixture* Fix, int* DtFailures)
{
    SimDtPcie_Reset();
    Fix->Live = DtAlloc_NumLive();
    Fix->Device = NULL;
    Fix->In = NULL;
    Fix->Out = NULL;
    Fix->Pool = NULL;
    Fix->Buffer = NULL;
    Fix->Thread = NULL;
    DtTest_SetCleanup(Cleanup, Fix);

    OsDrv* Drv = OsDrv_Open(SIM_DEVICE_INDEX);
    if (Drv == NULL || !OsDrv_IsEmulated(Drv))
    {
        printf("    FAIL: no emulated device at index 0; is CDTAPI_SIM=1 set?\n");
        (*DtFailures)++;
        OsDrv_Close(Drv);
        DtTest_Cleanup();
        return false;
    }
    OsDrv_Close(Drv);

    Fix->Device = DtDevice_Alloc();
    Fix->Buffer = (char*)malloc(BUFFER_SIZE);
    if (Fix->Device == NULL || Fix->Buffer == NULL ||
        DtDevice_AttachToSerial(Fix->Device, SIM_SERIAL) != DTAPI_OK)
    {
        printf("    FAIL: cannot set up\n");
        (*DtFailures)++;
        DtTest_Cleanup();
        return false;
    }
    return true;
}

// Frees what Start and the case made, lets the emulator close its files and free their
// frames, and checks that nothing is left open or allocated.
#define FINISH(Fix)                                                                      \
    do                                                                                   \
    {                                                                                    \
        DtTest_SetCleanup(NULL, NULL);                                                   \
        DtInpChannel_Free((Fix).In);                                                     \
        DtOutpChannel_Free((Fix).Out);                                                   \
        DtWorkerPool_Free((Fix).Pool);                                                   \
        DtDevice_Free((Fix).Device);                                                     \
        free((Fix).Buffer);                                                              \
        DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);                                    \
        SimDtPcie_Reset();                                                               \
        DT_ASSERT_EQ(DtAlloc_NumLive(), (Fix).Live);                                     \
    } while (0)

// Frame FrameNumber of the emulator's pattern of VidStd as a raw frame of 10-bit
// symbols, padded with zeros to a multiple of 8 bytes, in a new buffer of *Size bytes.
// *Padded is the same size.
static uint8_t* PatternFrame(int VidStd, uint32_t FrameNumber, size_t* Size,
                             size_t* Padded)
{
    DtSdiFrameLayout Layout;

    *Size = *Padded = 0;
    if (!DtSdiFrame_LayoutInit(&Layout, VidStd, 32))
        return NULL;
    *Size = DtSdiFrame_RawSize(&Layout, 10);
    *Padded = (*Size + 7) / 8 * 8;
    uint8_t* Frame = (uint8_t*)calloc(*Padded, 1);
    if (Frame == NULL)
        return NULL;

    uint16_t* Symbols = (uint16_t*)malloc(SIM_LINE_SYMBOLS * sizeof(uint16_t));
    size_t Symbol = 0;

    if (Symbols == NULL)
    {
        free(Frame);
        *Size = *Padded = 0;
        return NULL;
    }
    for (int Line = 1; Line <= Layout.NumLines; Line++)
    {
        int Count = SimChSdiRx_Line(VidStd, FrameNumber, Line, Symbols);

        for (int i = 0; i < Count; i++, Symbol++)
        {
            for (int b = 0; b < 10; b++)
            {
                if ((Symbols[i] >> b & 1) != 0)
                    Frame[(Symbol * 10 + (size_t)b) / 8] |=
                        (uint8_t)(1u << ((Symbol * 10 + (size_t)b) % 8));
            }
        }
    }
    free(Symbols);
    return Frame;
}

// Writes frames First to First + Count - 1 of the pattern of VidStd to Path, each padded,
// and then Extra bytes more. False when that fails.
static bool WriteFrames(const char* Path, int VidStd, uint32_t First, int Count,
                        size_t Extra)
{
    FILE* File = SimDtPcie_OpenFile(Path, "wb");
    bool Done = File != NULL;

    for (int i = 0; i < Count && Done; i++)
    {
        size_t Size, Padded;
        uint8_t* Frame = PatternFrame(VidStd, First + (uint32_t)i, &Size, &Padded);
        Done = Frame != NULL && fwrite(Frame, 1, Padded, File) == Padded;
        free(Frame);
    }
    for (size_t i = 0; i < Extra && Done; i++)
        Done = fputc(0, File) != EOF;
    if (File != NULL && fclose(File) != 0)
        Done = false;
    return Done;
}

// Frame Index of a file of frames Stride bytes apart, Size bytes of it, for a file too
// large to hold whole. NULL when the file is shorter than that.
static uint8_t* ReadFrameAt(const char* Path, size_t Index, size_t Stride, size_t Size)
{
    FILE* File = SimDtPcie_OpenFile(Path, "rb");
    uint8_t* Data = NULL;

    if (File == NULL)
        return NULL;
    if (fseek(File, (long)(Index * Stride), SEEK_SET) == 0)
    {
        Data = (uint8_t*)malloc(Size);
        if (Data != NULL && fread(Data, 1, Size, File) != Size)
        {
            free(Data);
            Data = NULL;
        }
    }
    fclose(File);
    return Data;
}

// The whole file at Path in a new buffer of *Size bytes; NULL when it cannot be read.
static uint8_t* ReadAll(const char* Path, size_t* Size)
{
    FILE* File = SimDtPcie_OpenFile(Path, "rb");
    uint8_t* Data = NULL;

    *Size = 0;
    if (File == NULL)
        return NULL;
    for (;;)
    {
        uint8_t* More = (uint8_t*)realloc(Data, *Size + 65536);
        if (More == NULL)
            break;
        Data = More;
        size_t Got = fread(Data + *Size, 1, 65536, File);
        *Size += Got;
        if (Got < 65536)
            break;
    }
    fclose(File);
    return Data;
}

// The source value for VidStd, named Name, playing the file at Path on PORT.
static const char* SourceValue(const char* Name, const char* Path)
{
    static char Value[256];
    snprintf(Value, sizeof(Value), "%d:%s:%s", PORT, Name, Path);
    return Value;
}

// Sets the I/O standard of the port at Port to VidStd.
static DtapiResult SetStandard(Fixture* Fix, int Port, int VidStd)
{
    int Value;
    int SubValue;
    bool Is4k = DtVidStd_Is4k(VidStd);
    DtapiResult Result = DtapiVidStd2IoStd(VidStd, Is4k ? 3 : -1, &Value, &SubValue);

    if (Result != DTAPI_OK)
        return Result;
    if (Is4k)
    {
        SimDtPcie_OverrideProperty("CAP_12GSDI", Port - 1, true, 1);
        SimDtPcie_OverrideProperty("CAP_2160P50", Port - 1, true, 1);
    }
    DtIoConfig Config = {Port, DTAPI_IOCONFIG_IOSTD, Value, SubValue, {-1, -1}};
    return DtDevice_SetIoConfig(Fix->Device, &Config, 1);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Source +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// A file of three frames plays on the input port: the port detects its standard, and a
// channel reads the three and then the first again, byte for byte. 720p50 is one of the
// standards whose frames the file pads; the name is taken in any case.
DT_TEST(SourcePlaysTheFile)
{
    static const struct
    {
        int VidStd;
        const char* Name;
    } Cases[] = {{DTAPI_VIDSTD_625I50, "625I50"},
                 {DTAPI_VIDSTD_525I59_94, "525i59_94"},
                 {DTAPI_VIDSTD_720P50, "720P50"},
                 {DTAPI_VIDSTD_1080I50, "1080I50"}};

    for (size_t c = 0; c < sizeof(Cases) / sizeof(Cases[0]); c++)
    {
        Fixture Fix;
        if (!Start(&Fix, DtFailures))
            return;
        DT_ASSERT(WriteFrames(SOURCE_FILE, Cases[c].VidStd, 100, 3, 0));
        DT_ASSERT(SimDtPcie_SetSdiSource(SourceValue(Cases[c].Name, SOURCE_FILE)));

        int Found = DTAPI_VIDSTD_UNKNOWN;
        DT_ASSERT_OK(DtDevice_DetectVidStd(Fix.Device, PORT, &Found));
        DT_ASSERT_EQ(Found, Cases[c].VidStd);

        Fix.In = DtInpChannel_Alloc();
        DT_ASSERT(Fix.In != NULL);
        DT_ASSERT_OK(SetStandard(&Fix, PORT, Cases[c].VidStd));
        DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.In, Fix.Device, PORT));
        DT_ASSERT_OK(
            DtInpChannel_SetRxMode(Fix.In, DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B));
        DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.In, DTAPI_RXCTRL_RCV));
        for (uint32_t i = 0; i < 4; i++)
        {
            size_t Size, Padded;
            uint8_t* Expected =
                PatternFrame(Cases[c].VidStd, 100 + i % 3, &Size, &Padded);
            int FrameSize = BUFFER_SIZE;
            DT_ASSERT_OK(DtInpChannel_ReadFrame(Fix.In, Fix.Buffer, &FrameSize, 2000));
            DT_ASSERT_EQ((size_t)FrameSize, Size);
            DT_ASSERT(Expected != NULL && memcmp(Fix.Buffer, Expected, Size) == 0);
            free(Expected);
        }
        FINISH(Fix);
    }
    remove(SOURCE_FILE);
}

// A value the source cannot use changes nothing: no signal, and the port's own pattern.
DT_TEST(SourceRefusesWhatItCannotUse)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT(WriteFrames(SOURCE_FILE, DTAPI_VIDSTD_625I50, 0, 2, 0));
    DT_ASSERT(WriteFrames(SINK_FILE, DTAPI_VIDSTD_625I50, 0, 1, 8));

    static const char* const Bad[] = {
        "0:625I50:" SOURCE_FILE,
        "9:625I50:" SOURCE_FILE,
        "x:625I50:" SOURCE_FILE,
        "625I50:" SOURCE_FILE,
        "1:625X50:" SOURCE_FILE,
        "1:2160P50:" SOURCE_FILE,
        "1:625I50:",
        "1:625I50",
        "1:625I50:no-such-file.sdi",
        "1:525I59_94:" SOURCE_FILE,
        "1:625I50:" SINK_FILE,
        "",
    };
    for (size_t i = 0; i < sizeof(Bad) / sizeof(Bad[0]); i++)
    {
        if (SimDtPcie_SetSdiSource(Bad[i]))
        {
            printf("    FAIL: \"%s\" was taken\n", Bad[i]);
            (*DtFailures)++;
        }
    }
    DT_ASSERT(!SimDtPcie_SetSdiSource(NULL));
    int Found = 0;
    DT_ASSERT(DtDevice_DetectVidStd(Fix.Device, PORT, &Found) != DTAPI_OK ||
              Found == DTAPI_VIDSTD_UNKNOWN);

    DT_ASSERT(!SimDtPcie_SetSdiSink("0:" SINK_FILE));
    DT_ASSERT(!SimDtPcie_SetSdiSink("2:"));
    DT_ASSERT(!SimDtPcie_SetSdiSink("2"));
    DT_ASSERT(!SimDtPcie_SetSdiSink(NULL));
    FINISH(Fix);
    remove(SOURCE_FILE);
    remove(SINK_FILE);
}

// On the clock the frames arrive whether or not a read waits, so that a program that
// looks at the FIFO load before it reads, as FFmpeg's device does, sees them come; off
// the clock, as a test has it, none arrives without a wait.
DT_TEST(SourceFollowsTheClock)
{
    for (int RealTime = 0; RealTime <= 1; RealTime++)
    {
        Fixture Fix;
        if (!Start(&Fix, DtFailures))
            return;
        DT_ASSERT(WriteFrames(SOURCE_FILE, DTAPI_VIDSTD_625I50, 7, 2, 0));
        DT_ASSERT(SimDtPcie_SetSdiSource(SourceValue("625I50", SOURCE_FILE)));
        SimDtPcie_SetRxRealTime(RealTime != 0);

        Fix.In = DtInpChannel_Alloc();
        DT_ASSERT(Fix.In != NULL);
        DT_ASSERT_OK(SetStandard(&Fix, PORT, DTAPI_VIDSTD_625I50));
        DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.In, Fix.Device, PORT));
        DT_ASSERT_OK(
            DtInpChannel_SetRxMode(Fix.In, DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B));
        DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.In, DTAPI_RXCTRL_RCV));

        int Load = 0;
        for (int Ms = 0; Ms < (RealTime ? 2000 : 200) && Load == 0; Ms += 10)
        {
            DT_ASSERT_OK(DtInpChannel_GetFifoLoad(Fix.In, &Load));
            if (Load == 0)
                OsTime_SleepMs(10);
        }
        if (RealTime)
        {
            size_t Size, Padded;
            uint8_t* Expected = PatternFrame(DTAPI_VIDSTD_625I50, 7, &Size, &Padded);
            int FrameSize = BUFFER_SIZE;
            DT_ASSERT(Load > 0 && (size_t)Load % Size == 0);
            DT_ASSERT_OK(DtInpChannel_ReadFrame(Fix.In, Fix.Buffer, &FrameSize, 1000));
            DT_ASSERT_EQ((size_t)FrameSize, Size);
            DT_ASSERT(Expected != NULL && memcmp(Fix.Buffer, Expected, Size) == 0);
            free(Expected);
        }
        else
        {
            DT_ASSERT_EQ(Load, 0);
        }
        FINISH(Fix);
    }
    remove(SOURCE_FILE);
}

// A dispatch function of the kind a program with its own threads writes: it starts a
// thread for each piece from the second to the eighth, does the first itself, and
// joins. A ninth piece and those after it do not run; the cases ask for four. A real
// program hands the pieces to a pool it already has; the shape of the call is what
// matters here.
typedef struct Piece
{
    DtJobFunc Work;
    void* Context;
    int Index;
    int Count;
} Piece;

static void RunPiece(void* Arg)
{
    const Piece* P = (const Piece*)Arg;

    P->Work(P->Context, P->Index, P->Count);
}

typedef struct Dispatcher
{
    int Calls; // How often the channel asked for the dispatch
} Dispatcher;

static void Dispatch(void* User, DtJobFunc Work, void* Context, int Count)
{
    OsThread* Thread[8];
    Piece Pieces[8];
    Dispatcher* Me = (Dispatcher*)User;
    int Started = 0;

    Me->Calls++;
    for (int i = 1; i < Count && i < 8; i++)
    {
        Pieces[i].Work = Work;
        Pieces[i].Context = Context;
        Pieces[i].Index = i;
        Pieces[i].Count = Count;
        Thread[i] = OsThread_Start(RunPiece, &Pieces[i]);
        Started = Thread[i] != NULL ? i : Started;
    }
    Work(Context, 0, Count);

    // A piece whose thread could not start is done here: every piece to the eighth runs.
    for (int i = 1; i < Count && i < 8; i++)
    {
        if (i <= Started && Thread[i] != NULL)
            OsThread_Join(Thread[i]);
        else
            Work(Context, i, Count);
    }
}

// A pool of NumThreads threads of the library's own; NULL when it cannot be had.
static DtWorkerPool* ThreadPool(int NumThreads)
{
    DtWorkerPool* Pool = DtWorkerPool_Alloc();

    if (Pool != NULL && DtWorkerPool_StartThreads(Pool, NumThreads) != DTAPI_OK)
        DtWorkerPool_Freep(&Pool);
    return Pool;
}

// A pool that hands its jobs to Dispatch above, which counts them in Me.
static DtWorkerPool* DispatchPool(Dispatcher* Me, int NumThreads)
{
    DtWorkerPool* Pool = DtWorkerPool_Alloc();

    if (Pool != NULL &&
        DtWorkerPool_SetDispatch(Pool, Dispatch, Me, NumThreads) != DTAPI_OK)
        DtWorkerPool_Freep(&Pool);
    return Pool;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Sink +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// What an output channel sends lands in the file, frame for frame and padded: the frames
// written appear whole and in order, with the black frames the channel may send before
// them and between them.
DT_TEST(SinkWritesWhatIsSent)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT(SimDtPcie_SetSdiSink("2:" SINK_FILE));

    Fix.Out = DtOutpChannel_Alloc();
    DT_ASSERT(Fix.Out != NULL);
    DT_ASSERT_OK(SetStandard(&Fix, PORT_OUTPUT, DTAPI_VIDSTD_625I50));
    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Fix.Out, Fix.Device, PORT_OUTPUT));
    DT_ASSERT_OK(DtOutpChannel_SetTxMode(
        Fix.Out, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B, 0));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Out, DTAPI_TXCTRL_HOLD));

    uint8_t* Frames[3];
    size_t Size = 0, Padded = 0;
    for (int i = 0; i < 3; i++)
    {
        Frames[i] = PatternFrame(DTAPI_VIDSTD_625I50, (uint32_t)i, &Size, &Padded);
        DT_ASSERT(Frames[i] != NULL);
        DT_ASSERT_OK(DtOutpChannel_WriteFrame(Fix.Out, Frames[i], (int)Size, 2000));
    }
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Out, DTAPI_TXCTRL_SEND));
    DT_ASSERT_OK(DtOutpChannel_Detach(Fix.Out, DTAPI_WAIT_UNTIL_SENT));
    DtOutpChannel_Freep(&Fix.Out);
    DtDevice_Freep(&Fix.Device);
    SimDtPcie_Reset(); // Closes the file

    size_t FileSize = 0;
    uint8_t* File = ReadAll(SINK_FILE, &FileSize);
    DT_ASSERT(File != NULL && FileSize > 0 && FileSize % Padded == 0);
    int Next = 0;
    for (size_t At = 0; File != NULL && At + Padded <= FileSize; At += Padded)
    {
        DT_ASSERT(File[At + Size] == 0 || Size == Padded);
        if (Next < 3 && memcmp(File + At, Frames[Next], Size) == 0)
            Next++;
    }
    DT_ASSERT_EQ(Next, 3);
    free(File);
    for (int i = 0; i < 3; i++)
        free(Frames[i]);
    FINISH(Fix);
    remove(SINK_FILE);
}

// Threads are not for 4K alone: a frame of VidStd, named Name, divides over four threads
// too, and the frame read and the frame sent are the ones a single thread gives. A
// 1080i50 line of 10-bit symbols is 6600 bytes, so its bands meet on whole bytes; a
// 720p24 line shares a byte with the lines before and after it, which is what a band
// has to get right where it starts and ends.
static void ThreadsGiveOneThreadsBytes(int VidStd, const char* Name, int* DtFailures)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT(WriteFrames(SOURCE_FILE, VidStd, 0, 1, 0));
    DT_ASSERT(SimDtPcie_SetSdiSource(SourceValue(Name, SOURCE_FILE)));
    DT_ASSERT(SimDtPcie_SetSdiSink("2:" SINK_FILE));

    size_t Size = 0, Padded = 0;
    uint8_t* Expected = PatternFrame(VidStd, 0, &Size, &Padded);
    char* Buffer = (char*)malloc(Size);
    Fix.In = DtInpChannel_Alloc();
    Fix.Out = DtOutpChannel_Alloc();
    Fix.Pool = ThreadPool(4);
    DT_ASSERT(Expected != NULL && Buffer != NULL && Fix.In != NULL && Fix.Out != NULL);
    DT_ASSERT(Fix.Pool != NULL);

    // Four pieces asked for, where the library would take one for a standard up to 3G.
    DT_ASSERT_OK(SetStandard(&Fix, PORT, VidStd));
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.In, Fix.Device, PORT));
    DT_ASSERT_OK(DtInpChannel_SetWorkerPool(Fix.In, Fix.Pool, 4));
    DT_ASSERT_OK(
        DtInpChannel_SetRxMode(Fix.In, DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.In, DTAPI_RXCTRL_RCV));
    int FrameSize = (int)Size;
    DT_ASSERT_OK(DtInpChannel_ReadFrame(Fix.In, Buffer, &FrameSize, 30000));
    DT_ASSERT_EQ((size_t)FrameSize, Size);
    DT_ASSERT_MEM(Buffer, Expected, Size);
    DtInpChannel_Freep(&Fix.In);

    DT_ASSERT_OK(SetStandard(&Fix, PORT_OUTPUT, VidStd));
    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Fix.Out, Fix.Device, PORT_OUTPUT));
    DT_ASSERT_OK(DtOutpChannel_SetWorkerPool(Fix.Out, Fix.Pool, 4));
    DtWorkerPool_Freep(&Fix.Pool);
    DT_ASSERT_OK(DtOutpChannel_SetTxMode(
        Fix.Out, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B, 0));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Out, DTAPI_TXCTRL_HOLD));

    // Twice over the stream: the second frame follows the first in the same call, and its
    // first line begins where the last line of the first ended.
    char* Two = (char*)malloc(2 * Size);
    DT_ASSERT(Two != NULL);
    memcpy(Two, Buffer, Size);
    memcpy(Two + Size, Buffer, Size);
    DT_ASSERT_OK(DtOutpChannel_Write(Fix.Out, Two, (int)(2 * Size)));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Out, DTAPI_TXCTRL_SEND));
    DT_ASSERT_OK(DtOutpChannel_Detach(Fix.Out, DTAPI_WAIT_UNTIL_SENT));
    DtOutpChannel_Freep(&Fix.Out);
    DtDevice_Freep(&Fix.Device);
    SimDtPcie_Reset(); // Closes the file

    size_t FileSize = 0;
    uint8_t* File = ReadAll(SINK_FILE, &FileSize);
    DT_ASSERT(File != NULL && FileSize >= 2 * Padded);
    int Found = 0;
    for (size_t At = 0; At + Padded <= FileSize; At += Padded)
    {
        if (memcmp(File + At, Expected, Size) == 0)
            Found++;
    }
    DT_ASSERT_EQ(Found, 2);
    free(File);
    free(Two);
    free(Expected);
    free(Buffer);
    FINISH(Fix);
    remove(SOURCE_FILE);
    remove(SINK_FILE);
}

DT_TEST(HdOverThreads)
{
    ThreadsGiveOneThreadsBytes(DTAPI_VIDSTD_1080I50, "1080I50", DtFailures);
    ThreadsGiveOneThreadsBytes(DTAPI_VIDSTD_720P24, "720P24", DtFailures);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= 4K +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// A file of one 2160p50 frame plays on the input port, and what the output port sends
// goes to a file of its own: the frame read is the one in the file, and the frame written
// is the one sent. The emulated card encodes the ring and the line headers by its own
// reading of the layout, so the two files must hold the same bytes.
DT_TEST(FourKThroughFiles)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT(WriteFrames(SOURCE_FILE, DTAPI_VIDSTD_2160P50, 0, 1, 0));
    DT_ASSERT(SimDtPcie_SetSdiSource(SourceValue("2160P50", SOURCE_FILE)));
    DT_ASSERT(SimDtPcie_SetSdiSink("2:" SINK_FILE));

    int Found = DTAPI_VIDSTD_UNKNOWN;
    DT_ASSERT_OK(DtDevice_DetectVidStd(Fix.Device, PORT, &Found));
    DT_ASSERT_EQ(Found, DTAPI_VIDSTD_2160P50);

    size_t Size = 0, Padded = 0;
    uint8_t* Expected = PatternFrame(DTAPI_VIDSTD_2160P50, 0, &Size, &Padded);
    char* Buffer = (char*)malloc(Size);
    Fix.In = DtInpChannel_Alloc();
    Fix.Out = DtOutpChannel_Alloc();
    DT_ASSERT(Expected != NULL && Buffer != NULL && Fix.In != NULL && Fix.Out != NULL);

    DT_ASSERT_OK(SetStandard(&Fix, PORT, DTAPI_VIDSTD_2160P50));
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.In, Fix.Device, PORT));
    DT_ASSERT_OK(
        DtInpChannel_SetRxMode(Fix.In, DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.In, DTAPI_RXCTRL_RCV));
    int FrameSize = (int)Size;
    DT_ASSERT_OK(DtInpChannel_ReadFrame(Fix.In, Buffer, &FrameSize, 30000));
    DT_ASSERT_EQ((size_t)FrameSize, Size);
    DT_ASSERT_MEM(Buffer, Expected, Size);
    DtInpChannel_Freep(&Fix.In);

    DT_ASSERT_OK(SetStandard(&Fix, PORT_OUTPUT, DTAPI_VIDSTD_2160P50));
    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Fix.Out, Fix.Device, PORT_OUTPUT));
    DT_ASSERT_OK(DtOutpChannel_SetTxMode(
        Fix.Out, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B, 0));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Out, DTAPI_TXCTRL_HOLD));
    DT_ASSERT_OK(DtOutpChannel_WriteFrame(Fix.Out, Buffer, (int)Size, 30000));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Out, DTAPI_TXCTRL_SEND));
    DT_ASSERT_OK(DtOutpChannel_Detach(Fix.Out, DTAPI_WAIT_UNTIL_SENT));
    DtOutpChannel_Freep(&Fix.Out);
    DtDevice_Freep(&Fix.Device);
    SimDtPcie_Reset(); // Closes the file

    size_t FileSize = 0;
    uint8_t* File = ReadAll(SINK_FILE, &FileSize);
    DT_ASSERT(File != NULL && FileSize >= Padded);
    DT_ASSERT_MEM(File, Expected, Size);
    free(File);
    free(Expected);
    free(Buffer);
    FINISH(Fix);
    remove(SOURCE_FILE);
    remove(SINK_FILE);
}

// The same frame over four threads: a threaded conversion gives the bytes a single
// thread gives, both ways. The output goes through DtOutpChannel_Write rather than
// WriteFrame, so that the batch of lines is taken from the stream a caller of Write
// hands over, which is what the device in FFmpeg calls.
DT_TEST(FourKOverThreads)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT(WriteFrames(SOURCE_FILE, DTAPI_VIDSTD_2160P50, 0, 1, 0));
    DT_ASSERT(SimDtPcie_SetSdiSource(SourceValue("2160P50", SOURCE_FILE)));
    DT_ASSERT(SimDtPcie_SetSdiSink("2:" SINK_FILE));

    size_t Size = 0, Padded = 0;
    uint8_t* Expected = PatternFrame(DTAPI_VIDSTD_2160P50, 0, &Size, &Padded);
    char* Buffer = (char*)malloc(Size);
    Fix.In = DtInpChannel_Alloc();
    Fix.Out = DtOutpChannel_Alloc();
    Fix.Pool = ThreadPool(4);
    DT_ASSERT(Expected != NULL && Buffer != NULL && Fix.In != NULL && Fix.Out != NULL);
    DT_ASSERT(Fix.Pool != NULL);

    // With 0 the library takes four pieces for 2160p50.
    DT_ASSERT_OK(SetStandard(&Fix, PORT, DTAPI_VIDSTD_2160P50));
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.In, Fix.Device, PORT));
    DT_ASSERT_EQ(DtInpChannel_SetWorkerPool(Fix.In, Fix.Pool, -1), DTAPI_E_INVALID_ARG);
    DT_ASSERT_OK(DtInpChannel_SetWorkerPool(Fix.In, Fix.Pool, 0));
    DT_ASSERT_OK(
        DtInpChannel_SetRxMode(Fix.In, DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.In, DTAPI_RXCTRL_RCV));
    int FrameSize = (int)Size;
    DT_ASSERT_OK(DtInpChannel_ReadFrame(Fix.In, Buffer, &FrameSize, 30000));
    DT_ASSERT_EQ((size_t)FrameSize, Size);
    DT_ASSERT_MEM(Buffer, Expected, Size);

    // Setting the pool again, with the layout in place, reallocates the working buffers.
    DT_ASSERT_OK(DtInpChannel_SetWorkerPool(Fix.In, Fix.Pool, 3));

    // A 2160p50 frame takes 28.4 MB of the 256 MB ring, so the tenth frame is the first
    // whose coded lines run across the end of it: those lines are copied into a band's
    // own line buffer first, and every band must use its own. Every frame read is the
    // frame in the file.
    for (int Frame = 0; Frame < 10; Frame++)
    {
        FrameSize = (int)Size;
        DT_ASSERT_OK(DtInpChannel_ReadFrame(Fix.In, Buffer, &FrameSize, 30000));
        DT_ASSERT_MEM(Buffer, Expected, Size);
    }
    DtInpChannel_Freep(&Fix.In);

    DT_ASSERT_OK(SetStandard(&Fix, PORT_OUTPUT, DTAPI_VIDSTD_2160P50));
    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Fix.Out, Fix.Device, PORT_OUTPUT));
    DT_ASSERT_OK(DtOutpChannel_SetWorkerPool(Fix.Out, Fix.Pool, 0));
    DtWorkerPool_Freep(&Fix.Pool);
    DT_ASSERT_OK(DtOutpChannel_SetTxMode(
        Fix.Out, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B, 0));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Out, DTAPI_TXCTRL_HOLD));

    // A coded 2160p50 frame takes 28.4 MB of the 256 MB buffer, so from the tenth frame a
    // line runs across the end of it. A batch stops at the last line that lies in one
    // piece and that line goes through the line buffer, so the frame must come out whole
    // all the same. The card must be sending for the tenth to have anywhere to go: the
    // buffer holds nine.
    DT_ASSERT_OK(DtOutpChannel_Write(Fix.Out, Buffer, (int)Size));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Out, DTAPI_TXCTRL_SEND));
    for (int Frame = 1; Frame < 10; Frame++)
        DT_ASSERT_OK(DtOutpChannel_Write(Fix.Out, Buffer, (int)Size));
    DT_ASSERT_OK(DtOutpChannel_Detach(Fix.Out, DTAPI_WAIT_UNTIL_SENT));
    DtOutpChannel_Freep(&Fix.Out);
    DtDevice_Freep(&Fix.Device);
    SimDtPcie_Reset(); // Closes the file

    // The file holds ten frames, which is too much to hold whole, so the last of them is
    // read where it lies: that is the one the wrap fell in.
    uint8_t* File = ReadFrameAt(SINK_FILE, 9, Padded, Size);
    DT_ASSERT(File != NULL);
    DT_ASSERT_MEM(File, Expected, Size);
    free(File);
    free(Expected);
    free(Buffer);
    FINISH(Fix);
    remove(SOURCE_FILE);
    remove(SINK_FILE);
}

// A program that has threads of its own gives a pool with a dispatch function instead,
// and gets the same frames: 2160p50 in and out through Dispatch above, byte for byte what
// one thread gives. The channel must have asked it for every frame, and no pool must put
// the channel back to converting in the calling thread.
DT_TEST(FourKThroughDispatch)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT(WriteFrames(SOURCE_FILE, DTAPI_VIDSTD_2160P50, 0, 1, 0));
    DT_ASSERT(SimDtPcie_SetSdiSource(SourceValue("2160P50", SOURCE_FILE)));
    DT_ASSERT(SimDtPcie_SetSdiSink("2:" SINK_FILE));

    size_t Size = 0, Padded = 0;
    uint8_t* Expected = PatternFrame(DTAPI_VIDSTD_2160P50, 0, &Size, &Padded);
    char* Buffer = (char*)malloc(Size);
    Fix.In = DtInpChannel_Alloc();
    Fix.Out = DtOutpChannel_Alloc();
    Dispatcher Reading = {0};
    Dispatcher Writing = {0};
    DT_ASSERT(Expected != NULL && Buffer != NULL && Fix.In != NULL && Fix.Out != NULL);

    DT_ASSERT_OK(SetStandard(&Fix, PORT, DTAPI_VIDSTD_2160P50));
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.In, Fix.Device, PORT));
    Fix.Pool = DtWorkerPool_Alloc();
    DT_ASSERT(Fix.Pool != NULL);
    DT_ASSERT_EQ(DtWorkerPool_SetDispatch(Fix.Pool, Dispatch, &Reading, 0),
                 DTAPI_E_INVALID_ARG);
    DtWorkerPool_Freep(&Fix.Pool);
    Fix.Pool = DispatchPool(&Reading, 4);
    DT_ASSERT(Fix.Pool != NULL);
    DT_ASSERT_OK(DtInpChannel_SetWorkerPool(Fix.In, Fix.Pool, 0));
    DtWorkerPool_Freep(&Fix.Pool);
    DT_ASSERT_OK(
        DtInpChannel_SetRxMode(Fix.In, DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.In, DTAPI_RXCTRL_RCV));
    int FrameSize = (int)Size;
    DT_ASSERT_OK(DtInpChannel_ReadFrame(Fix.In, Buffer, &FrameSize, 30000));
    DT_ASSERT_EQ((size_t)FrameSize, Size);
    DT_ASSERT_MEM(Buffer, Expected, Size);
    DT_ASSERT_EQ(Reading.Calls, 1);

    // No pool converts in the reading thread again, and the frame is still right.
    DT_ASSERT_OK(DtInpChannel_SetWorkerPool(Fix.In, NULL, 0));
    FrameSize = (int)Size;
    DT_ASSERT_OK(DtInpChannel_ReadFrame(Fix.In, Buffer, &FrameSize, 30000));
    DT_ASSERT_MEM(Buffer, Expected, Size);
    DT_ASSERT_EQ(Reading.Calls, 1);
    DtInpChannel_Freep(&Fix.In);

    DT_ASSERT_OK(SetStandard(&Fix, PORT_OUTPUT, DTAPI_VIDSTD_2160P50));
    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Fix.Out, Fix.Device, PORT_OUTPUT));
    Fix.Pool = DispatchPool(&Writing, 4);
    DT_ASSERT(Fix.Pool != NULL);
    DT_ASSERT_OK(DtOutpChannel_SetWorkerPool(Fix.Out, Fix.Pool, 0));
    DtWorkerPool_Freep(&Fix.Pool);
    DT_ASSERT_OK(DtOutpChannel_SetTxMode(
        Fix.Out, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B, 0));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Out, DTAPI_TXCTRL_HOLD));
    DT_ASSERT_OK(DtOutpChannel_WriteFrame(Fix.Out, Buffer, (int)Size, 30000));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Out, DTAPI_TXCTRL_SEND));
    DT_ASSERT_OK(DtOutpChannel_Detach(Fix.Out, DTAPI_WAIT_UNTIL_SENT));
    DT_ASSERT(Writing.Calls > 0);
    DtOutpChannel_Freep(&Fix.Out);
    DtDevice_Freep(&Fix.Device);
    SimDtPcie_Reset(); // Closes the file

    size_t FileSize = 0;
    uint8_t* File = ReadAll(SINK_FILE, &FileSize);
    DT_ASSERT(File != NULL && FileSize >= Padded);
    DT_ASSERT_MEM(File, Expected, Size);
    free(File);
    free(Expected);
    free(Buffer);
    FINISH(Fix);
    remove(SOURCE_FILE);
    remove(SINK_FILE);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Shared pools +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// What a thread of the program sends on an output channel while the main thread reads:
// NumFrames times Frame, holding the first until it is in the buffer.
typedef struct Sender
{
    DtOutpChannel* Out;
    const uint8_t* Frame;
    int Size;
    int NumFrames;
    DtapiResult Result;
} Sender;

static void SendFrames(void* Context)
{
    Sender* S = (Sender*)Context;

    S->Result = DtOutpChannel_Write(S->Out, S->Frame, S->Size);
    if (S->Result == DTAPI_OK)
        S->Result = DtOutpChannel_SetTxControl(S->Out, DTAPI_TXCTRL_SEND);
    for (int Frame = 1; Frame < S->NumFrames && S->Result == DTAPI_OK; Frame++)
        S->Result = DtOutpChannel_Write(S->Out, S->Frame, S->Size);
}

// Two channels share one pool of eight threads at four pieces each, and run at the same
// time: an output channel sends three 2160p50 frames from a thread of the test's while
// the main thread reads three. Both give byte for byte what one thread gives.
DT_TEST(TwoChannelsShareAPoolOfEight)
{
    enum
    {
        NUM_FRAMES = 3
    };
    Fixture Fix;
    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT(WriteFrames(SOURCE_FILE, DTAPI_VIDSTD_2160P50, 0, 1, 0));
    DT_ASSERT(SimDtPcie_SetSdiSource(SourceValue("2160P50", SOURCE_FILE)));
    DT_ASSERT(SimDtPcie_SetSdiSink("2:" SINK_FILE));

    size_t Size = 0, Padded = 0;
    uint8_t* Expected = PatternFrame(DTAPI_VIDSTD_2160P50, 0, &Size, &Padded);
    char* Buffer = (char*)malloc(Size);
    Fix.In = DtInpChannel_Alloc();
    Fix.Out = DtOutpChannel_Alloc();
    Fix.Pool = ThreadPool(8);
    DT_ASSERT(Expected != NULL && Buffer != NULL && Fix.In != NULL && Fix.Out != NULL);
    DT_ASSERT(Fix.Pool != NULL);

    DT_ASSERT_OK(SetStandard(&Fix, PORT, DTAPI_VIDSTD_2160P50));
    DT_ASSERT_OK(SetStandard(&Fix, PORT_OUTPUT, DTAPI_VIDSTD_2160P50));
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.In, Fix.Device, PORT));
    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Fix.Out, Fix.Device, PORT_OUTPUT));
    DT_ASSERT_OK(DtInpChannel_SetWorkerPool(Fix.In, Fix.Pool, 4));
    DT_ASSERT_OK(DtOutpChannel_SetWorkerPool(Fix.Out, Fix.Pool, 4));
    DtWorkerPool_Freep(&Fix.Pool);
    DT_ASSERT_OK(
        DtInpChannel_SetRxMode(Fix.In, DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B));
    DT_ASSERT_OK(DtOutpChannel_SetTxMode(
        Fix.Out, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B, 0));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Out, DTAPI_TXCTRL_HOLD));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.In, DTAPI_RXCTRL_RCV));

    Sender Send = {Fix.Out, Expected, (int)Size, NUM_FRAMES, DTAPI_E};
    Fix.Thread = OsThread_Start(SendFrames, &Send);
    DT_ASSERT(Fix.Thread != NULL);
    for (int Frame = 0; Frame < NUM_FRAMES; Frame++)
    {
        int FrameSize = (int)Size;
        DT_ASSERT_OK(DtInpChannel_ReadFrame(Fix.In, Buffer, &FrameSize, 30000));
        DT_ASSERT_MEM(Buffer, Expected, Size);
    }
    OsThread_Join(Fix.Thread);
    Fix.Thread = NULL;
    DT_ASSERT_OK(Send.Result);
    DtInpChannel_Freep(&Fix.In);
    DT_ASSERT_OK(DtOutpChannel_Detach(Fix.Out, DTAPI_WAIT_UNTIL_SENT));
    DtOutpChannel_Freep(&Fix.Out);
    DtDevice_Freep(&Fix.Device);
    SimDtPcie_Reset(); // Closes the file

    uint8_t* File = ReadFrameAt(SINK_FILE, NUM_FRAMES - 1, Padded, Size);
    DT_ASSERT(File != NULL);
    DT_ASSERT_MEM(File, Expected, Size);
    free(File);
    free(Expected);
    free(Buffer);
    FINISH(Fix);
    remove(SOURCE_FILE);
    remove(SINK_FILE);
}

// Reads one 1080i50 frame on In and holds it to Expected.
static void ReadOne(DtInpChannel* In, char* Buffer, const uint8_t* Expected, size_t Size,
                    int* DtFailures)
{
    DT_ASSERT_OK(
        DtInpChannel_SetRxMode(In, DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(In, DTAPI_RXCTRL_RCV));
    int FrameSize = (int)Size;
    DT_ASSERT_OK(DtInpChannel_ReadFrame(In, Buffer, &FrameSize, 30000));
    DT_ASSERT_MEM(Buffer, Expected, Size);
    DT_ASSERT_OK(DtInpChannel_SetRxControl(In, DTAPI_RXCTRL_IDLE));
}

// Switches In to ASI, gives it the pool Between if not NULL, and switches it back to
// 1080i50.
static void ThroughAsi(DtInpChannel* In, DtWorkerPool* Between, int* DtFailures)
{
    int Value = 0, SubValue = 0;

    DT_ASSERT_OK(DtInpChannel_SetIoConfig(In, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_ASI,
                                          -1, -1, -1));
    if (Between != NULL)
        DT_ASSERT_OK(DtInpChannel_SetWorkerPool(In, Between, 4));
    DT_ASSERT_OK(DtapiVidStd2IoStd(DTAPI_VIDSTD_1080I50, -1, &Value, &SubValue));
    DT_ASSERT_OK(
        DtInpChannel_SetIoConfig(In, DTAPI_IOCONFIG_IOSTD, Value, SubValue, -1, -1));
}

// A pool is set on an attached channel only, and stays through a switch to ASI and
// back, but not through a detach: the first frame and the one after the switch go
// through it. ASI takes a pool of its own without complaint and keeps it for when it
// has lines again, and the frame after that switch goes through the new one. Every
// frame is byte for byte the one in the file.
DT_TEST(PoolStaysThroughAsiAndBack)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT(WriteFrames(SOURCE_FILE, DTAPI_VIDSTD_1080I50, 0, 1, 0));
    DT_ASSERT(SimDtPcie_SetSdiSource(SourceValue("1080I50", SOURCE_FILE)));

    size_t Size = 0, Padded = 0;
    uint8_t* Expected = PatternFrame(DTAPI_VIDSTD_1080I50, 0, &Size, &Padded);
    char* Buffer = (char*)malloc(Size);
    Fix.In = DtInpChannel_Alloc();
    Dispatcher First = {0};
    Dispatcher Second = {0};
    Fix.Pool = DispatchPool(&First, 4);
    DT_ASSERT(Expected != NULL && Buffer != NULL && Fix.In != NULL && Fix.Pool != NULL);

    DT_ASSERT_EQ(DtInpChannel_SetWorkerPool(Fix.In, Fix.Pool, 4), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_OK(SetStandard(&Fix, PORT, DTAPI_VIDSTD_1080I50));
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.In, Fix.Device, PORT));
    DT_ASSERT_OK(DtInpChannel_SetWorkerPool(Fix.In, Fix.Pool, 4));
    DtWorkerPool_Freep(&Fix.Pool);
    ReadOne(Fix.In, Buffer, Expected, Size, DtFailures);
    DT_ASSERT_EQ(First.Calls, 1);

    ThroughAsi(Fix.In, NULL, DtFailures);
    ReadOne(Fix.In, Buffer, Expected, Size, DtFailures);
    DT_ASSERT_EQ(First.Calls, 2);

    Fix.Pool = DispatchPool(&Second, 4);
    DT_ASSERT(Fix.Pool != NULL);
    ThroughAsi(Fix.In, Fix.Pool, DtFailures);
    DtWorkerPool_Freep(&Fix.Pool);
    ReadOne(Fix.In, Buffer, Expected, Size, DtFailures);
    DT_ASSERT_EQ(First.Calls, 2);
    DT_ASSERT_EQ(Second.Calls, 1);

    // A detach lets go of the pool: attached again, the channel reads in its own thread.
    DT_ASSERT_OK(DtInpChannel_Detach(Fix.In, DTAPI_INSTANT_DETACH));
    DT_ASSERT_EQ(DtInpChannel_SetWorkerPool(Fix.In, NULL, 0), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.In, Fix.Device, PORT));
    ReadOne(Fix.In, Buffer, Expected, Size, DtFailures);
    DT_ASSERT_EQ(Second.Calls, 1);

    free(Expected);
    free(Buffer);
    FINISH(Fix);
    remove(SOURCE_FILE);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= For examples +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Leaves three frames of 1080i50 for the CTest run of DtDetectVidStd with
// CDTAPI_SIM_SDI_SOURCE naming this file, which runs after this suite.
DT_TEST(LeavesAFileForTheExamples)
{
    DT_ASSERT(WriteFrames(EXAMPLE_FILE, DTAPI_VIDSTD_1080I50, 0, 3, 0));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST_MAIN("SimSdiFiles", DT_RUN(SourcePlaysTheFile),
             DT_RUN(SourceRefusesWhatItCannotUse), DT_RUN(SourceFollowsTheClock),
             DT_RUN(SinkWritesWhatIsSent), DT_RUN(HdOverThreads),
             DT_RUN(FourKThroughFiles), DT_RUN(FourKOverThreads),
             DT_RUN(FourKThroughDispatch), DT_RUN(TwoChannelsShareAPoolOfEight),
             DT_RUN(PoolStaysThroughAsiAndBack), DT_RUN(LeavesAFileForTheExamples))
