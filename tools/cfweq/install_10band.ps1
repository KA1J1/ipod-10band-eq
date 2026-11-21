<#
.SYNOPSIS
Install or roll back the ten-band Custom EQ payload on a mounted iPod classic.

.DESCRIPTION
Only the payload file /osos-cfw.bin on the FAT32 partition is replaced. The NOR,
the companion (/cfw-loader.bin), themes and the music library are not touched, so
no DFU, installer, NOR dump or Apple firmware is needed. The Rockbox bootloader
checks only that /osos-cfw.bin is between 1 byte and 12 MiB, and the companion
reads the size fields this build already patches.

.PARAMETER Drive
Mounted iPod volume, for example I:.

.PARAMETER Image
Payload to install. Defaults to ..\..\build\release\osos-cfw.bin relative to this script.

.PARAMETER Rollback
Restore the most recent backup instead of installing.

.EXAMPLE
.\install_10band.ps1 -Drive I:
.EXAMPLE
.\install_10band.ps1 -Drive I: -Rollback
#>
[CmdletBinding(DefaultParameterSetName = 'Install')]
param(
    [Parameter(Mandatory = $true)]
    [string]$Drive,
    [Parameter(ParameterSetName = 'Install')]
    [string]$Image,
    [Parameter(Mandatory = $true, ParameterSetName = 'Rollback')]
    [switch]$Rollback
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSCommandPath
$backupDirectory = Join-Path $root 'backup'
$volume = $Drive.TrimEnd('\') + '\'
$target = $volume + 'osos-cfw.bin'

function Get-Sha256([string]$Path) {
    (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

# Free space is only known for a real drive letter; a mounted folder is fine too.
function Get-FreeSpace([string]$Path) {
    $name = $Path.TrimEnd('\').TrimEnd(':')
    if ($name -match '^[A-Za-z]$') {
        try { return (Get-PSDrive -Name $name).Free } catch { return $null }
    }
    return $null
}

function Assert-Device {
    if (-not (Test-Path -LiteralPath $volume)) {
        throw "Volume $volume is not mounted."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $volume 'iPod_Control'))) {
        throw "$volume does not look like an iPod (no iPod_Control directory)."
    }
}

# Refuse to modify anything unless the bootloader would accept the file.
function Assert-Payload([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "$Label missing: $Path" }
    $size = (Get-Item -LiteralPath $Path).Length
    if ($size -le 0 -or $size -gt 12MB) {
        throw "$Label has $size bytes; the bootloader accepts 1..12582912."
    }
    return $size
}

Assert-Device
New-Item -ItemType Directory -Force -Path $backupDirectory | Out-Null

if ($Rollback) {
    $latest = Get-ChildItem -LiteralPath $backupDirectory -Filter 'osos-cfw-*.bin' |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $latest) { throw "No backup in $backupDirectory." }
    $size = Assert-Payload $latest.FullName 'Backup'
    $expected = Get-Sha256 $latest.FullName
    Write-Host "Restoring $($latest.Name) ($size bytes, $expected)"
    Copy-Item -LiteralPath $latest.FullName -Destination $target -Force
    if ((Get-Sha256 $target) -ne $expected) { throw 'Readback hash differs; copy again.' }
    Write-Host 'Rollback written. Eject safely, then reboot the iPod.'
    exit 0
}

if (-not $Image) { $Image = Join-Path $root '..\..\build\release\osos-cfw.bin' }
$size = Assert-Payload $Image 'Payload'
$expected = Get-Sha256 $Image
Write-Host ("Payload: {0} ({1:N0} bytes, sha256 {2})" -f $Image, $size, $expected)

# Keep the payload that is on the device, even if it is the stock one.
if (Test-Path -LiteralPath $target) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $copy = Join-Path $backupDirectory "osos-cfw-$stamp.bin"
    Copy-Item -LiteralPath $target -Destination $copy -Force
    if ((Get-Sha256 $copy) -ne (Get-Sha256 $target)) {
        throw 'Backup readback differs; nothing was written.'
    }
    Write-Host ("Backed up the current payload to {0} ({1:N0} bytes)" -f $copy,
        (Get-Item -LiteralPath $copy).Length)
} else {
    Write-Warning 'No /osos-cfw.bin on the device; installing will add one.'
}

$free = Get-FreeSpace $volume
if ($null -ne $free -and $free -lt $size * 2) {
    throw "Not enough free space on $volume ($free bytes)."
}

Copy-Item -LiteralPath $Image -Destination $target -Force
if ((Get-Sha256 $target) -ne $expected) {
    throw 'Readback hash differs from the image; copy the file again.'
}
$written = (Get-Item -LiteralPath $target).Length
Write-Host ("Wrote {0} ({1:N0} bytes) and verified the hash" -f $target, $written)

Write-Host @'
Next steps:
  1. Eject the iPod with the Safely Remove Hardware dialog. Do not pull the cable.
  2. Unplug, then power on without pressing anything: RepriseOS boots.
  3. Settings, Custom EQ, Graphic EQ shows ten bands.
  4. Run cfweq_tool.py verify <drive>:\iPod_Control\Device\CFWEQ after saving.

Rollback:
  .\install_10band.ps1 -Drive I: -Rollback
'@
