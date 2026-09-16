# Contributing to CDtapiLite

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

Rules 4, 5 and 6 already match the surrounding DekTec code; they are adopted, not
invented. `.clang-format` is derived from `Win/Applications/StreamXpertV3/.clang-format`,
which states the house style in machine-readable form.

### File header

```c
// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtDevice.c *#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Device management - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause
```

The first line must name the file it is in. The style check verifies that, because a
copy-pasted header naming the wrong file otherwise survives for years.

## How the rules are enforced

| Rule | Enforced by |
|---|---|
| 1 | Review |
| 2 | `Scripts/check_style.sh` (tripwire on common phrasings), then review |
| 4 | `clang-format` and `Scripts/check_style.sh` |
| 5 | `Scripts/check_style.sh` |
| 6 | `clang-format` |
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

`Source/Drv/Abi/` holds files copied verbatim from the DekTec SDK. They keep their
original formatting so that a `diff` against the SDK copy stays clean and a future
driver-ABI update is a copy rather than a merge. Do not reformat them. See
[Source/Drv/Abi/VENDORED.md](Source/Drv/Abi/VENDORED.md).

## This repository will become public

CDtapiLite is BSD-3-Clause and is intended to be published. The history goes with it.
So, from the first commit onwards: no internal documents, no customer data, and no code
copied from DTAPI without a BSD-3-Clause header on it. Rewriting history afterwards is
painful and is usually only done halfway.

## Design documents

Design decisions are recorded in [`Documentation/`](Documentation/) as numbered
documents. Add a new one rather than rewriting an old one; see
[Documentation/README.md](Documentation/README.md).
