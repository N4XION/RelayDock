# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Installs the release ZIP into a portable OBS the way docs/manual-installation.md says, checks
that OBS loads it, and removes it again.

.DESCRIPTION
This is the release build, not the test build, so there is no scenario runner to drive. The
test starts OBS, reads the OBS log and closes OBS:

  - the ZIP has the layout the guide shows,
  - the DLL is not a test build,
  - OBS loads RelayDock from its own obs-plugins folder and logs the version of the ZIP,
  - OBS closes cleanly, with no crash report and no memory leak beyond its own baseline,
  - after removing the files, OBS starts without RelayDock again.

It changes only the portable OBS folder you pass, and puts it back the way it was.

.EXAMPLE
.\Test-ZipInstall.ps1 -ObsRoot C:\obs-32.2.2 -ZipPath .\release\RelayDock-1.0.0-windows-x64.zip
#>
param(
    [Parameter(Mandatory)][string]$ObsRoot,
    [Parameter(Mandatory)][string]$ZipPath
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ObsTestHarness.psm1') -Force

# These would make OBS load a development build from somewhere else. This test must not.
Remove-Item Env:OBS_PLUGINS_PATH, Env:OBS_PLUGINS_DATA_PATH -ErrorAction SilentlyContinue

$obsVersion = (Get-Item (Join-Path $ObsRoot 'bin\64bit\obs64.exe')).VersionInfo.ProductVersion
$report = New-TestReport -Title "ZIP install into a portable OBS $obsVersion"
$ZipPath = (Resolve-Path $ZipPath).Path

function Get-LeakCount([string]$Log) {
    $match = [regex]::Match($Log, 'Number of memory leaks: (\d+)')
    if ($match.Success) { return [int]$match.Groups[1].Value }
    return $null
}

$dllTarget = Join-Path $ObsRoot 'obs-plugins\64bit\relaydock.dll'
$dataTarget = Join-Path $ObsRoot 'data\obs-plugins\relaydock'
if ((Test-Path $dllTarget) -or (Test-Path $dataTarget)) {
    throw "This OBS folder already contains RelayDock. Remove '$dllTarget' and '$dataTarget' first."
}

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("relaydock-zip-test-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $work | Out-Null
try {
    # ---- The ZIP itself -----------------------------------------------------------------------
    Expand-Archive -LiteralPath $ZipPath -DestinationPath $work
    $dll = Join-Path $work 'relaydock\bin\64bit\relaydock.dll'
    $locale = Join-Path $work 'relaydock\data\locale\en-US.ini'
    $report.Check('the ZIP holds one folder named relaydock', (@(Get-ChildItem -LiteralPath $work).Count -eq 1 -and (Test-Path (Join-Path $work 'relaydock'))))
    $report.Check('it has the plugin, the locale file and both licence files',
        ((Test-Path $dll) -and (Test-Path $locale) -and (Test-Path (Join-Path $work 'relaydock\LICENSE.txt')) -and
         (Test-Path (Join-Path $work 'relaydock\THIRD_PARTY_LICENSES.txt'))))
    $text = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($dll))
    $report.Check('the DLL is a release build without the test scenario runner', (-not $text.Contains('RELAYDOCK_SCENARIO')))
    $zipVersion = (Get-Item $dll).VersionInfo.ProductVersion
    $report.Check('the DLL carries a version', ($zipVersion -match '^\d+\.\d+\.\d+'), "$zipVersion")

    # The checksum file next to the ZIP must name this ZIP with this hash.
    $sums = Join-Path (Split-Path -Parent $ZipPath) 'SHA256SUMS.txt'
    if (Test-Path $sums) {
        $hash = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
        $line = "$hash  $(Split-Path -Leaf $ZipPath)"
        $report.Check('SHA256SUMS.txt lists the ZIP with its hash', (@(Get-Content -LiteralPath $sums) -contains $line))
    } else {
        $report.Skip('SHA256SUMS.txt lists the ZIP with its hash', 'no SHA256SUMS.txt next to the ZIP')
    }

    # ---- Baseline, then install the way the guide says -----------------------------------------
    Initialize-ObsTestConfig -ObsRoot $ObsRoot -Reset | Out-Null
    $baseline = Start-ObsTest -ObsRoot $ObsRoot -NoPlugin
    [void](Wait-ObsLogLine -Session $baseline -Pattern '==== Startup complete' -TimeoutSec 90)
    Start-Sleep -Seconds 3
    $baselineStop = Stop-ObsTest -Session $baseline
    $baselineLog = Get-ObsLogText -Session $baseline
    $baselineLeaks = Get-LeakCount $baselineLog
    $report.Check('before the install, OBS runs without RelayDock', ($baselineStop.Clean -and $baselineLog -notmatch '\[RelayDock\]'))

    Copy-Item -LiteralPath $dll -Destination $dllTarget
    New-Item -ItemType Directory -Force -Path $dataTarget | Out-Null
    Copy-Item -Path (Join-Path $work 'relaydock\data\*') -Destination $dataTarget -Recurse

    # No OBS_PLUGINS_PATH here. OBS has to find the plugin in its own folder.
    Initialize-ObsTestConfig -ObsRoot $ObsRoot | Out-Null
    $session = Start-ObsTest -ObsRoot $ObsRoot -NoPlugin
    $loaded = Wait-ObsLogLine -Session $session -Pattern '\[RelayDock\] Loaded\.' -TimeoutSec 90
    Start-Sleep -Seconds 4
    $stop = Stop-ObsTest -Session $session
    $log = Get-ObsLogText -Session $session
    $leaks = Get-LeakCount $log

    $report.Check('OBS loads RelayDock from its own obs-plugins folder', $loaded)
    $versionLine = [regex]::Match($log, '\[RelayDock\] Loading version (\S+)')
    $report.Check('the log names the version of the ZIP', ($versionLine.Success -and $versionLine.Groups[1].Value.StartsWith(($zipVersion -split '\+')[0])),
        "$($versionLine.Groups[1].Value)")
    $report.Check('the installed build does not announce a test build', ($log -notmatch 'test scenario runner'))
    $report.Check('OBS did not reject the module',
        ($log -notmatch "Failed to load module file '[^']*relaydock|Module '[^']*relaydock[^']*' not loaded" -and
         $log -notmatch "relaydock[^\r\n]*compiled with newer libobs"))
    $report.Check('OBS lists relaydock.dll under Loaded Modules', ($log -match '(?m)^\d\d:\d\d:\d\d\.\d+:\s+relaydock\.dll\s*$'))
    $report.Check('OBS finds the English text in data\obs-plugins\relaydock', ($log -notmatch "Failed to load '[^']*' text for module: 'relaydock"))
    $report.Check('OBS closes cleanly with RelayDock installed', $stop.Clean, "exit code $($stop.ExitCode)")
    $report.Check('RelayDock logs that it unloaded', ($log -match '\[RelayDock\] Unloaded\.'))
    $report.Check('no crash report', (@(Get-ObsCrashFiles -Session $session).Count -eq 0))
    $report.Check('no memory leak beyond what OBS reports by itself',
        ($null -ne $leaks -and $null -ne $baselineLeaks -and $leaks -le $baselineLeaks), "$leaks with RelayDock, $baselineLeaks without")
} finally {
    # ---- Remove it, as the guide says ----------------------------------------------------------
    if (Test-Path $dllTarget) { Remove-Item -LiteralPath $dllTarget -Force }
    if (Test-Path $dataTarget) { Remove-Item -LiteralPath $dataTarget -Recurse -Force }
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}

Initialize-ObsTestConfig -ObsRoot $ObsRoot | Out-Null
$after = Start-ObsTest -ObsRoot $ObsRoot -NoPlugin
[void](Wait-ObsLogLine -Session $after -Pattern '==== Startup complete' -TimeoutSec 90)
Start-Sleep -Seconds 2
$afterStop = Stop-ObsTest -Session $after
$afterLog = Get-ObsLogText -Session $after
$report.Check('after removing the files, OBS starts and closes cleanly', $afterStop.Clean, "exit code $($afterStop.ExitCode)")
$report.Check('and its log has no line from RelayDock', ($afterLog -match '==== Startup complete' -and $afterLog -notmatch '\[RelayDock\]'))

exit $report.Finish()
