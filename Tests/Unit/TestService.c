// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestService.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Tests for the pipe and the messages to and from DtapiService
//
// SPDX-License-Identifier: BSD-3-Clause
//
// A fake service in this file listens on a pipe of its own, a named pipe in message mode
// on Windows and a listener FIFO in a directory of its own on Linux, and answers as
// DtapiService does: so the tests take the same path through the operating system as a
// real connection, without needing or disturbing the real service.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#ifndef _WIN32
    // With -std=c11 the C library declares only ISO C; this exposes the POSIX calls the
    // fake service makes.
    #define _GNU_SOURCE
#endif

// Standard includes
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Platform includes
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <errno.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

// CDTAPI includes
#include "Core/DtAlloc.h"           // Allocation seam.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsPipe.h"             // Interface under test.
#include "OAL/OsThread.h"           // The fake service's thread.
#include "Service/DtPtp.h"          // Interface under test.
#include "Service/DtService.h"      // Interface under test.
#include "Service/DtServiceProxy.h" // Interface under test.
#include "Service/DtXml.h"          // The fake PTP slave's XML.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Fake service +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// How long the fake service waits for the client before it gives up, so that a broken
// test ends rather than hangs.
#define FAKE_WAIT_MS 10000

// What the fake service does with each command.
typedef enum FakeBehaviour
{
    FAKE_ECHO,      // Answers with the command's own XML
    FAKE_EXCEPTION, // Answers with exception 20, ExclusiveInUse, and no XML
    FAKE_WRONG_CMD, // Answers as if to the next command
    FAKE_BAD_TEXT,  // Answers with XML that does not end in a zero character
    FAKE_CLOSE,     // Reads the command and closes the pipe without answering
    FAKE_PTP,       // Answers as DtapiService's PTP clock slave, as FakePtp says
} FakeBehaviour;

// The PTP clock slave FAKE_PTP plays: what it answers, and what it was asked.
typedef struct FakePtp
{
    int VersionMajor;    // The service's major version
    int AttachException; // The exception ATTACH answers, or DT_SERVICE_NO_EXCEPTION
    const char* Missing; // A parameter left out of the descriptions, or NULL
    int SlaveState;
    int LockStatus;
    int Domain;
    uint64_t MasterIdentity;
    const char* MasterInfo; // The value of MasterInfo
    bool Attached;          // Whether ATTACH came, with the values below
    int64_t AttachSerial;
    int64_t AttachPort;
    bool AttachExclusive;
    bool Detached; // Whether DETACH came
} FakePtp;

typedef struct FakeService
{
    FakeBehaviour Behaviour;
    char PipeName[64];
    int NumCommands; // The commands answered, or read for FAKE_CLOSE
    bool GotCleanup; // Whether CLEANUP_CONNECTION came
    FakePtp* Ptp;    // The slave FAKE_PTP plays
    OsThread* Thread;
#ifdef _WIN32
    HANDLE Pipe;
#else
    char Dir[64]; // The directory of the FIFOs, set as DTAPI_PIPES_PATH
    char ListenerPath[128];
    int Listener;
    int ReadFd;
    int WriteFd;
#endif
} FakeService;

#ifdef _WIN32

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeListen -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Creates the pipe, in message mode as DtapiService's.
//
static bool FakeListen(FakeService* Fake)
{
    snprintf(Fake->PipeName, sizeof(Fake->PipeName), "CdtapiTestService_%lu",
             (unsigned long)GetCurrentProcessId());
    char Path[128];
    snprintf(Path, sizeof(Path), "\\\\.\\pipe\\%s", Fake->PipeName);
    Fake->Pipe = CreateNamedPipeA(Path, PIPE_ACCESS_DUPLEX,
                                  PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                  1, 65536, 65536, 0, NULL);
    return Fake->Pipe != INVALID_HANDLE_VALUE;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeAccept -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool FakeAccept(FakeService* Fake)
{
    return ConnectNamedPipe(Fake->Pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeRead -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads Size bytes. A message longer than what is asked for is read in parts.
//
static bool FakeRead(FakeService* Fake, uint8_t* Buf, size_t Size)
{
    while (Size > 0)
    {
        DWORD Done = 0;
        if (!ReadFile(Fake->Pipe, Buf, (DWORD)Size, &Done, NULL) &&
            GetLastError() != ERROR_MORE_DATA)
            return false;
        Buf += Done;
        Size -= Done;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeWrite -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool FakeWrite(FakeService* Fake, const uint8_t* Buf, size_t Size)
{
    DWORD Done = 0;
    return WriteFile(Fake->Pipe, Buf, (DWORD)Size, &Done, NULL) && Done == Size;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeHangUp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void FakeHangUp(FakeService* Fake)
{
    if (Fake->Pipe != INVALID_HANDLE_VALUE)
    {
        DisconnectNamedPipe(Fake->Pipe);
        CloseHandle(Fake->Pipe);
        Fake->Pipe = INVALID_HANDLE_VALUE;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeRemove -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void FakeRemove(FakeService* Fake)
{
    (void)Fake;
}

#else

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeListen -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Makes a directory of its own with the listener FIFO in it, and points the client at
// it through DTAPI_PIPES_PATH. The listener is opened for reading and writing, so that
// the open does not wait for a client and a client's open finds a reader.
//
static bool FakeListen(FakeService* Fake)
{
    Fake->Listener = -1;
    Fake->ReadFd = -1;
    Fake->WriteFd = -1;
    snprintf(Fake->Dir, sizeof(Fake->Dir), "%s", "/tmp/cdtapi-service-XXXXXX");
    if (mkdtemp(Fake->Dir) == NULL)
        return false;
    snprintf(Fake->PipeName, sizeof(Fake->PipeName), "%s", "TestService");
    snprintf(Fake->ListenerPath, sizeof(Fake->ListenerPath), "%s/%s", Fake->Dir,
             Fake->PipeName);
    if (mkfifo(Fake->ListenerPath, 0600) != 0)
        return false;
    Fake->Listener = open(Fake->ListenerPath, O_RDWR);
    return Fake->Listener >= 0 && setenv("DTAPI_PIPES_PATH", Fake->Dir, 1) == 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeWaitReadable -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Waits until Fd has bytes, or its writer has closed it. A FIFO that no writer has
// opened yet does not count as closed.
//
static bool FakeWaitReadable(int Fd)
{
    struct pollfd Poll = {Fd, POLLIN, 0};
    return poll(&Poll, 1, FAKE_WAIT_MS) > 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadFd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool ReadFd(int Fd, uint8_t* Buf, size_t Size)
{
    while (Size > 0)
    {
        if (!FakeWaitReadable(Fd))
            return false;
        ssize_t Done = read(Fd, Buf, Size);
        if (Done < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
        if (Done <= 0)
            return false;
        Buf += Done;
        Size -= (size_t)Done;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeAccept -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the client's connect message, opens its two FIFOs, and answers 0.
//
static bool FakeAccept(FakeService* Fake)
{
    char Msg[200];
    if (!ReadFd(Fake->Listener, (uint8_t*)Msg, sizeof(Msg)))
        return false;
    Msg[99] = '\0';
    Msg[199] = '\0';
    Fake->ReadFd = open(Msg, O_RDONLY | O_NONBLOCK);
    Fake->WriteFd = open(Msg + 100, O_WRONLY);
    if (Fake->ReadFd < 0 || Fake->WriteFd < 0)
        return false;
    const uint8_t Ok[4] = {0, 0, 0, 0};
    return write(Fake->WriteFd, Ok, sizeof(Ok)) == (ssize_t)sizeof(Ok);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeRead -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool FakeRead(FakeService* Fake, uint8_t* Buf, size_t Size)
{
    return ReadFd(Fake->ReadFd, Buf, Size);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeWrite -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool FakeWrite(FakeService* Fake, const uint8_t* Buf, size_t Size)
{
    while (Size > 0)
    {
        ssize_t Done = write(Fake->WriteFd, Buf, Size);
        if (Done < 0 && errno == EINTR)
            continue;
        if (Done <= 0)
            return false;
        Buf += Done;
        Size -= (size_t)Done;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeHangUp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void FakeHangUp(FakeService* Fake)
{
    if (Fake->ReadFd >= 0)
        close(Fake->ReadFd);
    if (Fake->WriteFd >= 0)
        close(Fake->WriteFd);
    Fake->ReadFd = -1;
    Fake->WriteFd = -1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeRemove -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Removes the listener and the directory, which the client's FIFOs have left already.
//
static void FakeRemove(FakeService* Fake)
{
    if (Fake->Listener >= 0)
        close(Fake->Listener);
    Fake->Listener = -1;
    unlink(Fake->ListenerPath);
    rmdir(Fake->Dir);
}

#endif

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeReadMsg -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads a message with its length prefix into a new *Msg, freed with free.
//
static bool FakeReadMsg(FakeService* Fake, uint8_t** Msg, size_t* Size)
{
    uint8_t Prefix[8];
    if (!FakeRead(Fake, Prefix, 2))
        return false;
    size_t Length = (size_t)Prefix[0] | (size_t)Prefix[1] << 8;
    if (Length == 0xFFFF)
    {
        if (!FakeRead(Fake, Prefix + 2, 6))
            return false;
        Length = (size_t)Prefix[4] | (size_t)Prefix[5] << 8 | (size_t)Prefix[6] << 16 |
                 (size_t)Prefix[7] << 24;
    }
    *Msg = (uint8_t*)malloc(Length + 1);
    *Size = Length;
    return *Msg != NULL && FakeRead(Fake, *Msg, Length);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeWriteMsg -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the length prefix and the message in two writes, as DtapiService does.
//
static bool FakeWriteMsg(FakeService* Fake, const uint8_t* Msg, size_t Size)
{
    uint8_t Prefix[8];
    size_t PrefixSize = DtService_EncodeLength((uint32_t)Size, Prefix);
    return FakeWrite(Fake, Prefix, PrefixSize) && FakeWrite(Fake, Msg, Size);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeAnswer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Answers command Msg as the behaviour says.
//
static bool FakeAnswer(FakeService* Fake, const uint8_t* Msg, size_t Size)
{
    uint8_t Header[8];
    memcpy(Header, Msg, 4);
    memset(Header + 4, 0xFF, 4);
    if (Fake->Behaviour == FAKE_WRONG_CMD)
        Header[0]++;
    if (Fake->Behaviour == FAKE_EXCEPTION)
    {
        memset(Header + 4, 0, 4);
        Header[4] = 20;
        return FakeWriteMsg(Fake, Header, sizeof(Header));
    }

    // Echo the command's text; to make it bad, drop its zero character.
    size_t TextSize = Size - 4;
    if (Fake->Behaviour == FAKE_BAD_TEXT)
        TextSize -= DT_SERVICE_CHAR_SIZE;
    uint8_t* Answer = (uint8_t*)malloc(8 + TextSize);
    if (Answer == NULL)
        return false;
    memcpy(Answer, Header, 8);
    memcpy(Answer + 8, Msg + 4, TextSize);
    bool Written = FakeWriteMsg(Fake, Answer, 8 + TextSize);
    free(Answer);
    return Written;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeReply -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Answers command Cmd with the exception Exception, or, without one, with Xml.
//
static bool FakeReply(FakeService* Fake, uint8_t Cmd, int Exception, const char* Xml)
{
    uint8_t* Wire = NULL;
    size_t WireSize = 0;
    if (Exception == DT_SERVICE_NO_EXCEPTION &&
        DtService_TextToWire(Xml, DT_SERVICE_CHAR_SIZE, &Wire, &WireSize) != DTAPI_OK)
        return false;
    uint8_t* Answer = (uint8_t*)malloc(8 + WireSize);
    if (Answer == NULL)
    {
        DtAlloc_Free(Wire);
        return false;
    }
    uint32_t Reason = (uint32_t)Exception;
    uint8_t Header[8] = {Cmd,
                         0,
                         0,
                         0,
                         (uint8_t)Reason,
                         (uint8_t)(Reason >> 8),
                         (uint8_t)(Reason >> 16),
                         (uint8_t)(Reason >> 24)};
    memcpy(Answer, Header, 8);
    if (WireSize > 0)
        memcpy(Answer + 8, Wire, WireSize);
    bool Written = FakeWriteMsg(Fake, Answer, 8 + WireSize);
    free(Answer);
    DtAlloc_Free(Wire);
    return Written;
}

// The parameters of the fake PTP slave. Their IDs are not those of DtapiService 5.2.8,
// so that a client that does not look them up by name fails.
typedef struct FakePar
{
    const char* Name;
    int ParId;
    int Type; // The place in Variant::Type: 2 Int, 3 UInt64, 4 Bool, 5 String
} FakePar;
static const FakePar FakePars[] = {
    {"ClockOffset", 1, 2},          {"Enable", 5, 4},        {"SlaveState", 40, 2},
    {"MasterLockingStatus", 41, 2}, {"DomainNumber", 42, 2}, {"MasterIdentity", 43, 3},
    {"MasterInfo", 44, 5},
};
#define FAKE_NUM_PARS (int)(sizeof(FakePars) / sizeof(FakePars[0]))

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeParDescs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The descriptions of the group General, without the one the case leaves out.
//
static char* FakeParDescs(const FakePtp* Ptp)
{
    DtXmlOut Out;
    DtXmlOut_Init(&Out);
    DtXmlOut_Open(&Out, "ParDescs");
    int Count = 0;
    for (int i = 0; i < FAKE_NUM_PARS; i++)
        Count += Ptp->Missing == NULL || strcmp(FakePars[i].Name, Ptp->Missing) != 0;
    DtXmlOut_AttrInt(&Out, "Cnt", Count);
    for (int i = 0; i < FAKE_NUM_PARS; i++)
    {
        if (Ptp->Missing != NULL && strcmp(FakePars[i].Name, Ptp->Missing) == 0)
            continue;
        DtXmlOut_Open(&Out, "ParDesc");
        DtXmlOut_AttrInt(&Out, "ParId", FakePars[i].ParId);
        DtXmlOut_Attr(&Out, "Name", FakePars[i].Name);
        DtXmlOut_Attr(&Out, "Desc", "A parameter <of> the \"fake\" slave & more.");
        DtXmlOut_AttrInt(&Out, "VT", FakePars[i].Type);
        DtXmlOut_AttrBool(&Out, "W", false);
        DtXmlOut_AttrInt(&Out, "ECount", 0);
        DtVariant Zero;
        memset(&Zero, 0, sizeof(Zero));
        Zero.Type = (DtVariantType)FakePars[i].Type;
        DtService_WriteVariant(&Out, "Min", &Zero);
        DtService_WriteVariant(&Out, "Max", &Zero);
        DtService_WriteVariant(&Out, "Def", &Zero);
        DtXmlOut_Close(&Out, "ParDesc");
    }
    DtXmlOut_Close(&Out, "ParDescs");
    char* Text = NULL;
    DtXmlOut_Finish(&Out, &Text);
    return Text;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeParVals -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The values of the parameters Request asks for, <ParIds Cnt="n"> with an I per ID.
// Sets *Exception to 2, InvalidParId, for an ID it does not have.
//
static char* FakeParVals(const FakePtp* Ptp, const DtXmlElem* Request, int* Exception)
{
    DtXmlOut Out;
    DtXmlOut_Init(&Out);
    DtXmlOut_Open(&Out, "ParIdVals");
    DtXmlOut_AttrInt(&Out, "Cnt", Request->NumChildren);
    for (int i = 0; i < Request->NumChildren; i++)
    {
        int64_t Id = -1;
        DtXml_AttrInt(&Request->Children[i], "Val", &Id);
        const FakePar* Par = NULL;
        for (int p = 0; p < FAKE_NUM_PARS; p++)
        {
            if (FakePars[p].ParId == Id)
                Par = &FakePars[p];
        }
        if (Par == NULL)
        {
            *Exception = 2;
            break;
        }
        DtXmlOut_Open(&Out, "ParIdVal");
        DtXmlOut_AttrInt(&Out, "ParId", Id);
        DtXmlOut_AttrInt(&Out, "VT", Par->Type);
        char Value[32];
        if (strcmp(Par->Name, "SlaveState") == 0)
            DtXmlOut_AttrInt(&Out, "VV", Ptp->SlaveState);
        else if (strcmp(Par->Name, "MasterLockingStatus") == 0)
            DtXmlOut_AttrInt(&Out, "VV", Ptp->LockStatus);
        else if (strcmp(Par->Name, "DomainNumber") == 0)
            DtXmlOut_AttrInt(&Out, "VV", Ptp->Domain);
        else if (strcmp(Par->Name, "MasterInfo") == 0)
            DtXmlOut_Attr(&Out, "VV", Ptp->MasterInfo);
        else if (strcmp(Par->Name, "MasterIdentity") == 0)
        {
            snprintf(Value, sizeof(Value), "%" PRIu64, Ptp->MasterIdentity);
            DtXmlOut_Attr(&Out, "VV", Value);
        }
        else if (strcmp(Par->Name, "Enable") == 0)
            DtXmlOut_AttrBool(&Out, "VV", Ptp->SlaveState != 0);
        else
            DtXmlOut_AttrInt(&Out, "VV", 0);
        DtXmlOut_Close(&Out, "ParIdVal");
    }
    DtXmlOut_Close(&Out, "ParIdVals");
    char* Text = NULL;
    DtXmlOut_Finish(&Out, &Text);
    return Text;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeAnswerPtp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Answers command Msg as DtapiService's PTP clock slave does, and notes the attach and
// the detach.
//
static bool FakeAnswerPtp(FakeService* Fake, const uint8_t* Msg, size_t Size)
{
    FakePtp* Ptp = Fake->Ptp;
    char* Text = NULL;
    DtXmlElem Request;
    memset(&Request, 0, sizeof(Request));
    if (DtService_TextFromWire(Msg + 4, Size - 4, DT_SERVICE_CHAR_SIZE, &Text) !=
        DTAPI_OK)
        return false;
    if (Text[0] != '\0' && DtXml_Parse(Text, &Request) != DTAPI_OK)
    {
        DtAlloc_Free(Text);
        return false;
    }
    DtAlloc_Free(Text);

    int Exception = DT_SERVICE_NO_EXCEPTION;
    char* Answer = NULL;
    char Small[96] = "";
    switch (Msg[0])
    {
    case DT_SERVICE_CMD_VERSION:
        snprintf(Small, sizeof(Small),
                 "<Version Major=\"%d\" Minor=\"2\" BugFix=\"8\" Build=\"141\"/>\n",
                 Ptp->VersionMajor);
        break;
    case DT_SERVICE_CMD_ATTACH:
        Ptp->Attached = true;
        DtXml_AttrInt(&Request, "Serial", &Ptp->AttachSerial);
        DtXml_AttrInt(&Request, "Port", &Ptp->AttachPort);
        DtXml_AttrBool(&Request, "Exclusive", &Ptp->AttachExclusive);
        Exception = Ptp->AttachException;
        snprintf(Small, sizeof(Small), "<Id Val=\"0\"/>\n");
        break;
    case DT_SERVICE_CMD_DETACH:
        Ptp->Detached = true;
        break;
    case DT_SERVICE_CMD_GET_PARDESCS:
        Answer = FakeParDescs(Ptp);
        break;
    case DT_SERVICE_CMD_GET_PARVALS:
        Answer = FakeParVals(Ptp, &Request, &Exception);
        break;
    default:
        Exception = 13; // InvalidCmd
        break;
    }
    DtXml_Free(&Request);
    bool Written = FakeReply(Fake, Msg[0], Exception, Answer != NULL ? Answer : Small);
    DtAlloc_Free(Answer);
    return Written;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeServe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The fake service's thread: accepts one client and answers its commands until it ends
// the connection, closes it, or the behaviour says to stop.
//
static void FakeServe(void* Context)
{
    FakeService* Fake = (FakeService*)Context;
    if (!FakeAccept(Fake))
        return;
    for (;;)
    {
        uint8_t* Msg = NULL;
        size_t Size = 0;
        if (!FakeReadMsg(Fake, &Msg, &Size) || Size < 4)
        {
            free(Msg);
            break;
        }
        if (Msg[0] == DT_SERVICE_CMD_CLEANUP_CONNECTION)
        {
            Fake->GotCleanup = true;
            free(Msg);
            break;
        }
        Fake->NumCommands++;
        bool Answered = Fake->Behaviour == FAKE_PTP ? FakeAnswerPtp(Fake, Msg, Size)
                                                    : Fake->Behaviour != FAKE_CLOSE &&
                                                          FakeAnswer(Fake, Msg, Size);
        free(Msg);
        if (!Answered)
            break;
    }
    FakeHangUp(Fake);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeStart -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool FakeStart(FakeService* Fake, FakeBehaviour Behaviour, FakePtp* Ptp)
{
    memset(Fake, 0, sizeof(*Fake));
    Fake->Behaviour = Behaviour;
    Fake->Ptp = Ptp;
    if (!FakeListen(Fake))
        return false;
    Fake->Thread = OsThread_Start(FakeServe, Fake);
    return Fake->Thread != NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FakeStop -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Waits for the fake service's thread to end, and removes its pipe.
//
static void FakeStop(FakeService* Fake)
{
#ifdef _WIN32
    // A case that failed before it connected leaves the thread waiting in
    // ConnectNamedPipe, which has no time limit; a client that comes and goes ends the
    // wait. Once a client has come, this one finds the pipe busy or gone.
    char Path[128];
    snprintf(Path, sizeof(Path), "\\\\.\\pipe\\%s", Fake->PipeName);
    HANDLE Client =
        CreateFileA(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (Client != INVALID_HANDLE_VALUE)
        CloseHandle(Client);
#endif
    OsThread_Join(Fake->Thread);
    Fake->Thread = NULL;
    FakeHangUp(Fake);
    FakeRemove(Fake);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StopFake -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The cleanup of a failed case: the fake service as a test framework cleanup function.
//
static void StopFake(void* Context)
{
    FakeStop((FakeService*)Context);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Codec +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(LengthBelowFfffTakesTwoBytes)
{
    uint8_t Prefix[8];
    DT_ASSERT_EQ(DtService_EncodeLength(0x10, Prefix), 2);
    DT_ASSERT_MEM(Prefix, "\x10\x00", 2);
    DT_ASSERT_EQ(DtService_EncodeLength(0xFFFE, Prefix), 2);
    DT_ASSERT_MEM(Prefix, "\xFE\xFF", 2);
}

DT_TEST(LengthFromFfffTakesEightBytes)
{
    uint8_t Prefix[8];
    DT_ASSERT_EQ(DtService_EncodeLength(0xFFFF, Prefix), 8);
    DT_ASSERT_MEM(Prefix, "\xFF\xFF\x00\x00\xFF\xFF\x00\x00", 8);
    DT_ASSERT_EQ(DtService_EncodeLength(0x12345, Prefix), 8);
    DT_ASSERT_MEM(Prefix, "\xFF\xFF\x00\x00\x45\x23\x01\x00", 8);
}

// "A", e acute, the euro sign and a character beyond U+FFFF, in UTF-8.
static const char MixedText[] = "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";

DT_TEST(TextToUtf16HasSurrogatesAndZero)
{
    static const uint8_t Expected[] = {0x41, 0,    0xE9, 0,    0xAC, 0x20,
                                       0x3D, 0xD8, 0x00, 0xDE, 0,    0};
    uint8_t* Wire = NULL;
    size_t Size = 0;
    DT_ASSERT_OK(DtService_TextToWire(MixedText, 2, &Wire, &Size));
    DT_ASSERT_EQ(Size, sizeof(Expected));
    DT_ASSERT_MEM(Wire, Expected, sizeof(Expected));
    DtAlloc_Free(Wire);
}

DT_TEST(TextToUtf32HasOneUnitPerCharacter)
{
    static const uint8_t Expected[] = {0x41, 0, 0, 0,    0xE9, 0, 0, 0, 0xAC, 0x20,
                                       0,    0, 0, 0xF6, 0x01, 0, 0, 0, 0,    0};
    uint8_t* Wire = NULL;
    size_t Size = 0;
    DT_ASSERT_OK(DtService_TextToWire(MixedText, 4, &Wire, &Size));
    DT_ASSERT_EQ(Size, sizeof(Expected));
    DT_ASSERT_MEM(Wire, Expected, sizeof(Expected));
    DtAlloc_Free(Wire);
}

DT_TEST(TextComesBackFromBothWidths)
{
    for (int CharSize = 2; CharSize <= 4; CharSize += 2)
    {
        uint8_t* Wire = NULL;
        size_t Size = 0;
        char* Text = NULL;
        DT_ASSERT_OK(DtService_TextToWire(MixedText, CharSize, &Wire, &Size));
        DtapiResult Result = DtService_TextFromWire(Wire, Size, CharSize, &Text);
        DtAlloc_Free(Wire);
        DT_ASSERT_OK(Result);
        DT_ASSERT_STR(Text, MixedText);
        DtAlloc_Free(Text);
    }
}

DT_TEST(EmptyTextIsOneZeroCharacter)
{
    uint8_t* Wire = NULL;
    size_t Size = 0;
    DT_ASSERT_OK(DtService_TextToWire("", 4, &Wire, &Size));
    DT_ASSERT_EQ(Size, 4);
    char* Text = NULL;
    DtapiResult Result = DtService_TextFromWire(Wire, Size, 4, &Text);
    DtAlloc_Free(Wire);
    DT_ASSERT_OK(Result);
    DT_ASSERT_STR(Text, "");
    DtAlloc_Free(Text);
}

DT_TEST(TextThatIsNotUtf8IsRefused)
{
    // An overlong zero, a surrogate, and a lead byte without its continuation.
    static const char* const Bad[] = {"\xC0\x80", "\xED\xA0\x80", "\xE2\x82"};
    for (size_t i = 0; i < sizeof(Bad) / sizeof(Bad[0]); i++)
    {
        uint8_t* Wire = NULL;
        size_t Size = 0;
        DT_ASSERT_EQ(DtService_TextToWire(Bad[i], 2, &Wire, &Size), DTAPI_E_INVALID_ARG);
        DT_ASSERT(Wire == NULL);
    }
}

DT_TEST(WireTextMustEndInZero)
{
    static const uint8_t NoZero[] = {0x41, 0, 0x42, 0};
    char* Text = NULL;
    DT_ASSERT_EQ(DtService_TextFromWire(NoZero, sizeof(NoZero), 2, &Text),
                 DTAPI_E_COMMUNICATION);
    DT_ASSERT(Text == NULL);
    DT_ASSERT_EQ(DtService_TextFromWire(NoZero, 3, 2, &Text), DTAPI_E_COMMUNICATION);
    DT_ASSERT_EQ(DtService_TextFromWire(NoZero, 0, 2, &Text), DTAPI_E_COMMUNICATION);
}

DT_TEST(WireTextEndsAtItsFirstZero)
{
    static const uint8_t TwoParts[] = {0x41, 0, 0, 0, 0x42, 0, 0, 0};
    char* Text = NULL;
    DT_ASSERT_OK(DtService_TextFromWire(TwoParts, sizeof(TwoParts), 2, &Text));
    DT_ASSERT_STR(Text, "A");
    DtAlloc_Free(Text);
}

DT_TEST(WireTextWithLoneSurrogateIsRefused)
{
    static const uint8_t Utf16[] = {0x3D, 0xD8, 0x41, 0, 0, 0};
    static const uint8_t Utf32[] = {0x00, 0xDC, 0, 0, 0, 0, 0, 0};
    char* Text = NULL;
    DT_ASSERT_EQ(DtService_TextFromWire(Utf16, sizeof(Utf16), 2, &Text),
                 DTAPI_E_COMMUNICATION);
    DT_ASSERT_EQ(DtService_TextFromWire(Utf32, sizeof(Utf32), 4, &Text),
                 DTAPI_E_COMMUNICATION);
    DT_ASSERT(Text == NULL);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Pipe +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(ConnectWithoutServiceIsNotFound)
{
#ifndef _WIN32
    // An empty directory of its own, so that no real service is found either.
    char Dir[] = "/tmp/cdtapi-service-XXXXXX";
    DT_ASSERT(mkdtemp(Dir) != NULL);
    DT_ASSERT(setenv("DTAPI_PIPES_PATH", Dir, 1) == 0);
#endif
    OsPipe* Pipe = NULL;
    int Outcome = OsPipe_Connect("CdtapiNoSuchService", 100, &Pipe);
    DtService* Service = NULL;
    DtapiResult Result = DtService_Connect("CdtapiNoSuchService", &Service);
#ifndef _WIN32
    rmdir(Dir);
#endif
    DT_ASSERT_EQ(Outcome, OS_PIPE_NOT_FOUND);
    DT_ASSERT(Pipe == NULL);
    DT_ASSERT_EQ(Result, DTAPI_E_CONNECT_TO_SERVICE);
    DT_ASSERT(Service == NULL);
}

DT_TEST(WriteAfterServiceClosedFails)
{
    // The service hangs up after the first command. A write to the closed pipe must fail
    // rather than end the program with SIGPIPE.
    FakeService Fake;
    DT_ASSERT(FakeStart(&Fake, FAKE_CLOSE, NULL));
    DtTest_SetCleanup(StopFake, &Fake);
    OsPipe* Pipe = NULL;
    DT_ASSERT_EQ(OsPipe_Connect(Fake.PipeName, 2000, &Pipe), OS_PIPE_OK);

    // A VERSION command of four bytes, after its length.
    static const uint8_t Cmd[] = {4, 0, 1, 0, 0, 0};
    DT_ASSERT_EQ(OsPipe_Write(Pipe, Cmd, sizeof(Cmd), 1000), OS_PIPE_OK);
    OsThread_Join(Fake.Thread);
    Fake.Thread = NULL;
    int Outcome = OsPipe_Write(Pipe, Cmd, sizeof(Cmd), 1000);
    uint8_t Byte = 0;
    int ReadOutcome = OsPipe_Read(Pipe, &Byte, 1, 1000);
    OsPipe_Close(Pipe);
    DtTest_SetCleanup(NULL, NULL);
    FakeStop(&Fake);
    DT_ASSERT_EQ(Outcome, OS_PIPE_CLOSED);
    DT_ASSERT_EQ(ReadOutcome, OS_PIPE_CLOSED);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Messages +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConnectToFake -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Starts a fake service with Behaviour and connects to it.
//
static bool ConnectToFake(FakeService* Fake, FakeBehaviour Behaviour, DtService** Service)
{
    *Service = NULL;
    if (!FakeStart(Fake, Behaviour, NULL))
        return false;
    DtTest_SetCleanup(StopFake, Fake);
    return DtService_Connect(Fake->PipeName, Service) == DTAPI_OK;
}

DT_TEST(CommandIsAnsweredAndConnectionEnds)
{
    FakeService Fake;
    DtService* Service = NULL;
    DT_ASSERT(ConnectToFake(&Fake, FAKE_ECHO, &Service));

    char* Xml = NULL;
    int Exception = 0;
    DtapiResult First =
        DtService_Transfer(Service, DT_SERVICE_CMD_VERSION, "<Echo/>", &Xml, &Exception);
    char* Second = NULL;
    DtapiResult SecondResult = DtService_Transfer(Service, DT_SERVICE_CMD_GET_PARVAL,
                                                  MixedText, &Second, &Exception);
    DtService_Close(Service);
    DtTest_SetCleanup(NULL, NULL);
    FakeStop(&Fake);

    DT_ASSERT_OK(First);
    DT_ASSERT_STR(Xml, "<Echo/>");
    DT_ASSERT_OK(SecondResult);
    DT_ASSERT_STR(Second, MixedText);
    DT_ASSERT_EQ(Exception, DT_SERVICE_NO_EXCEPTION);
    DT_ASSERT_EQ(Fake.NumCommands, 2);
    DT_ASSERT(Fake.GotCleanup);
    DtAlloc_Free(Xml);
    DtAlloc_Free(Second);
}

DT_TEST(LongMessagesTakeTheLongPrefix)
{
    // Text of 40000 characters is 80000 bytes or more on the pipe either way.
    size_t Length = 40000;
    char* Long = (char*)malloc(Length + 1);
    DT_ASSERT(Long != NULL);
    for (size_t i = 0; i < Length; i++)
        Long[i] = (char)('a' + i % 26);
    Long[Length] = '\0';

    FakeService Fake;
    DtService* Service = NULL;
    bool Connected = ConnectToFake(&Fake, FAKE_ECHO, &Service);
    char* Xml = NULL;
    int Exception = 0;
    DtapiResult Result = Connected
                             ? DtService_Transfer(Service, DT_SERVICE_CMD_GET_PARVALS,
                                                  Long, &Xml, &Exception)
                             : DTAPI_E_INTERNAL;
    DtService_Close(Service);
    DtTest_SetCleanup(NULL, NULL);
    FakeStop(&Fake);

    bool Same = Xml != NULL && strcmp(Xml, Long) == 0;
    free(Long);
    DtAlloc_Free(Xml);
    DT_ASSERT(Connected);
    DT_ASSERT_OK(Result);
    DT_ASSERT(Same);
}

DT_TEST(ExceptionIsGivenWithoutXml)
{
    FakeService Fake;
    DtService* Service = NULL;
    DT_ASSERT(ConnectToFake(&Fake, FAKE_EXCEPTION, &Service));

    char* Xml = NULL;
    int Exception = 0;
    DtapiResult Result =
        DtService_Transfer(Service, DT_SERVICE_CMD_ATTACH, "<Attach/>", &Xml, &Exception);
    DtService_Close(Service);
    DtTest_SetCleanup(NULL, NULL);
    FakeStop(&Fake);

    DT_ASSERT_OK(Result);
    DT_ASSERT(Xml == NULL);
    DT_ASSERT_EQ(Exception, 20);
    DT_ASSERT(Fake.GotCleanup);
}

DT_TEST(AnswerToAnotherCommandIsRefused)
{
    FakeService Fake;
    DtService* Service = NULL;
    DT_ASSERT(ConnectToFake(&Fake, FAKE_WRONG_CMD, &Service));

    char* Xml = NULL;
    int Exception = 0;
    DtapiResult Result =
        DtService_Transfer(Service, DT_SERVICE_CMD_VERSION, "", &Xml, &Exception);
    DtapiResult Again =
        DtService_Transfer(Service, DT_SERVICE_CMD_VERSION, "", &Xml, &Exception);
    DtService_Close(Service);
    DtTest_SetCleanup(NULL, NULL);
    FakeStop(&Fake);

    DT_ASSERT_EQ(Result, DTAPI_E_COMMUNICATION);
    DT_ASSERT(Xml == NULL);

    // The connection that failed is not used again, nor told goodbye.
    DT_ASSERT_EQ(Again, DTAPI_E_CONNECT_TO_SERVICE);
    DT_ASSERT_EQ(Fake.NumCommands, 1);
    DT_ASSERT(!Fake.GotCleanup);
}

DT_TEST(AnswerWithoutZeroIsRefused)
{
    FakeService Fake;
    DtService* Service = NULL;
    DT_ASSERT(ConnectToFake(&Fake, FAKE_BAD_TEXT, &Service));

    char* Xml = NULL;
    int Exception = 0;
    DtapiResult Result =
        DtService_Transfer(Service, DT_SERVICE_CMD_VERSION, "<V/>", &Xml, &Exception);
    DtService_Close(Service);
    DtTest_SetCleanup(NULL, NULL);
    FakeStop(&Fake);

    DT_ASSERT_EQ(Result, DTAPI_E_COMMUNICATION);
    DT_ASSERT(Xml == NULL);
}

DT_TEST(ServiceThatHangsUpFailsTheTransfer)
{
    FakeService Fake;
    DtService* Service = NULL;
    DT_ASSERT(ConnectToFake(&Fake, FAKE_CLOSE, &Service));

    char* Xml = NULL;
    int Exception = 0;
    DtapiResult Result =
        DtService_Transfer(Service, DT_SERVICE_CMD_VERSION, "", &Xml, &Exception);
    DtService_Close(Service);
    DtTest_SetCleanup(NULL, NULL);
    FakeStop(&Fake);

    DT_ASSERT_EQ(Result, DTAPI_E_CONNECT_TO_SERVICE);
    DT_ASSERT(Xml == NULL);
}

DT_TEST(TransferChecksItsArguments)
{
    char* Xml = NULL;
    int Exception = 0;
    DT_ASSERT_EQ(DtService_Transfer(NULL, DT_SERVICE_CMD_VERSION, "", &Xml, &Exception),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtService_Connect(NULL, NULL), DTAPI_E_INVALID_ARG);
    DtService_Close(NULL);
    OsPipe_Close(NULL);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= PTP slave +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The serial number and port index the cases read.
#define PTP_SERIAL 2110000076
#define PTP_PORT_INDEX 0

// A grandmaster identity with its top bit set, which the service writes in MasterInfo
// as a negative number, and another one.
#define PTP_GM_HIGH 0xEC4670FFFE0A1B2Cu
#define PTP_GM_OTHER 0x001B19FFFE000001u

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MasterInfoXml -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the value of MasterInfo with the masters Ids into Xml, each with the attributes
// DtapiService writes; the first is time traceable, in domain 127, with clock class 6.
//
static void MasterInfoXml(char* Xml, size_t Size, const uint64_t* Ids, int NumIds)
{
    int Length = snprintf(Xml, Size, "<V Cnt=\"%d\">\n", NumIds);
    for (int i = 0; i < NumIds; i++)
    {
        Length += snprintf(
            Xml + Length, Size - (size_t)Length,
            "<MI AM=\"false\" FT=\"%s\" L59=\"false\" L61=\"false\" TT=\"%s\" "
            "CUOV=\"true\" ID=\"%" PRId64 "\" PID=\"%" PRId64 "\" PPN=\"1\" AI=\"0\" "
            "CA=\"33\" CC=\"%d\" CUO=\"37\" DN=\"%d\" MiId=\"0\" MaId=\"0\" "
            "OSLV=\"20061\" OT=\"1790000000.0\" P1=\"128\" P2=\"128\" SR=\"%d\" "
            "TS=\"32\" "
            "IP=\"192.168.39.1\" AMC=\"12\"/>\n",
            i == 0 ? "true" : "false", i == 0 ? "true" : "false", (int64_t)Ids[i],
            (int64_t)Ids[i], i == 0 ? 6 : 248, i == 0 ? 127 : 0, i + 1);
    }
    snprintf(Xml + Length, Size - (size_t)Length, "</V>\n");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- InitPtp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A slave of DtapiService 5 that is off, as on a card nobody has configured.
//
static void InitPtp(FakePtp* Ptp)
{
    memset(Ptp, 0, sizeof(*Ptp));
    Ptp->VersionMajor = 5;
    Ptp->AttachException = DT_SERVICE_NO_EXCEPTION;
    Ptp->Domain = 127;
    Ptp->MasterInfo = "<V Cnt=\"0\"/>\n";
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadFakePtp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Starts a fake PTP slave as Ptp describes it, attaches a proxy to it without exclusive
// access, and reads its status.
//
static DtapiResult ReadFakePtp(FakePtp* Ptp, DtPtpStatus* Status)
{
    FakeService Fake;
    memset(Status, 0, sizeof(*Status));
    if (!FakeStart(&Fake, FAKE_PTP, Ptp))
        return DTAPI_E_INTERNAL;
    DtServiceProxy* Proxy = NULL;
    DtapiResult Result =
        DtServiceProxy_AttachTo(Fake.PipeName, PTP_SERIAL, PTP_PORT_INDEX,
                                DT_SERVICE_PTP_CLOCK_SLAVE, false, &Proxy);
    if (Result == DTAPI_OK)
        Result = DtPtp_ReadStatus(Proxy, Status);
    DtServiceProxy_Detach(Proxy);
    FakeStop(&Fake);
    return Result;
}

DT_TEST(PtpSlaveThatIsOffHasNoMaster)
{
    FakePtp Ptp;
    InitPtp(&Ptp);
    DtPtpStatus Status;
    DT_ASSERT_OK(ReadFakePtp(&Ptp, &Status));
    DT_ASSERT_EQ(Status.SlaveState, DT_PTP_SLAVE_DISABLED);
    DT_ASSERT_EQ(Status.LockStatus, DT_PTP_LOCK_NOT_IN_USE);
    DT_ASSERT(!Status.HasMaster);
    DT_ASSERT_EQ(Status.Domain, 0);

    // Attached without exclusive access to the port asked for, and detached again.
    DT_ASSERT(Ptp.Attached);
    DT_ASSERT_EQ(Ptp.AttachSerial, PTP_SERIAL);
    DT_ASSERT_EQ(Ptp.AttachPort, PTP_PORT_INDEX);
    DT_ASSERT(!Ptp.AttachExclusive);
    DT_ASSERT(Ptp.Detached);
}

DT_TEST(PtpSlaveGivesTheChosenMaster)
{
    // Two masters heard; the slave, in domain 5, has chosen the second, whose identity
    // has its top bit set.
    static const uint64_t Ids[] = {PTP_GM_OTHER, PTP_GM_HIGH};
    static const uint8_t Eui64[8] = {0xEC, 0x46, 0x70, 0xFF, 0xFE, 0x0A, 0x1B, 0x2C};
    char Masters[2048];
    MasterInfoXml(Masters, sizeof(Masters), Ids, 2);
    FakePtp Ptp;
    InitPtp(&Ptp);
    Ptp.SlaveState = DT_PTP_SLAVE_SLAVE;
    Ptp.LockStatus = DT_PTP_LOCK_LOCKED;
    Ptp.Domain = 5;
    Ptp.MasterIdentity = PTP_GM_HIGH;
    Ptp.MasterInfo = Masters;

    DtPtpStatus Status;
    DT_ASSERT_OK(ReadFakePtp(&Ptp, &Status));
    DT_ASSERT_EQ(Status.SlaveState, DT_PTP_SLAVE_SLAVE);
    DT_ASSERT_EQ(Status.LockStatus, DT_PTP_LOCK_LOCKED);
    DT_ASSERT(Status.HasMaster);
    DT_ASSERT_MEM(Status.GrandmasterId, Eui64, 8);
    DT_ASSERT_EQ(Status.Domain, 5);
    DT_ASSERT(!Status.IsTimeTraceable);
    DT_ASSERT(!Status.IsFrequencyTraceable);
    DT_ASSERT_EQ(Status.ClockClass, 248);
    DT_ASSERT_EQ(Status.StepsRemoved, 2);
}

DT_TEST(PtpSlaveWithMasterNotListed)
{
    // The slave has chosen a master that its list does not have (yet).
    static const uint64_t Ids[] = {PTP_GM_OTHER};
    char Masters[1024];
    MasterInfoXml(Masters, sizeof(Masters), Ids, 1);
    FakePtp Ptp;
    InitPtp(&Ptp);
    Ptp.SlaveState = DT_PTP_SLAVE_UNCALIBRATED;
    Ptp.LockStatus = DT_PTP_LOCK_COLD_LOCKING;
    Ptp.MasterIdentity = PTP_GM_HIGH;
    Ptp.MasterInfo = Masters;

    DtPtpStatus Status;
    DT_ASSERT_OK(ReadFakePtp(&Ptp, &Status));
    DT_ASSERT(Status.HasMaster);
    DT_ASSERT_EQ(Status.GrandmasterId[0], 0xEC);
    DT_ASSERT(!Status.IsTimeTraceable);
    DT_ASSERT_EQ(Status.ClockClass, 0);
}

DT_TEST(PtpSlaveOfOldServiceIsIncompatible)
{
    FakePtp Ptp;
    InitPtp(&Ptp);
    Ptp.VersionMajor = 3;
    DtPtpStatus Status;
    DT_ASSERT_EQ(ReadFakePtp(&Ptp, &Status), DTAPI_E_SERVICE_INCOMP);
    DT_ASSERT(!Ptp.Attached);
}

DT_TEST(PtpSlaveHeldExclusivelyIsInUse)
{
    FakePtp Ptp;
    InitPtp(&Ptp);
    Ptp.AttachException = DT_SERVICE_EXC_EXCLUSIVE_IN_USE;
    DtPtpStatus Status;
    DT_ASSERT_EQ(ReadFakePtp(&Ptp, &Status), DTAPI_E_IN_USE);
    DT_ASSERT(Ptp.Attached);
    DT_ASSERT(!Ptp.Detached);
}

DT_TEST(PtpSlaveOnPortWithoutOneIsNotSupported)
{
    FakePtp Ptp;
    InitPtp(&Ptp);
    Ptp.AttachException = DT_SERVICE_EXC_NOT_SUPPORTED;
    DtPtpStatus Status;
    DT_ASSERT_EQ(ReadFakePtp(&Ptp, &Status), DTAPI_E_NOT_SUPPORTED);
}

DT_TEST(PtpSlaveWithoutParameterIsIncompatible)
{
    FakePtp Ptp;
    InitPtp(&Ptp);
    Ptp.Missing = "MasterInfo";
    DtPtpStatus Status;
    DT_ASSERT_EQ(ReadFakePtp(&Ptp, &Status), DTAPI_E_SERVICE_INCOMP);
    DT_ASSERT_EQ(Status.SlaveState, 0);
    DT_ASSERT(Ptp.Detached);
}

DT_TEST(PtpSlaveWithoutServiceCannotConnect)
{
#ifndef _WIN32
    char Dir[] = "/tmp/cdtapi-service-XXXXXX";
    DT_ASSERT(mkdtemp(Dir) != NULL);
    DT_ASSERT(setenv("DTAPI_PIPES_PATH", Dir, 1) == 0);
#endif
    DtServiceProxy* Proxy = NULL;
    DtapiResult Result = DtServiceProxy_AttachTo(
        "CdtapiNoSuchService", PTP_SERIAL, 0, DT_SERVICE_PTP_CLOCK_SLAVE, false, &Proxy);
#ifndef _WIN32
    rmdir(Dir);
#endif
    DT_ASSERT_EQ(Result, DTAPI_E_CONNECT_TO_SERVICE);
    DT_ASSERT(Proxy == NULL);
    DT_ASSERT_EQ(DtServiceProxy_AttachTo("X", PTP_SERIAL, -1, DT_SERVICE_PTP_CLOCK_SLAVE,
                                         false, &Proxy),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(
        DtServiceProxy_AttachTo("X", PTP_SERIAL, 0, DT_SERVICE_NONE, false, &Proxy),
        DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtServiceProxy_AttachTo("X", PTP_SERIAL, 0, DT_SERVICE_PTP_CLOCK_SLAVE,
                                         false, NULL),
                 DTAPI_E_INVALID_ARG);
}

DT_TEST(PtpExceptionsHaveTheirResults)
{
    static const struct
    {
        DtServiceExc Exception;
        DtapiResult Result;
    } Table[] = {
        {DT_SERVICE_EXC_NONE, DTAPI_OK},
        {DT_SERVICE_EXC_INVALID_PORT_NO, DTAPI_E_NOT_SUPPORTED},
        {DT_SERVICE_EXC_UNKNOWN_SERVICE, DTAPI_E_NOT_SUPPORTED},
        {DT_SERVICE_EXC_NOT_SUPPORTED, DTAPI_E_NOT_SUPPORTED},
        {DT_SERVICE_EXC_DEVICE_NOT_FOUND, DTAPI_E_NO_SUCH_DEVICE},
        {DT_SERVICE_EXC_DRIVER_INCOMPATIBLE, DTAPI_E_DRIVER_INCOMP},
        {DT_SERVICE_EXC_INCOMPATIBLE_SERVICE, DTAPI_E_SERVICE_INCOMP},
        {DT_SERVICE_EXC_EXCLUSIVE_IN_USE, DTAPI_E_IN_USE},
        {DT_SERVICE_EXC_NOT_EXCLUSIVE_ACCESS, DTAPI_E_IN_USE},
        {DT_SERVICE_EXC_INVALID_PAR_ID, DTAPI_E_NOT_FOUND},
        {DT_SERVICE_EXC_UNKNOWN_GROUP_NAME, DTAPI_E_NOT_FOUND},
        {DT_SERVICE_EXC_INVALID_PAR_TYPE, DTAPI_E_INVALID_ARG},
        {DT_SERVICE_EXC_PAR_OUT_OF_RANGE, DTAPI_E_INVALID_ARG},
        {DT_SERVICE_EXC_READ_ONLY_PAR, DTAPI_E_INVALID_ARG},
        {DT_SERVICE_EXC_DEVICE_NOT_ATTACHED, DTAPI_E_COMMUNICATION},
        {DT_SERVICE_EXC_PTP_SEND, DTAPI_E_COMMUNICATION},
        {(DtServiceExc)99, DTAPI_E_COMMUNICATION},
    };
    for (size_t i = 0; i < sizeof(Table) / sizeof(Table[0]); i++)
        DT_ASSERT_EQ(DtServiceProxy_ExceptionResult(Table[i].Exception), Table[i].Result);

    // The names are the service's numbers.
    DT_ASSERT_EQ(DT_SERVICE_EXC_INVALID_PAR_ID, 2);
    DT_ASSERT_EQ(DT_SERVICE_EXC_EXCLUSIVE_IN_USE, 20);
    DT_ASSERT_EQ(DT_SERVICE_EXC_READ_ONLY_PAR, 23);
    DT_ASSERT_EQ(DT_SERVICE_EXC_NOT_SUPPORTED, 27);
    DT_ASSERT_EQ(DT_SERVICE_EXC_PTP_SEND, 33);
}

DT_TEST(PtpMasterListThatIsBrokenIsRefused)
{
    DtPtpMasterInfo* Masters = NULL;
    int NumMasters = 1;
    DT_ASSERT_OK(DtPtp_ParseMasterInfo("", &Masters, &NumMasters));
    DT_ASSERT(Masters == NULL);
    DT_ASSERT_EQ(NumMasters, 0);
    DT_ASSERT_EQ(
        DtPtp_ParseMasterInfo("<V Cnt=\"1\"><MI ID=\"x\"/></V>", &Masters, &NumMasters),
        DTAPI_E_COMMUNICATION);
    DT_ASSERT_EQ(DtPtp_ParseMasterInfo("<V Cnt=\"1\"><MI", &Masters, &NumMasters),
                 DTAPI_E_COMMUNICATION);

    // A master without the attributes the service always writes.
    DT_ASSERT_EQ(
        DtPtp_ParseMasterInfo("<V Cnt=\"1\"><MI ID=\"1\"/></V>", &Masters, &NumMasters),
        DTAPI_E_COMMUNICATION);
    DT_ASSERT(Masters == NULL);
    DT_ASSERT_EQ(DtPtp_ParseMasterInfo(NULL, &Masters, &NumMasters), DTAPI_E_INVALID_ARG);
}

DT_TEST(PtpMasterListIsRead)
{
    static const uint64_t Ids[] = {PTP_GM_HIGH, PTP_GM_OTHER};
    char Xml[2048];
    MasterInfoXml(Xml, sizeof(Xml), Ids, 2);
    DtPtpMasterInfo* Masters = NULL;
    int NumMasters = 0;
    DT_ASSERT_OK(DtPtp_ParseMasterInfo(Xml, &Masters, &NumMasters));
    DT_ASSERT_EQ(NumMasters, 2);
    bool First =
        Masters[0].GrandmasterIdentity == PTP_GM_HIGH &&
        Masters[0].ParentPortIdentity == PTP_GM_HIGH && Masters[0].IsTimeTraceable &&
        Masters[0].IsFrequencyTraceable && Masters[0].DomainNumber == 127 &&
        Masters[0].GrandmasterClockClass == 6 && Masters[0].StepsRemoved == 1 &&
        Masters[0].CurrentLocalOffset == 37 && Masters[0].IsCurrentUtcOffsetValid &&
        Masters[0].GrandmasterClockAccuracy == 33 &&
        Masters[0].OffsetScaledLogVariance == 20061 &&
        Masters[0].GrandmasterPriority1 == 128 && Masters[0].TimeSource == 32 &&
        Masters[0].AnnounceMsg == 12 &&
        strcmp(Masters[0].OriginTimestamp, "1790000000.0") == 0 &&
        strcmp(Masters[0].IpAddress, "192.168.39.1") == 0;
    bool Second = Masters[1].GrandmasterIdentity == PTP_GM_OTHER &&
                  !Masters[1].IsTimeTraceable && Masters[1].StepsRemoved == 2;
    DtPtp_FreeMasterInfo(Masters);
    DT_ASSERT(First);
    DT_ASSERT(Second);
}

DT_TEST_MAIN(
    "Service", DT_RUN(LengthBelowFfffTakesTwoBytes),
    DT_RUN(LengthFromFfffTakesEightBytes), DT_RUN(TextToUtf16HasSurrogatesAndZero),
    DT_RUN(TextToUtf32HasOneUnitPerCharacter), DT_RUN(TextComesBackFromBothWidths),
    DT_RUN(EmptyTextIsOneZeroCharacter), DT_RUN(TextThatIsNotUtf8IsRefused),
    DT_RUN(WireTextMustEndInZero), DT_RUN(WireTextEndsAtItsFirstZero),
    DT_RUN(WireTextWithLoneSurrogateIsRefused), DT_RUN(ConnectWithoutServiceIsNotFound),
    DT_RUN(WriteAfterServiceClosedFails), DT_RUN(CommandIsAnsweredAndConnectionEnds),
    DT_RUN(LongMessagesTakeTheLongPrefix), DT_RUN(ExceptionIsGivenWithoutXml),
    DT_RUN(AnswerToAnotherCommandIsRefused), DT_RUN(AnswerWithoutZeroIsRefused),
    DT_RUN(ServiceThatHangsUpFailsTheTransfer), DT_RUN(TransferChecksItsArguments),
    DT_RUN(PtpSlaveThatIsOffHasNoMaster), DT_RUN(PtpSlaveGivesTheChosenMaster),
    DT_RUN(PtpSlaveWithMasterNotListed), DT_RUN(PtpSlaveOfOldServiceIsIncompatible),
    DT_RUN(PtpSlaveHeldExclusivelyIsInUse),
    DT_RUN(PtpSlaveOnPortWithoutOneIsNotSupported),
    DT_RUN(PtpSlaveWithoutParameterIsIncompatible),
    DT_RUN(PtpSlaveWithoutServiceCannotConnect), DT_RUN(PtpExceptionsHaveTheirResults),
    DT_RUN(PtpMasterListThatIsBrokenIsRefused), DT_RUN(PtpMasterListIsRead))
