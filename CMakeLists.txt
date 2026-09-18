# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* CMakeLists.txt *#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDtapiLite - Top-level build description
#
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.21)

# The major and minor number are those of the DTAPI whose behaviour CDtapiLite reproduces;
# the patch number is CDtapiLite's own. The driver is told the major and minor number in
# every property request, see Source/DtPcie/DtPcieCmd.c.
project(CDtapiLite
    VERSION 6.13.0
    DESCRIPTION "Native open-source C API for DekTec SDI and SMPTE ST 2110 interfaces"
    HOMEPAGE_URL "https://www.dektec.com"
    LANGUAGES C)

# Groups the test projects into a Tests folder in the Visual Studio solution.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Options -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

option(CDTAPILITE_BUILD_TESTS "Build the test suites" ON)
option(CDTAPILITE_BUILD_EXAMPLES "Build the example programs" ON)
option(CDTAPILITE_BUILD_SHARED "Build a shared library instead of a static one" OFF)

# Leaves out the platform driver backends so that the library builds and runs on a
# machine with no DekTec driver installed. The emulated device is always compiled in.
option(CDTAPILITE_SIM_ONLY "Build without the real driver backends" OFF)

option(CDTAPILITE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Generated files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/CDtapiLite_Version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated/CDtapiLite_Version.h"
    @ONLY)

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Library +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

set(CDTAPILITE_SOURCES
    Source/Api/DtApi.c
    Source/Api/DtResult.c
    Source/AvFifo/DtAvError.c
    Source/AvFifo/DtAvFrame.c
    Source/AvFifo/DtAvFrameProps.c
    Source/AvFifo/DtAvPacket.c
    Source/AvFifo/DtAvPipe.c
    Source/AvFifo/DtAvPort.c
    Source/AvFifo/DtAvRxFifo.c
    Source/AvFifo/DtAvPixConv.c
    Source/AvFifo/DtAvTime.c
    Source/AvFifo/DtAvTxFifo.c
    Source/AvFifo/DtSt2110Audio.c
    Source/AvFifo/DtSt2110Video.c
    Source/Channel/DtInpChannel.c
    Source/Channel/DtOutpChannel.c
    Source/Core/DtAlloc.c
    Source/Core/DtBuf.c
    Source/Core/DtRing.c
    Source/Core/DtVec.c
    Source/Device/DtAvInput.c
    Source/Device/DtDevice.c
    Source/Device/DtFunc.c
    Source/Net/DtNet.c
    Source/DtPcie/DtEthIp.c
    Source/DtPcie/DtPcieCmd.c
    Source/DtPcie/DtPcieCmdPipe.c
    Source/DtPcie/DtPcieCmdSdiTx.c
    Source/DtPcie/DtPcieStatus.c
    Source/DtPcie/DtIoConfig.c
    Source/OAL/OsDispatch.c
    Source/OAL/OsDmaBuffer.c
    Source/OAL/OsIoctlOutcome.c
    Source/OAL/OsNet.c
    Source/OAL/Sim/SimChSdiRx.c
    Source/OAL/Sim/SimDta2110.c
    Source/OAL/Sim/SimDta2178.c
    Source/OAL/Sim/SimDtPcie.c
    Source/OAL/Sim/SimNet.c
    Source/OAL/Sim/SimNw.c
    Source/OAL/Sim/SimSdiTx.c
    Source/Video/DtFrameProps.c
    Source/Video/DtSdiFrame.c
    Source/Video/DtSmpte352.c
    Source/Video/DtVidStd.c)

# The Linux buffer layout contains no Linux-specific code and is compiled everywhere, so
# that it is unit-tested on every platform even though its only user is the Linux backend.
list(APPEND CDTAPILITE_SOURCES
    Source/OAL/Linux/LinIoctlBuffer.c
    Source/OAL/Linux/LinTime.c)

# The SSSE3 and AVX2 pixel conversions exist for x86 processors only. The library calls
# them when CPUID reports SSSE3 or AVX2, so only their own files are compiled for it.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64|x86|i[3-6]86)$")
    list(APPEND CDTAPILITE_SOURCES
        Source/AvFifo/DtAvPixConvSsse3.c
        Source/AvFifo/DtAvPixConvAvx2.c)
    set(CDTAPILITE_HAVE_SSSE3 ON)
else()
    set(CDTAPILITE_HAVE_SSSE3 OFF)
endif()

# Threads, events, mutexes, DMA memory and the network belong to the operating system
# rather than to a device, so they are built for the host platform whether or not the
# driver backend is.
if(WIN32)
    list(APPEND CDTAPILITE_SOURCES
        Source/OAL/Windows/WinMemory.c
        Source/OAL/Windows/WinNet.c
        Source/OAL/Windows/WinThread.c)
else()
    list(APPEND CDTAPILITE_SOURCES
        Source/OAL/Linux/LinMemory.c
        Source/OAL/Linux/LinNet.c
        Source/OAL/Linux/LinThread.c)
endif()

# The real driver backend for this platform, or a stand-in that finds no hardware.
if(CDTAPILITE_SIM_ONLY)
    list(APPEND CDTAPILITE_SOURCES Source/OAL/OsNoPlatform.c)
elseif(WIN32)
    list(APPEND CDTAPILITE_SOURCES Source/OAL/Windows/WinDtPcie.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND CDTAPILITE_SOURCES Source/OAL/Linux/LinDtPcie.c)
else()
    message(FATAL_ERROR "CDtapiLite supports Windows and Linux; "
                        "use CDTAPILITE_SIM_ONLY=ON to build for emulation elsewhere.")
endif()

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IDE layout -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
#
# Everything under Include and Source is added to the target, so that an IDE shows the
# whole tree and not only what this platform compiles: headers, the X-macro tables, and
# the backends for the other platform. Files outside CDTAPILITE_SOURCES are marked
# HEADER_FILE_ONLY, which lists them without building them. source_group then turns the
# directory structure into the Visual Studio filters.
#
# The glob only decides what is shown. What is compiled stays the explicit list above.

file(GLOB_RECURSE CDTAPILITE_TREE CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.inc")

list(TRANSFORM CDTAPILITE_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/"
     OUTPUT_VARIABLE CDTAPILITE_BUILT)
set(CDTAPILITE_SHOWN_ONLY ${CDTAPILITE_TREE})
list(REMOVE_ITEM CDTAPILITE_SHOWN_ONLY ${CDTAPILITE_BUILT})
set_source_files_properties(${CDTAPILITE_SHOWN_ONLY} PROPERTIES HEADER_FILE_ONLY TRUE)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${CDTAPILITE_TREE})

if(CDTAPILITE_BUILD_SHARED)
    add_library(cdtapilite SHARED ${CDTAPILITE_BUILT} ${CDTAPILITE_SHOWN_ONLY})
    target_compile_definitions(cdtapilite PRIVATE CDTAPILITE_EXPORTS)
else()
    add_library(cdtapilite STATIC ${CDTAPILITE_BUILT} ${CDTAPILITE_SHOWN_ONLY})
    target_compile_definitions(cdtapilite PUBLIC CDTAPILITE_STATIC)
endif()

add_library(CDtapiLite::cdtapilite ALIAS cdtapilite)

set_target_properties(cdtapilite PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

target_include_directories(cdtapilite
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/Generated>
        $<INSTALL_INTERFACE:Include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Device
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/DtPcie)

if(CDTAPILITE_HAVE_SSSE3)
    target_compile_definitions(cdtapilite PRIVATE
        CDTAPILITE_HAVE_SSSE3=1 CDTAPILITE_HAVE_AVX2=1)
    # MSVC compiles the SSSE3 intrinsics without a flag and needs /arch:AVX2 for AVX2;
    # GCC and Clang, clang-cl too, need a flag for each.
    set(PixConvDir "${CMAKE_CURRENT_SOURCE_DIR}/Source/AvFifo")
    if(NOT MSVC OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
        set_source_files_properties("${PixConvDir}/DtAvPixConvSsse3.c"
            PROPERTIES COMPILE_OPTIONS -mssse3)
        set_source_files_properties("${PixConvDir}/DtAvPixConvAvx2.c"
            PROPERTIES COMPILE_OPTIONS -mavx2)
    else()
        set_source_files_properties("${PixConvDir}/DtAvPixConvAvx2.c"
            PROPERTIES COMPILE_OPTIONS /arch:AVX2)
    endif()
endif()

if(CDTAPILITE_SIM_ONLY)
    target_compile_definitions(cdtapilite PRIVATE CDTAPILITE_SIM_ONLY=1)
elseif(WIN32)
    # SetupAPI enumerates the DtPcie device interfaces.
    target_link_libraries(cdtapilite PRIVATE setupapi)
endif()

if(WIN32)
    # Winsock and IP Helper give the network.
    target_link_libraries(cdtapilite PRIVATE ws2_32 iphlpapi)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(cdtapilite PUBLIC Threads::Threads)
endif()

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-- Warnings -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

if(MSVC)
    set(CDTAPILITE_WARN_FLAGS /W4 /permissive-)
else()
    set(CDTAPILITE_WARN_FLAGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

target_compile_options(cdtapilite PRIVATE ${CDTAPILITE_WARN_FLAGS})

if(CDTAPILITE_WARNINGS_AS_ERRORS)
    if(MSVC)
        target_compile_options(cdtapilite PRIVATE /WX)
    else()
        target_compile_options(cdtapilite PRIVATE -Werror)
    endif()
endif()

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

if(CDTAPILITE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Examples +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

if(CDTAPILITE_BUILD_EXAMPLES)
    add_subdirectory(Examples)
endif()

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Install +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

include(GNUInstallDirs)

install(TARGETS cdtapilite
    EXPORT CDtapiLiteTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/CDtapiLite.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/CDtapiLite_Constants.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/CDtapiLite_AvFifo.h"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated/CDtapiLite_Version.h"
    DESTINATION Include)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cdtapilite.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cdtapilite.pc"
    @ONLY)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cdtapilite.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
