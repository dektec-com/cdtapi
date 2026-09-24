# Migrating from the wrapper

Until now, CDTAPI was a thin C wrapper around DTAPI: it called the closed C++ library
and carried it in its archive. This CDTAPI is the interface itself. It talks to the
`DtPcie` driver directly, links nothing closed, and is BSD-3-Clause, so a program that
uses it can be redistributed like any other.

The interface is the same one. This page lists what an application notices. It describes
the wrapper as DekTec's SDK shipped it; the `wrapper-final` tag in this repository holds
the same interface under other file names.

## What does not change

- **The functions.** Every function the wrapper's `CDTAPI.h` and `CDTAPI_AvFifo.h`
  declared is declared here, with the same meaning, and with the same prototype except
  for the three that set an I/O configuration, see below, and for `ReadFrame` and
  `Write`, which take a `void*` and a `const void*` buffer where the wrapper had `char*`.
- **The names and the values.** The structures, enumerations and macros are DTAPI's, as
  they were, down to the numbers: `DTAPI_OK`, the `DTAPI_E_` codes, `DTAPI_VIDSTD_`,
  `DTAPI_IOCONFIG_`, `MAX_DEVICE_NAME_SIZE`, and the rest.
- **`CDTAPI_VERSION`**, `CDTAPI_VERSION_MAJOR`, `_MINOR` and `_PATCH`, unchanged in
  spelling and in meaning.

For most applications, migrating is the include line, the library to link, and an
I/O configuration set with its extra arguments.

## The headers are lower case

| Was | Is |
|---|---|
| `CDTAPI.h` | `cdtapi.h` |
| `CDTAPI_AvFifo.h` | `cdtapi_avfifo.h` |
| `CDTAPI_Version.h` | `cdtapi_version.h`, included by `cdtapi.h` |
| — | `cdtapi_constants.h`, the result codes and constants, included by `cdtapi.h` |

A file system that tells capitals apart turns one spelling into a build that works on
one platform and not the other, and a vcpkg port name may be lower case only, so every
file name in CDTAPI is now lower case.

## An I/O configuration takes DTAPI's extra arguments

The wrapper's three functions that set an I/O configuration could not pass what some
configurations need: the port a double-buffered or looped output names, or the ISI of
one. They take it now, as DTAPI does, and each has a function beside it that reads a
configuration back:

| Was | Is |
|---|---|
| `DtDevice_SetIoConfig(Device, Port, Group, Value, SubValue)` | `DtDevice_SetIoConfig(Device, Configs, Count)`, a list of `DtIoConfig` |
| `DtInpChannel_SetIoConfig(InpChannel, Group, Value, SubValue)` | `DtInpChannel_SetIoConfig(InpChannel, Group, Value, SubValue, ParXtra0, ParXtra1)` |
| `DtOutpChannel_SetIoConfig(OutpChannel, Group, Value, SubValue)` | `DtOutpChannel_SetIoConfig(OutpChannel, Group, Value, SubValue, ParXtra0, ParXtra1)` |

Where a call had no extra arguments, pass `-1, -1`; a single configuration of the
device is a `DtIoConfig` of `{Port, Group, Value, SubValue, {-1, -1}}` and a count of 1.
The compiler finds every call that is left.

## `ENABLE_AVFIFO` is gone

The AV FIFO is always built and always declared. Remove the CMake option and the
`ENABLE_AVFIFO=1` definition from your build; nothing replaces them.

## A result instead of `DTAPI_E_EXCEPTION`

The wrapper caught the C++ exceptions DTAPI threw and turned every one of them into
`DTAPI_E_EXCEPTION`, leaving `GetLastException` as the only way to learn what had
happened. CDTAPI throws nothing and returns the code that says what went wrong:
`DTAPI_E_NOT_STARTED`, `DTAPI_E_NO_IPPARS`, `DTAPI_E_MULTICASTJOIN` and the rest, which
the comment above each group of functions in `cdtapi_avfifo.h` lists.

`DTAPI_E_EXCEPTION` is still defined, so code that names it still compiles, but nothing
returns it. Code that tested for it should test the general way:

    if (Result >= DTAPI_E)
        fprintf(stderr, "%s failed: %s\n", What, DtapiResult2Str(Result));

`GetLastException` is still there, and still gives the text of the calling thread's last
AV FIFO failure. It is now a description beside the code rather than the only clue.

## `GetFrameProperties` returns a result code

The wrapper's `GetFrameProperties` returned 1 when it found the frame's format and -1
when it did not. It returns a `DtapiResult` like every other function that can fail:
`DTAPI_OK` when found, `DTAPI_E_UNSUP_FORMAT` when no format matches and
`DTAPI_E_INVALID_ARG` for a null argument. The compiler does not catch a test written for
the old values, `== 1`, `> 0` or `< 0`, so look for every call:

    if (GetFrameProperties(Frame, &Properties) != DTAPI_OK)
        return;

## `DtapiResult` where the headers said `unsigned int`

`DtapiResult` is `uint32_t`, which is what `unsigned int` was on every platform CDTAPI
supports, so a variable of either type takes either. The typedef gives the return values
a name; the wrapper's `unsigned int` still compiles.

## The files that are installed

One set of files now serves every compiler version, and the directories per version are
gone:

| Was | Is |
|---|---|
| `Lib/<VCxx>/CDTAPI64MT.lib`, `CDTAPI64MTd.lib`, `CDTAPI64MD.lib`, `CDTAPI64MDd.lib` | `Lib/cdtapi64mt.lib`, `cdtapi64mtd.lib`, `cdtapi64md.lib`, `cdtapi64mdd.lib` |
| — | `Lib/cdtapi64.lib` with `Bin/cdtapi64.dll` and its `.pdb` |
| `Lib/<gcc x.y.z>/CDTAPI64.a` | `Lib/libcdtapi.a`, and `Lib/libcdtapi.so` beside it |
| `Include/CDTAPI.h` and the rest | `Include/cdtapi.h` and the rest |

There is a DLL now, which there was not: it suits an application of any C runtime, and a
plug-in that several modules load. `CDTAPI_DLL`, defined when compiling, tells the header
the functions are imported; without it a program still links and runs.

**DTAPI is no longer linked.** An application that linked CDTAPI and DTAPI because CDTAPI
needed it can drop DTAPI. What the static library still needs from the system is
`setupapi`, `ws2_32` and `iphlpapi` on Windows and `pthread` on Linux.

## In C++

The wrapper defined the `DTAPI_` macros in C only, unless `DEFINE_MACROS` was defined;
`cdtapi_constants.h` defines them always. `DEFINE_MACROS` is gone and does nothing.

A translation unit that includes both `DTAPI.h` and `cdtapi.h` therefore has both sets of
definitions. Include one or the other.

## What the wrapper did not have

CDTAPI declares 35 functions besides those of the wrapper:

| Function | Does |
|---|---|
| `DtapiGetVersion` | The library version as a string |
| `DtapiDeviceScan` | Describes every device with DTAPI's `DtDeviceDesc` |
| `DtInpChannel_ReadFrame2` | Reads a frame with its time of arrival |
| `DtOutpChannel_WriteFrame` | Writes one whole frame, with a time-out |
| `DtDevice_WaitForSignalTimeout` | `DtDevice_WaitForSignal` with a time limit and a result, for a program that must carry on when there is no signal |
| `DtDevice_GetIoConfig`, `DtInpChannel_GetIoConfig`, `DtOutpChannel_GetIoConfig` | Read a port's I/O configuration |
| `DtWorkPool_Alloc`, `DtWorkPool_Free`, `DtWorkPool_Freep` | A pool of threads that channels share, for the work they divide |
| `DtWorkPool_StartThreads`, `DtWorkPool_SetDispatch` | Run a pool's work on threads of the library's own, or on the program's pool |
| `DtWorkPool_ExpectThreads`, `DtWorkPool_Join`, `DtWorkPool_Dismiss`, `DtWorkPool_DismissAll` | Let the program's own threads join a pool, and send them back one by one or all at once |
| `DtWorkPoolMember_Alloc`, `DtWorkPoolMember_Free`, `DtWorkPoolMember_Freep` | One thread of the program's in a pool it joins |
| `DtInpChannel_SetWorkPool`, `DtOutpChannel_SetWorkPool` | Divide a channel's work, such as converting a frame's lines, over a pool |
| `DtInpChannel_Read`, `DtInpChannel_GetStatus`, `DtInpChannel_GetTsRateBps`, `DtInpChannel_GetViolCount`, `DtInpChannel_PolarityControl` | Receive DVB-ASI |
| `DtOutpChannel_GetTsRateBps`, `DtOutpChannel_SetTsRateBps`, `DtOutpChannel_SetTxPolarity`, `DtOutpChannel_ClearFlags` | Transmit DVB-ASI, and clear a transmit channel's latched flags |
| `AvFifo_RxFifo_Attach2`, `AvFifo_TxFifo_Attach2` | Attach with a choice between a hardware and a software pipe |
| `AvFifo_RxFifo_UsesHwPipe`, `AvFifo_TxFifo_UsesHwPipe` | Which kind of pipe a started FIFO uses |

## Two things to know about behaviour

Neither is new, and both are what the wrapper did as well:

- `DtDevice_WaitForSignal` retries without a time limit, so on a port with no signal it
  never returns. `DtDevice_WaitForSignalTimeout` is the one for a program that must carry
  on.
- `DtInpChannel_ReadFrame` refuses a buffer smaller than the frame, with
  `DTAPI_E_BUF_TOO_SMALL`, rather than filling what fits.

## Where the sources are

<https://github.com/dektec-com/cdtapi>, BSD-3-Clause. The wrapper is on the `wrapper`
branch and at the `wrapper-final` tag, for as long as anyone needs to look at it.

[`getting-started.md`](getting-started.md) has the build and link lines for both
platforms.
