# Getting started

This page takes an application from nothing to a program that talks to a DekTec card.
It assumes C and a compiler, and nothing about DekTec's other software.

## What you need

- **The `DtPcie` driver**, loaded. It is what CDTAPI talks to, and it is all CDTAPI
  needs at run time: there is no service, no closed library and no licence file.
- **A card**, or not: the emulated DTA-2178 answers when `CDTAPI_SIM=1` is set in the
  environment, so a program can be written and run before any hardware arrives.

## Getting the library

### With vcpkg

CDTAPI has a port in DekTec's own registry. A project names that registry in
`vcpkg-configuration.json`, beside its manifest:

    {
      "registries": [
        {
          "kind": "git",
          "repository": "https://github.com/dektec-com/dektec-vcpkg-registry",
          "baseline": "<the registry commit to build against>",
          "packages": [ "cdtapi" ]
        }
      ]
    }

and puts `cdtapi` among the `dependencies` in its `vcpkg.json`. The port builds the
library from source, so the triplet decides what comes out: `x64-windows` gives the DLL,
`x64-windows-static` and `x64-linux` the static library. Nothing else has to be
installed for it, and `find_package(cdtapi CONFIG)` finds it.

### From the SDK

DekTec's SDK distribution ships CDTAPI built, under `DTAPI`:

| Path | Holds |
|---|---|
| `DTAPI/Include` | `cdtapi.h`, `cdtapi_constants.h`, `cdtapi_avfifo.h`, `cdtapi_version.h` |
| `DTAPI/Lib` | The static libraries, and on Windows the DLL's import library |
| `DTAPI/Bin` | On Windows, `cdtapi64.dll` and its `.pdb` |

One set of files serves every compiler version.

### From source

The sources are at <https://github.com/dektec-com/cdtapi>, BSD-3-Clause:

    git clone https://github.com/dektec-com/cdtapi.git
    cd cdtapi
    Scripts/build.sh          # configure, build and test for the host

`Scripts\build.ps1` does the same from PowerShell. CMake 3.21 or newer and a C11
compiler are all it asks for; `Scripts/check_tools.sh` reports what is installed.

To install the headers and the library somewhere of your own:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    cmake --install build --prefix /usr/local

## A first program

    // list_ports.c
    #include <stdio.h>
    #include <stdlib.h>

    #include <cdtapi.h>

    int main(void)
    {
        int Count = 0;

        // With no room, the scan only reports how many ports there are.
        DtapiResult Result = DtapiHwFuncScan(0, &Count, NULL);
        if (Result >= DTAPI_E && Result != DTAPI_E_BUF_TOO_SMALL)
        {
            fprintf(stderr, "DtapiHwFuncScan: %s\n", DtapiResult2Str(Result));
            return 1;
        }
        if (Count == 0)
        {
            printf("no DekTec ports\n");
            return 0;
        }

        DtHwFuncDesc* Ports = calloc((size_t)Count, sizeof(DtHwFuncDesc));
        if (Ports == NULL)
            return 1;

        Result = DtapiHwFuncScan(Count, &Count, Ports);
        if (Result >= DTAPI_E)
        {
            fprintf(stderr, "DtapiHwFuncScan: %s\n", DtapiResult2Str(Result));
            free(Ports);
            return 1;
        }

        for (int i = 0; i < Count; i++)
            printf("%s  %s\n", Ports[i].DeviceName, Ports[i].Description);

        free(Ports);
        return 0;
    }

`cdtapi.h` is the only header to include; it brings in the constants and the version.
`cdtapi_avfifo.h` comes on top of it for SMPTE ST 2110.

## Building it

### Linux

    gcc list_ports.c -lcdtapi -lpthread -o list_ports

From the SDK, where the files are not on the default paths:

    gcc list_ports.c -I $SDK/DTAPI/Include -L $SDK/DTAPI/Lib -lcdtapi -lpthread \
        -o list_ports

`-lcdtapi` takes `libcdtapi.so` when it is there and `libcdtapi.a` otherwise;
`-static` or naming `libcdtapi.a` on the command line picks the static one. An
installed tree also carries `cdtapi.pc`:

    gcc list_ports.c $(pkg-config --cflags --libs cdtapi) -o list_ports

### Windows

A static library fixes which C runtime it calls, so the SDK ships one per runtime and
configuration. Pick the one that matches the compiler option the application is built
with:

| Compiler option | Library |
|---|---|
| `/MD` | `cdtapi64md.lib` |
| `/MDd` | `cdtapi64mdd.lib` |
| `/MT` | `cdtapi64mt.lib` |
| `/MTd` | `cdtapi64mtd.lib` |

    cl /MD /I %SDK%\DTAPI\Include list_ports.c ^
       %SDK%\DTAPI\Lib\cdtapi64md.lib setupapi.lib ws2_32.lib iphlpapi.lib

`setupapi`, `ws2_32` and `iphlpapi` are what the static library needs from Windows
itself; the DLL carries them.

The DLL suits an application of any runtime, and a plug-in that several modules load:

    cl /MD /DCDTAPI_DLL /I %SDK%\DTAPI\Include list_ports.c %SDK%\DTAPI\Lib\cdtapi64.lib

and `cdtapi64.dll` ships beside the program. `CDTAPI_DLL` is what tells the header the
functions are imported; without it the program still links and runs, through a thunk the
linker writes.

### With CMake

Building the sources as part of a project needs no install step:

    add_subdirectory(cdtapi)
    target_link_libraries(myapp PRIVATE cdtapi::cdtapi)

`FetchContent` fetches them instead:

    include(FetchContent)
    FetchContent_Declare(cdtapi
        GIT_REPOSITORY https://github.com/dektec-com/cdtapi.git
        GIT_TAG v6.13.0)
    FetchContent_MakeAvailable(cdtapi)
    target_link_libraries(myapp PRIVATE cdtapi::cdtapi)

`cdtapi::cdtapi` carries the include directory, the system libraries and, for a shared
build, `CDTAPI_DLL`. `CDTAPI_BUILD_TESTS=OFF` and `CDTAPI_BUILD_EXAMPLES=OFF` leave out
what an application does not need.

## Handling failures

Every call that can fail returns a `DtapiResult`. There are no exceptions and nothing is
reported through `errno`.

**Results below `DTAPI_E` are successes**, some of which carry a warning, such as
`DTAPI_OK_OBSOLETE_FW`. Compare against `DTAPI_E`, not against `DTAPI_OK`:

    if (Result >= DTAPI_E)
        fprintf(stderr, "%s failed: %s\n", What, DtapiResult2Str(Result));

`DtapiResult2Str` gives the name of the code, such as `"DTAPI_E_IN_USE"`, for a message
or a log line.

Some codes are answers rather than failures, and are worth testing for by name:
`DTAPI_E_BUF_TOO_SMALL` is how a scan reports how much room it needs, and `DTAPI_E_IDLE`
is what a write gives on a channel that has not been started. Each function's comment in
the header says which codes it returns and what each one means there.

For the AV FIFO, `GetLastException` gives the text of the calling thread's last failure,
beside the code the call returned.

## DVB-ASI

A port that carries ASI has `IsAsi` set in its `DtHwFuncDesc`. The same channels carry
it as carry SDI: once the port's I/O standard is `DTAPI_IOCONFIG_ASI`, an output channel
takes a transport stream with `DtOutpChannel_Write` and an input channel gives one with
`DtInpChannel_Read`. Setting the I/O standard through the channel switches it between
SDI and ASI:

    DtOutpChannel_SetIoConfig(Out, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_ASI, -1, -1, -1);
    DtOutpChannel_SetTxMode(Out, DTAPI_TXMODE_188, DTAPI_TXSTUFF_MODE_OFF);
    DtOutpChannel_SetTsRateBps(Out, 40000000);
    DtOutpChannel_SetTxControl(Out, DTAPI_TXCTRL_HOLD);
    DtOutpChannel_Write(Out, Packets, Size); // Whole packets, a multiple of 4 bytes
    DtOutpChannel_SetTxControl(Out, DTAPI_TXCTRL_SEND);
    ...
    DtOutpChannel_Detach(Out, DTAPI_WAIT_UNTIL_SENT);

and on the other side:

    DtInpChannel_SetIoConfig(In, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_ASI, -1, -1, -1);
    DtInpChannel_SetRxMode(In, DTAPI_RXMODE_ST188);
    DtInpChannel_SetRxControl(In, DTAPI_RXCTRL_RCV);
    DtInpChannel_Read(In, Buffer, Size, 1000); // Waits up to a second for Size bytes

An ASI output sends idle characters, K28.5, from the moment the channel attaches or
switches to ASI. Give the receiver at the other end about 200 ms to lock to them before
the stream starts, as DekTec's DtPlay does; a stream sent at once loses its first tens
of milliseconds.

The rate is in bits a second of 188-byte packets, also in the 204-byte modes, as in
DTAPI. `DtInpChannel_GetTsRateBps` and `DtInpChannel_GetStatus` report what arrives;
the flags say when the receive FIFO overflowed or the input lost sync, and when the
transmit FIFO ran dry. `DtReceiveTs` and `DtTransmitTs` in the examples do all of this.

## Without a card

    CDTAPI_SIM=1 ./list_ports

The emulated DTA-2178 has ten ports and no IP port. `CDTAPI_SIM_DTA2110=1` adds an
emulated DTA-2110, which has one, and `CDTAPI_SIM_LOOPBACK=1` makes the packets a
program sends arrive at its own receive side. The emulator starts afresh in each
process, so a configuration one program sets is gone for the next.

Two more give the emulated SDI ports something to receive and somewhere to send to,
through files:

    CDTAPI_SIM_SDI_SOURCE=1:1080I50:frames.sdi   # port 1 receives the file's frames
    CDTAPI_SIM_SDI_SINK=2:sent.sdi               # what port 2 sends goes to the file

The port is numbered from 1 and the video standard is a `DTAPI_VIDSTD_` name without
its prefix. A file holds whole frames of 10-bit symbols, packed least significant bit
first, each line from its EAV on and each frame padded with zeros to a multiple of 8
bytes: what FFmpeg's `sdi` format holds without its header, and what a 10-bit
`ReadFrame` gives but for the padding. The source plays the file's frames over and
over at the standard's frame rate, as a card receives them, so that a program that
looks at the FIFO load before it reads sees them arrive; a value the emulator cannot
use is reported on stderr and ignored.

## Where to go next

- [`Examples/README.md`](../Examples/README.md) lists example programs that configure a
  port, detect a video standard, receive and transmit SDI frames and ASI transport
  streams, and receive and transmit SMPTE ST 2110 video and audio, with the command
  lines to run them.
- The headers are the reference. `cdtapi.h` documents every function above its
  declaration: what it does, what it writes, and every result code it returns.
- [`migrating-from-the-wrapper.md`](migrating-from-the-wrapper.md), for an application
  built against the C wrapper that CDTAPI replaces.
