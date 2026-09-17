# Examples

Small command-line programs that show how an application uses the API. Each is one C
file, built with the library unless `CDTAPILITE_BUILD_EXAMPLES` is off.

| Program | Does |
|---|---|
| `DtListDevices` | Lists every port of every device: name, description, and whether it is SDI, AV FIFO, input or output |
| `DtConfigPort` | Makes an SDI port an input or output and, with `--vidstd`, sets its I/O standard for a video standard |
| `DtDetectVidStd` | Detects the video standard on an SDI input, once or, with `--timeout`, until one is found |
| `DtReceiveFrames` | Receives raw SDI frames from an input: one line per frame with its size and a hash, optionally the frames to files |
| `DtTransmitFrames` | Transmits raw SDI frames on an output, from files `DtReceiveFrames` wrote or as a generated test pattern, with the same line per frame |
| `DtListDeviceDescs` | Describes every device, one field of its descriptor per line; uses `DtapiDeviceScan`, a CDtapiLite addition |

Every program lists its options with `--help`. Without `--serial` a program uses the
first device that has a port that suits, and without `--port` the first such port.

## Trying them without a card

The emulated DTA-2178 answers when `CDTAPILITE_SIM=1` is set:

    CDTAPILITE_SIM=1 DtListDevices
    CDTAPILITE_SIM=1 DtConfigPort --port 2 --input --vidstd 1080I50
    CDTAPILITE_SIM=1 DtDetectVidStd --port 1
    CDTAPILITE_SIM=1 DtTransmitFrames --port 2 --vidstd 1080I50 --count 3

The emulator starts afresh in each process, so a configuration one program sets is gone
for the next. On a card the configuration stays.

## On a card

    DtListDevices
    DtConfigPort --port 1 --input --vidstd 1080I50
    DtDetectVidStd --port 1 --timeout 5000
    DtReceiveFrames --port 1 --count 10 --rxmode 10B --out frame
    DtConfigPort --port 5 --output
    DtTransmitFrames --port 5 --vidstd 1080I50 --in frame --count 250

A legal frame `DtTransmitFrames` sends through a cable to an input arrives with the hash
it printed, so the two programs' lines show whether it arrived bit for bit.

## Output and exit codes

The output is plain text, one line per item in a stable order, with results by their
names, so that two runs can be compared line by line. A program exits with 0 when it did
what was asked, 1 when an API call failed or the command line was wrong, and 2 when it
found nothing, such as no ports or no signal.

## Only CDTAPI.h

The programs use only what the original `CDTAPI.h` declares, apart from
`DtListDeviceDescs`, which shows a CDtapiLite addition and is built against
`CDtapiLite.h` only. `Common/ExampleCommon.h` includes `CDtapiLite.h`, or `CDTAPI.h`
when `EXAMPLE_WITH_CDTAPI` is defined, and where the original header is found each of
the other programs is also built that way, as `<Program>_Cdtapi`. CTest runs every build
of every program against the emulator.

On a machine with DTAPI's Linux SDK, `Scripts/compare_device_scan.sh <LinuxSDK>`
compares `DtListDeviceDescs` with DTAPI's own device scan.
`Scripts/compare_cdtapi.sh <CDTAPI> <LinuxSDK> <Program> [args]` builds one of the other
programs against the real CDTAPI library too, runs both builds and compares their output.
