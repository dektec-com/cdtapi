// #*#*#*#*#*#*#*#*#*#*#*#*#* DtReceiveFrames.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Example: receives raw SDI frames from an input channel
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Receives --count raw SDI frames from an input port, and prints a line per frame: its
// number, its size and a 64-bit hash of its bytes, the one DtTransmitFrames prints. A
// frame of the examples' test pattern ends the line with its frame number, as "number
// 1234", and with "gap of 3", "repeat" or "back" when the number does not count up by
// one (see ExamplePattern.h). The program reads the number from the one line that
// carries it, through a view of the raw frame; a 2160p frame, whose lines are spread
// over its four links, it parses whole. An 8-bit frame gives no number.
//
// --out also writes each frame to <out><number>.raw. --detect first detects the I/O
// standard of the signal and prints it. --threads converts the frames on a pool of that
// many threads, which 2160p needs on a slow processor:
//
//     9217800001:1  io standard HDSDI 1080I50
//
//     9217800001:1  frame 0  7425000 bytes  hash 3C0F2E6D89A1B437
//     9217800001:1  no frame within 1000 ms
//
// The port must be an input set to the signal's standard; DtConfigPort does that. Exits
// with 0 when every frame is received, 2 when a frame does not arrive in time, and 1
// when a call fails or the command line is wrong.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// MSVC deprecates fopen in favour of fopen_s, which other C libraries do not have. Asked
// for before any header.
#ifdef _MSC_VER
    #define _CRT_SECURE_NO_WARNINGS
#endif

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Example includes
#include "Common/ExampleCommon.h"  // The API and what the examples share.
#include "Common/ExamplePattern.h" // Reading the test pattern's frame number.
#include "cdtapi_sdi.h"            // The view and the parser.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Room for the largest frame an input channel delivers: 2160p24 on one 6G link, with
// 16-bit symbols, 49 500 000 bytes.
#define FRAME_BUFFER_SIZE (48 * 1024 * 1024)

static const ExampleOption g_Options[] = {
    {"--serial", true, "The device's serial number; the first device with an SDI input"},
    {"--port", true, "The port number; the first SDI input"},
    {"--vidstd", true,
     "Set the input to this video standard, such as 1080P50B; the port's own without it"},
    {"--count", true, "The number of frames to receive; 1 without it"},
    {"--rxmode", true, "8B, 10B or 16B symbols; 10B without it"},
    {"--timeout", true, "Milliseconds to wait for each frame; 1000 without it"},
    {"--out", true, "Write each frame to <out><number>.raw"},
    {"--detect", false, "First detect the input's I/O standard through the channel"},
    {"--threads", true,
     "Convert over a pool of this many threads of the library's own, two or more; one "
     "thread without it"},
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsSdiInput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns whether a port can be an SDI input, for Example_FindPort().
//
static bool IsSdiInput(const DtHwFuncDesc* Port)
{
    return Port->IsSdi && Port->IsInput;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RxModeFrom -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets *RxMode to the receive mode for the symbol size --rxmode names: "8B", "10B"
// (also when not given) or "16B". Returns false for another name.
//
static bool RxModeFrom(const char* Name, int* RxMode)
{
    if (Name == NULL || strcmp(Name, "10B") == 0)
        *RxMode = DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B;
    else if (strcmp(Name, "8B") == 0)
        *RxMode = DTAPI_RXMODE_SDI_FULL;
    else if (strcmp(Name, "16B") == 0)
        *RxMode = DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_16B;
    else
        return false;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes a frame of Size bytes to the file <Prefix><Number>.raw. Returns false when
// that fails.
//
static bool WriteFrame(const char* Prefix, int64_t Number, const char* Frame, int Size)
{
    char Path[1024];

    snprintf(Path, sizeof(Path), "%s%lld.raw", Prefix, (long long)Number);
    FILE* File = fopen(Path, "wb");
    if (File == NULL)
        return false;
    bool Written = fwrite(Frame, 1, (size_t)Size, File) == (size_t)Size;
    return fclose(File) == 0 && Written;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame numbers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// What the program needs to read the frame number of the examples' test pattern from a
// raw frame.
typedef struct NumberReader
{
    int VidStd;          // The frames' video standard
    int BitsPerSymbol;   // 10 or 16
    int Width;           // The image width in pixels
    int Height;          // The image height in lines
    DtSdiView* View;     // Points at the frame just received; NULL to read no numbers
    uint16_t* Luma;      // The luma of the code's line
    DtSdiParser* Parser; // 2160p and 3G level B only: parses the whole image
    DtSdiImage Image;    // 2160p and 3G level B only: the image, planar 10-bit
    bool LevelB;         // The frames are of 3G level B: two pictures each
    ExampleNumberCheck Check;
} NumberReader;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsLevelB -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns whether VidStd is a standard of 3G level B: only a raw frame of level B has a
// field to choose.
//
static bool IsLevelB(int VidStd, int BitsPerSymbol)
{
    size_t Size = 0;
    if (DtSdiView_RawFrameSize(VidStd, BitsPerSymbol, &Size) != DTAPI_OK)
        return false;
    uint8_t* Frame = (uint8_t*)calloc(Size, 1);
    DtSdiView* View = DtSdiView_Alloc();
    const bool LevelB =
        Frame != NULL && View != NULL &&
        DtSdiView_SetRawFrame(View, Frame, Size, VidStd, BitsPerSymbol) == DTAPI_OK &&
        DtSdiView_SetLevelBField(View, 1) == DTAPI_OK;
    DtSdiView_Free(View);
    free(Frame);
    return LevelB;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NumberReaderInit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets up *Reader for the frames Channel receives in RxMode. When the frames have 8-bit
// symbols, or the port's standard has no image, *Reader reads no numbers. Returns false
// when there is not enough memory.
//
static bool NumberReaderInit(NumberReader* Reader, DtInpChannel* Channel, int RxMode)
{
    int Value = -1;
    int Strides[3];

    memset(Reader, 0, sizeof(*Reader));
    Reader->BitsPerSymbol = (RxMode & DTAPI_RXMODE_SDI_10B) == DTAPI_RXMODE_SDI_10B   ? 10
                            : (RxMode & DTAPI_RXMODE_SDI_16B) == DTAPI_RXMODE_SDI_16B ? 16
                                                                                      : 8;
    if (Reader->BitsPerSymbol == 8 ||
        DtInpChannel_GetIoConfig(Channel, DTAPI_IOCONFIG_IOSTD, &Value, &Reader->VidStd,
                                 NULL, NULL) != DTAPI_OK ||
        DtSdiImage_GetSize(Reader->VidStd, DT_SDI_PIXFMT_YUV422P_10B, &Reader->Width,
                           &Reader->Height, Strides) != DTAPI_OK)
    {
        return true;
    }

    Reader->View = DtSdiView_Alloc();
    Reader->Luma = (uint16_t*)malloc((size_t)Reader->Width * sizeof(uint16_t));
    if (Reader->View == NULL || Reader->Luma == NULL)
        return false;
    Reader->LevelB = IsLevelB(Reader->VidStd, Reader->BitsPerSymbol);
    if (Reader->Height < 2160 && !Reader->LevelB)
        return true;

    // A 2160p line is spread over the four links, and a line of 3G level B over two, so
    // the whole image is parsed.
    Reader->Parser = DtSdiParser_Alloc();
    if (Reader->Parser == NULL)
        return false;
    Reader->Image.Format = DT_SDI_PIXFMT_YUV422P_10B;
    Reader->Image.Fields = DT_SDI_FIELDS_WOVEN;
    for (int p = 0; p < 3; p++)
    {
        Reader->Image.Strides[p] = Strides[p];
        Reader->Image.Planes[p] =
            (uint8_t*)malloc((size_t)Strides[p] * (size_t)Reader->Height);
        if (Reader->Image.Planes[p] == NULL)
            return false;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NumberReaderFree -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Frees what NumberReaderInit allocated. A zeroed *Reader is freed too.
//
static void NumberReaderFree(NumberReader* Reader)
{
    for (int p = 0; p < 3; p++)
        free(Reader->Image.Planes[p]);
    DtSdiParser_Free(Reader->Parser);
    free(Reader->Luma);
    DtSdiView_Free(Reader->View);
    memset(Reader, 0, sizeof(*Reader));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadPictureNumber -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the number of one picture of the raw frame Frame, of Size bytes, as ReadNumber
// describes: for a frame of 3G level B that of field Field, else Field is 0.
//
static void ReadPictureNumber(NumberReader* Reader, char* Frame, int Size, int Field,
                              char* Text, size_t TextSize)
{
    const int Line = ExamplePattern_CodeLine(Reader->Height);
    uint32_t Number = 0;
    bool Found = false;

    if (Reader->View != NULL &&
        DtSdiView_SetRawFrame(Reader->View, Frame, (size_t)Size, Reader->VidStd,
                              Reader->BitsPerSymbol) == DTAPI_OK &&
        (Field == 0 || DtSdiView_SetLevelBField(Reader->View, Field) == DTAPI_OK))
    {
        DtSdiSymbolPtr Symbols;
        if (Reader->Parser != NULL)
        {
            if (DtSdiParser_Parse(Reader->Parser, Reader->View, &Reader->Image, NULL,
                                  NULL) == DTAPI_OK)
            {
                const uint8_t* Luma = Reader->Image.Planes[0] +
                                      (size_t)Line * (size_t)Reader->Image.Strides[0];
                Found = ExamplePattern_ReadNumber((const uint16_t*)Luma, Reader->Width,
                                                  &Number);
            }
        }
        else if (DtSdiView_GetActiveLine(Reader->View, Line, &Symbols) == DTAPI_OK)
        {
            for (int x = 0; x < Reader->Width; x++)
                Reader->Luma[x] = DtSdiSymbolPtr_Get(&Symbols, 2 * (size_t)x + 1);
            Found = ExamplePattern_ReadNumber(Reader->Luma, Reader->Width, &Number);
        }
    }
    ExampleNumberCheck_Describe(&Reader->Check, Found, Number, Text, TextSize);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadNumber -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the frame number from the raw frame Frame, of Size bytes, and writes what the
// frame's line says about it to Text, of TextSize bytes; see
// ExampleNumberCheck_Describe(). Each luma sample is every second symbol of the line,
// after the Cb or Cr before it. A frame of 3G level B holds two pictures, each with its
// number: field 1's is written first, then field 2's.
//
static void ReadNumber(NumberReader* Reader, char* Frame, int Size, char* Text,
                       size_t TextSize)
{
    if (Reader->LevelB && TextSize > 0)
    {
        Text[0] = '\0';
        for (int Field = 1; Field <= 2; Field++)
        {
            char Part[64];
            ReadPictureNumber(Reader, Frame, Size, Field, Part, sizeof(Part));
            // Each part starts with the two spaces that set it off from the hash, and
            // may end in spaces; the second follows the first after a comma.
            size_t Length = strlen(Part);
            while (Length > 0 && Part[Length - 1] == ' ')
                Part[--Length] = '\0';
            const char* From = Part;
            while (Field == 2 && *From == ' ')
                From++;
            const size_t Used = strlen(Text);
            snprintf(Text + Used, TextSize - Used, "%s%s", Field == 1 ? "" : ", ", From);
        }
        return;
    }
    ReadPictureNumber(Reader, Frame, Size, 0, Text, TextSize);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Receive +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Receive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sets the receive mode, starts receiving and reads Count frames, waiting up to
// TimeoutMs for each. Returns the program's exit code.
//
static int Receive(DtInpChannel* Channel, const DtHwFuncDesc* Port, int RxMode,
                   int64_t Count, int64_t TimeoutMs, const char* Out, char* Frame,
                   NumberReader* Reader)
{
    unsigned int Result = DtInpChannel_SetRxMode(Channel, RxMode);

    if (Result != DTAPI_OK)
        return Example_Failed("DtInpChannel_SetRxMode", Result);
    Result = DtInpChannel_SetRxControl(Channel, DTAPI_RXCTRL_RCV);
    if (Result != DTAPI_OK)
        return Example_Failed("DtInpChannel_SetRxControl", Result);

    for (int64_t i = 0; i < Count; i++)
    {
        int Size = FRAME_BUFFER_SIZE;

        Result = DtInpChannel_ReadFrame(Channel, Frame, &Size, (int)TimeoutMs);
        if (Result == DTAPI_E_TIMEOUT)
        {
            printf("%s  no frame within %lld ms\n", Port->DeviceName,
                   (long long)TimeoutMs);
            return EXAMPLE_NOTHING;
        }
        if (Result != DTAPI_OK)
        {
            printf("%s  ", Port->DeviceName);
            return Example_Failed("DtInpChannel_ReadFrame", Result);
        }

        char Text[64];
        ReadNumber(Reader, Frame, Size, Text, sizeof(Text));
        printf("%s  frame %lld  %d bytes  hash %016llX%s\n", Port->DeviceName,
               (long long)i, Size, (unsigned long long)Example_Hash(Frame, (size_t)Size),
               Text);
        if (Out != NULL && !WriteFrame(Out, i, Frame, Size))
        {
            printf("Cannot write frame %lld to %s\n", (long long)i, Out);
            return EXAMPLE_FAILED;
        }
    }
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GivePool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Gives the channel a pool of NumThreads threads to convert its frames on, and the
// parser, when there is one, the same pool. Each picks how many of them a frame needs
// (NumThreads 0 in SetWorkerPool): 4 for 2160p50 and 2160p60, 2 for 2160p24 to 2160p30,
// and none up to 3G-SDI. Both keep the pool, so the program releases its own reference
// at once.
//
static unsigned int GivePool(DtInpChannel* Channel, DtSdiParser* Parser, int NumThreads)
{
    DtWorkerPool* Pool = DtWorkerPool_Alloc();
    unsigned int Result =
        Pool == NULL ? DTAPI_E_OUT_OF_MEM : DtWorkerPool_StartThreads(Pool, NumThreads);

    if (Result == DTAPI_OK)
        Result = DtInpChannel_SetWorkerPool(Channel, Pool, 0);
    if (Result == DTAPI_OK && Parser != NULL)
        Result = DtSdiParser_SetWorkerPool(Parser, Pool, 0);
    DtWorkerPool_Freep(&Pool);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AttachAndReceive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Attaches Device and Channel to Port, sets up the reader of the frame numbers, gives the
// channel and its parser a pool of Threads threads when asked, detects and prints the
// I/O standard when asked, and receives the frames.
// Returns the program's exit code.
//
static int AttachAndReceive(DtDevice* Device, DtInpChannel* Channel, char* Frame,
                            const DtHwFuncDesc* Port, int Argc, char** Argv, int RxMode,
                            int64_t Count, int64_t TimeoutMs, int64_t Threads)
{
    unsigned int Result = DtDevice_AttachToSerial(Device, Port->SerialNumber);
    if (!Example_Succeeded(Result))
        return Example_Failed("DtDevice_AttachToSerial", Result);
    Result = DtInpChannel_AttachToPort(Channel, Device, Port->Port);
    if (!Example_Succeeded(Result))
    {
        printf("%s  ", Port->DeviceName);
        return Example_Failed("DtInpChannel_AttachToPort", Result);
    }
    const char* VidStdName = Example_Value(Argc, Argv, "--vidstd");
    int VidStd = DTAPI_VIDSTD_UNKNOWN;
    if (VidStdName != NULL && Example_VidStdFromName(VidStdName, &VidStd))
    {
        int Value = -1;
        int SubValue = -1;
        const char* What = "DtapiVidStd2IoStd";
        Result = DtapiVidStd2IoStd(VidStd, -1, &Value, &SubValue);
        if (Result == DTAPI_OK)
        {
            What = "DtInpChannel_SetIoConfig";
            Result = DtInpChannel_SetIoConfig(Channel, DTAPI_IOCONFIG_IOSTD, Value,
                                              SubValue, -1, -1);
        }
        if (Result != DTAPI_OK)
        {
            printf("%s  ", Port->DeviceName);
            DtInpChannel_Detach(Channel, DTAPI_INSTANT_DETACH);
            return Example_Failed(What, Result);
        }
    }
    // The reader of the frame numbers needs the port's standard, and gets the pool for
    // its parser.
    NumberReader Reader;
    if (!NumberReaderInit(&Reader, Channel, RxMode))
    {
        NumberReaderFree(&Reader);
        DtInpChannel_Detach(Channel, DTAPI_INSTANT_DETACH);
        return Example_Failed("Allocating", DTAPI_E_OUT_OF_MEM);
    }
    Result = Threads > 0 ? GivePool(Channel, Reader.Parser, (int)Threads) : DTAPI_OK;
    if (Result != DTAPI_OK)
    {
        printf("%s  ", Port->DeviceName);
        Example_Failed("SetWorkerPool", Result);
        NumberReaderFree(&Reader);
        DtInpChannel_Detach(Channel, DTAPI_INSTANT_DETACH);
        return EXAMPLE_FAILED;
    }

    if (Example_HasFlag(Argc, Argv, "--detect"))
    {
        int Value = -1;
        int SubValue = -1;

        Result = DtInpChannel_DetectIoStd(Channel, &Value, &SubValue);
        printf("%s  ", Port->DeviceName);
        if (Result != DTAPI_OK)
            Example_Failed("DtInpChannel_DetectIoStd", Result);
        else
        {
            const char* Name = Example_VidStdName(SubValue);
            printf("io standard %s %s\n", Example_IoStdName(Value),
                   Name != NULL ? Name : "?");
        }
    }

    int Exit = Receive(Channel, Port, RxMode, Count, TimeoutMs,
                       Example_Value(Argc, Argv, "--out"), Frame, &Reader);
    NumberReaderFree(&Reader);
    DtInpChannel_Detach(Channel, DTAPI_INSTANT_DETACH);
    return Exit;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main(int Argc, char** Argv)
{
    int64_t Serial = 0;
    int64_t PortNumber = 0;
    int64_t Count = 1;
    int64_t TimeoutMs = 1000;
    int64_t Threads = 0;
    if (!Example_CheckArguments(Argc, Argv, "Receives raw SDI frames from an SDI input.",
                                g_Options,
                                (int)(sizeof(g_Options) / sizeof(g_Options[0]))) ||
        !Example_Int64(Argc, Argv, "--serial", &Serial) ||
        !Example_Int64(Argc, Argv, "--port", &PortNumber) ||
        !Example_Int64(Argc, Argv, "--count", &Count) ||
        !Example_Int64(Argc, Argv, "--timeout", &TimeoutMs) ||
        !Example_Int64(Argc, Argv, "--threads", &Threads))
    {
        return EXAMPLE_FAILED;
    }
    int VidStd = DTAPI_VIDSTD_UNKNOWN;
    const char* VidStdName = Example_Value(Argc, Argv, "--vidstd");
    if (VidStdName != NULL && !Example_VidStdFromName(VidStdName, &VidStd))
    {
        printf("Unknown video standard: %s\n", VidStdName);
        return EXAMPLE_FAILED;
    }
    int RxMode = 0;
    if (!RxModeFrom(Example_Value(Argc, Argv, "--rxmode"), &RxMode))
    {
        printf("Unknown receive mode: %s; 8B, 10B or 16B\n",
               Example_Value(Argc, Argv, "--rxmode"));
        return EXAMPLE_FAILED;
    }

    DtHwFuncDesc Port;
    unsigned int Result = Example_FindPort(Serial, (int)PortNumber, IsSdiInput, &Port);
    if (Result == DTAPI_E_NOT_FOUND)
    {
        printf("No SDI input that suits\n");
        return EXAMPLE_NOTHING;
    }
    if (Result != DTAPI_OK)
        return Example_Failed("DtapiHwFuncScan", Result);

    DtDevice* Device = DtDevice_Alloc();
    DtInpChannel* Channel = DtInpChannel_Alloc();
    char* Frame = (char*)malloc(FRAME_BUFFER_SIZE);
    int Exit;
    if (Device == NULL || Channel == NULL || Frame == NULL)
        Exit = Example_Failed("Allocating", DTAPI_E_OUT_OF_MEM);
    else
        Exit = AttachAndReceive(Device, Channel, Frame, &Port, Argc, Argv, RxMode, Count,
                                TimeoutMs, Threads);

    DtInpChannel_Free(Channel);
    DtDevice_Free(Device);
    free(Frame);
    return Exit;
}
