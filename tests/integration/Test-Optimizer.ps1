# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Checks automatic optimisation in a real OBS against a server that reads too slowly.

.DESCRIPTION
The test server takes at most 2000 Kbps from a destination that sends 3000 Kbps. OBS drops
frames, and RelayDock has to notice and react.

  automatic   Automatic mode lowers the bitrate by itself, on the running encoder, with no
              reconnect. A second destination on the same encoder changes with it.
  suggest     Suggest mode changes nothing until the suggestion is accepted. Lock Setting
              restores the saved bitrate and ends the suggestions.
  recovery    The server stops being slow after 30 seconds. The bitrate comes back step by step.

The optimiser's timers are shortened for the test (seconds instead of minutes). The rules
are the same ones a release build uses.

Needs a build with RELAYDOCK_TEST_HOOKS=ON (preset windows-hooks-x64).

.EXAMPLE
.\Test-Optimizer.ps1 -ObsRoot C:\obs-32.2.2 -BuildDir C:\RelayDock\build_hooks_x64
#>
param(
    [Parameter(Mandatory)][string]$ObsRoot,
    [Parameter(Mandatory)][string]$BuildDir,
    [string]$OutDir = (Join-Path $PSScriptRoot '..\output\optimizer'),
    [string[]]$Only = @(),
    [int]$Port = 19360
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ObsTestHarness.psm1') -Force

$paths = Get-BuildPaths -BuildDir $BuildDir
$obsVersion = (Get-Item (Join-Path $ObsRoot 'bin\64bit\obs64.exe')).VersionInfo.ProductVersion
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$server = "rtmp://127.0.0.1:$Port/live"
$report = New-TestReport -Title "Automatic optimisation on OBS $obsVersion"
$video = @{ op = 'obs_video'; base_width = 1280; base_height = 720; output_width = 1280; output_height = 720; fps = '30' }
$stream = @{ video = @{ bitrate_kbps = 3000 } }

function Test-Selected([string]$Name) { return ($Only.Count -eq 0) -or ($Only -contains $Name) }

function Add-Destination([string]$Ref, [string]$Key, [hashtable]$Config = @{}) {
    $merged = @{ name = "Sink $Ref"; server_url = $server }
    foreach ($entry in $Config.GetEnumerator()) { $merged[$entry.Key] = $entry.Value }
    return @{ op = 'add_destination'; ref = $Ref; provider = 'custom_rtmp'; stream_key = $Key; config = $merged }
}

function Invoke-WithSink([string]$Name, [hashtable]$Scenario, [int]$TimeoutSec = 240) {
    $dir = Join-Path $OutDir $Name
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $sink = Start-RtmpSink -SinkPath $paths.Sink -ReportPath (Join-Path $dir 'sink-report.json') -Port $Port
    try {
        $run = Invoke-ObsScenario -ObsRoot $ObsRoot -PluginRunDir $paths.PluginRunDir -Scenario $Scenario -OutDir $dir `
            -Name $Name -TimeoutSec $TimeoutSec
    } finally {
        $sinkReport = Stop-RtmpSink -Sink $sink
    }
    return [pscustomobject]@{ Run = $run; Sink = $sinkReport }
}

# ---------------------------------------------------------------------------------------------
if (Test-Selected 'automatic') {
    Write-Host ''
    Write-Host 'automatic: the bitrate comes down by itself, with no reconnect'
    $keys = @{ slow = 'slow2000-opt-auto-5c1e77a0'; fine = 'ok-opt-auto-9d24b3f6' }
    $outcome = Invoke-WithSink 'automatic' @{ steps = @(
            @{ op = 'clear' }, $video,
            @{ op = 'set'; performance_mode = 'custom'; optimizer = @{ mode = 'automatic' } },
            @{ op = 'optimizer_tuning'; sustain_ms = 4000; cooldown_ms = 6000; recover_after_ms = 600000 },
            (Add-Destination 'slow' $keys.slow $stream),
            (Add-Destination 'fine' $keys.fine $stream),
            @{ op = 'start_all' },
            @{ op = 'wait_phase'; ref = 'slow'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait_phase'; ref = 'fine'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait'; seconds = 2 },
            @{ op = 'snapshot'; label = 'before' },
            @{ op = 'wait_adjustment'; ref = 'slow'; state = 'reduced'; timeout_sec = 90 },
            @{ op = 'snapshot'; label = 'first_step' },
            @{ op = 'wait'; seconds = 50 },
            @{ op = 'snapshot'; label = 'settled' },
            @{ op = 'wait'; seconds = 12 },
            @{ op = 'snapshot'; label = 'calm' },
            @{ op = 'stop_all' },
            @{ op = 'wait_idle'; timeout_sec = 40 },
            @{ op = 'wait'; seconds = 3 },
            @{ op = 'snapshot'; label = 'stopped' },
            @{ op = 'quit' }) } 300
    $run = $outcome.Run
    Add-ObsRunChecks -Report $report -Run $run -Label 'automatic' -Secrets $keys.Values

    if ($run.Result -and $run.Result.snapshots.stopped) {
        $s = $run.Result.snapshots
        $report.Check('automatic: both destinations start on one shared encoder at 3000 Kbps',
            ($s.before.destinations.slow.video_encoder -eq $s.before.destinations.fine.video_encoder -and
             $s.before.destinations.slow.effective.bitrate_kbps -eq 3000 -and $s.before.encoders.video_live -eq 1))
        $report.Check('automatic: nothing is reduced before a problem is measured',
            ($s.before.destinations.slow.adjustment.bitrate_percent -eq 100))
        $first = $s.first_step.destinations
        $report.Check('automatic: the first step lowers the bitrate to 85 percent',
            ($first.slow.adjustment.bitrate_percent -eq 85 -and $first.slow.effective.bitrate_kbps -eq 2550),
            "$($first.slow.adjustment.bitrate_percent) percent, $($first.slow.effective.bitrate_kbps) Kbps")
        $report.Check('automatic: the destination on the same encoder changes with it',
            ($first.fine.adjustment.bitrate_percent -eq 85 -and $first.fine.effective.bitrate_kbps -eq 2550))
        $settled = $s.settled.destinations
        $report.Check('automatic: it keeps stepping down until the stream fits the connection',
            ($settled.slow.adjustment.bitrate_percent -le 55 -and $settled.slow.effective.bitrate_kbps -le 1650),
            "$($settled.slow.adjustment.bitrate_percent) percent, $($settled.slow.effective.bitrate_kbps) Kbps")
        $report.Check('automatic: frame drops stop once the bitrate fits',
            ($s.calm.destinations.slow.window_drop_percent -ge 0 -and $s.calm.destinations.slow.window_drop_percent -lt 1.5),
            "$([math]::Round($s.calm.destinations.slow.window_drop_percent, 2)) percent over the last ten seconds")
        $report.Check('automatic: the change happens on the running encoder',
            ($settled.slow.video_encoder -eq $s.before.destinations.slow.video_encoder -and $s.settled.encoders.video_live -eq 1),
            "$($settled.slow.video_encoder)")
        $report.Check('automatic: no destination reconnected',
            ($s.calm.destinations.slow.reconnects -eq 0 -and $s.calm.destinations.fine.reconnects -eq 0 -and
             $outcome.Sink.streams.($keys.slow).sessions -eq 1 -and $outcome.Sink.streams.($keys.fine).sessions -eq 1))
        $report.Check('automatic: the lower bitrate is what the server receives',
            ($s.calm.destinations.fine.stats.bitrate_kbps -gt 1200 -and $s.calm.destinations.fine.stats.bitrate_kbps -lt 2200),
            "$($s.calm.destinations.fine.stats.bitrate_kbps) Kbps measured")
        $history = @($s.calm.optimizer_history)
        $report.Check('automatic: every change is recorded as automatic',
            ($history.Count -ge 3 -and @($history | Where-Object { -not $_.automatic }).Count -eq 0), "$($history.Count) changes")
        $report.Check('automatic: the optimiser logs each change',
            ($run.Log -match 'Optimiser, automatic: Bitrate of Sink slow and Sink fine set to 85 percent'))
        $report.Check('automatic: reductions end when the streams stop',
            ($s.stopped.destinations.slow.adjustment.bitrate_percent -eq 100))
        $report.Check('automatic: the OBS interface never stalled', ($s.settled.max_ui_gap_ms -lt 500), "longest gap $($s.settled.max_ui_gap_ms) ms")
    }
}

# ---------------------------------------------------------------------------------------------
if (Test-Selected 'suggest') {
    Write-Host ''
    Write-Host 'suggest: nothing changes until you accept'
    $key = 'slow2000-opt-suggest-71fa0c3d'
    $outcome = Invoke-WithSink 'suggest' @{ steps = @(
            @{ op = 'clear' }, $video,
            @{ op = 'set'; performance_mode = 'custom'; optimizer = @{ mode = 'suggest' } },
            @{ op = 'optimizer_tuning'; sustain_ms = 4000; cooldown_ms = 6000; recover_after_ms = 600000 },
            (Add-Destination 'a' $key $stream),
            @{ op = 'start'; ref = 'a' },
            @{ op = 'wait_phase'; ref = 'a'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait_suggestion'; timeout_sec = 90 },
            @{ op = 'wait'; seconds = 8 },
            @{ op = 'snapshot'; label = 'suggested' },
            @{ op = 'suggestion'; action = 'apply' },
            @{ op = 'wait'; seconds = 2 },
            @{ op = 'snapshot'; label = 'applied' },
            @{ op = 'wait_suggestion'; timeout_sec = 90 },
            @{ op = 'snapshot'; label = 'second' },
            @{ op = 'suggestion'; action = 'lock' },
            @{ op = 'wait'; seconds = 3 },
            @{ op = 'snapshot'; label = 'locked' },
            @{ op = 'wait'; seconds = 25 },
            @{ op = 'snapshot'; label = 'after_lock' },
            @{ op = 'stop_all' },
            @{ op = 'wait_idle'; timeout_sec = 40 },
            @{ op = 'quit' }) } 300
    $run = $outcome.Run
    Add-ObsRunChecks -Report $report -Run $run -Label 'suggest' -Secrets @($key)

    if ($run.Result -and $run.Result.snapshots.after_lock) {
        $s = $run.Result.snapshots
        $suggestion = @($s.suggested.suggestions)[0]
        $report.Check('suggest: a suggestion appears and says what it proposes',
            ($suggestion.title -eq 'Lower the bitrate of Sink a to 85 percent'), "$($suggestion.title)")
        $report.Check('suggest: it explains the reason and the effect',
            ($suggestion.reason -match 'dropped frames' -and $suggestion.effect -match 'Nothing reconnects' -and -not $suggestion.needs_reconnect))
        $report.Check('suggest: nothing changed while the suggestion waited',
            ($s.suggested.destinations.a.adjustment.bitrate_percent -eq 100 -and $s.suggested.destinations.a.effective.bitrate_kbps -eq 3000))
        $report.Check('suggest: only one suggestion is shown at a time', (@($s.suggested.suggestions).Count -eq 1))
        $report.Check('suggest: Apply lowers the bitrate on the running encoder',
            ($s.applied.destinations.a.adjustment.bitrate_percent -eq 85 -and $s.applied.destinations.a.effective.bitrate_kbps -eq 2550 -and
             $s.applied.destinations.a.video_encoder -eq $s.suggested.destinations.a.video_encoder))
        $history = @($s.applied.optimizer_history)
        $report.Check('suggest: the change is recorded as accepted by you, not automatic',
            ($history.Count -eq 1 -and -not $history[0].automatic))
        $report.Check('suggest: the suggestion is gone once applied', (@($s.applied.suggestions).Count -eq 0))
        $report.Check('suggest: the next step is proposed while frames still drop',
            (@($s.second.suggestions)[0].title -eq 'Lower the bitrate of Sink a to 70 percent'), "$(@($s.second.suggestions)[0].title)")
        $report.Check('suggest: Lock Setting locks the bitrate', ($s.locked.destinations.a.locks.bitrate -eq $true))
        $report.Check('suggest: Lock Setting restores the saved bitrate',
            ($s.locked.destinations.a.adjustment.bitrate_percent -eq 100 -and $s.locked.destinations.a.effective.bitrate_kbps -eq 3000),
            "$($s.locked.destinations.a.effective.bitrate_kbps) Kbps")
        $report.Check('suggest: no further change is proposed for a locked setting', (@($s.after_lock.suggestions).Count -eq 0))
        $report.Check('suggest: the stream never reconnected',
            ($s.after_lock.destinations.a.reconnects -eq 0 -and $outcome.Sink.streams.$key.sessions -eq 1))
    }
}

# ---------------------------------------------------------------------------------------------
if (Test-Selected 'recovery') {
    Write-Host ''
    Write-Host 'recovery: the bitrate comes back once the connection is fine again'
    $key = 'slow2000for30-opt-recover-e83b5a12'
    $outcome = Invoke-WithSink 'recovery' @{ steps = @(
            @{ op = 'clear' }, $video,
            @{ op = 'set'; performance_mode = 'custom'; optimizer = @{ mode = 'automatic' } },
            @{ op = 'optimizer_tuning'; sustain_ms = 4000; cooldown_ms = 5000; recover_after_ms = 15000; probation_ms = 8000 },
            (Add-Destination 'a' $key $stream),
            @{ op = 'start'; ref = 'a' },
            @{ op = 'wait_phase'; ref = 'a'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait_adjustment'; ref = 'a'; state = 'reduced'; timeout_sec = 60 },
            @{ op = 'snapshot'; label = 'reduced' },
            @{ op = 'wait_adjustment'; ref = 'a'; state = 'none'; timeout_sec = 200 },
            @{ op = 'wait'; seconds = 12 },
            @{ op = 'snapshot'; label = 'recovered' },
            @{ op = 'stop_all' },
            @{ op = 'wait_idle'; timeout_sec = 40 },
            @{ op = 'quit' }) } 360
    $run = $outcome.Run
    Add-ObsRunChecks -Report $report -Run $run -Label 'recovery' -Secrets @($key)

    if ($run.Result -and $run.Result.snapshots.recovered) {
        $s = $run.Result.snapshots
        $report.Check('recovery: the bitrate was lowered while the server was slow',
            ($s.reduced.destinations.a.adjustment.bitrate_percent -lt 100))
        $report.Check('recovery: the full bitrate is back after the problem ended',
            ($s.recovered.destinations.a.adjustment.bitrate_percent -eq 100 -and $s.recovered.destinations.a.effective.bitrate_kbps -eq 3000),
            "$($s.recovered.destinations.a.effective.bitrate_kbps) Kbps")
        $history = @($s.recovered.optimizer_history | ForEach-Object { $_.text })
        $downs = @($history | Where-Object { $_ -match 'set to (85|70|55|40) percent' }).Count
        $report.Check("recovery: it steps back up one level at a time",
            ($downs -ge 1 -and $history.Count -ge 2 -and $history[-1] -match "set to 100 percent"), ($history -join " | "))
        $report.Check('recovery: the stream ran in one session with no reconnect',
            ($s.recovered.destinations.a.reconnects -eq 0 -and $outcome.Sink.streams.$key.sessions -eq 1))
        $report.Check('recovery: the stream is healthy at the full bitrate',
            ($s.recovered.destinations.a.window_drop_percent -ge 0 -and $s.recovered.destinations.a.window_drop_percent -lt 1.5 -and
             $s.recovered.destinations.a.stats.bitrate_kbps -gt 2500),
            "$($s.recovered.destinations.a.stats.bitrate_kbps) Kbps, $([math]::Round($s.recovered.destinations.a.window_drop_percent, 2)) percent dropped")
    }
}

exit $report.Finish()
