#!/usr/bin/env bash
# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# build.sh *#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
#
# CDtapiLite - Configures, builds, tests and lints the project
#
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

RepoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$RepoRoot"

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Usage -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

Usage()
{
    cat <<'USAGE'
Usage: Scripts/build.sh [options] [preset]

Configures, builds and tests one CMake preset. The preset defaults to the debug preset
for the host platform: windows-debug on Windows, linux-debug elsewhere.

Options:
  -c, --clean       Remove the preset's build directory before configuring
  -b, --build-only  Build without running the tests
  -t, --test-only   Run the tests without building first
  -l, --lint        Run the style checks as well
      --lint-only   Run the style checks and nothing else
  -L, --list        List the available presets
  -h, --help        Show this message

Examples:
  Scripts/build.sh                    # configure, build and test the host debug preset
  Scripts/build.sh linux-release      # the same for a named preset
  Scripts/build.sh -c windows-sim     # clean rebuild without the driver backends
  Scripts/build.sh --lint-only        # style checks only, no compiler needed
USAGE
}

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Arguments -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

Clean=0
DoBuild=1
DoTest=1
DoLint=0
Preset=""

while [ $# -gt 0 ]; do
    case "$1" in
        -c|--clean)      Clean=1 ;;
        -b|--build-only) DoTest=0 ;;
        -t|--test-only)  DoBuild=0 ;;
        -l|--lint)       DoLint=1 ;;
        --lint-only)     DoLint=1; DoBuild=0; DoTest=0 ;;
        -L|--list)       cmake --list-presets; exit 0 ;;
        -h|--help)       Usage; exit 0 ;;
        -*)              echo "Unknown option: $1" >&2; Usage >&2; exit 2 ;;
        *)               Preset="$1" ;;
    esac
    shift
done

if [ -z "$Preset" ]; then
    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*) Preset="windows-debug" ;;
        *)                    Preset="linux-debug" ;;
    esac
fi

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Steps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

Step()
{
    printf '\n==> %s\n' "$1"
}

if [ "$DoLint" -eq 1 ]; then
    Step "Style checks"
    Scripts/check_style.sh
fi

if [ "$DoBuild" -eq 1 ]; then
    if [ "$Clean" -eq 1 ] && [ -d "Build/$Preset" ]; then
        Step "Removing Build/$Preset"
        rm -rf "Build/$Preset"
    fi

    Step "Configuring preset '$Preset'"
    cmake --preset "$Preset"

    Step "Building preset '$Preset'"
    cmake --build --preset "$Preset"
fi

if [ "$DoTest" -eq 1 ]; then
    Step "Testing preset '$Preset'"
    ctest --preset "$Preset"
fi

Step "Done"
