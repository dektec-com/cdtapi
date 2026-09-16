#!/usr/bin/env bash
# #*#*#*#*#*#*#*#*#*#*#*#*#*# compare_cdtapi.sh *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDtapiLite - Runs an example on the real CDTAPI and on CDtapiLite, and compares them
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Builds the example program against CDTAPI.h and the real CDTAPI library, which itself
# is built from its source and DTAPI's object file from a DekTec Linux SDK. Runs that
# build and CDtapiLite's build of the same program with the same arguments, one after the
# other, and compares their output line by line.
#
#   Scripts/compare_cdtapi.sh <CDTAPI directory> <LinuxSDK directory> <Program> [args]
#
# The CDTAPI directory holds CDTAPI.cpp and CDTAPI.h; the SDK directory holds
# DTAPI/Include/DTAPI.h. CDtapiLite's build is taken from Build/linux-debug, or from the
# directory CDTAPILITE_BUILD names. Exits with 0 when the outputs are identical.

set -euo pipefail

RepoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ $# -lt 3 ]; then
    echo "Usage: $0 <CDTAPI directory> <LinuxSDK directory> <Program> [args]"
    exit 1
fi

Cdtapi="$(cd "$1" && pwd)"
Sdk="$(cd "$2" && pwd)"
Program="$3"
shift 3
Build="${CDTAPILITE_BUILD:-$RepoRoot/Build/linux-debug}"
Work="$(mktemp -d)"
trap 'rm -rf "$Work"' EXIT

DtapiObject="$(find "$Sdk/DTAPI/Lib" -name DTAPI64.o | sort | tail -n 1)"
if [ -z "$DtapiObject" ]; then
    echo "No DTAPI64.o under $Sdk/DTAPI/Lib"
    exit 1
fi

# CDTAPI.h includes its generated version header; any content will do here.
echo '#define CDTAPI_VERSION "compare"' > "$Work/CDTAPI_Version.h"

g++ -std=c++17 -O1 -I "$Cdtapi" -I "$Work" -I "$Sdk/DTAPI/Include" \
    -c "$Cdtapi/CDTAPI.cpp" -o "$Work/CDTAPI.o"
Examples="$RepoRoot/Examples"
for Source in "$Examples/$Program.c" "$Examples/Common/ExampleCommon.c"; do
    gcc -std=c11 -O1 -DEXAMPLE_WITH_CDTAPI -I "$Work" -I "$Cdtapi" -I "$Examples" \
        -c "$Source" -o "$Work/$(basename "$Source" .c).o"
done
g++ -o "$Work/$Program" "$Work/$Program.o" "$Work/ExampleCommon.o" "$Work/CDTAPI.o" \
    "$DtapiObject" -lpthread -ldl -latomic

Status=0
"$Work/$Program" "$@" > "$Work/cdtapi.txt" || Status=$?
echo "exit $Status" >> "$Work/cdtapi.txt"
Status=0
"$Build/Examples/$Program" "$@" > "$Work/cdtapilite.txt" || Status=$?
echo "exit $Status" >> "$Work/cdtapilite.txt"

if diff -u "$Work/cdtapi.txt" "$Work/cdtapilite.txt"; then
    echo "Identical: $Program $*"
    cat "$Work/cdtapi.txt"
else
    exit 1
fi
