#!/usr/bin/env python3
# #*#*#*#*#*#*#*#*#*#*# extract_ioconfig_relation.py *#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDtapiLite - Derives the I/O configuration relation from the DekTec capability XML
#
# SPDX-License-Identifier: BSD-3-Clause
#
# DTAPI validates an I/O configuration against a table that CapParser generates from
# SDK/Capabilities/CapabilityDefinitions.xml: for every DTAPI_IOCONFIG_ code, whether it
# is a group, a boolean I/O group, a value or a sub-value, and which codes sit one level
# below it. This script repeats that derivation (CapDefStore.cpp: Load, AddTrueAndFalse,
# EstablishHierarchy, EnumerateDefines, AddGrp, AddCap) and prints the entries of
# Source/Tables/DtlIoConfigList.inc, with each code's parents instead of its children.
#
# It is run by hand when DekTec adds I/O configuration codes; the build does not need the
# XML. The output is checked against the numbering already in the list, so a code that
# moved is reported rather than silently renumbered.
#
# Usage: Scripts/extract_ioconfig_relation.py <CapabilityDefinitions.xml> [<list.inc>]
#        Scripts/extract_ioconfig_relation.py --valid <CapabilityDefinitions.xml>
#
# The second form prints every valid combination of group, value and sub-value, worked
# out on the children lists the way DTAPI's IsValidConfig does, for the unit test that
# checks the parent encoding in the list against it.

import re
import sys
import xml.etree.ElementTree as ET


def Bool(Element, Name):
    return Element.get(Name, "false").strip().lower() == "true"


class Cap:
    def __init__(self, Name, Groups, Gold=False, Pseudo=False):
        self.Name = Name
        self.Groups = list(Groups)
        self.Gold = Gold
        self.Pseudo = Pseudo
        self.SubCaps = []
        self.IsCap = Pseudo
        self.IsSubCap = False
        self.IsBoolIo = Pseudo
        self.Index = -1


class Group:
    def __init__(self, Name, Boolean, IoCap):
        self.Name = Name
        self.Boolean = Boolean
        self.IoCap = IoCap
        self.Caps = []


def Derive(XmlPath):
    Root = ET.parse(XmlPath).getroot()
    Groups = [Group(G.get("name"), Bool(G, "boolean"), Bool(G, "iocap"))
              for G in Root.find("CapabilityGroups").findall("Group")]
    GroupMap = {G.Name: G for G in Groups}

    Caps = []
    Sections = ("PortCapabilities", "ApplicationCapabilities", "FwVariantCapabilities")
    for Section in Sections:
        for C in Root.find(Section).findall("Capability"):
            Caps.append(Cap(C.get("name"), C.get("group", "").split(), Bool(C, "gold")))
    Caps.append(Cap("TRUE", [], Pseudo=True))
    Caps.append(Cap("FALSE", [], Pseudo=True))
    CapMap = {C.Name: C for C in Caps}

    # EstablishHierarchy: TRUE and FALSE belong to every boolean I/O capability.
    for Pseudo in (C for C in Caps if C.Pseudo):
        for C in Caps:
            Pseudo.Groups += [C.Name for G in C.Groups if G == "BOOLIO"]

    for C in Caps:
        for G in C.Groups:
            if G in GroupMap:
                GroupMap[G].Caps.append(C)
            elif G in CapMap:
                CapMap[G].SubCaps.append(C)
            else:
                sys.exit(f"capability {C.Name}: group {G} is not defined")

    for G in Groups:
        for C in G.Caps:
            C.IsBoolIo = G.Boolean and G.IoCap
            C.IsCap = True
            for S in C.SubCaps:
                S.IsBoolIo = G.Boolean and G.IoCap
                S.IsSubCap = True

    # EnumerateDefines for I/O configuration codes.
    Codes = []

    def AddGrp(G):
        Codes.append({"Name": G.Name, "BoolIo": G.Boolean and G.IoCap, "Group": True,
                      "Value": False, "SubValue": False,
                      "Children": [C.Name for C in G.Caps]})

    def AddCap(C):
        if C.Index >= 0:
            return
        C.Index = len(Codes)
        Codes.append({"Name": C.Name, "BoolIo": C.IsBoolIo, "Group": False,
                      "Value": C.IsCap, "SubValue": C.IsSubCap,
                      "Children": [S.Name for S in C.SubCaps]})

    for G in Groups:
        if G.IoCap and not G.Boolean:
            AddGrp(G)
    for G in Groups:
        for C in G.Caps:
            if G.IoCap and G.Boolean:
                AddCap(C)
    for G in Groups:
        if not G.IoCap:
            continue
        for C in G.Caps:
            AddCap(C)
        for C in G.Caps:
            if C.Gold:
                continue
            for S in C.SubCaps:
                AddCap(S)

    Names = [C["Name"] for C in Codes]
    if len(set(Names)) != len(Names):
        sys.exit("duplicate I/O configuration name")
    for C in Codes:
        for Child in C["Children"]:
            if Child not in Names:
                sys.exit(f"{C['Name']}: child {Child} is not an I/O configuration code")
    return Codes


def Kinds(Code):
    Flags = []
    for Key, Flag in (("Group", "GROUP"), ("BoolIo", "BOOLIO"), ("Value", "VALUE"),
                      ("SubValue", "SUBVALUE")):
        if Code[Key]:
            Flags.append("DTL_IOCFG_" + Flag)
    return " | ".join(Flags) if Flags else "0"


def IsValid(Codes, Group, Value, SubValue):
    """DtConfigDefs::IsValidConfig, on the children lists as DTAPI holds them."""
    Count = len(Codes)
    if Group < 0 or Group >= Count:
        return False
    if not Codes[Group]["Group"] and not Codes[Group]["BoolIo"]:
        return False
    if Value < 0 or Value >= Count or not Codes[Value]["Value"]:
        return False
    if Codes[Value]["Name"] not in Codes[Group]["Children"]:
        return False
    if SubValue == -1:
        return not Codes[Value]["Children"]
    if Codes[Group]["BoolIo"]:
        return False
    if SubValue < 0 or SubValue >= Count or not Codes[SubValue]["SubValue"]:
        return False
    return Codes[SubValue]["Name"] in Codes[Value]["Children"]


def PrintValidTriples(Codes):
    """Every valid (group, value, sub-value), for the unit test to compare against."""
    Range = range(-1, len(Codes))
    for Group in Range:
        for Value in Range:
            for SubValue in Range:
                if IsValid(Codes, Group, Value, SubValue):
                    Name = lambda Code: ("-1" if Code < 0 else
                                         "DTAPI_IOCONFIG_" + Codes[Code]["Name"])
                    print(f"V({Name(Group)}, {Name(Value)}, {Name(SubValue)})")


def Main():
    sys.stdout.reconfigure(newline="\n")
    if len(sys.argv) > 2 and sys.argv[1] == "--valid":
        PrintValidTriples(Derive(sys.argv[2]))
        return
    if len(sys.argv) < 2:
        sys.exit("usage: extract_ioconfig_relation.py [--valid] <xml> [<list.inc>]")
    Codes = Derive(sys.argv[1])

    if len(sys.argv) > 2:
        with open(sys.argv[2], encoding="utf-8") as File:
            Existing = re.findall(r"^X\((\w+)", File.read(), re.MULTILINE)
        if Existing != [C["Name"] for C in Codes][:len(Existing)]:
            for Index, (Old, New) in enumerate(zip(Existing, (C["Name"] for C in Codes))):
                if Old != New:
                    sys.exit(f"code {Index} is {Old} in the list but {New} in the XML")
            sys.exit("the XML has fewer codes than the list")

    Parents = {C["Name"]: [] for C in Codes}
    for C in Codes:
        for Child in C["Children"]:
            if C["Name"] not in Parents[Child]:
                Parents[Child].append(C["Name"])

    # TRUE and FALSE belong to every boolean I/O capability. The list says so with one
    # marker rather than naming each, and the marker means exactly these codes.
    BoolIoCaps = [C["Name"] for C in Codes if C["BoolIo"] and not C["SubValue"]]

    for C in Codes:
        P = Parents[C["Name"]]
        if P and sorted(P) == sorted(BoolIoCaps):
            P = ["DTL_IOCFG_ANY_BOOLIO"]
        else:
            P = ["DTAPI_IOCONFIG_" + Name for Name in P]
        if len(P) > 2:
            sys.exit(f"{C['Name']} has {len(P)} parents; the list holds two")
        P = P + ["DTL_IOCFG_NONE"] * (2 - len(P))
        Line = f"X({C['Name']}, {Kinds(C)}, {P[0]}, {P[1]})"
        if len(Line) > 90:
            Line = f"X({C['Name']},\n  {Kinds(C)},\n  {P[0]}, {P[1]})"
        print(Line)


if __name__ == "__main__":
    Main()
