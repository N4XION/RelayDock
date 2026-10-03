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

$failures = New-Object System.Collections.Generic.List[string]
$obsVersion = (Get-Item (Join-Path $ObsRoot 'bin\64bit\obs64.exe')).VersionInfo.ProductVersion

# Baseline: OBS by itself.
Initialize-ObsTestConfig -ObsRoot $ObsRoot -Reset:$ResetConfig | Out-Null
$baseline = Start-ObsTest -ObsRoot $ObsRoot -NoPlugin
[void](Wait-ObsLogLine -Session $baseline -Pattern 'Loaded Modules:' -TimeoutSec 90)
Start-Sleep -Seconds 3
$baselineStop = Stop-ObsTest -Session $baseline
$baselineLeaks = Get-LeakCount (Get-ObsLogText -Session $baseline)
if (-not $baselineStop.Clean) {
    $failures.Add("Baseline: OBS without RelayDock did not exit cleanly. Fix the OBS test install first.")
}
Write-Host "Baseline on OBS ${obsVersion}: clean exit = $($baselineStop.Clean), memory leaks reported = $baselineLeaks"

for ($run = 1; $run -le $Repeat; $run++) {
    Initialize-ObsTestConfig -ObsRoot $ObsRoot | Out-Null
    $session = Start-ObsTest -ObsRoot $ObsRoot -PluginRunDir $PluginRunDir

    $loaded = Wait-ObsLogLine -Session $session -Pattern '\[RelayDock\] Loaded\.' -TimeoutSec 90
    if (-not $loaded) { $failures.Add("Run ${run}: RelayDock did not log its load line within 90 seconds.") }

    # Give OBS a moment to finish start-up before asking it to close.
    Start-Sleep -Seconds 3
    $stop = Stop-ObsTest -Session $session
    $log = Get-ObsLogText -Session $session

    if ($stop.Killed) { $failures.Add("Run ${run}: OBS did not exit after WM_CLOSE and was killed.") }
    elseif (-not $stop.Clean) { $failures.Add("Run ${run}: OBS exited with code $($stop.ExitCode).") }

    if ($log -notmatch '\[RelayDock\] Unloaded\.') { $failures.Add("Run ${run}: RelayDock did not log its unload line.") }
    if ($log -match "Failed to load module file '[^']*relaydock|Module '[^']*relaydock[^']*' not loaded") {
        $failures.Add("Run ${run}: OBS reported that the RelayDock module failed to load.")
    }
    if ($log -match "relaydock[^\r\n]*compiled with newer libobs") {
        $failures.Add("Run ${run}: OBS rejected RelayDock as built for a newer OBS version.")
    }

    $crashes = @(Get-ObsCrashFiles -Session $session)
    if ($crashes.Count -gt 0) { $failures.Add("Run ${run}: OBS wrote $($crashes.Count) crash report(s).") }

    $leaks = Get-LeakCount $log
    if ($null -eq $leaks) {
        $failures.Add("Run ${run}: OBS did not report its memory leak count, so the shutdown did not finish.")
    } elseif ($null -ne $baselineLeaks -and $leaks -gt $baselineLeaks) {
        $failures.Add("Run ${run}: OBS reported $leaks memory leak(s) with RelayDock and $baselineLeaks without it.")
    }

    Write-Host "Run $run of $Repeat on OBS ${obsVersion}:"
    @($log -split "`n" | Where-Object { $_ -match '\[RelayDock\]' }) | ForEach-Object { Write-Host "  $($_.Trim())" }
    Write-Host "  Clean exit = $($stop.Clean), memory leaks reported = $leaks"
}

Write-Host ''
if ($failures.Count -gt 0) {
    Write-Host "FAILED: plugin load test on OBS $obsVersion"
    $failures | ForEach-Object { Write-Host "  $_" }
    exit 1
}

Write-Host "PASSED: OBS $obsVersion loaded and unloaded RelayDock $Repeat time(s). No crash, no added memory leaks."
exit 0
