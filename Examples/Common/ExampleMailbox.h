// #*#*#*#*#*#*#*#*#*#*#*#*#* ExampleMailbox.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Examples: a mailbox through which one thread asks another and waits
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The thread that owns a FIFO is the only one that touches it. Another thread, such as
// the one an NMOS node calls a callback on, asks the owner through a mailbox: it posts an
// item and waits for the owner's answer. The mailbox holds one item at a time; the owner
// takes it between two frames and answers with a result and a text.

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>

// The largest item a mailbox holds, and the longest text of an answer with its null.
#define EXAMPLE_MAILBOX_ITEM_SIZE 4096
#define EXAMPLE_MAILBOX_TEXT_SIZE 256

typedef struct ExampleMailbox ExampleMailbox;

// Creates an empty mailbox. NULL when out of resources.
ExampleMailbox* ExampleMailbox_Create(void);

// Frees a mailbox no thread waits on. NULL does nothing.
void ExampleMailbox_Free(ExampleMailbox* Box);

// The asking thread: posts Item of Size bytes, at most EXAMPLE_MAILBOX_ITEM_SIZE, and
// waits up to TimeoutMs for the owner's answer, which it writes into *Result and Text, of
// EXAMPLE_MAILBOX_TEXT_SIZE bytes. False when the owner did not answer in time, the item
// then being withdrawn when the owner has not taken it yet, or when another item waits.
bool ExampleMailbox_Ask(ExampleMailbox* Box, const void* Item, size_t Size, int TimeoutMs,
                        unsigned int* Result, char* Text);

// The owner: answers the item it took with Result and Text, which may be NULL.
void ExampleMailbox_Answer(ExampleMailbox* Box, unsigned int Result, const char* Text);

// The owner: takes the item that waits into Item, of Size bytes, without waiting. False
// when none waits.
bool ExampleMailbox_Take(ExampleMailbox* Box, void* Item, size_t Size);
