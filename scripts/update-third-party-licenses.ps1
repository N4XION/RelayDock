# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Rebuilds THIRD_PARTY_LICENSES.md from the licence files stored next to the third-party code.

.DESCRIPTION
The licence texts live with the code they belong to (third_party\*, resources\icons). This
script collects them into one file for the repository root and for the release. Run it after
you add, update or remove third-party code.

.PARAMETER Check
Changes nothing. Exits with code 1 when THIRD_PARTY_LICENSES.md is out of date.
#>
param([switch]$Check)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$target = Join-Path $root 'THIRD_PARTY_LICENSES.md'

function Read-Text([string]$Relative) {
    return ([System.IO.File]::ReadAllText((Join-Path $root $Relative)) -replace "`r`n", "`n").Trim()
}

$sections = New-Object System.Collections.Generic.List[string]
$sections.Add(@'
# Third-party licences

RelayDock is free software under the GNU General Public License, version 2 or later. See `LICENSE`.

This file lists the software RelayDock contains, builds on or is built with, and the licence of each.

## Included in the RelayDock plugin
'@.Trim())

$sections.Add(@"
### Lucide icons

Version 1.51.0. https://lucide.dev

Used for the icons in RelayDock's interface. The files are in ``resources/icons``. Six of them (``ui-chevron-down``, ``ui-chevron-up`` and ``ui-check``, each in a light and a dark variant) are Lucide icons with a fixed stroke colour.

``````
$(Read-Text 'resources/icons/LICENSE.txt')
``````
"@)

$sections.Add(@"
### JSON for Modern C++ (nlohmann/json)

Version 3.12.0. https://github.com/nlohmann/json

Used to read and write RelayDock's settings. The files are in ``third_party/nlohmann``.

``````
$(Read-Text 'third_party/nlohmann/LICENSE.MIT')
``````
"@)

$sections.Add(@'
### OBS plugin template build files

https://github.com/obsproject/obs-plugintemplate

RelayDock's CMake files under `cmake/` are adapted from the OBS Project's plugin template. Copyright (C) the OBS Project and its contributors. GNU General Public License, version 2 or later, the same licence as RelayDock. See `LICENSE`.

## Used by RelayDock, not included in it

### OBS Studio

https://obsproject.com

RelayDock is a plugin for OBS Studio and links to its `libobs` and `obs-frontend-api` libraries. Copyright (C) the OBS Project and its contributors. GNU General Public License, version 2 or later. OBS Studio is installed separately. No part of it is in the RelayDock download.

### Qt 6

https://www.qt.io

RelayDock uses the Qt libraries that OBS Studio ships and loads. Copyright (C) The Qt Company Ltd and other contributors. Available under the GNU Lesser General Public License version 3 and the GNU General Public License. No Qt library is in the RelayDock download.

## Used to build and test RelayDock, not included in the plugin
'@.Trim())

$sections.Add(@"
### doctest

Version 2.5.3. https://github.com/doctest/doctest

Used for RelayDock's automated tests. The files are in ``third_party/doctest``.

``````
$(Read-Text 'third_party/doctest/LICENSE.txt')
``````
"@)

$sections.Add(@'
### Inno Setup

https://jrsoftware.org/isinfo.php

Used to build the Windows installer. The installer contains Inno Setup's setup program. Copyright (C) Jordan Russell and Martijn Laan. Inno Setup License: https://jrsoftware.org/files/is/license.txt

## Trademarks

Twitch, TikTok, YouTube, Facebook and OBS Studio are trademarks of their owners. RelayDock uses the names to say which service a destination connects to. RelayDock is not affiliated with their owners and ships none of their logos.
'@.Trim())

$content = ($sections -join "`n`n") + "`n"
$current = if (Test-Path $target) { [System.IO.File]::ReadAllText($target) -replace "`r`n", "`n" } else { '' }
if ($Check) {
    if ($current -cne $content) {
        Write-Host 'THIRD_PARTY_LICENSES.md is out of date. Run scripts/update-third-party-licenses.ps1.'
        exit 1
    }
    Write-Host 'THIRD_PARTY_LICENSES.md is up to date.'
    exit 0
}
[System.IO.File]::WriteAllText($target, $content, (New-Object System.Text.UTF8Encoding($false)))
Write-Host 'Wrote THIRD_PARTY_LICENSES.md.'
