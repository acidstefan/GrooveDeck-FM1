# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# GrooveDeck FM-1 auf FM1 Open: legt die GrooveDeck-App in eine FM1-Open-Arbeitskopie.
#
#   powershell -ExecutionPolicy Bypass -File fm1open\install.ps1 -Fm1Open C:\fm1\fm1_open
#
# Danach in der FM1-Open-Kopie:  .\sdk\Build.ps1 -FirmwareOnly -Toolchain C:\JL\pi32
# Ergebnis: build\dx7\generated\FM1_OFFICIAL_OPEN.fwsc
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Fm1Open,
    [int]$DelayLen = 24576,          # Delay-Speicher je Kanal (16 Bit): 24576 = 0,56 s, 96 KB
    [int]$Voices = 4,                # Stimmen je Bass/Lead
    [switch]$StereoReverb,           # Hall stereo (mehr RAM und Rechenzeit)
    [switch]$GoodFullRate            # Soundgoodizer-Analyse mit voller Rate (mehr Rechenzeit)
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$target = [IO.Path]::GetFullPath($Fm1Open)
$app = Join-Path $target 'app'
if (!(Test-Path (Join-Path $target 'sdk\Build.ps1')) -or !(Test-Path (Join-Path $app 'instrument.h'))) {
    throw "Keine FM1-Open-Arbeitskopie: $target (erwartet sdk\Build.ps1 und app\instrument.h)"
}

# 1. Oberflaeche, Projekte, Samples, Transfer, MIDI-Routing und DX7-Bibliothek von FM1 Open entfernen
foreach ($name in @('screen.c','project.c','presets.c','samples.c','transfer.c','midi_router.c',
                    'fm_library.c','fm_factory.c','fm_factory.inc')) {
    $file = Join-Path $app $name
    if (Test-Path $file) { Remove-Item -LiteralPath $file }
}

# 2. GrooveDeck-App-Dateien (screen.c, midi_router.c, project.c, audio_output.c, board\...) darueberlegen
Copy-Item -Path (Join-Path $PSScriptRoot 'overlay\app\*') -Destination $app -Recurse -Force

# 3. GrooveDeck-Kern nach app\gd, Einstellungen fuer FM1 Open vorne in gd_config.h
$gd = Join-Path $app 'gd'
if (Test-Path $gd) { Remove-Item -LiteralPath $gd -Recurse -Force }
New-Item -ItemType Directory -Path $gd | Out-Null
Get-ChildItem -Path (Join-Path $repo 'core') -File | Where-Object { $_.Extension -in @('.c','.h') } |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $gd }
$settings = "/* Von fm1open\install.ps1 gesetzt (FM1 Open) */`n" +
    "#define GD_TARGET_FM1 1`n#define GD_FM1OPEN 1`n#define GD_PRESETS_MINI 1`n" +
    "#define GD_DELAY_LEN $DelayLen`n#define GD_MAX_VOICES $Voices`n" +
    "#define GD_REVERB_STEREO $([int][bool]$StereoReverb)`n#define GD_GOOD_FULLRATE $([int][bool]$GoodFullRate)`n`n"
$config = Join-Path $gd 'gd_config.h'
[IO.File]::WriteAllText($config, $settings + [IO.File]::ReadAllText($config), (New-Object Text.UTF8Encoding $false))

Write-Host "GrooveDeck liegt jetzt in $app"
Write-Host "Bauen:  cd `"$target`"; .\sdk\Build.ps1 -FirmwareOnly -Toolchain C:\JL\pi32"
Write-Host "Ergebnis: $target\build\dx7\generated\FM1_OFFICIAL_OPEN.fwsc"
