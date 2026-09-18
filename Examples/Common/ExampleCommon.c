// #*#*#*#*#*#*#*#*#*#*#*#*#*# ExampleCommon.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - What the example programs share - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// With -std=c11 the C library declares only ISO C. Asked for before any header, this
// also exposes nanosleep and clock_gettime.
#ifndef _WIN32
    #define _POSIX_C_SOURCE 199309L
#endif

// Standard includes
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <time.h>
#endif

// Example includes
#include "ExampleCommon.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Arguments +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindOption -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static const ExampleOption* FindOption(const char* Name, const ExampleOption* Options,
                                       int NumOptions)
{
    for (int i = 0; i < NumOptions; i++)
    {
        if (strcmp(Options[i].Name, Name) == 0)
            return &Options[i];
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrintUsage -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void PrintUsage(const char* Program, const char* Usage,
                       const ExampleOption* Options, int NumOptions)
{
    printf("Usage: %s [options]\n%s\n\nOptions:\n", Program, Usage);
    for (int i = 0; i < NumOptions; i++)
    {
        printf("  %s%s\n      %s\n", Options[i].Name,
               Options[i].TakesValue ? " <value>" : "", Options[i].Help);
    }
    printf("  --help\n      Show this text\n");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_CheckArguments -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool Example_CheckArguments(int Argc, char** Argv, const char* Usage,
                            const ExampleOption* Options, int NumOptions)
{
    for (int i = 1; i < Argc; i++)
    {
        if (strcmp(Argv[i], "--help") == 0)
        {
            PrintUsage(Argv[0], Usage, Options, NumOptions);
            return false;
        }

        const ExampleOption* Option = FindOption(Argv[i], Options, NumOptions);
        if (Option == NULL)
        {
            printf("Unknown option: %s; --help lists the options\n", Argv[i]);
            return false;
        }
        if (Option->TakesValue)
        {
            if (i + 1 >= Argc)
            {
                printf("Option %s needs a value\n", Argv[i]);
                return false;
            }
            i++;
        }
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_HasFlag -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool Example_HasFlag(int Argc, char** Argv, const char* Name)
{
    for (int i = 1; i < Argc; i++)
    {
        if (strcmp(Argv[i], Name) == 0)
            return true;
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_Value -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The value is the argument after the option; Example_CheckArguments has made sure there
// is one.
//
const char* Example_Value(int Argc, char** Argv, const char* Name)
{
    for (int i = 1; i + 1 < Argc; i++)
    {
        if (strcmp(Argv[i], Name) == 0)
            return Argv[i + 1];
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_Int64 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool Example_Int64(int Argc, char** Argv, const char* Name, int64_t* Value)
{
    const char* Text = Example_Value(Argc, Argv, Name);
    char* End = NULL;

    if (Text == NULL)
        return true;

    errno = 0;
    int64_t Parsed = strtoll(Text, &End, 10);
    if (End == Text || *End != '\0' || errno != 0)
    {
        printf("Option %s needs a number, not \"%s\"\n", Name, Text);
        return false;
    }
    *Value = Parsed;
    return true;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Ports +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_FindPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The scan is asked for the number of ports first and then for the ports themselves.
//
unsigned int Example_FindPort(int64_t Serial, int Port, ExampleSuits Suits,
                              DtHwFuncDesc* Found)
{
    int Count = 0;
    unsigned int Result = DtapiHwFuncScan(0, &Count, NULL);

    if (Result != DTAPI_OK && Result != DTAPI_E_BUF_TOO_SMALL)
        return Result;
    if (Count <= 0)
        return DTAPI_E_NOT_FOUND;

    DtHwFuncDesc* Ports = (DtHwFuncDesc*)calloc((size_t)Count, sizeof(DtHwFuncDesc));
    if (Ports == NULL)
        return DTAPI_E_OUT_OF_MEM;

    Result = DtapiHwFuncScan(Count, &Count, Ports);
    if (Result == DTAPI_OK)
    {
        Result = DTAPI_E_NOT_FOUND;
        for (int i = 0; i < Count; i++)
        {
            if ((Serial == 0 || Ports[i].SerialNumber == Serial) &&
                (Port == 0 || Ports[i].Port == Port) &&
                (Suits == NULL || Suits(&Ports[i])))
            {
                *Found = Ports[i];
                Result = DTAPI_OK;
                break;
            }
        }
    }

    free(Ports);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_Failed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int Example_Failed(const char* What, unsigned int Result)
{
    printf("%s: %s\n", What, DtapiResult2Str(Result));
    return EXAMPLE_FAILED;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_Succeeded -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The errors start at DTAPI_E; every result below it is a success.
//
bool Example_Succeeded(unsigned int Result)
{
    return Result < DTAPI_E;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Names +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Every video standard of the header, named after its macro.
static const struct
{
    int VidStd;
    const char* Name;
} g_VidStds[] = {
#define V(Name) {DTAPI_VIDSTD_##Name, #Name}
    V(UNKNOWN),   V(525I59_94),    V(625I50),      V(720P23_98),   V(720P24),
    V(720P25),    V(720P29_97),    V(720P30),      V(720P50),      V(720P59_94),
    V(720P60),    V(1080P23_98),   V(1080P24),     V(1080P25),     V(1080P29_97),
    V(1080P30),   V(1080PSF23_98), V(1080PSF24),   V(1080PSF25),   V(1080PSF29_97),
    V(1080PSF30), V(1080I50),      V(1080I59_94),  V(1080I60),     V(1080P50),
    V(1080P50B),  V(1080P59_94),   V(1080P59_94B), V(1080P60),     V(1080P60B),
    V(2160P50),   V(2160P50B),     V(2160P59_94),  V(2160P59_94B), V(2160P60),
    V(2160P60B),  V(2160P23_98),   V(2160P24),     V(2160P25),     V(2160P29_97),
    V(2160P30),
#undef V
};

#define VIDSTD_COUNT ((int)(sizeof(g_VidStds) / sizeof(g_VidStds[0])))

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_VidStdName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* Example_VidStdName(int VidStd)
{
    int i;

    for (i = 0; i < VIDSTD_COUNT; i++)
    {
        if (g_VidStds[i].VidStd == VidStd)
            return g_VidStds[i].Name;
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_VidStdFromName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool Example_VidStdFromName(const char* Name, int* VidStd)
{
    for (int i = 0; i < VIDSTD_COUNT; i++)
    {
        const char* A = g_VidStds[i].Name;
        const char* B = Name;

        while (*A != '\0' && toupper((unsigned char)*A) == toupper((unsigned char)*B))
        {
            A++;
            B++;
        }
        if (*A == '\0' && *B == '\0')
        {
            *VidStd = g_VidStds[i].VidStd;
            return true;
        }
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_IoStdName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* Example_IoStdName(int Value)
{
    switch (Value)
    {
    case DTAPI_IOCONFIG_SDI:
        return "SDI";
    case DTAPI_IOCONFIG_HDSDI:
        return "HDSDI";
    case DTAPI_IOCONFIG_3GSDI:
        return "3GSDI";
    case DTAPI_IOCONFIG_6GSDI:
        return "6GSDI";
    case DTAPI_IOCONFIG_12GSDI:
        return "12GSDI";
    case DTAPI_IOCONFIG_ASI:
        return "ASI";
    default:
        return "?";
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Time +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_SleepMs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void Example_SleepMs(int Ms)
{
#ifdef _WIN32
    Sleep((DWORD)Ms);
#else
    struct timespec Time = {.tv_sec = Ms / 1000, .tv_nsec = (long)(Ms % 1000) * 1000000L};
    nanosleep(&Time, NULL);
#endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_NowMs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int64_t Example_NowMs(void)
{
#ifdef _WIN32
    return (int64_t)GetTickCount64();
#else
    struct timespec Time;
    clock_gettime(CLOCK_MONOTONIC, &Time);
    return (int64_t)Time.tv_sec * 1000 + Time.tv_nsec / 1000000;
#endif
}
