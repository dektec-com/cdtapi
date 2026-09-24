# Examples

Small command-line programs that show how an application uses the API. Each is one C
file, built with the library unless `CDTAPI_BUILD_EXAMPLES` is off.

| Program | Does |
|---|---|
| `DtListDevices` | Lists every port of every device: name, description, and whether it is SDI, ASI, AV FIFO, input or output |
| `DtConfigPort` | Makes a port an input or output and sets its I/O standard: with `--vidstd` for a video standard, with `--asi` to DVB-ASI |
| `DtDetectVidStd` | Detects the video standard on an SDI input, once or, with `--timeout`, until one is found |
| `DtReceiveFrames` | Receives raw SDI frames from an input: one line per frame with its size and a hash, optionally the frames to files; `--threads` converts them over a pool of threads |
| `DtTransmitFrames` | Transmits raw SDI frames on an output, from files `DtReceiveFrames` wrote or as a generated test pattern, with the same line per frame; `--threads` codes them over a pool of threads |
| `DtReceiveTs` | Receives a transport stream from an ASI input, optionally to a file, with the rate, packet size, lock and flags once a second; `--check` checks `DtTransmitTs`'s numbered packets one by one |
| `DtTransmitTs` | Transmits a transport stream on an ASI output at a set rate: a file, numbered packets, or an MPEG-2 test picture; `--generate` writes either stream to a file instead |
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
    CDTAPI_SIM=1 DtTransmitTs --port 2 --count 2000 --rate 40000000

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

### 2160p over threads

Converting a 2160p frame between the raw frame and the lines the card carries is more
than one slow core has time for at 50 or 60 frames a second. `--threads` gives the
channel a pool of that many threads of the library's own, and the channel divides each
frame over it: into 4 pieces for 2160p50 and 2160p60, 2 for 2160p24 to 2160p30, and one
up to 3G, where the pool goes unused. `GivePool` in either program has the calls:

    DtConfigPort --port 1 --input --vidstd 2160P50 --linkstd 3
    DtReceiveFrames --port 1 --count 10 --threads 4
    DtConfigPort --port 5 --output
    DtTransmitFrames --port 5 --vidstd 2160P50 --linkstd 3 --in frame --count 250         --threads 4

The frames are the same whatever the number of threads. A pool can also run the pieces
on a program's own threads, which join it, or on a pool the program already has; the
`Parallel work` section of `cdtapi.h` describes both.

### DVB-ASI

The two ASI programs set the port's I/O standard to ASI themselves; the direction is
DtConfigPort's. With a cable from port 5 to port 1, numbered packets are checked one by
one as they arrive, in one shell and then another:

    DtReceiveTs --port 1 --count 250000 --check
    DtTransmitTs --port 5 --count 250000 --rate 40000000

`DtReceiveTs` prints `gaps 0  bad 0` when every packet arrived, in order. `--txmode 204`
and `--rxmode 204` do the same with 204-byte packets. The test picture, for a player or
an analyser on the output, and the files a player such as DekTec's DtPlay takes:

    DtTransmitTs --port 5 --stream service --rate 10000000
    DtTransmitTs --generate service.ts --stream service --rate 10000000
    DtTransmitTs --generate numbered.ts --count 250000

The test picture is one MPEG-2 service, "CDTAPI ASI test": colour bars, the time code of
each frame and a block that moves, so that a lost frame shows at a glance.

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

Each frame is given a time of day a little after the card's clock, one frame period
after the one before, and the card's scheduler sends it at that time. So the scheduler
sets the pace, and the program only keeps the FIFO full: when `AvFifo_TxFifo_Write`
refuses a frame with `DTAPI_E_FIFO_FULL`, it waits a moment and writes the same frame
again. A program that paced itself would fall behind the card's clock as soon as a frame
took longer to make than a frame period, and its frames would go out late.

## Output and exit codes

The output is plain text, one line per item in a stable order, with results by their
names, so that two runs can be compared line by line. A program exits with 0 when it did
what was asked, 1 when a call or a check failed or the command line was wrong, `--help`
included, and 2 when it found nothing, such as no ports or no signal.

## The headers they use

Every program includes `cdtapi.h`, and the two ST 2110 programs `cdtapi_avfifo.h` as
well, through `Common/ExampleCommon.h` and `Common/ExampleAvFifo.h`. The ASI programs
make their streams with `Common/ExampleTsStream.c`. Nothing of the
library's own headers is used, so what a program does, an application can do. CTest runs
every program against the emulator.

On a machine with DTAPI's Linux SDK, `Scripts/compare_device_scan.sh <LinuxSDK>`
compares `DtListDeviceDescs` with DTAPI's own device scan.
