# #*#*#*#*#*#*#*#*#*#*#*#*#*#* unlock_surround.ps1 #*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
#
# CDTAPI - Makes the working tree writable again after Surround SCM has been over it
#
# SPDX-License-Identifier: BSD-3-Clause
#
# The PowerShell counterpart of Scripts/unlock_surround.sh. Surround SCM hands out
# read-only files unless it is told otherwise: "sscm get -e" and "sscm ci -w" keep them
# writable, and without those git cannot replace a file, so a pull or a checkout fails
# halfway. This clears the read-only attribute of the files git tracks.
#
# It does nothing unless the tree is a Surround working directory, which is what the
# .MySCMServerInfo in its root says, so a plain clone, a build server or an outside
# contributor never notices it.

[CmdletBinding()]
param([switch] $Quiet)

$ErrorActionPreference = "Stop"

$Root = & git rev-parse --show-toplevel 2>$null
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($Root)) { exit 0 }
Set-Location $Root
if (-not (Test-Path ".MySCMServerInfo")) { exit 0 }

$Unlocked = 0
foreach ($Path in (& git ls-files))
{
    $File = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if ($null -ne $File -and $File.IsReadOnly)
    {
        $File.IsReadOnly = $false
        $Unlocked++
    }
}

if ($Unlocked -ne 0 -and -not $Quiet)
{
    "Surround left $Unlocked file(s) read-only; they are writable again."
    "Keep them that way with 'sscm ci -w' and 'sscm get -e'."
}
exit 0
