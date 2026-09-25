#!/usr/bin/env bash
# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* check_style.sh *#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDTAPI - Enforces the coding rules that clang-format cannot express
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Rules 4 and 6 are enforced by clang-format 18.1.8, which this script also runs, and
# fails without. The rest are checked here:
#
#   Rule 2  Comments describe what the code does, never what it used to do.
#   Rule 4  No line longer than 90 characters. Checked here as well as by clang-format,
#           because clang-format reflows code but leaves an over-long comment alone.
#   Rule 5  Every source file starts with a header naming the file.
#   Rule 8  Every header guards itself with #pragma once, right after the file header.
#   Rule 9  No goto.
#   Rule 11 The public headers declare each section's functions in alphabetical order.
#
# It also fails when Documentation/ or the internal notes, CLAUDE.md and CLAUDE.local.md,
# are tracked: both belong outside this public repository.
#
# Source/DtPcie/Abi is skipped by the rules: it holds files vendored verbatim from the
# SDK. What is checked there is that they are still the bytes SHA256SUMS records.
#
# With --staged, which the pre-commit hook passes, the rules look only at the C files the
# commit adds or changes, so that a commit does not wait for the whole tree. A staged
# .clang-format or check_style.sh changes what every file is held to, so either of them
# brings the whole tree back. The checks that are not per file always run, and CI and
# Scripts/build.sh check everything.

set -uo pipefail

RepoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$RepoRoot"

Staged=0
if [ "${1:-}" = "--staged" ]; then
    Staged=1
    git diff --cached --name-only | grep -qxE '\.clang-format|Scripts/check_style\.sh' \
        && Staged=0
fi

MaxLineLength=90
Failures=0

Fail()
{
    echo "  $1"
    Failures=$((Failures + 1))
}

# Files the rules apply to: CDTAPI's own C sources and headers, and with --staged only
# those of them the commit adds or changes.
OwnFiles()
{
    if [ "$Staged" -eq 1 ]; then
        git diff --cached --name-only --diff-filter=ACMR \
            -- Examples Include Source Tests Tools
    else
        find Examples Include Source Tests Tools -type f 2>/dev/null
    fi | grep -E '\.[ch]$' | grep -v '^Source/DtPcie/Abi/' | sort
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

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Rule 8: #pragma once -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
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

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- No internal documents -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
#
# The design documents live in a repository of their own, because they describe DTAPI's
# internals and DekTec's own infrastructure. This repository is public.
#
echo "Internal documents stay out"
Internal=$(git ls-files 'Documentation/*' 'Documentation' 2>/dev/null | head -5)
if [ -n "$Internal" ]; then
    Fail "Documentation/ belongs in dektec-com/cdtapi-design, not here: $(echo $Internal)"
fi

# The notes for DekTec's own tooling name internal machines and repositories. .gitignore
# keeps them out; this catches the one that was added with -f anyway.
Notes=$(git ls-files 'CLAUDE.md' 'CLAUDE.local.md' 2>/dev/null | head -5)
if [ -n "$Notes" ]; then
    Fail "Internal notes are not published: $(echo $Notes)"
fi

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Rule 9: no goto -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
#
# A function that must clean up after failures hands its steps to a helper and cleans up
# after it; a goto, even to a cleanup label, is refused. Comments are not looked at.
#
echo "Rule 9: no goto"
while IFS= read -r File; do
    while IFS= read -r Hit; do
        Fail "$File:${Hit%%:*}: goto"
    done < <(grep -nE '^[^/]*\bgoto\b' "$File")
done < <(OwnFiles)

# .-.-.-.-.-.-.-.-.- Rule 11: public functions in alphabetical order -.-.-.-.-.-.-.-.-.-.-
#
# In each section of a public header, from one banner to the next, the functions come in
# alphabetical order, ignoring case; the types between them are not looked at.
#
echo "Rule 11: public functions in alphabetical order"
while IFS= read -r File; do
    while IFS= read -r Hit; do
        Fail "$Hit"
    done < <(LC_ALL=C awk -v f="$File" '
        /^\/\/ (\+=|\.-)/ { Last = ""; next }
        /^CDTAPI_API/ {
            Name = $0
            sub(/\(.*/, "", Name)
            match(Name, /[A-Za-z0-9_]+$/)
            Name = substr(Name, RSTART, RLENGTH)
            Key = tolower(Name)
            if (Last != "" && Key < Last)
                printf "%s:%d: %s after %s\n", f, NR, Name, LastName
            Last = Key
            LastName = Name
        }' "$File")
done < <(OwnFiles | grep '^Include/')

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Vendored ABI -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
#
# Source/DtPcie/Abi holds the driver's headers byte for byte as the SDK has them, and
# SHA256SUMS beside them records those bytes. A refresh from the SDK writes the sums
# again in the same commit; any other change to a vendored file fails here, as does a
# header added to the directory without a sum.
#
echo "Vendored ABI unchanged"
if ! (cd Source/DtPcie/Abi && sha256sum --quiet -c SHA256SUMS) >/dev/null 2>&1; then
    Fail "Source/DtPcie/Abi: a file differs from SHA256SUMS; see VENDORED.md"
fi
for File in $(git ls-files 'Source/DtPcie/Abi/*.h'); do
    grep -qE "[ *]${File##*/}\$" Source/DtPcie/Abi/SHA256SUMS \
        || Fail "$File: vendored without a sum in SHA256SUMS"
done

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Rules 4 and 6: format -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

# Builds of clang-format do not format alike, not even within one major version, so the
# version is exact here. Scripts/check_tools.sh checks the same one.
ClangFormatVersion="18.1.8"
ClangFormat="${CLANG_FORMAT:-clang-format}"
if ! command -v "$ClangFormat" >/dev/null 2>&1; then
    Fail "clang-format not found; pip install clang-format==$ClangFormatVersion, or point CLANG_FORMAT at it. Scripts/check_tools.sh lists what this project needs."
elif [ "$("$ClangFormat" --version | sed 's/.*version //;s/[^0-9.].*//')" != \
       "$ClangFormatVersion" ]; then
    Fail "clang-format is $("$ClangFormat" --version | sed 's/.*version //'), and this project is formatted with $ClangFormatVersion; another build reformats files that are right."
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
