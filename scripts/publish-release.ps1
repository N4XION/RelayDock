# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Publishes a release on GitHub: the notes from docs/release-notes and the files from release\.

.DESCRIPTION
The release workflow runs this after it built and checked the files. The script:

1. reads docs/release-notes/<version>.md,
2. turns its links into full addresses, because a release page is not inside the repository,
3. adds the SHA-256 hash of every file, the commit and the compiler version,
4. creates the release for the tag and uploads the files. A version with a suffix, such as
   1.0.0-rc.1, becomes a pre-release.

With -DryRun it prints the notes and publishes nothing. Use that to read the notes before you
tag.

It needs the GitHub command line tool `gh`, signed in, or a GH_TOKEN in the environment.

.EXAMPLE
.\scripts\publish-release.ps1 -Tag v1.0.0 -Repository owner/RelayDock -DryRun
#>
param(
    [Parameter(Mandatory)][string]$Tag,
    [Parameter(Mandatory)][string]$Repository,
    [string]$BuildDir = 'build_x64',
    [string]$ReleaseDir = '',
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $ReleaseDir) { $ReleaseDir = Join-Path $repo 'release' }
if ($Tag -notmatch '^v\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$') { throw "'$Tag' is not a version tag such as v1.0.0 or v1.0.0-rc.1." }
if ($Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') { throw "'$Repository' is not in the form owner/name." }
$version = $Tag.Substring(1)
$prerelease = $version.Contains('-')

$notesFile = Join-Path $repo "docs\release-notes\$version.md"
if (-not (Test-Path -LiteralPath $notesFile)) { throw "docs/release-notes/$version.md is missing." }
$text = [System.IO.File]::ReadAllText($notesFile, [System.Text.Encoding]::UTF8)

# The release has its own title. Drop the heading that repeats it.
$text = [regex]::Replace($text, '\A\s*# [^\r\n]*\r?\n', '')

# Links in the notes are relative to docs/release-notes, so they work inside the repository.
$base = "https://github.com/$Repository/blob/$Tag"
$text = [regex]::Replace($text, '\]\(\.\./\.\./([^)\s]+)\)', { param($m) "]($base/$($m.Groups[1].Value))" })
$text = [regex]::Replace($text, '\]\(\.\./([^)\s]+)\)', { param($m) "]($base/docs/$($m.Groups[1].Value))" })
$text = [regex]::Replace($text, '\]\((?!https?://|#)([^)\s]+)\)', { param($m) "]($base/docs/release-notes/$($m.Groups[1].Value))" })

# ---- Files, hashes, commit, compiler --------------------------------------------------------------
$files = @(Get-ChildItem -LiteralPath $ReleaseDir -File | Sort-Object Name)
if ($files.Count -eq 0) { throw "No release files in $ReleaseDir. Run scripts\package.ps1 first." }
$sumsFile = Join-Path $ReleaseDir 'SHA256SUMS.txt'
if (-not (Test-Path -LiteralPath $sumsFile)) { throw 'SHA256SUMS.txt is missing from the release files.' }

$commit = (& git -C $repo rev-parse HEAD 2>$null)
$compiler = ''
$compilerFile = Get-ChildItem -Path (Join-Path $repo "$BuildDir\CMakeFiles") -Filter 'CMakeCXXCompiler.cmake' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if ($compilerFile) {
    $match = [regex]::Match((Get-Content -LiteralPath $compilerFile.FullName -Raw), 'CMAKE_CXX_COMPILER_VERSION "([^"]+)"')
    if ($match.Success) { $compiler = $match.Groups[1].Value }
}

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add($text.TrimEnd())
$lines.Add('')
$lines.Add('## Checksums')
$lines.Add('')
$lines.Add('SHA-256 of every file of this release. [installation.md](' + "$base/docs/installation.md" + ') shows how to check your download.')
$lines.Add('')
$lines.Add('```')
foreach ($line in Get-Content -LiteralPath $sumsFile) { if ($line.Trim()) { $lines.Add($line) } }
$lines.Add('```')
$lines.Add('')
$built = "GitHub Actions built these files from commit ``$commit``"
if ($compiler) { $built += " with the Microsoft C++ compiler $compiler" }
$lines.Add("$built. [building-from-source.md]($base/docs/building-from-source.md) shows how to build the same version and compare.")
$body = ($lines -join "`n") + "`n"

if ($DryRun) {
    Write-Host "Release:     RelayDock $version$(if ($prerelease) { ' (pre-release)' })"
    Write-Host "Repository:  $Repository"
    Write-Host "Files:       $(($files | ForEach-Object { $_.Name }) -join ', ')"
    Write-Host ''
    Write-Host $body
    exit 0
}

$bodyFile = Join-Path ([System.IO.Path]::GetTempPath()) "relaydock-release-notes-$version.md"
[System.IO.File]::WriteAllText($bodyFile, $body, (New-Object System.Text.UTF8Encoding($false)))

# gh writes "release not found" to its error stream. That is an answer here, not a failure.
$ErrorActionPreference = 'Continue'
& gh release view $Tag --repo $Repository 2>$null | Out-Null
$exists = $LASTEXITCODE -eq 0
$ErrorActionPreference = 'Stop'

$paths = @($files | ForEach-Object { $_.FullName })
if ($exists) {
    # A second run for the same tag replaces the files and the notes.
    & gh release upload $Tag @paths --clobber --repo $Repository
    if ($LASTEXITCODE -ne 0) { throw "gh release upload failed with exit code $LASTEXITCODE." }
    $arguments = @('release', 'edit', $Tag, '--repo', $Repository, '--title', "RelayDock $version", '--notes-file', $bodyFile, '--draft=false')
    if ($prerelease) { $arguments += '--prerelease' }
    & gh @arguments
    if ($LASTEXITCODE -ne 0) { throw "gh release edit failed with exit code $LASTEXITCODE." }
} else {
    $arguments = @('release', 'create', $Tag) + $paths + @('--repo', $Repository, '--verify-tag', '--title', "RelayDock $version", '--notes-file', $bodyFile)
    if ($prerelease) { $arguments += '--prerelease' }
    & gh @arguments
    if ($LASTEXITCODE -ne 0) { throw "gh release create failed with exit code $LASTEXITCODE." }
}
Write-Host "Published RelayDock $version at https://github.com/$Repository/releases/tag/$Tag"
