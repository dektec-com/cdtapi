#!/usr/bin/env bash
# #*#*#*#*#*#*#*#*#*#*#*#* compare_device_scan.sh *#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDtapiLite - Compares CDtapiLite's device scan with DTAPI's on a machine with a card
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Builds Scripts/Compare/DtapiDeviceScanRef.cpp against DTAPI from a DekTec Linux SDK,
# runs it and CDtapiLite's DtListDeviceDescs, and compares their output line by line.
#
#   Scripts/compare_device_scan.sh <LinuxSDK directory> [<CDtapiLite build directory>]
#
# The SDK directory is the one holding DTAPI/Include/DTAPI.h. The build directory
# defaults to Build/linux-debug. Exits with 0 when the outputs are identical.

set -euo pipefail

RepoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <LinuxSDK directory> [<CDtapiLite build directory>]"
    exit 1
fi

Sdk="$(cd "$1" && pwd)"
Build="${2:-$RepoRoot/Build/linux-debug}"
Work="$(mktemp -d)"
trap 'rm -rf "$Work"' EXIT

DtapiObject="$(find "$Sdk/DTAPI/Lib" -name DTAPI64.o | sort | tail -n 1)"
if [ -z "$DtapiObject" ]; then
    echo "No DTAPI64.o under $Sdk/DTAPI/Lib"
    exit 1
fi

g++ -O1 -I"$Sdk/DTAPI/Include" "$RepoRoot/Scripts/Compare/DtapiDeviceScanRef.cpp" \
    "$DtapiObject" -lpthread -ldl -o "$Work/DtapiDeviceScanRef"

"$Work/DtapiDeviceScanRef" > "$Work/dtapi.txt" || true
"$Build/Examples/DtListDeviceDescs" > "$Work/cdtapilite.txt" || true

if diff -u "$Work/dtapi.txt" "$Work/cdtapilite.txt"; then
    echo "Identical: $(tail -n 1 "$Work/dtapi.txt")"
else
    exit 1
fi
