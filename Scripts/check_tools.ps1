# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* check_tools.ps1 #*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDTAPI - What this project needs to build and test, and whether it is there
#
# SPDX-License-Identifier: BSD-3-Clause
#
# The PowerShell counterpart of Scripts/check_tools.sh, for use from a plain Windows shell
# or from the Visual Studio terminal. It prints one line per tool and exits 1 when
# something is missing or of the wrong version.

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

# The versions the project is held to. clang-format is exact, because its major version
# decides how code is formatted: another one reformats files that are already right.
$ClangFormatMajor = 18
$CMakeMinimum = [version] "3.21"
$PythonMinimum = [version] "3.8"
$VisualStudioMinimum = [version] "17.0"

$script:Failures = 0

function Report($Tool, $What)
{
    "{0,-14} {1}" -f $Tool, $What
}

function Fail($Tool, $What, $How)
{
    $script:Failures++
    "{0,-14} {1}" -f $Tool, $What
    "{0,-14}   {1}" -f "", $How
}

# The version of a program, from its own --version line; $null when it is not there.
function VersionOf($Command, $Arguments, $Pattern)
{
    $Found = Get-Command $Command -ErrorAction SilentlyContinue
    if ($null -eq $Found) { return $null }
    $Output = & $Found.Source @Arguments 2>&1 | Out-String
    if ($Output -match $Pattern) { return $Matches[1] }
    return "unknown"
}

"Tools for CDTAPI, on Windows"
""

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- clang-format -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

$ClangFormat = if ($env:CLANG_FORMAT) { $env:CLANG_FORMAT } else { "clang-format" }
$Version = VersionOf $ClangFormat @("--version") "version ([0-9][^\s]*)"
if ($null -eq $Version)
{
    Fail "clang-format" "not found" "winget install LLVM.LLVM, or set CLANG_FORMAT"
}
elseif ([int] ($Version -split "\.")[0] -ne $ClangFormatMajor)
{
    Fail "clang-format" "$Version, and $ClangFormatMajor is what the project uses" `
         "install LLVM $ClangFormatMajor, or point CLANG_FORMAT at its clang-format"
}
else
{
    Report "clang-format" $Version
}

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CMake -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

$Version = VersionOf "cmake" @("--version") "version ([0-9][^\s]*)"
if ($null -eq $Version)
{
    Fail "cmake" "not found" "winget install Kitware.CMake"
}
elseif ([version] (($Version -split "-")[0]) -lt $CMakeMinimum)
{
    Fail "cmake" "$Version, and $CMakeMinimum is the minimum" "winget install Kitware.CMake"
}
else
{
    Report "cmake" $Version
}

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Python +.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

$Version = VersionOf "python" @("--version") "Python ([0-9][^\s]*)"
if ($null -eq $Version)
{
    Fail "python" "not found" "winget install Python.Python.3"
}
elseif ([version] (($Version -split "[a-z]")[0]) -lt $PythonMinimum)
{
    Fail "python" "$Version, and $PythonMinimum is the minimum" `
         "winget install Python.Python.3"
}
else
{
    Report "python" $Version
}

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Visual Studio -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
#
# What the Windows presets build with; CMake picks the newest that is installed. vswhere
# comes with Visual Studio and is where to ask.
#
$VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $VsWhere)
{
    $Version = & $VsWhere -latest -products * -property catalog_productDisplayVersion
    if ([string]::IsNullOrWhiteSpace($Version))
    {
        Fail "visual studio" "none installed" `
             "install Visual Studio 2022 or newer with the C and C++ workload"
    }
    elseif ([version] (($Version -split "-")[0]) -lt $VisualStudioMinimum)
    {
        Fail "visual studio" "$Version, and 2022 (17.0) is the minimum" `
             "install Visual Studio 2022 or newer with the C and C++ workload"
    }
    else
    {
        Report "visual studio" $Version
    }
}
else
{
    Fail "visual studio" "not found" `
         "install Visual Studio 2022 or newer with the C and C++ workload"
}

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Git -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

$Version = VersionOf "git" @("--version") "git version ([0-9][^\s]*)"
if ($null -eq $Version)
{
    Fail "git" "not found" "winget install Git.Git"
}
else
{
    Report "git" $Version
}

""
if ($script:Failures -eq 0)
{
    "Everything needed is there."
    exit 0
}
"Missing or wrong: $script:Failures."
exit 1
