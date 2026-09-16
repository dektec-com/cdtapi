# Examples

Small command-line programs that show how an application uses the API. Each is one C
file, built with the library unless `CDTAPILITE_BUILD_EXAMPLES` is off.

| Program | Does |
|---|---|
| `DtListDevices` | Lists every port of every device: name, description, and whether it is SDI, AV FIFO, input or output |
| `DtConfigPort` | Makes an SDI port an input or output and, with `--vidstd`, sets its I/O standard for a video standard |
| `DtDetectVidStd` | Detects the video standard on an SDI input, once or, with `--timeout`, until one is found |

Every program lists its options with `--help`. Without `--serial` a program uses the
first device that has a port that suits, and without `--port` the first such port.

## Trying them without a card

The emulated DTA-2178 answers when `CDTAPILITE_SIM=1` is set:

    CDTAPILITE_SIM=1 DtListDevices
    CDTAPILITE_SIM=1 DtConfigPort --port 2 --input --vidstd 1080I50
    CDTAPILITE_SIM=1 DtDetectVidStd --port 1

The emulator starts afresh in each process, so a configuration one program sets is gone
for the next. On a card the configuration stays.

## On a card

    DtListDevices
    DtConfigPort --port 1 --input --vidstd 1080I50
    DtDetectVidStd --port 1 --timeout 5000

## Output and exit codes

The output is plain text, one line per item in a stable order, with results by their
names, so that two runs can be compared line by line. A program exits with 0 when it did
what was asked, 1 when an API call failed or the command line was wrong, and 2 when it
found nothing, such as no ports or no signal.

## Only CDTAPI.h

The programs use only what the original `CDTAPI.h` declares. `Common/ExampleCommon.h`
includes `CDtapiLite.h`, or `CDTAPI.h` when `EXAMPLE_WITH_CDTAPI` is defined, and where the
original header is found each program is also built that way, as `<Program>_Cdtapi`.
CTest runs both builds of every program against the emulator.
