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
| clang-format | exactly 18.1.8 | Rules 4 and 6. Another build formats differently and would reformat files that are right. `pip install clang-format==18.1.8` is the surest way to the same one the CI uses |
| CMake | 3.21 or newer | The presets, which need schema 6 |
| Python | 3.8 or newer | `Scripts/fix_banners.py` and the generated test cases |
| Visual Studio | 2022 or newer | The Windows presets; CMake builds with the newest that is installed |
| GCC | 11 or newer | The Linux presets |
| Ninja | any | What the Linux presets build with |
| Git | any | |

`CLANG_FORMAT` points at another clang-format, for a machine where the right version is
not the one on the path.

## Surround SCM, where the tree is shared with it

Inside DekTec this working tree is a Surround SCM working directory as well, which the
`.MySCMServerInfo` files in it say. Surround hands out read-only files unless it is told
otherwise, and git cannot replace a file that is read-only: a pull or a checkout then
fails halfway. Two things keep that from happening:

- `sscm ci -w` and `sscm get -e` leave the files writable. Make that the habit.
- `Scripts/unlock_surround.sh`, or `.ps1`, clears the read-only bit of everything git
  tracks. Both build scripts run it, and so do the `post-checkout` and `post-merge`
  hooks. Outside a Surround working directory it does nothing, so a plain clone, the
  build server and an outside contributor never notice it.

Git ignores what belongs to Surround, `.MySCMServerInfo` and the `*.vssscc` files, and
`.sscmignore` keeps `.git`, `.github` and the build directories out of Surround.

## A comment describes this code

A comment says what the code it stands over does, and why it does it that way. It does
not say what the code used to do, and it does not describe code outside this project.

CDTAPI reproduces behaviour that other software has too, and the behaviour itself belongs
in a comment whenever it explains a choice: the order a receive FIFO is started in, why a
buffer is the size it is, the value of an output delay. State it as a fact about this
code. What must not appear is another project's source: no class, function or file names
from DTAPI or from the driver, no code, no line numbers. A reader of this repository has
none of that to look at, so a name from it explains nothing; and the code that implements
the behaviour elsewhere is not ours to publish.

The names the driver's interface itself uses — the commands, structures and properties in
`Source/DtPcie/Abi/` that travel over the wire — are this library's own vocabulary and
belong wherever they are needed.

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
| Everything else | The compilers' warnings, as errors, and review. `.clang-tidy` configures clang-tidy for a run by hand; no gate runs it |

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
driver-ABI update is a copy rather than a merge. Do not reformat them: `SHA256SUMS`
beside them records their bytes, and `Scripts/check_style.sh` fails on a file that no
longer matches. See [Source/DtPcie/Abi/VENDORED.md](Source/DtPcie/Abi/VENDORED.md).

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
