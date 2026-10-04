# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Checks that building RelayDock twice gives the same relaydock.dll, and compares a build with
a DLL you got from somewhere else.

.DESCRIPTION
Without -CompareWith, the script builds the plugin twice from scratch, both times in the same
scratch folder, and requires the two DLLs to be the same byte for byte.

With -CompareWith, it also compares its build with that DLL, for example the one from a
release ZIP. Use the same commit and the same compiler version as the release.

The linker stamps a DLL with an identifier it derives from the debug file, and the debug file
records the build folder. A build in another folder therefore differs in those fields, and
only in those: the time stamp fields and the identifier of the debug file. The comparison
with -CompareWith ignores exactly these fields and requires everything else, which is all
code and all data, to be the same.

.PARAMETER Preset
CMake preset to build.

.PARAMETER ScratchDir
Build folder for this check. The script deletes and recreates it. Default: build_repro in the
repository.

.PARAMETER CompareWith
A relaydock.dll to compare the build with.

.PARAMETER Builds
2 builds twice and compares the two. 1 builds once, for use with -CompareWith.

.EXAMPLE
.\scripts\check-reproducible.ps1

.EXAMPLE
.\scripts\check-reproducible.ps1 -Builds 1 -CompareWith C:\Downloads\relaydock\bin\64bit\relaydock.dll
#>
param(
    [string]$Preset = 'windows-x64',
    [string]$ScratchDir = '',
    [string]$CompareWith = '',
    [ValidateSet(1, 2)][int]$Builds = 2
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $ScratchDir) { $ScratchDir = Join-Path $repo 'build_repro' }
if ($Builds -eq 1 -and -not $CompareWith) { throw 'With -Builds 1 there is nothing to compare. Pass -CompareWith too.' }
if ($CompareWith) { $CompareWith = (Resolve-Path -LiteralPath $CompareWith).Path }

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

function Get-Sha256([byte[]]$Bytes) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return -join ($sha.ComputeHash($Bytes) | ForEach-Object { $_.ToString('x2') }) } finally { $sha.Dispose() }
}

# Returns a copy of a 64-bit DLL with the fields cleared that the linker derives from the debug
# file: the time stamps in the file header, the export table and the debug directory, the
# debug file's identifier, and the checksum.
function Get-ImageWithoutBuildIdentity([byte[]]$Image) {
    $copy = [byte[]]$Image.Clone()
    function Clear-Range([int]$Offset, [int]$Count) {
        if ($Offset -lt 0 -or $Offset + $Count -gt $copy.Length) { throw 'The DLL is damaged: a table points outside the file.' }
        [Array]::Clear($copy, $Offset, $Count)
    }

    if ($copy.Length -lt 0x200 -or $copy[0] -ne 0x4D -or $copy[1] -ne 0x5A) { throw 'Not a Windows program file.' }
    $pe = [BitConverter]::ToInt32($copy, 0x3C)
    if ([BitConverter]::ToUInt32($copy, $pe) -ne 0x4550) { throw 'Not a Windows program file.' }
    $sectionCount = [BitConverter]::ToUInt16($copy, $pe + 6)
    $optionalSize = [BitConverter]::ToUInt16($copy, $pe + 20)
    $optional = $pe + 24
    if ([BitConverter]::ToUInt16($copy, $optional) -ne 0x20B) { throw 'Not a 64-bit DLL.' }

    $sections = @()
    $table = $optional + $optionalSize
    for ($i = 0; $i -lt $sectionCount; $i++) {
        $entry = $table + $i * 40
        $sections += [pscustomobject]@{
            Rva     = [BitConverter]::ToUInt32($copy, $entry + 12)
            RawSize = [BitConverter]::ToUInt32($copy, $entry + 16)
            Raw     = [BitConverter]::ToUInt32($copy, $entry + 20)
        }
    }
    function Convert-Rva([uint32]$Rva) {
        foreach ($section in $sections) {
            if ($Rva -ge $section.Rva -and $Rva -lt $section.Rva + $section.RawSize) { return [int]($Rva - $section.Rva + $section.Raw) }
        }
        throw 'The DLL is damaged: an address lies in no section.'
    }

    Clear-Range ($pe + 8) 4          # Time stamp in the file header
    Clear-Range ($optional + 64) 4   # Checksum

    $directories = $optional + 112
    $exportRva = [BitConverter]::ToUInt32($copy, $directories)
    if ($exportRva -ne 0) { Clear-Range ((Convert-Rva $exportRva) + 4) 4 }

    $debugRva = [BitConverter]::ToUInt32($copy, $directories + 6 * 8)
    $debugSize = [BitConverter]::ToUInt32($copy, $directories + 6 * 8 + 4)
    if ($debugRva -ne 0) {
        $debug = Convert-Rva $debugRva
        for ($entry = $debug; $entry + 28 -le $debug + $debugSize; $entry += 28) {
            Clear-Range ($entry + 4) 4
            $type = [BitConverter]::ToUInt32($copy, $entry + 12)
            $dataSize = [BitConverter]::ToInt32($copy, $entry + 16)
            $data = [BitConverter]::ToInt32($copy, $entry + 24)
            if ($type -eq 2 -and $dataSize -ge 24 -and [BitConverter]::ToUInt32($copy, $data) -eq 0x53445352) {
                Clear-Range ($data + 4) 20   # Identifier and age of the debug file, after "RSDS"
            } elseif ($type -eq 16) {
                Clear-Range $data $dataSize  # The linker's hash of the debug file
            }
        }
    }
    return , $copy
}

function Get-DifferentByteCount([byte[]]$First, [byte[]]$Second) {
    if ($First.Length -ne $Second.Length) { return -1 }
    $count = 0
    for ($i = 0; $i -lt $First.Length; $i++) { if ($First[$i] -ne $Second[$i]) { $count++ } }
    return $count
}

$cmake = Find-CMake
function Invoke-Build([int]$Number) {
    if (Test-Path -LiteralPath $ScratchDir) { Remove-Item -LiteralPath $ScratchDir -Recurse -Force }
    Write-Host "Build $Number of $Builds in $ScratchDir"
    Push-Location $repo
    try {
        & $cmake --preset $Preset -B $ScratchDir | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Configure failed with exit code $LASTEXITCODE." }
        & $cmake --build $ScratchDir --config RelWithDebInfo --target relaydock | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }
    } finally {
        Pop-Location
    }
    $dll = Join-Path $ScratchDir 'RelWithDebInfo\relaydock.dll'
    if (-not (Test-Path -LiteralPath $dll)) { throw "The build did not produce $dll." }
    return , [System.IO.File]::ReadAllBytes($dll)
}

$failed = $false
$first = Invoke-Build 1
Write-Host "  sha256 $(Get-Sha256 $first)"

if ($Builds -eq 2) {
    $second = Invoke-Build 2
    Write-Host "  sha256 $(Get-Sha256 $second)"
    if ((Get-Sha256 $first) -eq (Get-Sha256 $second)) {
        Write-Host '[PASS] Two builds in the same folder give the same DLL, byte for byte.'
    } else {
        $failed = $true
        Write-Host "[FAIL] The two builds differ in $(Get-DifferentByteCount $first $second) bytes."
    }
}

if ($CompareWith) {
    $other = [System.IO.File]::ReadAllBytes($CompareWith)
    Write-Host "Comparing with $CompareWith"
    Write-Host "  sha256 $(Get-Sha256 $other)"
    if ((Get-Sha256 $first) -eq (Get-Sha256 $other)) {
        Write-Host '[PASS] The build and that DLL are the same, byte for byte.'
    } elseif ($first.Length -eq $other.Length -and
        (Get-Sha256 (Get-ImageWithoutBuildIdentity $first)) -eq (Get-Sha256 (Get-ImageWithoutBuildIdentity $other))) {
        $bytes = Get-DifferentByteCount $first $other
        Write-Host "[PASS] All code and data are the same. $bytes bytes differ, all in the time stamp and debug file identifier fields."
    } else {
        $failed = $true
        Write-Host '[FAIL] The build and that DLL differ in code or data. Check the commit and the compiler version.'
    }
}

if ($failed) { exit 1 }
exit 0
