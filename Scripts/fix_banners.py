#!/usr/bin/env python3
# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* fix_banners.py *#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDtapiLite - Regenerates banner and separator comments at exactly 90 columns
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Banner lines are decorative runs of #*, +=, .- around a title. Typed by hand they come
# out one or two characters too long or short, which the style check then rejects. This
# rebuilds each one from its title so the width is right by construction.
#
# Usage: Scripts/fix_banners.py <file> [<file> ...]

import re
import sys

WIDTH = 90

# The first line of a file: "// #*#*... Name *#*#... (C) 2026 DekTec"
FILE_BANNER = re.compile(r"^(?P<lead>//|#) #\*[#*]* (?P<name>\S+) \*[#*]* (?P<copy>\(C\) .+)$")

# A section separator: "// +=+=... Title +=+=..." or "// .-.-... Title -.-.-..."
SEPARATOR = re.compile(r"^(?P<indent>\s*)(?P<lead>//|#) (?P<fill>\+=|\.-)[+=.\-]* "
                       r"(?P<title>.+?) [+=.\-]+$")


def Fill(Pattern, Length):
    return (Pattern * (Length // len(Pattern) + 1))[:Length]


def FileBanner(Match):
    Lead, Name, Copy = Match.group("lead"), Match.group("name"), Match.group("copy")
    # lead + " " + left + " " + name + " " + right + " " + copy
    Available = WIDTH - len(Lead) - len(Name) - len(Copy) - 4
    Left = Available // 2
    Right = Available - Left
    return "%s %s %s %s %s" % (Lead, Fill("#*", Left), Name, Fill("*#", Right), Copy)


def Separator(Match):
    Indent, Lead = Match.group("indent"), Match.group("lead")
    Title, Pattern = Match.group("title"), Match.group("fill")
    Available = WIDTH - len(Indent) - len(Lead) - len(Title) - 3
    # The left-hand run is kept to whole pairs, so it always ends on the second character
    # of its pattern; an odd leftover character goes to the right-hand run.
    Left = (Available // 2) & ~1
    Right = Available - Left
    # DekTec convention, as in LibDekTec_C/Source/Device.c: a ".-" rule mirrors around
    # its title, so the right-hand run starts with "-."; a "+=" rule does not mirror.
    RightPattern = "-." if Pattern == ".-" else "+="
    return "%s%s %s %s %s" % (Indent, Lead, Fill(Pattern, Left), Title,
                              Fill(RightPattern, Right))


def Process(Path):
    with open(Path, encoding="utf-8", newline="") as Handle:
        Text = Handle.read()
    Newline = "\r\n" if "\r\n" in Text else "\n"
    Lines = Text.split(Newline)
    Changed = 0
    for Index, Line in enumerate(Lines):
        Match = FILE_BANNER.match(Line) if Index == 0 else None
        Fixed = FileBanner(Match) if Match else None
        if Fixed is None:
            Match = SEPARATOR.match(Line)
            Fixed = Separator(Match) if Match else None
        if Fixed is not None and Fixed != Line:
            Lines[Index] = Fixed
            Changed += 1
    if Changed:
        with open(Path, "w", encoding="utf-8", newline="") as Handle:
            Handle.write(Newline.join(Lines))
    return Changed


if __name__ == "__main__":
    Total = 0
    for FilePath in sys.argv[1:]:
        Count = Process(FilePath)
        if Count:
            print("%s: %d banner line(s) regenerated" % (FilePath, Count))
        Total += Count
    sys.exit(0)
