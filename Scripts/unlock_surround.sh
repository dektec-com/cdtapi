#!/usr/bin/env bash
# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* unlock_surround.sh #*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDTAPI - Makes the working tree writable again after Surround SCM has been over it
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Surround SCM hands out read-only files unless it is told otherwise: "sscm get -e" and
# "sscm ci -w" keep them writable, and without those git cannot replace a file, so a pull
# or a checkout fails halfway. This clears the read-only bit of the files git tracks.
#
# It does nothing unless the tree is a Surround working directory, which is what the
# .MySCMServerInfo in its root says, so a plain clone, a build server or an outside
# contributor never notices it.
#
# Usage: Scripts/unlock_surround.sh [--quiet]

set -u

Root=$(git rev-parse --show-toplevel 2>/dev/null) || exit 0
cd "$Root" || exit 0
[ -f ".MySCMServerInfo" ] || exit 0

Quiet=0
[ "${1:-}" = "--quiet" ] && Quiet=1

Unlocked=0
while IFS= read -r File; do
    [ -f "$File" ] || continue
    if [ ! -w "$File" ]; then
        chmod u+w "$File" 2>/dev/null && Unlocked=$((Unlocked + 1))
    fi
done < <(git ls-files)

if [ "$Unlocked" -ne 0 ] && [ "$Quiet" -eq 0 ]; then
    echo "Surround left $Unlocked file(s) read-only; they are writable again."
    echo "Keep them that way with 'sscm ci -w' and 'sscm get -e'."
fi
exit 0
