# CDTAPI

A native, open-source C API for DekTec SDI, DVB-ASI and SMPTE ST 2110 interfaces.

CDTAPI talks to the DekTec `DtPcie` driver directly over its ioctl interface. It
contains no closed-source component, and needs none at run time: the driver is what it
needs, and the driver is open source as well.

- **Licence:** BSD-3-Clause. See [LICENSE](LICENSE).
- **Language:** C11. No dependencies beyond libc and the OS API.
- **Platforms:** Linux and Windows.
- **Status:** early development.

## What it makes possible

DekTec hardware can now be used from open-source software that is shipped to others.
CDTAPI is BSD-3-Clause and links nothing closed, so a project under the GPL, the LGPL or
a similar licence can support DekTec cards and still be redistributed as it always was.

That was not possible before. A capture SDK that ships as a closed library puts the
project that links it in a corner: FFmpeg, for one, keeps such SDKs in
`EXTERNAL_LIBRARY_NONFREE_LIST`, where building against them needs `--enable-nonfree`
and the result may not be redistributed at all. Wrapping a closed library in a C
interface changes nothing about that, because the closed code is still linked in.

With CDTAPI, DekTec support belongs with the ordinary `--enable-lib*` options, and the
build that comes out is redistributable like any other.

## Documentation

- [`Docs/getting-started.md`](Docs/getting-started.md) — what an application needs, a
  first program, how to build and link it on both platforms, and how failures are
  reported.
- [`Docs/migrating-from-the-wrapper.md`](Docs/migrating-from-the-wrapper.md) — what an
  application built against the C wrapper over DTAPI notices.
- [`Examples/README.md`](Examples/README.md) — the example programs and the command
  lines to run them, with and without a card.

The headers are the reference: `cdtapi.h` documents every function above its
declaration, with the result codes it returns.

## Building

    Scripts/build.sh                 # configure, build and test for the host
    Scripts/build.sh --list          # show the available presets
    Scripts/build.sh linux-release
    Scripts/build.sh -c windows-sim  # clean rebuild, no driver backends needed
    Scripts/build.sh --lint-only     # style checks only

On Windows, `Scripts\build.ps1` takes the same options from PowerShell.

Visual Studio 2026 opens the directory directly: **File > Open > Folder**. It reads
`CMakePresets.json`, and the test suites appear in Test Explorer.

### Platform status

The build and every test suite are verified on Windows, with Visual Studio 2026 and the
`windows-*` presets, and on Linux (Ubuntu, gcc 15) with the `linux-*` presets. The
Linux driver backend has also talked to a DTA-2178: it reads the card's identity,
properties, I/O configuration and SDI receiver status, and receives and transmits SD, HD
and 3G frames, which arrive through a loopback cable bit for bit, as the real CDTAPI's
do.

Everything can be built and tested **without DekTec hardware**. The `*-sim` presets
leave out the driver backends entirely; the emulated device is always compiled in and
is selected at run time with `CDTAPI_SIM=1`.

## Repository layout

| Path | Contents |
|---|---|
| `Include/` | Public headers |
| `Source/Core/` | Containers used throughout the library |
| `Source/OAL/` | OS abstraction: `Linux/`, `Windows/`, and the `Sim/` emulator |
| `Source/DtPcie/` | Commands of the DtPcie driver, with its vendored ABI under `Abi/` |
| `Source/Device/` | Device scan, attach and I/O configuration |
| `Source/Channel/` | Input and output channels, with an SDI and an ASI side each |
| `Source/Ts/` | Transport-stream packets from the card's receive format, and ASI's 8b/10b code |
| `Source/Video/` | Video-standard tables and detection |
| `Source/Tables/` | Tables generated from the SDK capability descriptions |
| `Tests/` | `Unit/`, `Abi/`, `Sim/`, `Compat/`, `Conformance/` and `Bench/` suites |
| `Examples/` | Example programs that list devices, configure a port, detect a video standard, receive and transmit SDI frames and ASI transport streams, and receive and transmit SMPTE ST 2110 video and audio |
| `Scripts/` | Build and style-check entry points |
| `Docs/` | Getting started, and migrating from the C wrapper |

## Relationship to DTAPI

CDTAPI is a C interface of its own, not a wrapper: it talks to the DtPcie driver itself
and needs no DTAPI. Its names, values and layouts are DTAPI's, so that what an
application knows of DTAPI it knows here, and it replaces the C wrapper that carried
DTAPI's closed library in its archive.

Beside that surface it has what an application needs and DTAPI's C wrapper left out:
`DtapiDeviceScan` with DTAPI's `DtDeviceDesc`, `DtInpChannel_ReadFrame2` with each
frame's time of arrival, and `DtOutpChannel_WriteFrame`, which writes one whole frame
with a time-out. `DtInpChannel` receives SD, HD, 3G and 2160p over one 6G or 12G link,
and DVB-ASI, and `DtOutpChannel` transmits them. `cdtapi_avfifo.h` carries SMPTE ST 2110 video and audio, with a choice between
hardware and software pipes and a specific result code for every failure.

## Versions

CDTAPI's major and minor version number are those of the DTAPI whose behaviour it
reproduces, now 6.14; the patch number counts CDTAPI's own releases. The driver is
told that DTAPI version, with bug-fix number 0, in every property request, so that it
answers as it answers DTAPI 6.14.0.

Where CDTAPI is checked out beside DTAPI, as it is in DekTec's SDK tree, every configure
checks that the two numbers still agree and fails the build when they do not.
`Scripts/check_dtapi_version.cmake` does that, and `cmake -P` runs it on its own. Where
CDTAPI stands alone, which is how it comes from GitHub, there is nothing to compare
against and the check does nothing.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the coding rules and how they are enforced.
