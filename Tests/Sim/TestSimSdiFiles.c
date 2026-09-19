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
#include "OAL/Sim/SimChSdiRx.h"     // The emulated source's frames.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.
#include "Video/DtSdiFrame.h"       // Frame sizes.
#include "cdtapi.h"                 // Public API under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Port 1, an input by default, and port 2, an output by default.
#define PORT 1
#define PORT_OUTPUT 2

// Large enough for a 1080-line frame with 10-bit symbols.
#define BUFFER_SIZE (8 * 1024 * 1024)

// The files the cases write, in the directory CTest runs them in.
#define SOURCE_FILE "SimSdiFiles_source.sdi"
#define SINK_FILE "SimSdiFiles_sink.sdi"
#define EXAMPLE_FILE "SimSdiFiles_1080i50.sdi"

typedef struct Fixture
{
    int Live;
    DtDevice* Device;
    char* Buffer; // BUFFER_SIZE bytes
} Fixture;

// Resets the emulator and attaches a device object. Returns false, having recorded a
// failure, when that is not possible.
static bool Start(Fixture* Fix, int* DtFailures)
{
    SimDtPcie_Reset();
    Fix->Live = DtAlloc_Live();
    Fix->Device = NULL;
    Fix->Buffer = NULL;

    OsDrv* Drv = OsDrv_Open(SIM_DEVICE_INDEX);
    if (Drv == NULL || !OsDrv_IsEmulated(Drv))
    {
        printf("    FAIL: no emulated device at index 0; is CDTAPI_SIM=1 set?\n");
        (*DtFailures)++;
        OsDrv_Close(Drv);
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
        return false;
    }
    return true;
}

// Frees what Start made, lets the emulator close its files and free their frames, and
// checks that nothing is left open or allocated.
#define FINISH(Fix)                                                                      \
    do                                                                                   \
    {                                                                                    \
        DtDevice_Free((Fix).Device);                                                     \
        free((Fix).Buffer);                                                              \
        DT_ASSERT_EQ(SimDtPcie_OpenHandles(), 0);                                        \
        SimDtPcie_Reset();                                                               \
        DT_ASSERT_EQ(DtAlloc_Live(), (Fix).Live);                                        \
    } while (0)

// Frame FrameNumber of the emulator's pattern of VidStd as a raw frame of 10-bit
// symbols, into a new buffer of *Size bytes, followed by zeros up to *Padded bytes, a
// multiple of 8.
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

    uint16_t Symbols[8250];
    size_t Symbol = 0;
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
    DtapiResult Result = DtapiVidStd2IoStd(VidStd, -1, &Value, &SubValue);

    if (Result != DTAPI_OK)
        return Result;
    DtIoConfig Config = {Port, DTAPI_IOCONFIG_IOSTD, Value, SubValue, {-1, -1}};
    return DtDevice_SetIoConfig(Fix->Device, &Config, 1);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Source +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// A file of three frames plays on the input port: the port detects its standard, and a
// channel reads the three and then the first again, byte for byte. 720p50 is the
// standard whose frames the file pads; the name is taken in any case.
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

        DtInpChannel* Channel = DtInpChannel_Alloc();
        DT_ASSERT(Channel != NULL);
        DT_ASSERT_OK(SetStandard(&Fix, PORT, Cases[c].VidStd));
        DT_ASSERT_OK(DtInpChannel_AttachToPort(Channel, Fix.Device, PORT));
        DT_ASSERT_OK(DtInpChannel_SetRxMode(Channel, DTAPI_RXMODE_SDI_FULL |
                                                         DTAPI_RXMODE_SDI_10B));
        DT_ASSERT_OK(DtInpChannel_SetRxControl(Channel, DTAPI_RXCTRL_RCV));
        for (uint32_t i = 0; i < 4; i++)
        {
            size_t Size, Padded;
            uint8_t* Expected =
                PatternFrame(Cases[c].VidStd, 100 + i % 3, &Size, &Padded);
            int FrameSize = BUFFER_SIZE;
            DT_ASSERT_OK(DtInpChannel_ReadFrame(Channel, Fix.Buffer, &FrameSize, 2000));
            DT_ASSERT_EQ((size_t)FrameSize, Size);
            DT_ASSERT(Expected != NULL && memcmp(Fix.Buffer, Expected, Size) == 0);
            free(Expected);
        }
        DtInpChannel_Free(Channel);
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

    DtOutpChannel* Channel = DtOutpChannel_Alloc();
    DT_ASSERT(Channel != NULL);
    DT_ASSERT_OK(SetStandard(&Fix, PORT_OUTPUT, DTAPI_VIDSTD_625I50));
    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Channel, Fix.Device, PORT_OUTPUT));
    DT_ASSERT_OK(DtOutpChannel_SetTxMode(
        Channel, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B, 0));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Channel, DTAPI_TXCTRL_HOLD));

    uint8_t* Frames[3];
    size_t Size = 0, Padded = 0;
    for (int i = 0; i < 3; i++)
    {
        Frames[i] = PatternFrame(DTAPI_VIDSTD_625I50, (uint32_t)i, &Size, &Padded);
        DT_ASSERT(Frames[i] != NULL);
        DT_ASSERT_OK(DtOutpChannel_WriteFrame(Channel, Frames[i], (int)Size, 2000));
    }
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Channel, DTAPI_TXCTRL_SEND));
    DT_ASSERT_OK(DtOutpChannel_Detach(Channel, DTAPI_WAIT_UNTIL_SENT));
    DtOutpChannel_Free(Channel);
    DtDevice_Free(Fix.Device);
    Fix.Device = NULL;
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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= For examples +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Leaves three frames of 1080i50 for the CTest run of DtDetectVidStd with
// CDTAPI_SIM_SDI_SOURCE naming this file, which runs after this suite.
DT_TEST(LeavesAFileForTheExamples)
{
    DT_ASSERT(WriteFrames(EXAMPLE_FILE, DTAPI_VIDSTD_1080I50, 0, 3, 0));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST_MAIN("SimSdiFiles", DT_RUN(SourcePlaysTheFile),
             DT_RUN(SourceRefusesWhatItCannotUse), DT_RUN(SinkWritesWhatIsSent),
             DT_RUN(LeavesAFileForTheExamples))
