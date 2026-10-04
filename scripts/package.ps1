# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Builds the release files: the ZIP, the installer, the licence notice and the checksums.

.DESCRIPTION
1. Builds the release preset.
2. Installs the plugin into a staging folder in the layout OBS expects.
3. Checks the staged DLL: it must not be a test build and must not contain a personal path.
4. Writes the ZIP with fixed timestamps and a fixed file order, so the same source gives the
   same ZIP.
5. Builds the installer with Inno Setup, when Inno Setup is installed.
6. Writes SHA256SUMS.txt for every release file.

Everything this script uses is free. GitHub Actions runs it for a tagged release.

.PARAMETER Preset
CMake preset to build. The default is the release preset.

.PARAMETER OutDir
Where the release files go. Default: release\ in the repository.

.PARAMETER SkipBuild
Package what the preset's build folder already holds.

.PARAMETER RequireInstaller
Fail when Inno Setup is missing instead of skipping the installer. CI sets it.

.PARAMETER TestInstallerDir
Also build a test build of the installer into this folder, for tests\integration\Test-Installer.ps1
-TestBuild. A test build has its own identity in Windows and refuses to run without /DIR, so the
test can run on a PC that has RelayDock installed. It is never a release file.

.EXAMPLE
.\scripts\package.ps1
#>
param(
    [string]$Preset = 'windows-x64',
    [string]$OutDir = '',
    [switch]$SkipBuild,
    [switch]$RequireInstaller,
    [string]$TestInstallerDir = ''
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) { $OutDir = Join-Path $repo 'release' }

function Find-CMake {
    $onPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
        if ($install) {
            $candidate = Join-Path $install 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path $candidate) { return $candidate }
        }
    }
    throw 'CMake was not found. Install Visual Studio 2022 with the C++ workload, or put cmake on the PATH.'
}

function Find-InnoSetup {
    $onPath = Get-Command iscc -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    foreach ($candidate in @(
            (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
            (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
            (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'))) {
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

function Invoke-Checked([string]$File, [string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$([System.IO.Path]::GetFileName($File)) failed with exit code $LASTEXITCODE." }
}

# ---- Version ------------------------------------------------------------------------------------
$spec = Get-Content (Join-Path $repo 'buildspec.json') -Raw | ConvertFrom-Json
$version = $spec.version
if ($spec.versionSuffix) { $version = "$version-$($spec.versionSuffix)" }
$baseName = "RelayDock-$version-windows-x64"
Write-Host "Packaging RelayDock $version"

# ---- Build --------------------------------------------------------------------------------------
$cmake = Find-CMake
$presets = Get-Content (Join-Path $repo 'CMakePresets.json') -Raw | ConvertFrom-Json
$userPresetsFile = Join-Path $repo 'CMakeUserPresets.json'
$allPresets = @($presets.configurePresets)
if (Test-Path $userPresetsFile) { $allPresets += @((Get-Content $userPresetsFile -Raw | ConvertFrom-Json).configurePresets) }
# A preset may take its build folder from the preset it inherits.
function Resolve-BinaryDir([string]$Name) {
    $preset = $allPresets | Where-Object { $_.name -eq $Name } | Select-Object -First 1
    if (-not $preset) { throw "No CMake preset named '$Name'." }
    if ($preset.binaryDir) { return $preset.binaryDir }
    foreach ($parent in @($preset.inherits)) {
        if ($parent) {
            $found = Resolve-BinaryDir $parent
            if ($found) { return $found }
        }
    }
    return $null
}
$buildDir = Resolve-BinaryDir $Preset
if (-not $buildDir) { throw "The preset '$Preset' names no build folder." }
$buildDir = $buildDir -replace '\$\{sourceDir\}', $repo -replace '\$env\{LOCALAPPDATA\}', $env:LOCALAPPDATA -replace '/', '\'

Push-Location $repo
try {
    if (-not $SkipBuild) {
        Invoke-Checked $cmake @('--preset', $Preset)
        Invoke-Checked $cmake @('--build', '--preset', $Preset)
    }

    # ---- Stage ----------------------------------------------------------------------------------
    $stage = Join-Path $OutDir 'stage'
    if (Test-Path $OutDir) { Remove-Item -LiteralPath $OutDir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    Invoke-Checked $cmake @('--install', $buildDir, '--config', 'RelWithDebInfo', '--prefix', $stage, '--component', 'Runtime')
} finally {
    Pop-Location
}

$dll = Join-Path $stage 'relaydock\bin\64bit\relaydock.dll'
$locale = Join-Path $stage 'relaydock\data\locale\en-US.ini'
foreach ($file in $dll, $locale) {
    if (-not (Test-Path $file)) { throw "The build did not produce $file." }
}

# ---- Checks on the DLL ----------------------------------------------------------------------------
$bytes = [System.IO.File]::ReadAllBytes($dll)
$ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
$wide = [System.Text.Encoding]::Unicode.GetString($bytes)
if ($ascii.Contains('RELAYDOCK_SCENARIO')) {
    throw 'This DLL contains the test scenario runner. Build the release preset, not the test preset.'
}
foreach ($personal in @($env:USERPROFILE, $env:USERNAME) | Where-Object { $_ -and $_.Length -ge 4 }) {
    if ($ascii.Contains($personal) -or $wide.Contains($personal)) {
        throw "The DLL contains the personal path or name '$personal'. A release file must not."
    }
}
$fileVersion = (Get-Item $dll).VersionInfo
Write-Host "  relaydock.dll $($fileVersion.ProductVersion), $([math]::Round($bytes.Length / 1KB)) KB"

# ---- Licence notice ---------------------------------------------------------------------------------
Copy-Item (Join-Path $repo 'THIRD_PARTY_LICENSES.md') (Join-Path $OutDir 'THIRD_PARTY_LICENSES.txt')
Copy-Item (Join-Path $repo 'LICENSE') (Join-Path $stage 'relaydock\LICENSE.txt')
Copy-Item (Join-Path $repo 'THIRD_PARTY_LICENSES.md') (Join-Path $stage 'relaydock\THIRD_PARTY_LICENSES.txt')

# ---- ZIP, written entry by entry so it is the same every time ----------------------------------------
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$commitTime = (& git -C $repo log -1 --format=%cI 2>$null)
$stamp = if ($commitTime) { [DateTimeOffset]::Parse($commitTime).ToUniversalTime() } else { [DateTimeOffset]::new(2026, 1, 1, 0, 0, 0, [TimeSpan]::Zero) }
# The ZIP format stores local time with two second steps and no time zone.
$stamp = [DateTimeOffset]::new($stamp.Year, $stamp.Month, $stamp.Day, $stamp.Hour, $stamp.Minute, ($stamp.Second - ($stamp.Second % 2)), [TimeSpan]::Zero)

$zipPath = Join-Path $OutDir "$baseName.zip"
$zipStream = [System.IO.File]::Open($zipPath, [System.IO.FileMode]::Create)
$zip = New-Object System.IO.Compression.ZipArchive($zipStream, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    $files = Get-ChildItem -LiteralPath (Join-Path $stage 'relaydock') -Recurse -File | Sort-Object { $_.FullName.ToLowerInvariant() }
    foreach ($file in $files) {
        $relative = 'relaydock/' + $file.FullName.Substring((Join-Path $stage 'relaydock').Length + 1).Replace('\', '/')
        $entry = $zip.CreateEntry($relative, [System.IO.Compression.CompressionLevel]::Optimal)
        $entry.LastWriteTime = $stamp
        $target = $entry.Open()
        try {
            $source = [System.IO.File]::OpenRead($file.FullName)
            try { $source.CopyTo($target) } finally { $source.Dispose() }
        } finally { $target.Dispose() }
    }
} finally {
    $zip.Dispose()
    $zipStream.Dispose()
}
Write-Host "  $baseName.zip"

# ---- Installer --------------------------------------------------------------------------------------
$iscc = Find-InnoSetup
if ($iscc) {
    Invoke-Checked $iscc @(
        '/Qp',
        "/DAppVersion=$version",
        "/DAppVersionNumeric=$($spec.version)",
        "/DStageDir=$(Join-Path $stage 'relaydock')",
        "/DOutputDir=$OutDir",
        "/DOutputBaseName=$baseName-Setup",
        "/DObsMinimumVersion=$($spec.obs.minimumVersion)",
        (Join-Path $repo 'installer\relaydock.iss'))
    Write-Host "  $baseName-Setup.exe"

    if ($TestInstallerDir) {
        New-Item -ItemType Directory -Force -Path $TestInstallerDir | Out-Null
        Invoke-Checked $iscc @(
            '/Qp',
            '/DTestInstall=1',
            "/DAppVersion=$version",
            "/DAppVersionNumeric=$($spec.version)",
            "/DStageDir=$(Join-Path $stage 'relaydock')",
            "/DOutputDir=$((Resolve-Path $TestInstallerDir).Path)",
            "/DOutputBaseName=$baseName-Setup-test",
            "/DObsMinimumVersion=$($spec.obs.minimumVersion)",
            (Join-Path $repo 'installer\relaydock.iss'))
        Write-Host "  $baseName-Setup-test.exe, a test build, in $TestInstallerDir"

        # The same files under a higher version number, for tests\integration\Test-Update.ps1:
        # the newer release that RelayDock downloads and installs over the test install. It has
        # the file name a real release gives its installer, because RelayDock checks the name.
        $updateDir = Join-Path (Resolve-Path $TestInstallerDir).Path 'update'
        New-Item -ItemType Directory -Force -Path $updateDir | Out-Null
        Invoke-Checked $iscc @(
            '/Qp',
            '/DTestInstall=1',
            '/DAppVersion=9.9.9',
            '/DAppVersionNumeric=9.9.9',
            "/DStageDir=$(Join-Path $stage 'relaydock')",
            "/DOutputDir=$updateDir",
            '/DOutputBaseName=RelayDock-9.9.9-windows-x64-Setup',
            "/DObsMinimumVersion=$($spec.obs.minimumVersion)",
            (Join-Path $repo 'installer\relaydock.iss'))
        Write-Host "  update\RelayDock-9.9.9-windows-x64-Setup.exe, a test build with a higher version number"
    }
} elseif ($RequireInstaller) {
    throw 'Inno Setup 6 was not found, and -RequireInstaller is set.'
} else {
    Write-Host '  Inno Setup 6 is not installed. The installer was skipped. Get it free from jrsoftware.org.'
}

# ---- Checksums ----------------------------------------------------------------------------------------
Remove-Item -LiteralPath $stage -Recurse -Force
$lines = Get-ChildItem -LiteralPath $OutDir -File | Where-Object { $_.Name -ne 'SHA256SUMS.txt' } | Sort-Object Name | ForEach-Object {
    $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $($_.Name)"
}
[System.IO.File]::WriteAllText((Join-Path $OutDir 'SHA256SUMS.txt'), (($lines -join "`n") + "`n"), (New-Object System.Text.UTF8Encoding($false)))
Write-Host ''
$lines | ForEach-Object { Write-Host "  $_" }
Write-Host ''
Write-Host "Release files are in $OutDir"
