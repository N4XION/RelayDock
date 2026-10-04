# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Checks that OBS Studio loads RelayDock, registers the dock, and unloads it without a crash.

.DESCRIPTION
1. Starts a portable OBS without RelayDock and records how many memory leaks OBS itself
   reports at exit. Some OBS versions report one or more with no third-party plugin loaded.
2. Starts the same OBS with the plugin build, waits for RelayDock's load line in the OBS
   log, closes OBS the way a user does, and checks the result.

The test fails when OBS reports the module as failed, writes a crash report, has to be
killed, exits with a non-zero code, or reports more memory leaks than it does by itself.

.PARAMETER ObsRoot
Folder of an extracted OBS Studio ZIP.

.PARAMETER PluginRunDir
<build>\rundir\<config> from a RelayDock build.

.PARAMETER Repeat
How many load and unload cycles to run with the plugin.

.EXAMPLE
.\Test-PluginLoad.ps1 -ObsRoot C:\obs-32.2.2 -PluginRunDir C:\build\rundir\RelWithDebInfo
#>
param(
    [Parameter(Mandatory)][string]$ObsRoot,
    [Parameter(Mandatory)][string]$PluginRunDir,
    [int]$Repeat = 1,
    [switch]$ResetConfig
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ObsTestHarness.psm1') -Force

function Get-LeakCount([string]$Log) {
    $match = [regex]::Match($Log, 'Number of memory leaks: (\d+)')
    if ($match.Success) { return [int]$match.Groups[1].Value }
    return $null
}

$obsVersion = (Get-Item (Join-Path $ObsRoot 'bin\64bit\obs64.exe')).VersionInfo.ProductVersion
$report = New-TestReport -Title "Plugin load and unload on OBS $obsVersion"

# Baseline: OBS by itself.
Initialize-ObsTestConfig -ObsRoot $ObsRoot -Reset:$ResetConfig | Out-Null
$baseline = Start-ObsTest -ObsRoot $ObsRoot -NoPlugin
[void](Wait-ObsLogLine -Session $baseline -Pattern 'Loaded Modules:' -TimeoutSec 90)
Start-Sleep -Seconds 3
$baselineStop = Stop-ObsTest -Session $baseline
$baselineLeaks = Get-LeakCount (Get-ObsLogText -Session $baseline)
$report.Check('baseline: OBS without RelayDock starts and exits cleanly', $baselineStop.Clean, "memory leaks reported = $baselineLeaks")

for ($run = 1; $run -le $Repeat; $run++) {
    Initialize-ObsTestConfig -ObsRoot $ObsRoot | Out-Null
    $session = Start-ObsTest -ObsRoot $ObsRoot -PluginRunDir $PluginRunDir

    $loaded = Wait-ObsLogLine -Session $session -Pattern '\[RelayDock\] Loaded\.' -TimeoutSec 90
    # Give OBS a moment to finish start-up before asking it to close.
    Start-Sleep -Seconds 3
    $stop = Stop-ObsTest -Session $session
    $log = Get-ObsLogText -Session $session
    $crashes = @(Get-ObsCrashFiles -Session $session)
    $leaks = Get-LeakCount $log

    $label = "run $run"
    $report.Check("${label}: RelayDock logs that it loaded", $loaded)
    $report.Check("${label}: OBS did not reject the module",
        ($log -notmatch "Failed to load module file '[^']*relaydock|Module '[^']*relaydock[^']*' not loaded" -and
         $log -notmatch "relaydock[^\r\n]*compiled with newer libobs"))
    $report.Check("${label}: OBS closed by itself and exited cleanly", $stop.Clean, "exit code $($stop.ExitCode)")
    $report.Check("${label}: RelayDock logs that it unloaded", ($log -match '\[RelayDock\] Unloaded\.'))
    $report.Check("${label}: OBS wrote no crash report", ($crashes.Count -eq 0))
    $report.Check("${label}: RelayDock adds no memory leak to what OBS reports by itself",
        ($null -ne $leaks -and $null -ne $baselineLeaks -and $leaks -le $baselineLeaks), "$leaks with RelayDock, $baselineLeaks without")

    @($log -split "`n" | Where-Object { $_ -match '\[RelayDock\]' }) | ForEach-Object { Write-Host "    $($_.Trim())" }
}

exit $report.Finish()
