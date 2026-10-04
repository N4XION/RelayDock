# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Checks the documentation: every link and picture it refers to exists, and the text follows the
project's writing rules. CI runs it on every push.

.DESCRIPTION
For every Markdown file in the repository (third-party files excluded):
  - a relative link or image must point at a file or folder that exists,
  - the text outside code must contain no em dash, no semicolon, no asterisk and no emoji,
  - no "TODO", "TBD", "lorem ipsum" or "coming soon" may be left in.

Links to the repository's own GitHub pages (../../issues, ../../releases) are not checked.

Exits with code 1 when it finds something.
#>
param([string]$Root = (Split-Path -Parent $PSScriptRoot))

$ErrorActionPreference = 'Stop'
$findings = New-Object System.Collections.Generic.List[string]

Push-Location $Root
try { $files = @(& git ls-files --cached --others --exclude-standard '*.md') } finally { Pop-Location }
$files = $files | Where-Object { $_ -notlike 'third_party/*' -and $_ -ne 'THIRD_PARTY_LICENSES.md' }

$linkPattern = [regex]'!?\[[^\]]*\]\(([^)\s]+)(?:\s+"[^"]*")?\)'
# Pictographs, dingbats and the variation selector that turns a symbol into an emoji.
$emojiPattern = [regex]("[{0}-{1}{2}]|[{3}-{4}][{5}-{6}]" -f [char]0x2600, [char]0x27BF, [char]0xFE0F, [char]0xD83C, [char]0xD83E, [char]0xDC00, [char]0xDFFF)
$leftover = [regex]'(?i)\b(TODO|TBD|FIXME|lorem ipsum|coming soon|placeholder text)\b'

foreach ($file in $files) {
    $path = Join-Path $Root $file
    $folder = Split-Path -Parent $path
    $inFence = $false
    $number = 0
    foreach ($line in [System.IO.File]::ReadLines($path)) {
        $number++
        if ($line -match '^\s*```') { $inFence = -not $inFence; continue }
        if ($inFence) { continue }

        # Links and pictures
        foreach ($match in $linkPattern.Matches($line)) {
            $target = $match.Groups[1].Value
            if ($target -match '^(https?:|mailto:|#)' -or $target -match '^\.\./\.\./(issues|releases|security)') { continue }
            $target = ($target -split '#')[0]
            if (-not $target) { continue }
            $resolved = Join-Path $folder ($target -replace '/', '\')
            if (-not (Test-Path -LiteralPath $resolved)) {
                $findings.Add("${file}:${number}: link to '$target', which does not exist")
            }
        }

        # Writing rules apply to prose, not to code or to quoted error text in inline code.
        $prose = [regex]::Replace($line, '`[^`]*`', '')
        $prose = [regex]::Replace($prose, '\]\([^)]*\)', ']')
        if ($prose.Contains([string][char]0x2014)) { $findings.Add("${file}:${number}: em dash") }
        if ($prose.Contains(';')) { $findings.Add("${file}:${number}: semicolon") }
        if ($prose.Contains('*')) { $findings.Add("${file}:${number}: asterisk") }
        if ($emojiPattern.IsMatch($prose)) { $findings.Add("${file}:${number}: emoji") }
        if ($leftover.IsMatch($prose) -and $file -ne 'scripts/check-docs.ps1') { $findings.Add("${file}:${number}: unfinished text") }
    }
}

if ($findings.Count -gt 0) {
    Write-Host 'Documentation problems:'
    $findings | ForEach-Object { Write-Host "  $_" }
    exit 1
}
Write-Host "Documentation is fine. $(@($files).Count) files checked."
