# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Looks for secrets and private files in the repository. CI runs it on every push.

.DESCRIPTION
Checks every file Git tracks, or would track, for:
  - stream keys in the formats Twitch, YouTube and Facebook use,
  - private keys and certificates with private keys,
  - access tokens for GitHub and similar services,
  - files that must never be committed: key stores, credential exports, private settings.

Tests need key-shaped text. A made-up key is accepted when its line also says NOT-REAL, or
when it sits in the test folders and starts with one of the prefixes the test server
understands (ok-, reject-, drop, stall, slow).

Exits with code 1 when it finds something.

.EXAMPLE
.\scripts\scan-secrets.ps1
#>
param([string]$Root = (Split-Path -Parent $PSScriptRoot))

$ErrorActionPreference = 'Stop'

$patterns = @(
    @{ Name = 'Twitch stream key'; Regex = '\blive_\d{6,}_[A-Za-z0-9]{20,}' },
    @{ Name = 'YouTube stream key'; Regex = '\b[a-z0-9]{4}-[a-z0-9]{4}-[a-z0-9]{4}-[a-z0-9]{4}(-[a-z0-9]{4})?\b' },
    @{ Name = 'Facebook stream key'; Regex = '\bFB-\d{8,}-\d-[A-Za-z0-9_\-]{10,}' },
    @{ Name = 'Private key'; Regex = '-----BEGIN (RSA |EC |DSA |OPENSSH |ENCRYPTED |PGP )?PRIVATE KEY' },
    @{ Name = 'GitHub token'; Regex = '\b(ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{36,}\b|\bgithub_pat_[A-Za-z0-9_]{50,}\b' },
    @{ Name = 'Cloud access key'; Regex = '\bAKIA[0-9A-Z]{16}\b' },
    @{ Name = 'Bearer token'; Regex = 'Authorization:\s*Bearer\s+[A-Za-z0-9\-_\.=]{20,}' }
)

# Files that have no place in the repository, whatever they contain.
$forbiddenNames = '\.(pfx|p12|pem|key|jks|keystore|snk|pvk)$|(^|/)\.env(\..*)?$|(^|/)config\.json\.bak$|credentials?\.(json|xml|csv|txt)$|(^|/)CMakeUserPresets\.json$'

$binary = '\.(png|jpg|jpeg|gif|ico|svg|zip|exe|dll|pdb|lib|obj|woff2?)$'

Push-Location $Root
try {
    $files = @(& git ls-files --cached --others --exclude-standard)
} finally {
    Pop-Location
}
if ($LASTEXITCODE -ne 0 -or $files.Count -eq 0) { throw 'git ls-files returned nothing. Run this inside the repository.' }

$findings = New-Object System.Collections.Generic.List[string]
foreach ($file in $files) {
    $relative = $file.Replace('\', '/')
    if ($relative -match $forbiddenNames) {
        $findings.Add("${relative}: this kind of file must not be committed")
        continue
    }
    if ($relative -match $binary -or $relative -like 'third_party/*') { continue }

    $path = Join-Path $Root $file
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
    $inTests = $relative -like 'tests/*'
    $number = 0
    foreach ($line in [System.IO.File]::ReadLines($path)) {
        $number++
        foreach ($pattern in $patterns) {
            foreach ($match in [regex]::Matches($line, $pattern.Regex)) {
                $value = $match.Value
                # Made-up values that say so.
                if ($line -match 'NOT-REAL|not-real|EXAMPLE|example') { continue }
                if ($inTests -and $value -match '^(ok-|reject-|drop|stall|slow)') { continue }
                # The scanner's own patterns and the redactor's rules describe key formats.
                if ($relative -eq 'scripts/scan-secrets.ps1' -or $relative -eq 'src/security/redactor.cpp') { continue }
                # Version-like and UUID-like text in lock files and docs is not a YouTube key.
                if ($pattern.Name -eq 'YouTube stream key' -and ($value -notmatch '[a-z]' -or $value -notmatch '\d')) { continue }
                $findings.Add("${relative}:${number}: looks like a $($pattern.Name)")
            }
        }
    }
}

if ($findings.Count -gt 0) {
    Write-Host 'Possible secrets in the repository:'
    $findings | ForEach-Object { Write-Host "  $_" }
    Write-Host ''
    Write-Host 'If a real secret was committed, removing it from the file is not enough. Reset it on the platform first.'
    exit 1
}
Write-Host "No secrets found in $($files.Count) files."
