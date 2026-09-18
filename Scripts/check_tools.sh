#!/usr/bin/env bash
# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* check_tools.sh #*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDTAPI - What this project needs to build and test, and whether it is there
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Prints one line per tool: what was found, and what to install when it is missing or too
# old. Exits 0 when everything needed is there, 1 when it is not. CONTRIBUTING.md names
# the same versions; this script is what checks them.

set -u

# The versions the project is held to. clang-format is exact to the patch number: builds
# of one major version do not format alike either, so another one reformats files that
# are already right. pip install clang-format==<version> gives exactly this one.
ClangFormatVersion="18.1.8"
CMakeMinimum="3.21"
PythonMinimum="3.8"
GccMinimum=11

Failures=0
Report()
{
    printf '%-14s %s\n' "$1" "$2"
}

Fail()
{
    Failures=$((Failures + 1))
    printf '%-14s %s\n' "$1" "$2"
    printf '%-14s   %s\n' "" "$3"
}

# Whether $1 is at least $2, comparing dotted numbers.
AtLeast()
{
    [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -1)" = "$2" ]
}

# The first number of a version string, such as 18 in "18.1.8".
Major()
{
    printf '%s' "$1" | sed 's/[^0-9]*\([0-9][0-9]*\).*/\1/'
}

case "$(uname -s)" in
MINGW* | MSYS* | CYGWIN*) System="Windows" ;;
*) System=$(uname -s) ;;
esac

echo "Tools for CDTAPI, on $System"
echo

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- clang-format -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

ClangFormat="${CLANG_FORMAT:-clang-format}"
if command -v "$ClangFormat" >/dev/null 2>&1; then
    Version=$("$ClangFormat" --version | sed 's/.*version //' | tr -d '\r')
    if [ "$Version" = "$ClangFormatVersion" ]; then
        Report "clang-format" "$Version"
    else
        Fail "clang-format" "$Version, and the project is formatted with $ClangFormatVersion" \
             "pip install clang-format==$ClangFormatVersion, or point CLANG_FORMAT at it"
    fi
else
    Fail "clang-format" "not found" \
         "pip install clang-format==$ClangFormatVersion, or point CLANG_FORMAT at it"
fi

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CMake -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

if command -v cmake >/dev/null 2>&1; then
    Version=$(cmake --version | head -1 | sed 's/.*version //')
    if AtLeast "$Version" "$CMakeMinimum"; then
        Report "cmake" "$Version"
    else
        Fail "cmake" "$Version, and $CMakeMinimum is the minimum" \
             "apt install cmake, or winget install Kitware.CMake"
    fi
else
    Fail "cmake" "not found" "apt install cmake, or winget install Kitware.CMake"
fi

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Python -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

# A candidate that answers with anything but a version, such as the stub Windows puts on
# the path when Python is not installed, is passed over.
Python=""
Version=""
for Candidate in python3 python; do
    command -v "$Candidate" >/dev/null 2>&1 || continue
    Answer=$("$Candidate" --version 2>&1 | sed 's/.*Python //')
    case "$Answer" in
    [0-9]*)
        Python="$Candidate"
        Version="$Answer"
        break
        ;;
    esac
done
if [ -n "$Python" ]; then
    if AtLeast "$Version" "$PythonMinimum"; then
        Report "python" "$Version"
    else
        Fail "python" "$Version, and $PythonMinimum is the minimum" \
             "apt install python3, or winget install Python.Python.3"
    fi
else
    Fail "python" "not found" "apt install python3, or winget install Python.Python.3"
fi

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- The compiler -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

case "$System" in
Windows)
    # Visual Studio is what the Windows presets build with; CMake picks the newest it
    # finds. vswhere, which comes with Visual Studio, is where to ask.
    VsWhere="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
    if [ -x "$VsWhere" ]; then
        Version=$("$VsWhere" -latest -products '*' -property catalog_productDisplayVersion |
                  tr -d '\r')
        [ -z "$Version" ] && Version="unknown"
        if AtLeast "$Version" "17.0"; then
            Report "visual studio" "$Version"
        else
            Fail "visual studio" "$Version, and 2022 (17.0) is the minimum" \
                 "install Visual Studio 2022 or newer with the C and C++ workload"
        fi
    else
        Fail "visual studio" "not found" \
             "install Visual Studio 2022 or newer with the C and C++ workload"
    fi
    ;;
*)
    if command -v gcc >/dev/null 2>&1; then
        Version=$(gcc -dumpfullversion -dumpversion 2>/dev/null)
        if [ "$(Major "$Version")" -ge "$GccMinimum" ] 2>/dev/null; then
            Report "gcc" "$Version"
        else
            Fail "gcc" "$Version, and $GccMinimum is the minimum" "apt install gcc"
        fi
    else
        Fail "gcc" "not found" "apt install build-essential"
    fi
    if command -v ninja >/dev/null 2>&1; then
        Report "ninja" "$(ninja --version)"
    else
        Fail "ninja" "not found, and the Linux presets build with it" \
             "apt install ninja-build"
    fi
    ;;
esac

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Git -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

if command -v git >/dev/null 2>&1; then
    Report "git" "$(git --version | sed 's/git version //')"
else
    Fail "git" "not found" "apt install git, or winget install Git.Git"
fi

echo
if [ "$Failures" -eq 0 ]; then
    echo "Everything needed is there."
    exit 0
fi
echo "Missing or wrong: $Failures."
exit 1
