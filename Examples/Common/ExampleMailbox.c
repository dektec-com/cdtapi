// #*#*#*#*#*#*#*#*#*#*#*#*#* ExampleMailbox.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Examples: a mailbox through which one thread asks another and waits
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// With -std=c11 the C library declares only ISO C. Asked for before any header, this
// also exposes clock_gettime and the threads of POSIX.
#ifndef _WIN32
    #define _POSIX_C_SOURCE 200112L
#endif

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Platform includes
#ifdef _WIN32
    #include <windows.h>
#else
    #include <errno.h>
    #include <pthread.h>
    #include <time.h>
#endif

// Example includes
#include "ExampleMailbox.h" // Interface being implemented.

// Where the item is in its round trip. The lock guards it.
typedef enum MailboxState
{
    MAILBOX_EMPTY = 0, // nothing posted, or an answer collected
    MAILBOX_POSTED,    // posted, for the owner to take
    MAILBOX_TAKEN,     // taken, for the owner to answer
    MAILBOX_ANSWERED   // answered, for the asking thread to collect
} MailboxState;

struct ExampleMailbox
{
#ifdef _WIN32
    CRITICAL_SECTION Lock;
    CONDITION_VARIABLE Changed;
#else
    pthread_mutex_t Lock;
    pthread_cond_t Changed;
#endif
    MailboxState State;
    unsigned char Item[EXAMPLE_MAILBOX_ITEM_SIZE];
    size_t Size;
    unsigned int Result;
    char Text[EXAMPLE_MAILBOX_TEXT_SIZE];
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Lock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Takes the mailbox's lock.
//
static void Lock(ExampleMailbox* Box)
{
#ifdef _WIN32
    EnterCriticalSection(&Box->Lock);
#else
    pthread_mutex_lock(&Box->Lock);
#endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Unlock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Releases the mailbox's lock.
//
static void Unlock(ExampleMailbox* Box)
{
#ifdef _WIN32
    LeaveCriticalSection(&Box->Lock);
#else
    pthread_mutex_unlock(&Box->Lock);
#endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Signal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Wakes the threads waiting for the mailbox to change.
//
static void Signal(ExampleMailbox* Box)
{
#ifdef _WIN32
    WakeAllConditionVariable(&Box->Changed);
#else
    pthread_cond_broadcast(&Box->Changed);
#endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WaitUntil -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Waits, with the lock held, until the item is answered or TimeoutMs milliseconds have
// passed. Returns whether it is answered.
//
static bool WaitUntilAnswered(ExampleMailbox* Box, int TimeoutMs)
{
#ifdef _WIN32
    const ULONGLONG End = GetTickCount64() + (ULONGLONG)TimeoutMs;
    while (Box->State != MAILBOX_ANSWERED)
    {
        const ULONGLONG Now = GetTickCount64();
        if (Now >= End ||
            !SleepConditionVariableCS(&Box->Changed, &Box->Lock, (DWORD)(End - Now)))
            return Box->State == MAILBOX_ANSWERED;
    }
    return true;
#else
    struct timespec End;
    clock_gettime(CLOCK_REALTIME, &End);
    End.tv_sec += TimeoutMs / 1000;
    End.tv_nsec += (long)(TimeoutMs % 1000) * 1000000L;
    if (End.tv_nsec >= 1000000000L)
    {
        End.tv_sec++;
        End.tv_nsec -= 1000000000L;
    }
    while (Box->State != MAILBOX_ANSWERED)
    {
        if (pthread_cond_timedwait(&Box->Changed, &Box->Lock, &End) == ETIMEDOUT)
            return Box->State == MAILBOX_ANSWERED;
    }
    return true;
#endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleMailbox_Answer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// When the asking thread has given up waiting and withdrawn the item, the answer is
// dropped.
//
void ExampleMailbox_Answer(ExampleMailbox* Box, unsigned int Result, const char* Text)
{
    Lock(Box);
    if (Box->State == MAILBOX_TAKEN)
    {
        Box->Result = Result;
        snprintf(Box->Text, sizeof(Box->Text), "%s", Text != NULL ? Text : "");
        Box->State = MAILBOX_ANSWERED;
        Signal(Box);
    }
    Unlock(Box);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleMailbox_Ask -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool ExampleMailbox_Ask(ExampleMailbox* Box, const void* Item, size_t Size, int TimeoutMs,
                        unsigned int* Result, char* Text)
{
    if (Size > sizeof(Box->Item))
        return false;
    Lock(Box);
    if (Box->State != MAILBOX_EMPTY)
    {
        Unlock(Box);
        return false;
    }
    memcpy(Box->Item, Item, Size);
    Box->Size = Size;
    Box->State = MAILBOX_POSTED;
    Signal(Box);
    const bool Answered = WaitUntilAnswered(Box, TimeoutMs);
    if (Answered)
    {
        *Result = Box->Result;
        snprintf(Text, EXAMPLE_MAILBOX_TEXT_SIZE, "%s", Box->Text);
    }
    // Withdrawn when not answered: an item not taken yet is never taken, and the answer
    // to one taken goes nowhere.
    Box->State = MAILBOX_EMPTY;
    Unlock(Box);
    return Answered;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleMailbox_Create -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
ExampleMailbox* ExampleMailbox_Create(void)
{
    ExampleMailbox* Box = (ExampleMailbox*)calloc(1, sizeof(ExampleMailbox));
    if (Box == NULL)
        return NULL;
#ifdef _WIN32
    InitializeCriticalSection(&Box->Lock);
    InitializeConditionVariable(&Box->Changed);
#else
    if (pthread_mutex_init(&Box->Lock, NULL) != 0)
    {
        free(Box);
        return NULL;
    }
    if (pthread_cond_init(&Box->Changed, NULL) != 0)
    {
        pthread_mutex_destroy(&Box->Lock);
        free(Box);
        return NULL;
    }
#endif
    return Box;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleMailbox_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void ExampleMailbox_Free(ExampleMailbox* Box)
{
    if (Box == NULL)
        return;
#ifdef _WIN32
    DeleteCriticalSection(&Box->Lock);
#else
    pthread_cond_destroy(&Box->Changed);
    pthread_mutex_destroy(&Box->Lock);
#endif
    free(Box);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleMailbox_Take -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool ExampleMailbox_Take(ExampleMailbox* Box, void* Item, size_t Size)
{
    Lock(Box);
    const bool Waits = Box->State == MAILBOX_POSTED && Box->Size <= Size;
    if (Waits)
    {
        memcpy(Item, Box->Item, Box->Size);
        Box->State = MAILBOX_TAKEN;
    }
    Unlock(Box);
    return Waits;
}
