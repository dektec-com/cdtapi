# CDtapiLite

A native, open-source C API for DekTec SDI interfaces.

CDtapiLite talks to the DekTec `DtPcie` driver directly over its documented ioctl
interface. It contains no closed-source component, so an application that links it —
FFmpeg in particular — stays redistributable.

- **Licence:** BSD-3-Clause. See [LICENSE](LICENSE).
- **Language:** C11. No dependencies beyond libc and the OS API.
- **Platforms:** Linux and Windows.
- **Status:** early development. See [Documentation](Documentation/) for the design.

## Why it exists

FFmpeg's `configure` places closed-source capture SDKs in
`EXTERNAL_LIBRARY_NONFREE_LIST`. Building FFmpeg against one of those requires
`--enable-nonfree`, and the resulting binary cannot be redistributed at all — which is
what happens today with Blackmagic DeckLink. Wrapping a closed library in a C API does
not change that; the closed code is still linked in.

An open-source library does change it. With CDtapiLite, DekTec support can sit in
FFmpeg's ordinary `EXTERNAL_LIBRARY_LIST`, alongside the other `--enable-lib*` options,
and the resulting build is redistributable under the LGPL like any other.

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
properties, I/O configuration and SDI receiver status, and attaches its receive channels.
Without a signal source the examples give the same output as the real CDTAPI there;
receiving frames on a card is still to be confirmed.

Everything can be built and tested **without DekTec hardware**. The `*-sim` presets
leave out the driver backends entirely; the emulated device is always compiled in and
is selected at run time with `CDTAPILITE_SIM=1`.

## Repository layout

| Path | Contents |
|---|---|
| `Include/` | Public headers |
| `Source/Core/` | Containers used throughout the library |
| `Source/OAL/` | OS abstraction: `Linux/`, `Windows/`, and the `Sim/` emulator |
| `Source/DtPcie/` | Commands of the DtPcie driver, with its vendored ABI under `Abi/` |
| `Source/Device/` | Device scan, attach and I/O configuration |
| `Source/Channel/` | SDI input and output channels |
| `Source/Video/` | Video-standard tables and detection |
| `Source/Tables/` | Tables generated from the SDK capability descriptions |
| `Tests/` | `Unit/`, `Abi/`, `Sim/` and `Conformance/` suites |
| `Examples/` | Example programs that list devices, configure a port, detect a video standard and receive frames |
| `Documentation/` | Numbered design documents |
| `Scripts/` | Build and style-check entry points |

## Relationship to CDTAPI

CDtapiLite is interface-compatible with the existing `CDTAPI` C wrapper for the
`DtDevice`, `DtInpChannel` and `DtOutpChannel` surface, and installs a `CDTAPI.h`
compatibility header. It also adds what CDTAPI.h leaves out and an application needs, such
as `DtapiDeviceScan` with DTAPI's `DtDeviceDesc`, `DtInpChannel_ReadFrame2` with each
frame's time of arrival, and `DtOutpChannel_WriteFrame`, which writes one whole frame with
a time-out. `DtInpChannel` receives SD, HD and 3G, and `DtOutpChannel`
transmits them. The compatibility covers `CDTAPI.h` only: `CDTAPI_AvFifo.h` and the
`ENABLE_AVFIFO` define have no equivalent yet.

## Versions

CDtapiLite's major and minor version number are those of the DTAPI whose behaviour it
reproduces, now 6.13; the patch number counts CDtapiLite's own releases. The driver is
told that DTAPI version, with bug-fix number 0, in every property request, so that it
answers as it answers DTAPI 6.13.0.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the coding rules and how they are enforced.
