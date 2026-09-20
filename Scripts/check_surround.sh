#!/usr/bin/env bash
# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* check_surround.sh #*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
#
# CDTAPI - Compares what git holds with what Surround SCM holds
#
# SPDX-License-Identifier: BSD-3-Clause
#
# The library is developed in git and shipped from Surround SCM, which does not notice a
# new file by itself: every file is added to it by hand. This script lists what the two
# disagree about, so that the difference can be put right in Surround:
#
#   not in Surround   a file git holds that the directory's .MySCMServerInfo does not
#                     list, or a whole directory that has no .MySCMServerInfo at all
#   not in git        a file Surround holds that git does not, which is a file that was
#                     renamed or removed on this side
#
# Surround leaves a .MySCMServerInfo in every working directory, which names the files it
# holds there; it appears when Surround last wrote the directory, so a directory that was
# never added has none. The names in .sscmignore stay out of Surround on purpose and are
# skipped here. The script changes nothing: adding and removing in Surround is the user's
# own doing.
#
# Where there is no .MySCMServerInfo at all, the working copy is a plain clone and the
# script says so and exits 0.

set -uo pipefail

RepoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$RepoRoot"

if [ ! -f .MySCMServerInfo ]; then
    echo "No .MySCMServerInfo: this working copy is not mirrored into Surround SCM."
    exit 0
fi

# The names .sscmignore keeps out, one per line, as path prefixes or file names.
Ignored=()
if [ -f .sscmignore ]; then
    while IFS= read -r Line; do
        Line="${Line%$'\r'}"
        [ -n "$Line" ] && Ignored+=("$Line")
    done < .sscmignore
fi

# Whether a path is one of those names or lies under one.
IsIgnored()
{
    local Path="$1"
    local Name
    for Name in ${Ignored[@]+"${Ignored[@]}"}; do
        case "$Path" in
        "$Name" | "$Name"/*) return 0 ;;
        esac
        case "${Path##*/}" in
        "$Name") return 0 ;;
        esac
    done
    return 1
}

# The files Surround holds in a directory: every line after the four header lines, up to
# the first semicolon.
SurroundFiles()
{
    sed -e '1,4d' -e 's/;.*//' -e 's/\r$//' "$1/.MySCMServerInfo" | grep -v '^$'
}

Missing=0
Extra=0
NoDirectory=0

# Every directory git holds a file in, the root first.
Directories="$(git ls-files | sed -e 's|[^/]*$||' -e 's|/$||' | sort -u)"

for Dir in . $Directories; do
    Dir="${Dir:-.}"
    IsIgnored "${Dir#./}" && continue

    if [ "$Dir" = "." ]; then
        InGit="$(git ls-files | grep -v '/')"
    else
        InGit="$(git ls-files "$Dir/" | grep "^$Dir/[^/]*$" | sed "s|^$Dir/||")"
    fi
    Wanted=""
    for File in $InGit; do
        Candidate="$Dir/$File"
        IsIgnored "${Candidate#./}" || Wanted="$Wanted$File"$'\n'
    done
    Wanted="$(printf '%s' "$Wanted" | grep -v '^$' | sort -u)"
    [ -z "$Wanted" ] && continue

    if [ ! -f "$Dir/.MySCMServerInfo" ]; then
        echo "not in Surround: the whole directory ${Dir#./}"
        NoDirectory=$((NoDirectory + 1))
        continue
    fi

    Shown="${Dir#./}"
    [ "$Shown" = "." ] && Shown=""
    [ -n "$Shown" ] && Shown="$Shown/"

    InSurround="$(SurroundFiles "$Dir" | sort -u)"
    while IFS= read -r File; do
        [ -n "$File" ] && echo "not in Surround: $Shown$File"
    done < <(comm -23 <(printf '%s\n' "$Wanted") <(printf '%s\n' "$InSurround"))
    Missing=$((Missing + $(comm -23 <(printf '%s\n' "$Wanted") \
        <(printf '%s\n' "$InSurround") | grep -c '[^[:space:]]')))

    while IFS= read -r File; do
        [ -n "$File" ] && echo "not in git:      $Shown$File"
    done < <(comm -13 <(printf '%s\n' "$Wanted") <(printf '%s\n' "$InSurround"))
    Extra=$((Extra + $(comm -13 <(printf '%s\n' "$Wanted") \
        <(printf '%s\n' "$InSurround") | grep -c '[^[:space:]]')))
done

echo
if [ $((Missing + Extra + NoDirectory)) -eq 0 ]; then
    echo "Surround SCM holds every file git holds."
else
    echo "$Missing file(s) to add to Surround, $NoDirectory directory/directories never"\
         "added, $Extra file(s) Surround holds that git does not."
fi
exit 0
