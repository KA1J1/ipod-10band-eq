<#
.SYNOPSIS
Write /osos-cfw.bin (and optionally the CFWEQ settings file) only inside a
stable USB window.

.DESCRIPTION
The iPod's USB link can drop while it enumerates. This script waits until the
volume has stayed mounted for a few seconds, copies the payload to
osos-cfw.bin.new, verifies its SHA256, then swaps it into place, so the previous
file survives a drop that happens before the swap. Only /osos-cfw.bin and, when
-EqFile is given, iPod_Control\Device\CFWEQ are written.

.PARAMETER Drive
Mounted iPod volume, for example I:.

.PARAMETER Image
Payload to write. Defaults to ..\..\build\release\osos-cfw.bin relative to this
script.

.PARAMETER EqFile
Optional CFWEQ settings file to write to iPod_Control\Device\CFWEQ.

.PARAMETER StableSeconds
Seconds the volume must stay mounted before a write starts.

.PARAMETER TimeoutSeconds
Give up after this many seconds without a stable window.

.EXAMPLE
.\write_device_window.ps1 -Drive I: -Image ..\..\build\release\osos-cfw.bin

.EXAMPLE
.\write_device_window.ps1 -Drive I: -EqFile .\CFWEQ.profiles
#>
param(
    [string]$Drive = 'I:',
    [string]$Image = "",
    [string]$EqFile = "",
    [int]$StableSeconds = 12,
    [int]$TimeoutSeconds = 240
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSCommandPath
$deviceRoot = $Drive.TrimEnd('\') + '\'
$device = Join-Path $deviceRoot 'iPod_Control\Device'
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)

if (-not $Image) { $Image = Join-Path $root '..\..\build\release\osos-cfw.bin' }

function Wait-Stable {
    param([int]$Need)
    $run = 0
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $device) {
            $null = Get-ChildItem -LiteralPath $device -ErrorAction SilentlyContinue
            $run++
            if ($run -ge ($Need * 2)) { return $true }
        } else {
            $run = 0
        }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Write-Verified {
    param([string]$Source, [string]$Target, [string]$ExpectedHash)
    if (-not (Wait-Stable -Need $StableSeconds)) { throw "no stable window for $Target" }
    $staged = "$Target.new"
    Copy-Item -LiteralPath $Source -Destination $staged -Force
    $hash = (Get-FileHash -LiteralPath $staged -Algorithm SHA256).Hash.ToLower()
    if ($ExpectedHash -and $hash -ne $ExpectedHash) { throw "staged hash mismatch for $Target" }
    if (-not (Wait-Stable -Need 3)) { throw "dropped before swap of $Target" }
    Remove-Item -LiteralPath $Target -Force -ErrorAction SilentlyContinue
    Move-Item -LiteralPath $staged -Destination $Target -Force
    $final = (Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash.ToLower()
    if ($ExpectedHash -and $final -ne $ExpectedHash) { throw "final hash mismatch for $Target" }
    return $final
}

$imageHash = (Get-FileHash -LiteralPath $Image -Algorithm SHA256).Hash.ToLower()
Write-Output "payload source : $Image ($imageHash)"
$payload = Write-Verified -Source $Image -Target (Join-Path $deviceRoot 'osos-cfw.bin') -ExpectedHash $imageHash
Write-Output "payload written: $payload"
if ($EqFile) {
    $eqHash = (Get-FileHash -LiteralPath $EqFile -Algorithm SHA256).Hash.ToLower()
    Write-Output "eq source      : $EqFile ($eqHash)"
    $eq = Write-Verified -Source $EqFile -Target (Join-Path $device 'CFWEQ') -ExpectedHash $eqHash
    Write-Output "eq written     : $eq"
}
Write-Output 'Eject the iPod safely, then power on without pressing anything.'
