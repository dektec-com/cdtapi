# Getting started

This page takes an application from nothing to a program that talks to a DekTec card.
It assumes C and a compiler, and nothing about DekTec's other software.

## What you need

- **The `DtPcie` driver**, loaded. It is what CDTAPI talks to, and it is all CDTAPI
  needs at run time: no service for the API itself (ST 2110 timing needs DekTec's PTP
  service, see `Examples/README.md`), no closed library and no licence file.
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
| `DTAPI/Include` | `cdtapi.h`, `cdtapi_constants.h`, `cdtapi_avfifo.h`, `cdtapi_service.h`, `cdtapi_version.h` |
| `DTAPI/Lib` | The static libraries, and on Windows the DLL's import library |
| `DTAPI/Bin` | On Windows, `cdtapi64.dll` and its `.pdb` |

One set of files serves every compiler version.

### From source

The sources are at <https://github.com/dektec-com/cdtapi>, BSD-3-Clause:

    git clone https://github.com/dektec-com/cdtapi.git
    cd cdtapi
    Scripts/build.sh          # configure, build and test for the host

`Scripts\build.ps1` does the same from PowerShell. CMake 3.22 or newer and a C11
compiler, with Ninja on Linux, are all it asks for; `Scripts/check_tools.sh` reports
what is installed.

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
`cdtapi_avfifo.h` comes on top of it for SMPTE ST 2110, and `cdtapi_service.h` for the
PTP clock slave of an IP port, which DtapiService runs: `Examples/DtPtpSlave.c` shows it
and sets it.

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
        GIT_TAG v6.14.8)
    FetchContent_MakeAvailable(cdtapi)
    target_link_libraries(myapp PRIVATE cdtapi::cdtapi)

`cdtapi::cdtapi` carries the include directory, the system libraries and, for a shared
build, `CDTAPI_DLL`. `CDTAPI_BUILD_TESTS=OFF` and `CDTAPI_BUILD_EXAMPLES=OFF` leave out
what an application does not need.

## Handling failures

Every call that can fail returns a `DtapiResult`, except the AV FIFO's `Read`,
`GetFromMemPool` and `SetMaxSize`, which return NULL or nothing and set the text
`GetLastException` gives. There are no exceptions and nothing is reported through
`errno`.

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

## The image and the audio of an SDI frame

An SDI channel reads and writes raw frames: the whole raster with its timing references,
line numbers, CRCs and blanking. `cdtapi_sdi.h` turns such a frame into what a program
works with, and back:

- **The parser** takes a frame apart into the image, the embedded audio and the other
  ancillary packets.
- **The builder** puts a frame together from an image, audio and ancillary packets, and
  adds everything else the frame needs.

Both work on a **view**, a `DtSdiView`, which says where the frame is. A view points
either at a raw frame in the program's memory, or at a frame in an input channel's
buffer, where the card wrote it. In the second case nothing is copied.

To receive, a program lends each frame from the channel, parses it, and gives it back:

    DtInpChannel_SetRxMode(In, DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B);
    DtInpChannel_SetRxControl(In, DTAPI_RXCTRL_RCV);
    for (;;)
    {
        DtInpChannel_AcquireFrame(In, View, 1000, NULL); // The next frame, in place
        DtSdiParser_Parse(Parser, View, &Image, &Audio, &Anc);
        DtInpChannel_ReleaseFrame(In, View); // The card may use that memory again
        ...
    }

Lending needs a 10-bit receive mode. While a frame is lent, the card cannot write over
it, so give it back as soon as the parser is done. `DtInpChannel_ReadFrame` still works
too; it copies the frame into a buffer, which the parser can then read through a view
of that buffer.

To send, a program borrows room for each frame in the card's transmit buffer, builds
the frame there, and hands it to the card:

    DtSdiBuilder_GetNumAudioSamples(Builder, VidStd, 0, &NumSamples); // Per channel
    DtOutpChannel_AcquireFrame(Out, View, 1000);           // Room for the next frame
    DtSdiBuilder_Build(Builder, View, &Image, &Audio, NULL);
    DtOutpChannel_CommitFrame(Out, View);                  // The card sends it

At a 1001 frame rate, such as 59.94 Hz, the number of audio samples per frame varies
with the audio cadence, so ask the builder before each frame.

Building in the card's buffer saves copying each frame: about a quarter of the time up
to 3G-SDI, and about two thirds in 2160p. The room always holds 10-bit symbols,
whatever the transmit mode. A program that sends this way must keep up with the card
itself. From the first `AcquireFrame` until the channel goes idle, the channel sends no
black frames of its own: when a frame comes too late, the card runs out of data, the
receiver loses the signal for a moment, and the channel latches `DTAPI_TX_FIFO_UFL`. So
put a few frames in the buffer before setting the channel to send. In that time
`DtOutpChannel_Write` and `DtOutpChannel_WriteFrame` return `DTAPI_E_IN_USE`.

A program can also build each frame in its own buffer and write it with
`DtOutpChannel_WriteFrame`, which copies it into the card's buffer. The channel then
fills a gap with a black frame when the program is late:

    DtSdiView_SetRawFrame(View, Frame, FrameSize, VidStd, 10);
    DtSdiBuilder_Build(Builder, View, &Image, &Audio, NULL);
    DtOutpChannel_WriteFrame(Out, Frame, (int)FrameSize, 1000);

The image can be in six pixel formats, among them v210, UYVY and planar 4:2:2, in 8 or
10 bits. The audio is PCM or raw AES3 subframes, on up to 16 channels, interleaved or
planar. The program supplies every buffer.

By default the builder leaves the line CRCs and the packet checksums to the card, which
fills them in while it sends. `DtSdiBuilder_SetChecksums` makes the builder fill them in
itself, which a frame needs that goes to a file rather than to a card.

One frame of 2160p is too much work for one thread at 50 or 60 Hz. A `DtWorkerPool`
from `cdtapi.h` spreads it over several threads: create one, start its threads, and
give it to the channel, the parser and the builder with their `SetWorkerPool` calls.
They can share one pool. `DtReceiveSdi` and `DtTransmitSdi` in the examples show all
of this.

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
the stream starts; a stream sent at once loses its first tens of milliseconds.

The rate is in bits a second of 188-byte packets, also in the 204-byte modes, as in
DTAPI. `DtInpChannel_GetTsRateBps` and `DtInpChannel_GetStatus` report what arrives;
the flags say when the receive FIFO overflowed or the input lost sync, and when the
transmit FIFO ran dry. `DtReceiveTs` and `DtTransmitTs` in the examples do all of this.

## NMOS

The NMOS bridge, in `cdtapi_nmos.h`, connects the AV FIFOs of an IP port to NMOS. With
it, a program registers a FIFO as an NMOS sender or receiver, lets a controller connect
it through IS-05, and converts between an SDP and a FIFO's configuration. The node
itself comes from [dtnmos](https://github.com/dektec-com/dtnmos), version 0.5.2 or a later
0.5.

The bridge is optional, so that a program without NMOS needs nothing besides CDTAPI:

- **With vcpkg**, ask for the feature: `cdtapi[nmos]` among the `dependencies`, with
  `dtnmos` added to the registry's `packages` as well.
- **From source**, configure with `-DCDTAPI_WITH_NMOS=ON` and a dtnmos that
  `find_package(dtnmos 0.5.2)` finds. The presets `windows-sim-nmos` and `linux-sim-nmos`
  take it from vcpkg, and need `VCPKG_ROOT` set.

Only then is `cdtapi_nmos.h` installed. A library built without the bridge still exports
its functions, which return `DTAPI_E_NOT_SUPPORTED`, and `DtapiHasNmos()` tells a program
which library it has. `DtNmos2110` in the examples runs a node with a receiver or a
sender that a controller connects.

The bridge reports the port's PTP clock, which DtapiService's PTP clock slave keeps (see
`cdtapi_service.h`): a sender's SDP names the grandmaster in `a=ts-refclk` while the
slave is locked to it, and `DtNmosAvFifo_ClockFromPort()` gives the node its IS-04
clock, for the `Clock` of its config and for `DtNmosNode_SetClock()` when the lock
changes. Without DtapiService the reference clock is the port's MAC address, and the
node's clock internal.

## Without a card

    CDTAPI_SIM=1 ./list_ports

The emulated DTA-2178 has ten ports and no IP port. `CDTAPI_SIM_DTA2110=1` adds an
emulated DTA-2110, which has one, at device index 1: the value is the index, and 0 is
the DTA-2178's. `CDTAPI_SIM_LOOPBACK=1` makes the packets a program sends arrive at its
own receive side. The emulator starts afresh in each process, so a configuration one
program sets is gone for the next.

Two more give the emulated SDI ports something to receive and somewhere to send to,
through files:

    CDTAPI_SIM_SDI_SOURCE=1:1080I50:frames.sdi   # port 1 receives the file's frames
    CDTAPI_SIM_SDI_SINK=2:sent.sdi               # what port 2 sends goes to the file

Together they let a program receive what another one sent, one after the other:

    CDTAPI_SIM=1 CDTAPI_SIM_SDI_SINK=2:sent.sdi DtTransmitSdi --port 2 \
        --vidstd 1080I50 --count 50
    CDTAPI_SIM=1 CDTAPI_SIM_SDI_SOURCE=1:1080I50:sent.sdi DtReceiveSdi --port 1 \
        --vidstd 1080I50 --count 50 --out received

The port is numbered from 1 and the video standard is a `DTAPI_VIDSTD_` name without
its prefix. A file holds whole frames of 10-bit symbols, packed least significant bit
first, each line from its EAV on and each frame padded with zeros to a multiple of 8
bytes: what FFmpeg's `sdi` format holds without its header, and what a 10-bit
`ReadFrame` gives, except for the padding. The source plays the file's frames over and
over at the standard's frame rate, as a card receives them, so that a program that
looks at the FIFO load before it reads sees them arrive; a value the emulator cannot
use is reported on stderr and ignored.

## Where to go next

- [`Examples/README.md`](../Examples/README.md) lists example programs that configure a
  port, detect a video standard, receive and transmit SDI frames, take the image and
  the audio out of SDI and put them back in, receive and transmit ASI transport
  streams, and receive and transmit SMPTE ST 2110 video and audio, with the command
  lines to run them.
- The headers are the reference: each function's comment, and the notes at the head of
  its section, say what it does, what it writes and which result codes it returns.
- [`migrating-from-the-wrapper.md`](migrating-from-the-wrapper.md), for an application
  built against the C wrapper that CDTAPI replaces.
