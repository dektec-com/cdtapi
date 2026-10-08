# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* CMakeLists.txt *#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDTAPI - Example programs, and their smoke tests against the emulator
#
# SPDX-License-Identifier: BSD-3-Clause

set(ExamplePrograms DtListDevices DtConfigPort DtDetectVidStd DtReceiveFrames
    DtTransmitFrames DtReceiveSdi DtTransmitSdi DtReceive2110 DtTransmit2110
    DtListDeviceDescs DtReceiveTs DtTransmitTs DtPtpSlave)

# Programs of SMPTE ST 2110, which also take ExampleAvFifo.c and the AV FIFO header.
set(ExampleAvPrograms DtReceive2110 DtTransmit2110)

# DtNmos2110 needs the NMOS bridge, and is built only with it. It is a program of ST 2110,
# and also takes the mailbox of ExampleMailbox.c.
if(CDTAPI_WITH_NMOS)
    list(APPEND ExamplePrograms DtNmos2110)
    list(APPEND ExampleAvPrograms DtNmos2110)
endif()
set(ExampleMailboxPrograms DtNmos2110)

# Programs of DVB-ASI, which also take the test streams of ExampleTsStream.c.
set(ExampleTsPrograms DtReceiveTs DtTransmitTs)

# Programs that send or check the test pattern and the test tone of ExamplePattern.c.
set(ExamplePatternPrograms DtTransmitSdi)

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Programs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

# Adds one program.
function(cdtapi_add_example Program)
    set(Target "${Program}")
    set(Sources
        "${CMAKE_CURRENT_SOURCE_DIR}/${Program}.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Common/ExampleCommon.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Common/ExampleCommon.h")
    if(Program IN_LIST ExampleAvPrograms)
        list(APPEND Sources
            "${CMAKE_CURRENT_SOURCE_DIR}/Common/ExampleAvFifo.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/Common/ExampleAvFifo.h")
    endif()
    if(Program IN_LIST ExampleMailboxPrograms)
        list(APPEND Sources
            "${CMAKE_CURRENT_SOURCE_DIR}/Common/ExampleMailbox.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/Common/ExampleMailbox.h")
    endif()
    if(Program IN_LIST ExampleTsPrograms)
        list(APPEND Sources
            "${CMAKE_CURRENT_SOURCE_DIR}/Common/ExampleTsStream.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/Common/ExampleTsStream.h")
    endif()
    if(Program IN_LIST ExamplePatternPrograms)
        list(APPEND Sources
            "${CMAKE_CURRENT_SOURCE_DIR}/Common/ExamplePattern.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/Common/ExamplePattern.h")
    endif()
    add_executable(${Target} ${Sources})
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Examples" FILES ${Sources})

    set_target_properties(${Target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        FOLDER "Examples")
    target_include_directories(${Target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${Target} PRIVATE cdtapi)
    target_compile_options(${Target} PRIVATE ${CDTAPI_WARN_FLAGS})
    if(CDTAPI_WARNINGS_AS_ERRORS)
        if(MSVC)
            target_compile_options(${Target} PRIVATE /WX)
        else()
            target_compile_options(${Target} PRIVATE -Werror)
        endif()
    endif()

endfunction()

foreach(Program IN LISTS ExamplePrograms)
    cdtapi_add_example(${Program})
endforeach()

# DtReceive2110 reads an SDP file through the NMOS bridge, in a library built with it.
if(CDTAPI_WITH_NMOS)
    target_compile_definitions(DtReceive2110 PRIVATE CDTAPI_EXAMPLE_WITH_NMOS=1)
else()
    target_compile_definitions(DtReceive2110 PRIVATE CDTAPI_EXAMPLE_WITH_NMOS=0)
endif()

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Smoke tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
#
# Each program runs against the emulated DTA-2178, and passes when its output matches.
# The regular expression spans lines where a test checks more than one.
#

if(NOT CDTAPI_BUILD_TESTS)
    return()
endif()

# Registers a run of Program with the arguments after Expected. Every entry of
# ExampleExtraEnv, such as "CDTAPI_SIM_DTA2110=set:1", is added to the environment of
# the runs registered while it is set.
function(cdtapi_example_test Name Program Expected)
    set(Test "Example_${Name}")
    add_test(NAME ${Test} COMMAND ${Program} ${ARGN})
    set_tests_properties(${Test} PROPERTIES
        PASS_REGULAR_EXPRESSION "${Expected}"
        ENVIRONMENT_MODIFICATION "CDTAPI_SIM=set:1")
    foreach(Variable IN LISTS ExampleExtraEnv)
        set_property(TEST ${Test} APPEND PROPERTY ENVIRONMENT_MODIFICATION "${Variable}")
    endforeach()

    # As for the test suites: an MSVC AddressSanitizer build finds its runtime next to
    # the compiler.
    if(MSVC AND CMAKE_C_FLAGS MATCHES "fsanitize=address")
        get_filename_component(MsvcBin "${CMAKE_C_COMPILER}" DIRECTORY)
        set_property(TEST ${Test} APPEND PROPERTY
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${MsvcBin}")
    endif()
endfunction()

cdtapi_example_test(ListDevices DtListDevices
    "9217800001:1  DTA-2178 port 1  SDI,ASI,INPUT,OUTPUT.*9217800001:9  DTA-2178 port 9  -\n.*10 ports")
cdtapi_example_test(ListDevicesOfNoDevice DtListDevices "^0 ports" --serial 1)
cdtapi_example_test(ListDevicesHelp DtListDevices "^Usage: .*--serial" --help)

# The PTP clock slave of the emulated DTA-2110, through the emulated DtapiService, which
# starts afresh in each run, with the slave off.
set(ExampleExtraEnv "CDTAPI_SIM_DTA2110=set:1")
cdtapi_example_test(ListDevicesWithPtp DtListDevices
    "9211000001:1  DTA-2110 port 1  AVFIFO  [^\n]*  PTP DISABLED NOT_IN_USE\n")
cdtapi_example_test(PtpSlave DtPtpSlave
    "^9211000001:1  config  enable no  domain 127  delay auto  protocol IPv4  scope 5  peer 0.0.0.0\n9211000001:1  status  DISABLED  NOT_IN_USE  no master\n$")
cdtapi_example_test(PtpSlaveEnable DtPtpSlave
    "^9211000001:1  set  ENABLE,DOMAIN  DTAPI_OK\n9211000001:1  saved  DTAPI_OK\n9211000001:1  config  enable yes  domain 0 .*\n9211000001:1  status  SLAVE  LOCKED  grandmaster 00-1B-19-FF-FE-00-00-01  domain 0  traceable  class 6  steps 1\n9211000001:1  master  00-1B-19-FF-FE-00-00-01  domain 0  class 6  priority 128/128  steps 1  traceable  192.168.0.1\n9211000001:1  1 masters"
    --enable --domain 0 --save --masters)
cdtapi_example_test(PtpSlavePeer DtPtpSlave
    "^9211000001:1  set  DELAY,PROTOCOL,SCOPE,PEER  DTAPI_OK\n9211000001:1  config  enable no  domain 127  delay p2p  protocol IPv6  scope 2  peer fe80::1\n"
    --delay p2p --protocol ipv6 --scope 2 --peer fe80::1)
cdtapi_example_test(PtpSlaveDomainOutOfRange DtPtpSlave
    "^9211000001:1  set  DOMAIN  DTAPI_E_INVALID_ARG\n9211000001:1  DtPtpSlave_SetConfig: DomainNumber 200 is not allowed"
    --domain 200)
cdtapi_example_test(PtpSlaveBadDelay DtPtpSlave "^Unknown delay mechanism: soon" --delay soon)
cdtapi_example_test(PtpSlaveEnableAndDisable DtPtpSlave
    "^Give --enable or --disable, not both" --enable --disable)
set(ExampleExtraEnv "")
cdtapi_example_test(PtpSlaveWithoutIpPort DtPtpSlave "^No IP port that suits")

cdtapi_example_test(ListDeviceDescs DtListDeviceDescs
    "^Device 1\n  Category: 0\n  Serial: 9217800001\n.*  PcieMaxSlotPower: 25000\n1 devices")
cdtapi_example_test(ListDeviceDescsOfNoDevice DtListDeviceDescs "^0 devices" --serial 1)

cdtapi_example_test(ConfigPort DtConfigPort
    "9217800001:2  IODIR INPUT  DTAPI_OK\n9217800001:2  IOSTD 3GSDI 1080P50  DTAPI_OK"
    --port 2 --input --vidstd 1080p50)
cdtapi_example_test(ConfigPortFourLinks DtConfigPort
    "^9217800001:3  IOSTD 3GSDI 1080P60  DTAPI_OK" --port 3 --vidstd 2160P60 --linkstd 0)
cdtapi_example_test(ConfigPortWithoutLink DtConfigPort
    "DtapiVidStd2IoStd: DTAPI_E_INVALID_LINKSTD" --vidstd 2160P60)
cdtapi_example_test(ConfigPortUnknownStandard DtConfigPort
    "Unknown video standard: 1080X" --vidstd 1080X)
cdtapi_example_test(ConfigPortAsi DtConfigPort
    "^9217800001:5  IODIR OUTPUT  DTAPI_OK\n9217800001:5  IOSTD ASI  DTAPI_OK"
    --port 5 --output --asi)
cdtapi_example_test(ConfigPortAsiAndVidStd DtConfigPort
    "^Give --vidstd or --asi, not both" --asi --vidstd 1080I50)
cdtapi_example_test(ConfigPortNothingToDo DtConfigPort "^Nothing to do")
cdtapi_example_test(ConfigPortBadOption DtConfigPort "^Unknown option: --bogus" --bogus)

cdtapi_example_test(DetectVidStd DtDetectVidStd "^9217800001:1  no video standard")
cdtapi_example_test(DetectVidStdWaiting DtDetectVidStd
    "^9217800001:1  no video standard" --timeout 50)
# The emulator plays a file of frames on port 1 when CDTAPI_SIM_SDI_SOURCE names one; the
# SimSdiFiles suite leaves such a file in its directory.
set(ExampleExtraEnv
    "CDTAPI_SIM_SDI_SOURCE=set:1:1080I50:${CMAKE_BINARY_DIR}/Tests/SimSdiFiles_1080i50.sdi")
cdtapi_example_test(DetectVidStdFromFile DtDetectVidStd "^9217800001:1  1080I50")
set(ExampleExtraEnv "")
set_tests_properties(Example_DetectVidStdFromFile PROPERTIES
    FIXTURES_REQUIRED SimSdiSourceFile)
cdtapi_example_test(DetectVidStdOnOutput DtDetectVidStd
    "^9217800001:2  DtDevice_DetectVidStd: DTAPI_E_INVALID_MODE" --port 2)
cdtapi_example_test(DetectVidStdNoInput DtDetectVidStd
    "^No SDI input that suits" --port 9)
cdtapi_example_test(DetectVidStdBadNumber DtDetectVidStd
    "^Option --timeout needs a number" --timeout soon)

cdtapi_example_test(ReceiveFramesWithoutSignal DtReceiveFrames
    "^9217800001:1  no frame within 100 ms" --timeout 100)
cdtapi_example_test(ReceiveFramesOnOutput DtReceiveFrames
    "^9217800001:2  DtInpChannel_AttachToPort: DTAPI_E_NO_DT_INPUT" --port 2)
cdtapi_example_test(ReceiveFramesBadMode DtReceiveFrames
    "^Unknown receive mode: 12B" --rxmode 12B)
cdtapi_example_test(ReceiveFramesOverThreads DtReceiveFrames
    "^9217800001:1  no frame within 100 ms" --timeout 100 --threads 4)
cdtapi_example_test(ReceiveFramesDetect DtReceiveFrames
    "^9217800001:1  DtInpChannel_DetectIoStd: DTAPI_E_INVALID_VIDSTD" --detect --timeout 20)

cdtapi_example_test(TransmitFramesPattern DtTransmitFrames
    "^9217800001:2  frame 0  7425000 bytes  hash [0-9A-F]+\n9217800001:2  frame 1  7425000 bytes  hash [0-9A-F]+\n9217800001:2  frame 2 "
    --port 2 --vidstd 1080I50 --count 3)
cdtapi_example_test(TransmitFramesOverThreads DtTransmitFrames
    "^9217800001:2  frame 0  7425000 bytes  hash [0-9A-F]+
9217800001:2  frame 1 "
    --port 2 --vidstd 1080I50 --count 2 --threads 4)
cdtapi_example_test(TransmitFramesSd16Bits DtTransmitFrames
    "^9217800001:2  frame 0  1801800 bytes  hash [0-9A-F]+\n9217800001:2  latched"
    --port 2 --vidstd 525I59_94 --txmode 16B --flags)
cdtapi_example_test(TransmitFramesOnInput DtTransmitFrames
    "^9217800001:1  DtOutpChannel_AttachToPort: DTAPI_E_NO_DT_OUTPUT"
    --port 1 --vidstd 1080I50)
cdtapi_example_test(TransmitFrames8Bits DtTransmitFrames
    "^9217800001:2  frame 0  5940000 bytes  hash [0-9A-F]+\n9217800001:2  DtOutpChannel_SetTxControl: DTAPI_E_CONFIG_RAW_SDI"
    --port 2 --vidstd 1080I50 --txmode 8B)
cdtapi_example_test(TransmitFramesBadMode DtTransmitFrames
    "^Unknown transmit mode: 12B" --txmode 12B)
cdtapi_example_test(TransmitFramesNothingToSend DtTransmitFrames
    "^Give --in, or --vidstd for the test pattern")
cdtapi_example_test(TransmitFramesMissingFile DtTransmitFrames
    "^Cannot read no-such-frame0.raw" --in no-such-frame)
cdtapi_example_test(TransmitFramesNoPattern DtTransmitFrames
    "^No test pattern for video standard 2160P50" --vidstd 2160P50)

# The SDI parser and builder. The round trip runs DtTransmitSdi into a file through the
# emulator's SDI sink, then DtReceiveSdi from that file through its SDI source, and then
# DtTransmitSdi again from the image and audio files that DtReceiveSdi wrote. The tests
# do not check for underflows: the emulator takes frames in real time, and a slow build,
# such as a debug or ASan build, can be late and let the emulated card run out of data.
set(SdiRoundTrip "${CMAKE_CURRENT_BINARY_DIR}/TransmitSdi_1080i50.sdi")
set(SdiOut "${CMAKE_CURRENT_BINARY_DIR}/ReceiveSdi")
cdtapi_example_test(TransmitSdiPattern DtTransmitSdi
    "^9217800001:2  frame 0  1920x1080  audio 1920 samples\n9217800001:2  frame 1  1920x1080  audio 1920 samples\n9217800001:2  frame 2 "
    --port 2 --vidstd 1080I50 --count 3)
cdtapi_example_test(TransmitSdiCadence DtTransmitSdi
    "^9217800001:2  frame 0  1280x720  audio 801 samples\n9217800001:2  frame 1  1280x720  audio 800 samples\n"
    --port 2 --vidstd 720P59_94 --count 2 --threads 4)
set(ExampleExtraEnv "CDTAPI_SIM_SDI_SINK=set:2:${SdiRoundTrip}")
cdtapi_example_test(TransmitSdiToFile DtTransmitSdi
    "^9217800001:2  frame 0 .*9217800001:2  frame 2  1920x1080  audio 1920 samples\n"
    --port 2 --vidstd 1080I50 --count 3 --checksums)
set(ExampleExtraEnv "CDTAPI_SIM_SDI_SOURCE=set:1:1080I50:${SdiRoundTrip}")
cdtapi_example_test(ReceiveSdiFromFile DtReceiveSdi
    "^9217800001:1  frame 0  1920x1080  audio 1920 samples  channels 1-4  packets 0\n9217800001:1  frame 1 "
    --port 1 --vidstd 1080I50 --count 2 --timeout 2000 --out "${SdiOut}")
set(ExampleExtraEnv "")
cdtapi_example_test(TransmitSdiFromFiles DtTransmitSdi
    "^9217800001:2  frame 0  1920x1080  audio 1920 samples\n9217800001:2  frame 1 .*9217800001:2  frame 2 "
    --port 2 --vidstd 1080I50 --count 3 --in "${SdiOut}")
set_tests_properties(Example_TransmitSdiToFile PROPERTIES FIXTURES_SETUP SdiRoundTripFile)
set_tests_properties(Example_ReceiveSdiFromFile PROPERTIES
    FIXTURES_REQUIRED SdiRoundTripFile FIXTURES_SETUP SdiRoundTripOut)
set_tests_properties(Example_TransmitSdiFromFiles PROPERTIES
    FIXTURES_REQUIRED SdiRoundTripOut)
cdtapi_example_test(ReceiveSdiWithoutSignal DtReceiveSdi
    "^9217800001:1  no frame within 100 ms" --timeout 100)
cdtapi_example_test(ReceiveSdiBadChannels DtReceiveSdi
    "^Give --channels from 1 to 16" --channels 17)
cdtapi_example_test(TransmitSdiNoVidStd DtTransmitSdi "^Give --vidstd, such as 1080I50")
cdtapi_example_test(TransmitSdiMissingFile DtTransmitSdi
    "^Cannot read no-such-file.yuv" --vidstd 1080I50 --in no-such-file)

# The ASI programs set the port's I/O standard to ASI themselves. The emulated card has
# no signal on its inputs, and each program has a card of its own, so what one sends
# does not reach the other.
cdtapi_example_test(ReceiveTsWithoutSignal DtReceiveTs
    "^9217800001:1  io standard ASI\n9217800001:1  no data within 100 ms\n9217800001:1  0 packets  rate 0  packets of [?]  no lock  flags -"
    --timeout 100)
cdtapi_example_test(ReceiveTsOnOutput DtReceiveTs
    "^9217800001:2  DtInpChannel_AttachToPort: DTAPI_E_NO_DT_INPUT" --port 2)
cdtapi_example_test(ReceiveTsBadMode DtReceiveTs
    "^Unknown receive mode: 188B" --rxmode 188B)
cdtapi_example_test(ReceiveTsNoPort DtReceiveTs "^No ASI input that suits" --port 9)

cdtapi_example_test(TransmitTsNumbered DtTransmitTs
    "^9217800001:2  io standard ASI\n(9217800001:2  [0-9]+ packets  load [0-9]+  flags -\n)*9217800001:2  sent 2000 packets"
    --port 2 --count 2000 --rate 40000000)
cdtapi_example_test(TransmitTsService DtTransmitTs
    "9217800001:2  sent 500 packets" --port 2 --stream service --count 500 --rate 20000000)
cdtapi_example_test(TransmitTs204Stuffed DtTransmitTs
    "9217800001:2  sent 300 packets" --port 2 --txmode 204 --stuff --count 300)
cdtapi_example_test(TransmitTsOnInput DtTransmitTs
    "^9217800001:1  DtOutpChannel_AttachToPort: DTAPI_E_NO_DT_OUTPUT" --port 1)
cdtapi_example_test(TransmitTsBadRate DtTransmitTs
    "^9217800001:2  io standard ASI\nDtOutpChannel_SetTsRateBps: DTAPI_E_INVALID_RATE"
    --port 2 --rate 500000000 --count 1)
cdtapi_example_test(TransmitTsGenerate DtTransmitTs
    "^wrote 1000 packets to generated.ts" --generate generated.ts --count 1000)
cdtapi_example_test(TransmitTsGenerateService DtTransmitTs
    "^wrote 66489 packets to service.ts" --generate service.ts --stream service)
cdtapi_example_test(TransmitTsServiceOf204 DtTransmitTs
    "^The test service has 188-byte packets" --stream service --txmode 204)
cdtapi_example_test(TransmitTsServiceTooSlow DtTransmitTs
    "^The test service needs a rate from 3000000" --stream service --rate 1000000)
cdtapi_example_test(TransmitTsBadStream DtTransmitTs
    "^Unknown stream: noise" --stream noise)
cdtapi_example_test(TransmitTsMissingFile DtTransmitTs
    "^Cannot read no-such-file.ts" --file no-such-file.ts)

# The ST 2110 programs need the emulated DTA-2110, which the environment adds; the
# loopback makes the packets a program sends arrive at its own receive side, although
# each program has an emulated card of its own and so cannot receive what another sends.
set(ExampleExtraEnv "CDTAPI_SIM_DTA2110=set:1" "CDTAPI_SIM_LOOPBACK=set:1")

cdtapi_example_test(Transmit2110Video DtTransmit2110
    "9211000001:1  hardware pipe  239.1.2.3:5004  320x240 25Hz 10-bit\n9211000001:1  frame 0  192000 bytes  tod .*  rtp .*\n.*sent 2 frames"
    --count 2 --width 320 --height 240 --rate 25)
cdtapi_example_test(Transmit2110EightBit DtTransmit2110
    "320x240 25Hz 8-bit\n9211000001:1  frame 0  153600 bytes"
    --count 1 --width 320 --height 240 --rate 25 --8bit --8bit)
cdtapi_example_test(Transmit2110Audio DtTransmit2110
    "software pipe  239.1.2.3:5004  2 channels L24 48000 Hz\n9211000001:1  frame 0  2880 bytes"
    --audio 2 --count 2)
cdtapi_example_test(Transmit2110ToAddress DtTransmit2110
    "239.9.9.9:5008  320x240 25Hz 10-bit" --ip 239.9.9.9 --udp 5008 --count 1
    --width 320 --height 240 --rate 25)
cdtapi_example_test(Transmit2110BadAddress DtTransmit2110
    "^Not an IPv4 address: 239.1.2" --ip 239.1.2)
cdtapi_example_test(Transmit2110BadPipe DtTransmit2110
    "^Unknown pipe: fast" --pipe fast)
cdtapi_example_test(Transmit2110Help DtTransmit2110 "^Usage: .*--audio" --help)

cdtapi_example_test(Receive2110WithoutStream DtReceive2110
    "9211000001:1  hardware pipe  239.1.2.3:5004  video as 10b\n9211000001:1  no frame within 100 ms\n9211000001:1  ok 0  incomplete 0  gaps 0  packet errors 0  dropped 0  sync 0"
    --count 1 --timeout 100)
cdtapi_example_test(Receive2110SoftwarePipe DtReceive2110
    "^9211000001:1  software pipe" --count 1 --timeout 20 --pipe sw)
cdtapi_example_test(Receive2110Audio DtReceive2110
    "2 channels L24 48000 Hz" --audio 2 --count 1 --timeout 20)
cdtapi_example_test(Receive2110BadFormat DtReceive2110
    "^Unknown receive format: 12b" --format 12b)

# The stream of an SDP file, through the NMOS bridge, or a word that the build has none.
set(ExampleSdp "${CMAKE_CURRENT_SOURCE_DIR}/Sdp/Video1080p25.sdp")
if(CDTAPI_WITH_NMOS)
    cdtapi_example_test(Receive2110FromSdp DtReceive2110
        "9211000001:1  hardware pipe  239.1.2.3:5004  video as 10b\n9211000001:1  no frame within 20 ms"
        --sdp "${ExampleSdp}" --count 1 --timeout 20)
    cdtapi_example_test(Receive2110FromSdpRefused DtReceive2110
        "DtNmosAvFifo_RxConfigFromFlow: DTAPI_E_NOT_SUPPORTED\n.*the flow has 10; use Uyvy422_10b"
        --sdp "${ExampleSdp}" --format 8b)
    cdtapi_example_test(Receive2110FromMissingSdp DtReceive2110
        "^Cannot open .*Missing.sdp" --sdp "${CMAKE_CURRENT_SOURCE_DIR}/Sdp/Missing.sdp")
else()
    cdtapi_example_test(Receive2110FromSdpWithoutNmos DtReceive2110
        "^--sdp needs a library built with the NMOS bridge" --sdp "${ExampleSdp}")
endif()

# A node with a receiver or a sender of a FIFO, for a second, on its own address, with a
# registry that is not there: it serves, and nothing connects it.
if(CDTAPI_WITH_NMOS)
    set(ExampleRegistry --registry http://127.0.0.1:9 --host 127.0.0.1 --seconds 1)
    cdtapi_example_test(Nmos2110Receive DtNmos2110
        "9211000001:1  node [0-9a-f-]+ at http://127.0.0.1:[0-9]+\n9211000001:1  clock internal\n9211000001:1  receiver [0-9a-f-]+  video as 10b"
        --receive ${ExampleRegistry})
    cdtapi_example_test(Nmos2110ReceiveAudio DtNmos2110
        "9211000001:1  receiver [0-9a-f-]+  audio" --receive --audio 2 ${ExampleRegistry})
    cdtapi_example_test(Nmos2110Send DtNmos2110
        "9211000001:1  node [0-9a-f-]+ at http://127.0.0.1:[0-9]+\n9211000001:1  clock internal\n9211000001:1  sender [0-9a-f-]+"
        --send --width 320 --height 240 --rate 25 ${ExampleRegistry})
    cdtapi_example_test(Nmos2110NeitherWay DtNmos2110
        "^Give one of --receive and --send" --registry http://127.0.0.1:9)
    cdtapi_example_test(Nmos2110WithoutRegistry DtNmos2110
        "^Give the registry with --registry" --receive)
endif()

set(ExampleExtraEnv "")

# Without the emulated DTA-2110 there is no IP port at all.
cdtapi_example_test(Transmit2110WithoutPort DtTransmit2110 "^No IP port that suits")
cdtapi_example_test(Receive2110WithoutPort DtReceive2110 "^No IP port that suits")
