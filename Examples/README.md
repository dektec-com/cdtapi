# Examples

Small command-line programs that show how an application uses the API. Each is one C
file, built with the library unless `CDTAPI_BUILD_EXAMPLES` is off.

| Program | Does |
|---|---|
| `DtListDevices` | Lists every port of every device: name, description, and whether it is SDI, AV FIFO, input or output |
| `DtConfigPort` | Makes an SDI port an input or output and, with `--vidstd`, sets its I/O standard for a video standard |
| `DtDetectVidStd` | Detects the video standard on an SDI input, once or, with `--timeout`, until one is found |
| `DtReceiveFrames` | Receives raw SDI frames from an input: one line per frame with its size and a hash, optionally the frames to files |
| `DtTransmitFrames` | Transmits raw SDI frames on an output, from files `DtReceiveFrames` wrote or as a generated test pattern, with the same line per frame |
| `DtListDeviceDescs` | Describes every device, one field of its descriptor per line; uses `DtapiDeviceScan`, a CDTAPI addition |
| `DtTransmit2110` | Transmits SMPTE ST 2110 video, a moving test pattern, or audio on an IP port: one line per frame with its time of day and RTP timestamp |
| `DtReceive2110` | Receives ST 2110 video or audio on an IP port: one line per frame with its size, rows, time of day, timestamp and a hash, and the statistics at the end |

Every program lists its options with `--help`. Without `--serial` a program uses the
first device that has a port that suits, and without `--port` the first such port.

## Trying them without a card

The emulated DTA-2178 answers when `CDTAPI_SIM=1` is set:

    CDTAPI_SIM=1 DtListDevices
    CDTAPI_SIM=1 DtConfigPort --port 2 --input --vidstd 1080I50
    CDTAPI_SIM=1 DtDetectVidStd --port 1
    CDTAPI_SIM=1 DtTransmitFrames --port 2 --vidstd 1080I50 --count 3

The emulator starts afresh in each process, so a configuration one program sets is gone
for the next. On a card the configuration stays.

The emulated DTA-2178 has no IP port. `CDTAPI_SIM_DTA2110` adds an emulated DTA-2110,
a card with one, at the device index it holds, and `CDTAPI_SIM_LOOPBACK` makes the
packets a program sends arrive at its own receive side:

    CDTAPI_SIM=1 CDTAPI_SIM_DTA2110=1 DtTransmit2110 --count 2 --width 320 \
        --height 240 --rate 25
    CDTAPI_SIM=1 CDTAPI_SIM_DTA2110=1 DtReceive2110 --count 1 --timeout 100

Each program has an emulated card of its own, so one cannot receive what another sends;
the `SimAvFifo` test suite is where transmission and reception meet.

## On a card

    DtListDevices
    DtConfigPort --port 1 --input --vidstd 1080I50
    DtDetectVidStd --port 1 --timeout 5000
    DtReceiveFrames --port 1 --count 10 --rxmode 10B --out frame
    DtConfigPort --port 5 --output
    DtTransmitFrames --port 5 --vidstd 1080I50 --in frame --count 250

A legal frame `DtTransmitFrames` sends through a cable to an input arrives with the hash
it printed, so the two programs' lines show whether it arrived bit for bit.

### What ST 2110 needs first

The two 2110 programs ask more of the machine than the SDI ones, because the port they
use is a network interface of the card rather than a cable:

- **An IP port with an address.** The address belongs to the card's own network
  interface, not to the host's, and is set with DekTec's tools before a program runs.
  `AvFifo_*_Start` gives `DTAPI_E_NW_DRIVER` when the port has no network interface,
  `DTAPI_E_DISABLED` when it is disabled, and `DTAPI_E_NO_ADAPTER_IP_ADDR` when it has no
  address of the IP version asked for.
- **DekTec's service running**, which is what runs a PTP slave on the port and keeps the
  card's clock locked.
- **A PTP grandmaster on the network** for that slave to lock to. Without it the card's
  clock free-runs: frames still go out, but their times of day are the card's own, which
  is not ST 2110 timing and is not what a receiver expects.
- **The network in between** carrying the multicast group. `AvFifo_RxFifo_Start` gives
  `DTAPI_E_MULTICASTJOIN` when joining fails, and a transmit FIFO gives
  `DTAPI_E_DST_MAC_ADDR` when the destination does not answer.

None of this applies to the emulator, which has no network and no clock to lock.

On a card with an IP port, one machine sending and another receiving:

    DtTransmit2110 --ip 239.1.2.3 --udp 5004 --count 250
    DtReceive2110 --ip 239.1.2.3 --udp 5004 --count 250 --format 10b

Each frame is given a time of day a little after the card's clock, and the card's
scheduler sends it at that time.

## Output and exit codes

The output is plain text, one line per item in a stable order, with results by their
names, so that two runs can be compared line by line. A program exits with 0 when it did
what was asked, 1 when an API call failed or the command line was wrong, and 2 when it
found nothing, such as no ports or no signal.

## The headers they use

Every program includes `cdtapi.h`, and the two ST 2110 programs `cdtapi_avfifo.h` as
well, through `Common/ExampleCommon.h` and `Common/ExampleAvFifo.h`. Nothing of the
library's own headers is used, so what a program does, an application can do. CTest runs
every program against the emulator.

On a machine with DTAPI's Linux SDK, `Scripts/compare_device_scan.sh <LinuxSDK>`
compares `DtListDeviceDescs` with DTAPI's own device scan.
