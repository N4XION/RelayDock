# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Builds RelayDock and runs its tests from one command.

.DESCRIPTION
Finds the CMake that ships with Visual Studio, then runs the steps you ask for against a
CMake preset. Presets come from CMakePresets.json. Put machine-specific ones (a build folder
outside OneDrive, for example) in CMakeUserPresets.json, which Git ignores.

.PARAMETER Preset
CMake preset name. "core-tests" builds only the OBS-independent library and its tests.
"windows-x64" builds the plugin too.

.PARAMETER Steps
Any of: configure, build, test. Default is build and test. The first build configures by itself.

.EXAMPLE
.\scripts\dev.ps1 -Preset core-tests

.EXAMPLE
.\scripts\dev.ps1 -Preset windows-x64 -Steps configure,build,test
#>
param(
    [string]$Preset = 'core-tests',
    [ValidateSet('configure', 'build', 'test')][string[]]$Steps = @('build', 'test'),
    [string]$TestFilter = ''
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Find-CMake {
    $onPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($onPath) { return (Split-Path -Parent $onPath.Source) }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
        if ($install) {
            $bin = Join-Path $install 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
            if (Test-Path (Join-Path $bin 'cmake.exe')) { return $bin }
        }
    }
    throw 'CMake was not found. Install Visual Studio 2022 with "C++ CMake tools for Windows", or put cmake on PATH.'
}

$env:PATH = (Find-CMake) + ';' + $env:PATH
Push-Location $repo
try {
    $configured = $false
    $presets = & cmake --list-presets=all | Out-String
    if ($presets -notmatch [regex]::Escape("`"$Preset`"")) {
        throw "Unknown CMake preset '$Preset'. Run 'cmake --list-presets' to see the choices."
    }

    if ($Steps -contains 'configure') {
        & cmake --preset $Preset
        if ($LASTEXITCODE -ne 0) { throw "Configure failed with exit code $LASTEXITCODE." }
        $configured = $true
    }

    if ($Steps -contains 'build') {
        & cmake --build --preset $Preset
        if ($LASTEXITCODE -ne 0) {
            if ($configured) { throw "Build failed with exit code $LASTEXITCODE." }
            # A missing build folder is the usual cause on a first run. Configure once, then retry.
            & cmake --preset $Preset
            if ($LASTEXITCODE -ne 0) { throw "Configure failed with exit code $LASTEXITCODE." }
            & cmake --build --preset $Preset
            if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }
        }
    }

    if ($Steps -contains 'test') {
        $ctestArgs = @('--preset', $Preset)
        if ($TestFilter) { $ctestArgs += @('-R', $TestFilter) }
        & ctest @ctestArgs
        if ($LASTEXITCODE -ne 0) { throw "Tests failed with exit code $LASTEXITCODE." }
    }
} finally {
    Pop-Location
}
