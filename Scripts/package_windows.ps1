# #*#*#*#*#*#*#*#*#*#*#*#*# package_windows.ps1 *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
#
# CDTAPI - Builds and packs what the SDK ships on Windows
#
# SPDX-License-Identifier: BSD-3-Clause
#
# The SDK ships one set of CDTAPI's files for every Visual C++ version it serves: the four
# static archives of the two C runtimes and two configurations, cdtapi64mt, cdtapi64mtd,
# cdtapi64md and cdtapi64mdd, and the DLL cdtapi64 with its import library and symbols,
# built against the dynamic runtime. This builds the three trees they come from with
# Visual Studio 2022's toolset, v143, so that a Visual C++ 17 linker links the archives as
# well as an 18 one does, installs them in the SDK's layout, and packs them into
# Build\sdk-windows\cdtapi-X.Y.Z-sdk-windows-x64.zip with its SHA-256 beside it, which the
# release attaches.
#
# Needs CMake and Visual Studio 2022 or newer with the v143 toolset; PowerShell 7.

[CmdletBinding()]
param(
    [string] $Generator = "Visual Studio 17 2022",
    [string] $Toolset = "v143"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

function Write-Step([string] $Message)
{
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

# Runs a native program and stops when it fails, which $ErrorActionPreference does not
# do for a program's exit code.
function Invoke-Checked([string] $Program, [string[]] $Arguments)
{
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0)
    {
        throw "$Program $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- The version -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

$Match = Select-String -Path (Join-Path $RepoRoot 'CMakeLists.txt') `
    -Pattern '^\s*VERSION\s+([0-9.]+)\s*$' | Select-Object -First 1
if (-not $Match)
{
    throw "CMakeLists.txt names no version"
}
$Version = $Match.Matches[0].Groups[1].Value
$Name = "cdtapi-$Version-sdk-windows-x64"

$Out = Join-Path $RepoRoot 'Build' 'sdk-windows'
$Stage = Join-Path $Out 'stage' $Name
if (Test-Path $Out)
{
    Remove-Item -Recurse -Force $Out
}

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Building -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

$Common = @('-G', $Generator, '-T', $Toolset, '-A', 'x64',
    '-DCDTAPI_SDK_LAYOUT:BOOL=ON',
    '-DCDTAPI_BUILD_TESTS:BOOL=OFF',
    '-DCDTAPI_BUILD_EXAMPLES:BOOL=OFF')

# Each tree with the runtime it calls and the configurations the SDK takes from it.
$StaticRuntime = 'MultiThreaded$<$<CONFIG:Debug>:Debug>'
$Trees = @(
    @{ Name = 'x64-MT'; Configs = @('Release', 'Debug');
       Options = @("-DCMAKE_MSVC_RUNTIME_LIBRARY=$StaticRuntime") },
    @{ Name = 'x64-MD'; Configs = @('Release', 'Debug'); Options = @() },
    @{ Name = 'x64-DLL'; Configs = @('RelWithDebInfo');
       Options = @('-DCDTAPI_BUILD_SHARED:BOOL=ON') }
)

foreach ($Tree in $Trees)
{
    $Dir = Join-Path $Out $Tree.Name
    Write-Step "Configuring $($Tree.Name) with $Toolset"
    Invoke-Checked cmake ($Common + $Tree.Options + @('-S', $RepoRoot, '-B', $Dir))
    foreach ($Config in $Tree.Configs)
    {
        Write-Step "Building $($Tree.Name) $Config"
        Invoke-Checked cmake @('--build', $Dir, '--config', $Config)
        Invoke-Checked cmake @('--install', $Dir, '--config', $Config, '--prefix', $Stage)
    }
}

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Packing -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

Write-Step "Packing $Name.zip"
$Zip = Join-Path $Out "$Name.zip"
Compress-Archive -Path $Stage -DestinationPath $Zip
$Hash = (Get-FileHash -Algorithm SHA256 $Zip).Hash.ToLowerInvariant()
# The format sha256sum writes and checks: the hash, two spaces and the file's name, and a
# line feed alone, which a check on Linux needs.
[System.IO.File]::WriteAllText("$Zip.sha256", "$Hash  $Name.zip`n")

Get-ChildItem -Recurse -File $Stage | ForEach-Object {
    $_.FullName.Substring((Split-Path -Parent $Stage).Length + 1)
}
Get-Content "$Zip.sha256"

Write-Step "Done"
