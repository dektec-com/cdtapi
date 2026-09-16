#!/usr/bin/env bash
# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* check_style.sh *#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDtapiLite - Enforces the coding rules that clang-format cannot express
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Rules 4 and 6 are enforced by clang-format, which this script also runs when it is
# available. The rest are checked here:
#
#   Rule 2  Comments describe what the code does, never what it used to do.
#   Rule 4  No line longer than 90 characters. Checked here as well as by clang-format,
#           because clang-format reflows code but leaves an over-long comment alone.
#   Rule 5  Every source file starts with a header naming the file.
#
# Source/Drv/Abi is skipped: it holds files vendored verbatim from the SDK.

set -uo pipefail

RepoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$RepoRoot"

MaxLineLength=90
Failures=0

Fail()
{
    echo "  $1"
    Failures=$((Failures + 1))
}

# Files the rules apply to: CDtapiLite's own C sources and headers.
OwnFiles()
{
    find Examples Include Source Tests Tools -type f \( -name '*.c' -o -name '*.h' \) 2>/dev/null \
        | grep -v '^Source/Drv/Abi/' \
        | sort
}

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Rule 4: line length -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

echo "Rule 4: line length <= $MaxLineLength"
while IFS= read -r File; do
    awk -v max="$MaxLineLength" -v f="$File" '
        length > max { printf "%s:%d: %d characters\n", f, NR, length }' "$File"
done < <(OwnFiles) | while IFS= read -r Line; do echo "  $Line"; done

LongLines=$(while IFS= read -r File; do
    awk -v max="$MaxLineLength" 'length > max { c++ } END { print c + 0 }' "$File"
done < <(OwnFiles) | awk '{ s += $1 } END { print s + 0 }')
[ "$LongLines" -ne 0 ] && Failures=$((Failures + LongLines))

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Rule 5: file header -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
#
# The first line must be the DekTec banner and must name the file it is in. That second
# half is the point: a copy-pasted header naming the wrong file otherwise survives for
# years.

echo "Rule 5: file header present and naming the file"
while IFS= read -r File; do
    Base="$(basename "$File")"
    FirstLine="$(head -1 "$File")"
    case "$FirstLine" in
        *'#*#*'*) ;;
        *) Fail "$File:1: missing the '#*#*' banner header"; continue ;;
    esac
    case "$FirstLine" in
        *"$Base"*) ;;
        *) Fail "$File:1: header does not name '$Base'" ;;
    esac
done < <(OwnFiles)

# .-.-.-.-.-.-.-.-.-.-.-.-.-.- Rule 2: no historical comments -.-.-.-.-.-.-.-.-.-.-.-.-.-.
#
# Only the obvious phrasings are caught. The rest is a review matter; this is a tripwire,
# not a proof.

echo "Rule 2: comments do not describe former behaviour"
HistoryPattern='used to (be|do|call|return)|previously|changed from|was renamed'
HistoryPattern="$HistoryPattern"'|no longer|formerly|in the old |old implementation'
while IFS= read -r File; do
    grep -nEi "^[[:space:]]*(//|\*|/\*).*($HistoryPattern)" "$File" 2>/dev/null \
        | while IFS= read -r Hit; do echo "  $File:$Hit"; done
done < <(OwnFiles)

HistoryHits=$(while IFS= read -r File; do
    grep -cEi "^[[:space:]]*(//|\*|/\*).*($HistoryPattern)" "$File" 2>/dev/null
done < <(OwnFiles) | awk '{ s += $1 } END { print s + 0 }')
[ "$HistoryHits" -ne 0 ] && Failures=$((Failures + HistoryHits))

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Rules 4 and 6: format -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

ClangFormat="${CLANG_FORMAT:-clang-format}"
if command -v "$ClangFormat" >/dev/null 2>&1; then
    echo "Rules 4 and 6: clang-format"
    while IFS= read -r File; do
        if ! "$ClangFormat" --dry-run --Werror "$File" >/dev/null 2>&1; then
            Fail "$File: not formatted; run '$ClangFormat -i $File'"
        fi
    done < <(OwnFiles)
else
    echo "Rules 4 and 6: clang-format not found, skipping (set CLANG_FORMAT to override)"
fi

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Verdict -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

echo
if [ "$Failures" -eq 0 ]; then
    echo "Style checks passed."
    exit 0
fi

echo "Style checks failed with $Failures problem(s)."
exit 1
