# Contributing to CDTAPI

## Coding rules

1. **Everything is in English** — code, identifiers, comments, documentation and commit
   messages.
2. **Comments describe what the code does, never what it used to do.** No "changed
   from…", no "used to be…", no commented-out previous versions. The reason for a change
   belongs in the commit message, which is where the history lives.
3. **Lint enforces the rules.** See below.
4. **No line longer than 90 characters**, in code and in comments alike.
5. **Every source file starts with a header** describing what the file contains.
6. **An opening brace goes on its own line** (Allman), for functions, `if`, `for`,
   `while`, `switch` and struct definitions.
7. **A value whose width matters has a fixed-width type** from `<stdint.h>` — `uint8_t`,
   `int32_t`, `uint32_t`, `int64_t` and the like — also in `cdtapi.h`. Counters,
   indices and port numbers stay `int`; sizes stay `size_t`, text `char`. `long` and
   `unsigned long` appear only where an operating-system interface defines them, such
   as `timespec.tv_nsec`. The result code is `unsigned int`, as DTAPI's is, and the
   vendored driver ABI keeps its own types.
8. **A header guards itself with `#pragma once`**, as the first line after the file
   header, rather than with an `#ifndef` guard, as `LibDekTec_C` does. The vendored driver
   ABI keeps its own guards.
9. **A variable is declared where it is first needed**, with its first value when it has
   one, one declaration per line; a loop counter in its `for`. Results are `DtapiResult`.
   No `goto`: a function that must clean up after failures hands the steps to a helper
   and cleans up after it.
10. **A function that is not static is named `Class_Function`**: the type it works on,
    or the component it belongs to, then an underscore, as `OsMutex_Lock`,
    `DtRing_Skip`, `DtPcieCmd_CdmacSetOpMode`, `OsTime_SleepMs` and `SimDtPcie_Reset`,
    and as the public API and `LibDekTec_C` have it. A static function has a plain name.

Rules 4, 5 and 6 already match the surrounding DekTec code; they are adopted, not
invented. `.clang-format` is derived from `Win/Applications/StreamXpertV3/.clang-format`,
which states the house style in machine-readable form.

### File header

```c
// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtDevice.c *#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Device management - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause
```

The first line must name the file it is in. The style check verifies that, because a
copy-pasted header naming the wrong file otherwise survives for years.

## Tools

`Scripts/check_tools.sh`, or `Scripts/check_tools.ps1` on Windows, says what is there and
what is missing:

| Tool | Version | For |
|---|---|---|
| clang-format | exactly 18 | Rules 4 and 6. Another major version formats differently and would reformat files that are right |
| CMake | 3.21 or newer | The presets, which need schema 6 |
| Python | 3.8 or newer | `Scripts/fix_banners.py` and the generated test cases |
| Visual Studio | 2022 or newer | The Windows presets; CMake builds with the newest that is installed |
| GCC | 11 or newer | The Linux presets |
| Ninja | any | What the Linux presets build with |
| Git | any | |

`CLANG_FORMAT` points at another clang-format, for a machine where the right version is
not the one on the path.

## What a comment may say about DTAPI

CDTAPI reproduces DTAPI's behaviour, and a comment that says which behaviour is being
reproduced is what makes the code maintainable: "the order DTAPI's receive FIFO starts
in", or the value of an output delay. What a comment must not carry is DTAPI's own
source: no code, no file names, no line numbers. Behaviour is what this library
implements; the code that implements it elsewhere is not ours to publish.

## How the rules are enforced

| Rule | Enforced by |
|---|---|
| 1 | Review |
| 2 | `Scripts/check_style.sh` (tripwire on common phrasings), then review |
| 4 | `clang-format` and `Scripts/check_style.sh` |
| 5 | `Scripts/check_style.sh` |
| 6 | `clang-format` |
| 7 | Review |
| 8 | `Scripts/check_style.sh` |
| 9 | Review |
| 10 | Review |
| Everything else | `clang-tidy`, warnings-as-errors |

Run them locally:

    Scripts/build.sh --lint-only

Banner and separator comments are easy to type one character too wide. Rather than
count, regenerate them:

    python Scripts/fix_banners.py <file> ...

Install the pre-commit hook once, and the same checks run before each commit:

    Scripts/install_hooks.sh

The hook is a convenience, not the gate. CI runs the same checks and is what actually
blocks a merge, so a contributor without the hook installed is still stopped.

## Vendored code is exempt

`Source/DtPcie/Abi/` holds files copied verbatim from the DekTec SDK. They keep their
original formatting so that a `diff` against the SDK copy stays clean and a future
driver-ABI update is a copy rather than a merge. Do not reformat them. See
[Source/DtPcie/Abi/VENDORED.md](Source/DtPcie/Abi/VENDORED.md).

## This repository will become public

CDTAPI is BSD-3-Clause and is intended to be published. The history goes with it.
So, from the first commit onwards: no internal documents, no customer data, and no code
copied from DTAPI without a BSD-3-Clause header on it. Rewriting history afterwards is
painful and is usually only done halfway.

## Design documents

Design decisions are recorded as numbered documents, and a decision that changes gets a
new document rather than a rewrite of the old one. They are not in this repository: they
describe how DTAPI behaves, how DekTec builds and ships its SDK, and why this library
exists, which is not ours to publish. They live in `dektec-com/cdtapi-design`, and a
comment or a commit message refers to them by number, such as "plan 0009".

The style check refuses a file under `Documentation/` here, so that an internal document
cannot walk back in by accident.
