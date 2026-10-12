#!/usr/bin/env bash
# #*#*#*#*#*#*#*#*#*#*#*#* build_ubuntu_18.04.sh *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDTAPI - Builds, tests and packs the library on the oldest Linux the SDK serves
#
# SPDX-License-Identifier: BSD-3-Clause
#
# The SDK serves Linux systems as old as Ubuntu 18.04, with GCC 7.5 and glibc 2.27. A
# shared library names the lowest glibc version each function it calls needs, so one
# built on a newer system does not load there; one built on glibc 2.27 loads on every
# later one. This script builds in an Ubuntu 18.04 container, with that system's own
# compiler and CMake 3.22 from Kitware, and does three things there:
#
#   - builds the linux-release preset and runs its suites, against the emulated device;
#   - builds what the SDK ships, the static library in Release and the shared one in
#     RelWithDebInfo, installed in the SDK's layout;
#   - checks that the shared library needs no glibc newer than 2.27 and that the static
#     one calls none of the names newer glibc headers put in, such as __isoc23_strtol.
#
# With --package it packs the SDK's files into
# Build/ubuntu-18.04/cdtapi-X.Y.Z-sdk-linux-x64.tar.gz, with its SHA-256 beside it, which
# the release attaches.
#
# It needs docker or podman, and runs itself again inside the container with --inside.

set -euo pipefail

RepoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$RepoRoot"

Image="docker.io/library/ubuntu:18.04"
OldestGlibc="2.27"
CMakeVersion="3.22.6"
CMakeSha256="09e1b34026c406c5bf4d1b053eadb3a8519cb360e37547ebf4b70ab766d94fbc"

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Usage -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

Usage()
{
    cat <<'USAGE'
Usage: Scripts/build_ubuntu_18.04.sh [--package]

Builds and tests CDTAPI in an Ubuntu 18.04 container, builds the SDK's files and checks
the glibc they need.

Options:
  -p, --package  Pack the SDK's files into Build/ubuntu-18.04 as well
  -h, --help     Show this message
USAGE
}

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Arguments -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

Inside=0
Package=0

while [ $# -gt 0 ]; do
    case "$1" in
        --inside)     Inside=1 ;;
        -p|--package) Package=1 ;;
        -h|--help)    Usage; exit 0 ;;
        *)            echo "Unknown option: $1" >&2; Usage >&2; exit 2 ;;
    esac
    shift
done

Step()
{
    printf '\n==> %s\n' "$1"
}

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- On the host -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

# The host starts the container on this same script, with the tree mounted at /src.
if [ "$Inside" -eq 0 ]; then
    Engine=""
    for Candidate in docker podman; do
        if command -v "$Candidate" >/dev/null 2>&1; then
            Engine="$Candidate"
            break
        fi
    done
    if [ -z "$Engine" ]; then
        echo "docker or podman is needed to build in an Ubuntu 18.04 container" >&2
        exit 1
    fi

    Arguments=(--inside)
    [ "$Package" -eq 1 ] && Arguments+=(--package)

    Step "Starting $Image with $Engine"
    exec "$Engine" run --rm -v "$RepoRoot:/src:Z" -w /src "$Image" \
        bash Scripts/build_ubuntu_18.04.sh "${Arguments[@]}"
fi

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- In the container -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

Step "Installing GCC, Ninja and CMake $CMakeVersion"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends \
    gcc libc6-dev binutils ninja-build curl ca-certificates >/dev/null

CMakeName="cmake-$CMakeVersion-linux-x86_64"
curl -sSLf -o "/tmp/$CMakeName.tar.gz" \
    "https://github.com/Kitware/CMake/releases/download/v$CMakeVersion/$CMakeName.tar.gz"
echo "$CMakeSha256  /tmp/$CMakeName.tar.gz" | sha256sum -c --quiet
tar xzf "/tmp/$CMakeName.tar.gz" -C /opt
export PATH="/opt/$CMakeName/bin:$PATH"

gcc --version | head -1
ldd --version | head -1
cmake --version | head -1

# The build directories stay in the container; only the package comes out.
Work=/tmp/cdtapi
rm -rf "$Work"

Step "Building and testing preset 'linux-release'"
cmake --preset linux-release -B "$Work/test"
cmake --build "$Work/test"
(cd "$Work/test" && ctest --output-on-failure)

Step "Building the SDK's files"
Version="$(sed -n 's/^ *VERSION \([0-9.]*\) *$/\1/p' CMakeLists.txt | head -1)"
Name="cdtapi-$Version-sdk-linux-x64"
Stage="$Work/stage/$Name"

SdkOptions=(-G Ninja
    -DCDTAPI_SDK_LAYOUT:BOOL=ON
    -DCDTAPI_BUILD_TESTS:BOOL=OFF
    -DCDTAPI_BUILD_EXAMPLES:BOOL=OFF)
cmake "${SdkOptions[@]}" -DCMAKE_BUILD_TYPE:STRING=Release -S . -B "$Work/static"
cmake --build "$Work/static"
cmake "${SdkOptions[@]}" -DCDTAPI_BUILD_SHARED:BOOL=ON \
    -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo -S . -B "$Work/shared"
cmake --build "$Work/shared"
cmake --install "$Work/static" --prefix "$Stage"
cmake --install "$Work/shared" --prefix "$Stage"

Step "Checking the glibc they need"
Shared="$(ls "$Stage"/Lib/libcdtapi.so.*.*.* | head -1)"
Newest="$(objdump -T "$Shared" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' \
    | sort -uV | tail -1)"
echo "$(basename "$Shared") needs glibc $Newest"
Higher="$(printf '%s\n%s\n' "$Newest" "$OldestGlibc" | sort -V | tail -1)"
if [ "$Higher" != "$OldestGlibc" ]; then
    echo "  it needs a glibc newer than $OldestGlibc" >&2
    exit 1
fi
if nm -u "$Stage/Lib/libcdtapi.a" | grep -q '__isoc23_'; then
    echo "  libcdtapi.a calls functions of a glibc newer than $OldestGlibc:" >&2
    nm -u "$Stage/Lib/libcdtapi.a" | grep '__isoc23_' | sort -u >&2
    exit 1
fi
echo "libcdtapi.a calls no function newer glibc headers put in"

if [ "$Package" -eq 1 ]; then
    Step "Packing $Name.tar.gz"
    Out=/src/Build/ubuntu-18.04
    mkdir -p "$Out"
    tar czf "$Out/$Name.tar.gz" -C "$Work/stage" "$Name"
    (cd "$Out" && sha256sum "$Name.tar.gz" > "$Name.tar.gz.sha256")
    # The container runs as root; what it leaves in the tree belongs to the tree's owner.
    chown -R "$(stat -c %u:%g /src)" "$Out"
    tar tzf "$Out/$Name.tar.gz"
    cat "$Out/$Name.tar.gz.sha256"
fi

Step "Done"
