# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Builds docs/versions.md, the page that lists every version with its files and its changes.

.DESCRIPTION
CHANGELOG.md is the one place where changes are written down. Each version has a heading with
its release date:

    ## 1.0.1 (2026-11-02)

A version that is not out yet has no date:

    ## 1.1.0

This script turns that into docs/versions.md: a table with one row per version (date, kind,
the names of the installer and the ZIP with their download links, a link to the release page)
and below it what changed in each version. The download links point at the releases of the
repository named in buildspec.json.

With -Check the script writes nothing and fails when docs/versions.md is out of date. CI runs
that on every push.

.EXAMPLE
.\scripts\update-versions.ps1

.EXAMPLE
.\scripts\update-versions.ps1 -Check
#>
param([switch]$Check)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$utf8 = New-Object System.Text.UTF8Encoding($false)
$changelog = [System.IO.File]::ReadAllText((Join-Path $repo 'CHANGELOG.md'), [System.Text.Encoding]::UTF8) -replace "`r`n", "`n"
$spec = Get-Content (Join-Path $repo 'buildspec.json') -Raw | ConvertFrom-Json
$repository = [string]$spec.repository
$target = Join-Path $repo 'docs\versions.md'

# ---- Read the versions out of the changelog ------------------------------------------------------
$versions = New-Object System.Collections.Generic.List[object]
$heading = [regex]'(?m)^## (\d+\.\d+\.\d+(?:-[0-9A-Za-z.]+)?)(?: \((\d{4}-\d{2}-\d{2})\))?[ \t]*$'
$found = $heading.Matches($changelog)
if ($found.Count -eq 0) { throw 'CHANGELOG.md has no version heading such as "## 1.0.0 (2026-10-04)".' }
for ($i = 0; $i -lt $found.Count; $i++) {
    $start = $found[$i].Index + $found[$i].Length
    $end = if ($i + 1 -lt $found.Count) { $found[$i + 1].Index } else { $changelog.Length }
    $body = $changelog.Substring($start, $end - $start).Trim()
    $version = $found[$i].Groups[1].Value
    $kind = if ($version -match '-rc\.') { 'Release candidate' } elseif ($version.Contains('-')) { 'Pre-release' } else { 'Release' }
    $versions.Add([pscustomobject]@{ Version = $version; Date = $found[$i].Groups[2].Value; Kind = $kind; Body = $body })
}

# A heading that looks like a version but is not written the way this script reads it would
# silently drop that version from the page.
$loose = [regex]::Matches($changelog, '(?m)^## \d[^\n]*$')
if ($loose.Count -ne $found.Count) {
    throw 'A version heading in CHANGELOG.md is not in the form "## 1.0.0 (2026-10-04)" or "## 1.0.0".'
}

function Get-Anchor([string]$Version) { return 'version-' + ($Version.ToLowerInvariant() -replace '[^a-z0-9 -]', '') }
function Get-FileName([string]$Version, [string]$Suffix) { return "RelayDock-$Version-windows-x64$Suffix" }
function Get-Download([string]$Version, [string]$File) { return "https://github.com/$repository/releases/download/v$Version/$File" }

# ---- Write the page ------------------------------------------------------------------------------
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('# Versions')
$lines.Add('')
$lines.Add('Every RelayDock version, its files and what changed in it. The newest is at the top.')
$lines.Add('')
$lines.Add('`scripts/update-versions.ps1` makes this page from `CHANGELOG.md`. Change the text there, not here.')
$lines.Add('')
$lines.Add('| Version | Released | Kind | Installer | ZIP for a portable OBS | More |')
$lines.Add('| --- | --- | --- | --- | --- | --- |')
foreach ($v in $versions) {
    $setup = Get-FileName $v.Version '-Setup.exe'
    $zip = Get-FileName $v.Version '.zip'
    $changes = "[What changed](#$(Get-Anchor $v.Version))"
    if ($v.Date -and $repository) {
        $lines.Add("| $($v.Version) | $($v.Date) | $($v.Kind) | [$setup]($(Get-Download $v.Version $setup)) | [$zip]($(Get-Download $v.Version $zip)) | $changes, [release page](https://github.com/$repository/releases/tag/v$($v.Version)) |")
    } else {
        $released = if ($v.Date) { $v.Date } else { 'Not released yet' }
        $lines.Add("| $($v.Version) | $released | $($v.Kind) | ``$setup`` | ``$zip`` | $changes |")
    }
}
$lines.Add('')
$lines.Add('Every release also carries `SHA256SUMS.txt`, with the SHA-256 hash of each file, and `THIRD_PARTY_LICENSES.txt`. [installation.md](installation.md) shows how to check a download against the hashes.')
$lines.Add('')
$lines.Add("The newest version needs Windows 11 (64-bit) and OBS Studio $($spec.obs.minimumVersion) or newer.")
$lines.Add('')
$lines.Add('## Update from an older version')
$lines.Add('')
$lines.Add('1. Download the installer of the newer version from the table.')
$lines.Add('2. Close OBS Studio.')
$lines.Add('3. Open the downloaded file and follow its steps.')
$lines.Add('')
$lines.Add('The installer replaces the old version. Your destinations, settings and stream keys stay. For a portable OBS, copy the files from the ZIP over the old ones, as [manual-installation.md](manual-installation.md) describes.')
$lines.Add('')
$lines.Add('RelayDock looks for a newer version each time OBS starts and tells you when there is one. Settings, Updates has a Check for updates button and the switch for the check at start-up. RelayDock never downloads or installs anything by itself.')

foreach ($v in $versions) {
    $lines.Add('')
    $lines.Add("## Version $($v.Version)")
    $lines.Add('')
    $released = if ($v.Date) { "Released $($v.Date)." } else { 'Not released yet.' }
    $lines.Add("$released $($v.Kind).")
    $lines.Add('')
    $lines.Add('Files:')
    $lines.Add('')
    foreach ($file in (Get-FileName $v.Version '-Setup.exe'), (Get-FileName $v.Version '.zip'), 'SHA256SUMS.txt', 'THIRD_PARTY_LICENSES.txt') {
        if ($v.Date -and $repository) { $lines.Add("- [$file]($(Get-Download $v.Version $file))") } else { $lines.Add("- ``$file``") }
    }
    $lines.Add('')
    $lines.Add($v.Body)
}

$text = ($lines -join "`n") + "`n"

if ($Check) {
    $current = if (Test-Path -LiteralPath $target) { [System.IO.File]::ReadAllText($target, [System.Text.Encoding]::UTF8) -replace "`r`n", "`n" } else { '' }
    if ($current -ne $text) {
        Write-Host 'docs/versions.md does not match CHANGELOG.md. Run scripts\update-versions.ps1 and commit the result.'
        exit 1
    }
    Write-Host "docs/versions.md matches CHANGELOG.md. $($versions.Count) version(s)."
    exit 0
}

[System.IO.File]::WriteAllText($target, $text, $utf8)
Write-Host "Wrote $($versions.Count) version(s) to docs/versions.md."
