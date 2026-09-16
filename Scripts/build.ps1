# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# build.ps1 *#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDtapiLite - Configures, builds, tests and lints the project on Windows
#
# SPDX-License-Identifier: BSD-3-Clause
#
# The PowerShell counterpart of Scripts/build.sh, for use from a plain Windows shell or
# from the Visual Studio terminal. It takes the same options.

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string] $Preset = "windows-debug",

    [Alias("c")] [switch] $Clean,
    [Alias("b")] [switch] $BuildOnly,
    [Alias("t")] [switch] $TestOnly,
    [Alias("l")] [switch] $Lint,
    [switch] $LintOnly,
    # No short alias: PowerShell matches aliases case-insensitively, so -L
    # would collide with -l for Lint.
    [switch] $List
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

function Write-Step([string] $Message)
{
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

# Locates Git's bash rather than whatever "bash" resolves to. On a default Windows
# install that name is the WSL launcher in System32, which reports that no
# distribution is installed and fails.
#
# Paths are assembled with Join-Path segments rather than written out, so that no
# backslash escaping is involved.
function Find-Bash()
{
    $Candidates = @()

    $Git = Get-Command git -ErrorAction SilentlyContinue
    if ($Git)
    {
        # Both <install>/cmd/git.exe and <install>/bin/git.exe sit one level below
        # the install root, next to which bin/bash.exe lives.
        $GitRoot = Split-Path -Parent (Split-Path -Parent $Git.Source)
        $Candidates += (Join-Path $GitRoot 'bin' 'bash.exe')
    }

    foreach ($ProgramDir in @($env:ProgramFiles, ${env:ProgramFiles(x86)}))
    {
        if ($ProgramDir)
        {
            $Candidates += (Join-Path $ProgramDir 'Git' 'bin' 'bash.exe')
        }
    }

    foreach ($Candidate in $Candidates)
    {
        if ($Candidate -and (Test-Path $Candidate))
        {
            return $Candidate
        }
    }

    throw "Could not find Git bash. Install Git for Windows, or run Scripts/build.sh."
}

function Invoke-Checked([string] $What, [scriptblock] $Action)
{
    & $Action
    if ($LASTEXITCODE -ne 0)
    {
        throw "$What failed with exit code $LASTEXITCODE"
    }
}

if ($List)
{
    cmake --list-presets
    exit 0
}

$DoBuild = -not $TestOnly -and -not $LintOnly
$DoTest = -not $BuildOnly -and -not $LintOnly
$DoLint = $Lint -or $LintOnly

if ($DoLint)
{
    Write-Step "Style checks"
    # The checks are one bash script rather than two implementations that can disagree.
    $Bash = Find-Bash
    Invoke-Checked "Style checks" { & $Bash Scripts/check_style.sh }
}

if ($DoBuild)
{
    $BuildDir = Join-Path "Build" $Preset
    if ($Clean -and (Test-Path $BuildDir))
    {
        Write-Step "Removing $BuildDir"
        Remove-Item -Recurse -Force $BuildDir
    }

    Write-Step "Configuring preset '$Preset'"
    Invoke-Checked "Configure" { cmake --preset $Preset }

    Write-Step "Building preset '$Preset'"
    Invoke-Checked "Build" { cmake --build --preset $Preset }
}

if ($DoTest)
{
    Write-Step "Testing preset '$Preset'"
    Invoke-Checked "Test" { ctest --preset $Preset }
}

Write-Step "Done"
