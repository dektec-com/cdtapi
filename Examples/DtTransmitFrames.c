// #*#*#*#*#*#*#*#*#*#*#*#*# DtTransmitFrames.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Example: transmits raw SDI frames on an output channel
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Sends --count raw SDI frames on an output port. The frames come from:
//   --in        the files <in>0.raw, <in>1.raw and so on, as DtReceiveFrames --out
//               writes them; after the last file, the first comes again
//   otherwise   the examples' test pattern of the video standard --vidstd, with the
//               frame number in the picture (see ExamplePattern.h); the SDI builder
//               builds it into raw frames, with valid line numbers and CRCs
// --vidstd also sets the port to that standard; --linkstd says how a 4K standard is
// carried. The program prints a line per frame, as DtReceiveFrames does, so that the
// hashes of what one port sends and another receives can be compared:
//
//     9217800001:5  frame 0  7425000 bytes  hash 3C0F2E6D89A1B437
//
// --threads builds and converts the frames on a pool of that many threads, which 2160p
// needs. --flags prints the channel's latched flags at the end. The program
// waits until the card has sent every frame before it detaches. The port must be an
// output; DtConfigPort makes it one. Exits with 0 when every frame is written, 2 when
// there is no SDI output, and 1 when a call fails or the command line is wrong.

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
#include "Common/ExamplePattern.h" // The test pattern.
#include "cdtapi_sdi.h"            // The SDI builder.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The most frame files --in reads.
#define MAX_FILES 1000

static const ExampleOption g_Options[] = {
    {"--serial", true, "The device's serial number; the first device with an SDI output"},
    {"--port", true, "The port number; the first SDI output"},
    {"--vidstd", true,
     "Set the I/O standard and the pattern for this video standard, such as 1080I50"},
    {"--linkstd", true,
     "How 4K is carried: 0 or 1 four 3G links, 2 6G, 3 12G; default -1"},
    {"--txmode", true, "8B, 10B or 16B symbols; 10B without it"},
    {"--in", true, "Transmit the frames in <in>0.raw, <in>1.raw and so on"},
    {"--count", true, "The number of frames to transmit; without it one, or every file"},
    {"--flags", false, "Print the latched flags after the last frame"},
    {"--threads", true,
     "Build and code the frames over a pool of this many threads of the library's own, "
     "two or more; one thread without it"},
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsSdiOutput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns whether a port can be an SDI output, for Example_FindPort().
//
static bool IsSdiOutput(const DtHwFuncDesc* Port)
{
    return Port->IsSdi && Port->IsOutput;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Fnv1a64 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the 64-bit FNV-1a hash of Size bytes of Data, the hash DtReceiveFrames prints.
//
static uint64_t Fnv1a64(const char* Data, int Size)
{
    uint64_t Hash = 0xCBF29CE484222325ull;

    for (int i = 0; i < Size; i++)
    {
        Hash ^= (uint8_t)Data[i];
        Hash *= 0x100000001B3ull;
    }
    return Hash;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TxModeFrom -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets *TxMode and *BitsPerSymbol for the symbol size --txmode names: "8B", "10B"
// (also when not given) or "16B". Returns false for another name.
//
static bool TxModeFrom(const char* Name, int* TxMode, int* BitsPerSymbol)
{
    *TxMode = DTAPI_TXMODE_SDI_FULL;
    if (Name == NULL || strcmp(Name, "10B") == 0)
    {
        *TxMode |= DTAPI_TXMODE_SDI_10B;
        *BitsPerSymbol = 10;
    }
    else if (strcmp(Name, "8B") == 0)
        *BitsPerSymbol = 8;
    else if (strcmp(Name, "16B") == 0)
    {
        *TxMode |= DTAPI_TXMODE_SDI_16B;
        *BitsPerSymbol = 16;
    }
    else
        return false;
    return true;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame files +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The frames read from files.
typedef struct Frames
{
    int Count;             // Frames read
    char* Data[MAX_FILES]; // The frames
    int Size[MAX_FILES];   // Their sizes in bytes
} Frames;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadFile -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the file Path into a new buffer, and sets *Size to its size. Returns the buffer,
// for the caller to free, or NULL when the file cannot be read or is empty.
//
static char* ReadFile(const char* Path, int* Size)
{
    FILE* File = fopen(Path, "rb");
    if (File == NULL)
        return NULL;

    char* Data = NULL;
    long Length = -1;
    if (fseek(File, 0, SEEK_END) == 0)
        Length = ftell(File);
    if (Length > 0 && Length <= 0x7FFFFFFF && fseek(File, 0, SEEK_SET) == 0)
        Data = (char*)malloc((size_t)Length);
    if (Data != NULL && fread(Data, 1, (size_t)Length, File) != (size_t)Length)
    {
        free(Data);
        Data = NULL;
    }
    fclose(File);
    *Size = (int)Length;
    return Data;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadFrames -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the files <Prefix>0.raw, <Prefix>1.raw and so on into *Out, up to the first that
// does not exist. Returns false, after printing why, when there is none.
//
static bool ReadFrames(const char* Prefix, Frames* Out)
{
    Out->Count = 0;
    while (Out->Count < MAX_FILES)
    {
        char Path[1024];

        snprintf(Path, sizeof(Path), "%s%d.raw", Prefix, Out->Count);
        int Size = 0;
        char* Data = ReadFile(Path, &Size);
        if (Data == NULL)
            break;
        Out->Data[Out->Count] = Data;
        Out->Size[Out->Count] = Size;
        Out->Count++;
    }
    if (Out->Count == 0)
        printf("Cannot read %s0.raw\n", Prefix);
    return Out->Count > 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FreeFrames -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Frees the frames read into *Set.
//
static void FreeFrames(Frames* Set)
{
    for (int i = 0; i < Set->Count; i++)
        free(Set->Data[i]);
    Set->Count = 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test pattern +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The examples' test pattern, built into raw frames by the SDI builder. A raw frame holds
// every line, from its EAV on, in line number order, its symbols packed in the transmit
// mode's size, and is padded with zeros to a multiple of 8 bytes. The builder writes
// symbols of 10 or 16 bits. For the 8-bit transmit mode the program builds 16-bit
// symbols and keeps the top 8 bits of each, and leaves the line CRCs to the card: a CRC
// over the 10-bit values does not hold for the 8-bit values the card sends.
//

// The test pattern, and what builds its raw frames.
typedef struct Generator
{
    ExamplePattern Pattern; // The image, drawn for each frame
    DtSdiImage Image;       // The same image, as the builder reads it
    DtSdiBuilder* Builder;  // Builds the raw frames
    DtSdiView* View;        // Points the builder at Built
    int VidStd;             // The frames' video standard
    int BitsPerSymbol;      // Of the frames sent: 8, 10 or 16
    uint8_t* Built;         // The frame the builder writes, of 10-bit or 16-bit symbols
    size_t BuiltSize;       // Bytes of Built
    char* Frame;            // The frame sent: Built, or its 8-bit copy
    int FrameSize;          // Bytes of Frame
} Generator;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GeneratorInit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sets up *Gen for frames of VidStd with symbols of BitsPerSymbol bits. Returns false,
// after printing why, for a standard the builder does not build, such as a level-B 3G
// standard, or when there is not enough memory.
//
static bool GeneratorInit(Generator* Gen, int VidStd, int BitsPerSymbol)
{
    int Width = 0;
    int Height = 0;
    int Strides[3];
    const int BuiltBits = BitsPerSymbol == 10 ? 10 : 16;

    memset(Gen, 0, sizeof(*Gen));
    Gen->VidStd = VidStd;
    Gen->BitsPerSymbol = BitsPerSymbol;
    if (DtSdiImage_GetSize(VidStd, DT_SDI_PIXFMT_YUV422P_10B, &Width, &Height, Strides) !=
            DTAPI_OK ||
        DtSdiView_RawFrameSize(VidStd, BuiltBits, &Gen->BuiltSize) != DTAPI_OK)
    {
        printf("No test pattern for video standard %s\n", Example_VidStdName(VidStd));
        return false;
    }

    Gen->Builder = DtSdiBuilder_Alloc();
    Gen->View = DtSdiView_Alloc();
    Gen->Built = (uint8_t*)malloc(Gen->BuiltSize);
    if (BitsPerSymbol == 8)
    {
        Gen->FrameSize = (int)((Gen->BuiltSize / 2 + 7) / 8 * 8);
        Gen->Frame = (char*)calloc((size_t)Gen->FrameSize, 1);
    }
    else
    {
        Gen->FrameSize = (int)Gen->BuiltSize;
        Gen->Frame = (char*)Gen->Built;
    }
    if (Gen->Builder == NULL || Gen->View == NULL || Gen->Built == NULL ||
        Gen->Frame == NULL || !ExamplePattern_Init(&Gen->Pattern, Width, Height))
    {
        printf("Allocating: DTAPI_E_OUT_OF_MEM\n");
        return false;
    }
    DtSdiBuilder_SetChecksums(Gen->Builder, BitsPerSymbol != 8);

    // The pattern's planes hold 16-bit samples, line after line without a gap.
    Gen->Image.Format = DT_SDI_PIXFMT_YUV422P_10B;
    Gen->Image.Fields = DT_SDI_FIELDS_WOVEN;
    Gen->Image.Planes[0] = (uint8_t*)Gen->Pattern.Y;
    Gen->Image.Planes[1] = (uint8_t*)Gen->Pattern.Cb;
    Gen->Image.Planes[2] = (uint8_t*)Gen->Pattern.Cr;
    Gen->Image.Strides[0] = 2 * Width;
    Gen->Image.Strides[1] = Width;
    Gen->Image.Strides[2] = Width;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GeneratorFree -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Frees what GeneratorInit allocated. A zeroed *Gen is freed too.
//
static void GeneratorFree(Generator* Gen)
{
    if (Gen->Frame != (char*)Gen->Built)
        free(Gen->Frame);
    free(Gen->Built);
    DtSdiView_Free(Gen->View);
    DtSdiBuilder_Free(Gen->Builder);
    ExamplePattern_Free(&Gen->Pattern);
    memset(Gen, 0, sizeof(*Gen));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Makes frame Number of the pattern in Gen->Frame: draws the image, builds the raw frame,
// and for 8-bit symbols keeps the top 8 bits of each 16-bit symbol. Returns what the
// builder returns.
//
static unsigned int MakeFrame(Generator* Gen, int64_t Number)
{
    const int BuiltBits = Gen->BitsPerSymbol == 10 ? 10 : 16;

    ExamplePattern_Draw(&Gen->Pattern, Number);
    unsigned int Result = DtSdiView_SetRawFrame(Gen->View, Gen->Built, Gen->BuiltSize,
                                                Gen->VidStd, BuiltBits);
    if (Result == DTAPI_OK)
        Result = DtSdiBuilder_Build(Gen->Builder, Gen->View, &Gen->Image, NULL, NULL);
    if (Result != DTAPI_OK || Gen->BitsPerSymbol != 8)
        return Result;

    // Each 16-bit symbol is two bytes, least significant first, with its ten bits at the
    // bottom.
    const size_t Symbols = Gen->BuiltSize / 2;
    for (size_t i = 0; i < Symbols; i++)
    {
        const unsigned int Symbol =
            (unsigned int)Gen->Built[2 * i] | (unsigned int)Gen->Built[2 * i + 1] << 8;
        Gen->Frame[i] = (char)(Symbol >> 2 & 0xFF);
    }
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmit +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Where the frames come from.
typedef struct Source
{
    Frames Files;  // With --in
    Generator Gen; // Otherwise
} Source;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteNext -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes frame Number to the channel and prints its line. Returns the program's exit
// code.
//
static int WriteNext(DtOutpChannel* Channel, const DtHwFuncDesc* Port, Source* Src,
                     int64_t Number)
{
    char* Frame = Src->Gen.Frame;
    int Size = Src->Gen.FrameSize;

    if (Src->Files.Count > 0)
    {
        Frame = Src->Files.Data[Number % Src->Files.Count];
        Size = Src->Files.Size[Number % Src->Files.Count];
    }
    else
    {
        unsigned int Built = MakeFrame(&Src->Gen, Number);
        if (Built != DTAPI_OK)
            return Example_Failed("DtSdiBuilder_Build", Built);
    }

    unsigned int Result = DtOutpChannel_Write(Channel, Frame, Size);
    printf("%s  ", Port->DeviceName);
    if (Result != DTAPI_OK)
        return Example_Failed("DtOutpChannel_Write", Result);
    printf("frame %lld  %d bytes  hash %016llX\n", (long long)Number, Size,
           (unsigned long long)Fnv1a64(Frame, Size));
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Transmit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the first frame while the channel holds, then starts sending and writes the
// other frames. Returns the program's exit code.
//
static int Transmit(DtOutpChannel* Channel, const DtHwFuncDesc* Port, Source* Src,
                    int64_t Count, bool Flags)
{
    unsigned int Result = DtOutpChannel_SetTxControl(Channel, DTAPI_TXCTRL_HOLD);
    if (Result != DTAPI_OK)
    {
        printf("%s  ", Port->DeviceName);
        return Example_Failed("DtOutpChannel_SetTxControl", Result);
    }

    for (int64_t i = 0; i < Count; i++)
    {
        int Exit = WriteNext(Channel, Port, Src, i);
        if (Exit != EXAMPLE_OK)
            return Exit;
        if (i == 0)
        {
            Result = DtOutpChannel_SetTxControl(Channel, DTAPI_TXCTRL_SEND);
            if (Result != DTAPI_OK)
            {
                printf("%s  ", Port->DeviceName);
                return Example_Failed("DtOutpChannel_SetTxControl", Result);
            }
        }
    }

    if (Flags)
    {
        int Status = 0;
        int Latched = 0;

        Result = DtOutpChannel_GetFlags(Channel, &Status, &Latched);
        printf("%s  ", Port->DeviceName);
        if (Result != DTAPI_OK)
            return Example_Failed("DtOutpChannel_GetFlags", Result);
        printf("latched%s%s%s\n", (Latched & DTAPI_TX_FIFO_UFL) != 0 ? " FIFO_UFL" : "",
               (Latched & DTAPI_TX_DMA_UFL) != 0 ? " DMA_UFL" : "",
               (Latched & (DTAPI_TX_FIFO_UFL | DTAPI_TX_DMA_UFL)) == 0 ? " none" : "");
    }
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GivePool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Gives the channel a pool of NumThreads threads to convert its frames on, and the
// builder, when there is one, the same pool to build them on. Each picks how many of
// them a frame needs, for the standard it has (NumThreads 0 in SetWorkerPool): 4 for
// 2160p50 and 2160p60, 2 for 2160p24 to 2160p30, and none up to 3G-SDI. Both keep the
// pool, so the program releases its own reference at once.
//
static unsigned int GivePool(DtOutpChannel* Channel, DtSdiBuilder* Builder,
                             int NumThreads)
{
    DtWorkerPool* Pool = DtWorkerPool_Alloc();
    unsigned int Result =
        Pool == NULL ? DTAPI_E_OUT_OF_MEM : DtWorkerPool_StartThreads(Pool, NumThreads);

    if (Result == DTAPI_OK)
        Result = DtOutpChannel_SetWorkerPool(Channel, Pool, 0);
    if (Result == DTAPI_OK && Builder != NULL)
        Result = DtSdiBuilder_SetWorkerPool(Builder, Pool, 0);
    DtWorkerPool_Freep(&Pool);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AttachAndTransmit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Attaches Device and Channel to Port, gives the channel and the builder a pool of
// Threads threads when asked, sets the I/O standard when asked, then the transmit mode,
// and sends. The transmit mode comes last because changing the standard between SDI and
// ASI resets it. Returns the program's exit code.
//
static int AttachAndTransmit(DtDevice* Device, DtOutpChannel* Channel,
                             const DtHwFuncDesc* Port, int TxMode, int VidStd,
                             int LinkStd, Source* Src, int64_t Count, bool Flags,
                             int64_t Threads)
{
    unsigned int Result = DtDevice_AttachToSerial(Device, Port->SerialNumber);
    if (!Example_Succeeded(Result))
        return Example_Failed("DtDevice_AttachToSerial", Result);
    Result = DtOutpChannel_AttachToPort(Channel, Device, Port->Port);
    if (!Example_Succeeded(Result))
    {
        printf("%s  ", Port->DeviceName);
        return Example_Failed("DtOutpChannel_AttachToPort", Result);
    }

    const char* What = "DtOutpChannel_SetWorkerPool";
    Result = Threads > 0 ? GivePool(Channel, Src->Gen.Builder, (int)Threads) : DTAPI_OK;
    if (Result == DTAPI_OK && VidStd != DTAPI_VIDSTD_UNKNOWN)
    {
        int Value = -1;
        int SubValue = -1;

        What = "DtapiVidStd2IoStd";
        Result = DtapiVidStd2IoStd(VidStd, LinkStd, &Value, &SubValue);
        if (Result == DTAPI_OK)
        {
            What = "DtOutpChannel_SetIoConfig";
            Result = DtOutpChannel_SetIoConfig(Channel, DTAPI_IOCONFIG_IOSTD, Value,
                                               SubValue, -1, -1);
        }
    }
    if (Result == DTAPI_OK)
    {
        What = "DtOutpChannel_SetTxMode";
        Result = DtOutpChannel_SetTxMode(Channel, TxMode, 0);
    }
    if (Result != DTAPI_OK)
    {
        printf("%s  ", Port->DeviceName);
        Example_Failed(What, Result);
        DtOutpChannel_Detach(Channel, DTAPI_INSTANT_DETACH);
        return EXAMPLE_FAILED;
    }

    int Exit = Transmit(Channel, Port, Src, Count, Flags);
    Result = DtOutpChannel_Detach(Channel, Exit == EXAMPLE_OK ? DTAPI_WAIT_UNTIL_SENT
                                                              : DTAPI_INSTANT_DETACH);
    if (Exit == EXAMPLE_OK && Result != DTAPI_OK)
    {
        printf("%s  ", Port->DeviceName);
        Exit = Example_Failed("DtOutpChannel_Detach", Result);
    }
    return Exit;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LoadSource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the frame files In into *Src, or, when In is NULL, sets up the test pattern.
// Returns false, after printing why, when that fails.
//
static bool LoadSource(const char* In, int VidStd, int BitsPerSymbol, Source* Src)
{
    memset(Src, 0, sizeof(*Src));
    if (In != NULL)
        return ReadFrames(In, &Src->Files);

    if (VidStd == DTAPI_VIDSTD_UNKNOWN)
    {
        printf("Give --in, or --vidstd for the test pattern\n");
        return false;
    }
    return GeneratorInit(&Src->Gen, VidStd, BitsPerSymbol);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main(int Argc, char** Argv)
{
    int64_t Serial = 0;
    int64_t PortNumber = 0;
    int64_t Count = -1;
    int64_t LinkStd = -1;
    int64_t Threads = 0;
    if (!Example_CheckArguments(Argc, Argv, "Transmits raw SDI frames on an SDI output.",
                                g_Options,
                                (int)(sizeof(g_Options) / sizeof(g_Options[0]))) ||
        !Example_Int64(Argc, Argv, "--serial", &Serial) ||
        !Example_Int64(Argc, Argv, "--port", &PortNumber) ||
        !Example_Int64(Argc, Argv, "--count", &Count) ||
        !Example_Int64(Argc, Argv, "--linkstd", &LinkStd) ||
        !Example_Int64(Argc, Argv, "--threads", &Threads))
    {
        return EXAMPLE_FAILED;
    }
    int TxMode = 0;
    int BitsPerSymbol = 0;
    if (!TxModeFrom(Example_Value(Argc, Argv, "--txmode"), &TxMode, &BitsPerSymbol))
    {
        printf("Unknown transmit mode: %s; 8B, 10B or 16B\n",
               Example_Value(Argc, Argv, "--txmode"));
        return EXAMPLE_FAILED;
    }
    int VidStd = DTAPI_VIDSTD_UNKNOWN;
    const char* VidStdName = Example_Value(Argc, Argv, "--vidstd");
    if (VidStdName != NULL && !Example_VidStdFromName(VidStdName, &VidStd))
    {
        printf("Unknown video standard: %s\n", VidStdName);
        return EXAMPLE_FAILED;
    }

    Source Src;
    if (!LoadSource(Example_Value(Argc, Argv, "--in"), VidStd, BitsPerSymbol, &Src))
    {
        FreeFrames(&Src.Files);
        GeneratorFree(&Src.Gen);
        return EXAMPLE_FAILED;
    }
    if (Count < 0)
        Count = Src.Files.Count > 0 ? Src.Files.Count : 1;

    DtHwFuncDesc Port;
    int Exit = EXAMPLE_FAILED;
    unsigned int Result = Example_FindPort(Serial, (int)PortNumber, IsSdiOutput, &Port);
    DtDevice* Device = DtDevice_Alloc();
    DtOutpChannel* Channel = DtOutpChannel_Alloc();
    if (Result == DTAPI_E_NOT_FOUND)
    {
        printf("No SDI output that suits\n");
        Exit = EXAMPLE_NOTHING;
    }
    else if (Result != DTAPI_OK)
        Exit = Example_Failed("DtapiHwFuncScan", Result);
    else if (Device == NULL || Channel == NULL)
        Exit = Example_Failed("Allocating", DTAPI_E_OUT_OF_MEM);
    else
        Exit =
            AttachAndTransmit(Device, Channel, &Port, TxMode, VidStd, (int)LinkStd, &Src,
                              Count, Example_HasFlag(Argc, Argv, "--flags"), Threads);

    DtOutpChannel_Free(Channel);
    DtDevice_Free(Device);
    FreeFrames(&Src.Files);
    GeneratorFree(&Src.Gen);
    return Exit;
}
