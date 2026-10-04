# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Builds data/locale/en-US.ini from the interface strings in the source code.

.DESCRIPTION
Every interface string in RelayDock is written in the code as a key and its English text:

    loc("Card.Status.Live", "LIVE")
    uiTextF("Card.Stop.Tip", "Stop {0}", name)

This script collects those pairs and writes them to data/locale/en-US.ini, the file OBS reads
and translators copy. The code stays the single source of the English text.

It stops with an error when one key is used with two different texts.

.PARAMETER Check
Changes nothing. Exits with code 1 when en-US.ini does not match the source code. Used by CI.

.EXAMPLE
.\scripts\update-locale.ps1

.EXAMPLE
.\scripts\update-locale.ps1 -Check
#>
param([switch]$Check)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sourceDir = Join-Path $root 'src'
$localeFile = Join-Path $root 'data\locale\en-US.ini'

# loc("Key", "Text"   locf("Key", "Text"   uiText("Key", "Text"   uiTextF("Key", "Text"
$pattern = [regex]'\b(?:loc|locf|uiText|uiTextF)\(\s*"([A-Za-z0-9_.]+)"\s*,\s*"((?:[^"\\]|\\.)*)"'

# locn(count, "Key.One", "Text", "Key.Many", "Texts"   and uiTextN(...)
$pluralPattern = [regex]'\b(?:locn|uiTextN)\(\s*[^,]+,\s*"([A-Za-z0-9_.]+)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"([A-Za-z0-9_.]+)"\s*,\s*"((?:[^"\\]|\\.)*)"'

$strings = @{}
$conflicts = New-Object System.Collections.Generic.List[string]

function Add-String([string]$Key, [string]$Value) {
    if ($script:strings.ContainsKey($Key)) {
        if ($script:strings[$Key] -cne $Value) { $script:conflicts.Add("$Key`n    $($script:strings[$Key])`n    $Value") }
    } else {
        $script:strings[$Key] = $Value
    }
}

Get-ChildItem -Path $sourceDir -Recurse -Include *.cpp, *.h | Sort-Object FullName | ForEach-Object {
    $text = [System.IO.File]::ReadAllText($_.FullName)
    foreach ($match in $pattern.Matches($text)) { Add-String $match.Groups[1].Value $match.Groups[2].Value }
    foreach ($match in $pluralPattern.Matches($text)) {
        Add-String $match.Groups[1].Value $match.Groups[2].Value
        Add-String $match.Groups[3].Value $match.Groups[4].Value
    }
}

if ($conflicts.Count -gt 0) {
    Write-Host 'The same key is used with different English text:'
    $conflicts | ForEach-Object { Write-Host "  $_" }
    exit 1
}

# Strings that are not written with loc() in the code.
$strings['Plugin.Description'] = 'Stream to several platforms from one OBS session.'

# The C++ literal and the OBS locale format escape quotes and line breaks the same way, so the
# text is written as it stands in the code.
$lines = New-Object System.Collections.Generic.List[string]
foreach ($key in ($strings.Keys | Sort-Object { $_ } -CaseSensitive)) {
    $lines.Add("$key=`"$($strings[$key])`"")
}
$content = ($lines -join "`n") + "`n"

$current = if (Test-Path $localeFile) { [System.IO.File]::ReadAllText($localeFile) } else { '' }
if ($Check) {
    if ($current -cne $content) {
        Write-Host 'data/locale/en-US.ini is out of date. Run scripts/update-locale.ps1.'
        exit 1
    }
    Write-Host "en-US.ini matches the source code. $($strings.Count) strings."
    exit 0
}

if ($current -cne $content) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $localeFile) | Out-Null
    [System.IO.File]::WriteAllText($localeFile, $content, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "Wrote $($strings.Count) strings to data/locale/en-US.ini."
} else {
    Write-Host "en-US.ini is up to date. $($strings.Count) strings."
}
