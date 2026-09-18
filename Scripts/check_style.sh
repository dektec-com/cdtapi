#!/usr/bin/env bash
# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* check_style.sh *#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDTAPI - Enforces the coding rules that clang-format cannot express
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
#   Rule 8  Every header guards itself with #pragma once, right after the file header.
#
# Source/DtPcie/Abi is skipped: it holds files vendored verbatim from the SDK.

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

# Files the rules apply to: CDTAPI's own C sources and headers.
OwnFiles()
{
    find Examples Include Source Tests Tools -type f \( -name '*.c' -o -name '*.h' \) 2>/dev/null \
        | grep -v '^Source/DtPcie/Abi/' \
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

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Rule 8: #pragma once -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
#
# The first line of a header that is neither a comment nor blank must be #pragma once.

echo "Rule 8: headers guarded by #pragma once"
while IFS= read -r File; do
    case "$File" in
        *.h) ;;
        *) continue ;;
    esac
    FirstCode="$(awk '!/^[[:space:]]*(\/\/.*)?$/ { print; exit }' "$File")"
    [ "$FirstCode" = "#pragma once" ] \
        || Fail "$File: the first line after the file header is not '#pragma once'"
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

# The major version decides how code is formatted, so another one reformats files that
# are already right. Scripts/check_tools.sh checks the same number.
ClangFormatMajor=18
ClangFormat="${CLANG_FORMAT:-clang-format}"
if ! command -v "$ClangFormat" >/dev/null 2>&1; then
    Fail "clang-format not found; install version $ClangFormatMajor, or point CLANG_FORMAT at it. Scripts/check_tools.sh lists what this project needs."
elif [ "$("$ClangFormat" --version | sed 's/.*version \([0-9][0-9]*\).*/\1/')" != \
       "$ClangFormatMajor" ]; then
    Fail "clang-format is $("$ClangFormat" --version | sed 's/.*version //'), and this project is formatted with version $ClangFormatMajor; another one reformats files that are right."
else
    echo "Rules 4 and 6: clang-format"
    while IFS= read -r File; do
        if ! "$ClangFormat" --dry-run --Werror "$File" >/dev/null 2>&1; then
            Fail "$File: not formatted; run '$ClangFormat -i $File'"
        fi
    done < <(OwnFiles)
fi

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Verdict -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

echo
if [ "$Failures" -eq 0 ]; then
    echo "Style checks passed."
    exit 0
fi

echo "Style checks failed with $Failures problem(s)."
exit 1
