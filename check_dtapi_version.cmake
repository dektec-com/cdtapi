# *#*#*#*#*#*#*#*#*#*#*# check_dtapi_version.cmake #*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
#
# CDTAPI - Checks that CDTAPI's version is in lockstep with DTAPI's
#
# SPDX-License-Identifier: BSD-3-Clause
#
# CDTAPI's major and minor number are those of the DTAPI whose behaviour it reproduces;
# only the patch number is its own. Where the two live side by side, as they do in the
# SDK tree, this reads DTAPI's number and fails the build when they have drifted apart.
# Where CDTAPI stands alone, which is how a customer takes it from GitHub, there is
# nothing to compare against and this does nothing.
#
# It runs at configure time from CMakeLists.txt, and by hand:
#
#     cmake -P Scripts/check_dtapi_version.cmake
#

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CDTAPI's own version -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

get_filename_component(CdtapiRoot "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Included from CMakeLists.txt the project's version is the one to check; run on its own
# there is no project yet, so the same number is read from where CMake declares it.
if(DEFINED PROJECT_VERSION AND PROJECT_NAME STREQUAL "CDTAPI")
    set(CdtapiVersion "${PROJECT_VERSION}")
else()
    file(READ "${CdtapiRoot}/CMakeLists.txt" CdtapiCMakeLists)
    string(REGEX MATCH "project\\(CDTAPI[ \t\r\n]+VERSION[ \t\r\n]+([0-9]+\\.[0-9]+\\.[0-9]+)"
           CdtapiVersionMatch "${CdtapiCMakeLists}")
    if(NOT CdtapiVersionMatch)
        message(FATAL_ERROR "check_dtapi_version: no project version in CMakeLists.txt")
    endif()
    set(CdtapiVersion "${CMAKE_MATCH_1}")
endif()

string(REGEX MATCH "^([0-9]+)\\.([0-9]+)" CdtapiMajorMinor "${CdtapiVersion}")
set(CdtapiMajor "${CMAKE_MATCH_1}")
set(CdtapiMinor "${CMAKE_MATCH_2}")

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DTAPI's version -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

# DTAPI beside CDTAPI, as Libraries/DTAPI beside Libraries/CDTAPI. Its version lives in
# the template its public header is generated from, which is where DTAPI itself takes it.
get_filename_component(DtapiRoot "${CdtapiRoot}/../DTAPI" ABSOLUTE)
set(DtapiHeader "${DtapiRoot}/Source/DTAPI.h.tpl")

if(NOT EXISTS "${DtapiHeader}")
    set(DtapiHeader "")
endif()

if(DtapiHeader STREQUAL "")
    message(STATUS "CDTAPI ${CdtapiVersion}: no DTAPI beside it, version check skipped")
else()
    file(READ "${DtapiHeader}" DtapiHeaderText)
    string(REGEX MATCH "#define[ \t]+DTAPI_VERSION_MAJOR[ \t]+([0-9]+)" DtapiMajorMatch
           "${DtapiHeaderText}")
    set(DtapiMajor "${CMAKE_MATCH_1}")
    string(REGEX MATCH "#define[ \t]+DTAPI_VERSION_MINOR[ \t]+([0-9]+)" DtapiMinorMatch
           "${DtapiHeaderText}")
    set(DtapiMinor "${CMAKE_MATCH_1}")

    # .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- The comparison -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

    if(NOT DtapiMajorMatch OR NOT DtapiMinorMatch)
        message(FATAL_ERROR
            "CDTAPI version check: no DTAPI_VERSION_MAJOR and DTAPI_VERSION_MINOR in\n"
            "    ${DtapiHeader}\n"
            "Either that file is not DTAPI's header, or DTAPI has moved its version. "
            "Fix this script, or move the DTAPI directory out of the way.")
    elseif(NOT CdtapiMajor EQUAL DtapiMajor OR NOT CdtapiMinor EQUAL DtapiMinor)
        message(FATAL_ERROR
            "CDTAPI ${CdtapiVersion} is not in lockstep with DTAPI "
            "${DtapiMajor}.${DtapiMinor}.\n"
            "CDTAPI reproduces a DTAPI version's behaviour and carries its major and "
            "minor number; only the patch number is CDTAPI's own. Set the project "
            "version in\n"
            "    ${CdtapiRoot}/CMakeLists.txt\n"
            "to ${DtapiMajor}.${DtapiMinor}.<patch>, or correct DTAPI's version in\n"
            "    ${DtapiHeader}")
    else()
        message(STATUS "CDTAPI ${CdtapiVersion} is in lockstep with DTAPI "
                       "${DtapiMajor}.${DtapiMinor}")
    endif()
endif()
