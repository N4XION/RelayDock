# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Streams one Custom RTMP destination from a real OBS to the local RTMP sink and checks the result.

.DESCRIPTION
Covers the output engine end to end:
  - a destination added, saved and started
  - OBS connects, publishes and goes live
  - video, audio and keyframes arrive at the server with the expected size and frame rate
  - Stop ends the stream cleanly and frees the encoders
  - the stream key appears in no OBS log line
  - OBS exits cleanly afterwards

Needs a build with RELAYDOCK_TEST_HOOKS=ON (preset windows-hooks-x64).

.EXAMPLE
.\Test-CustomRtmp.ps1 -ObsRoot C:\obs-32.2.2 -BuildDir C:\RelayDock\build_hooks_x64
#>
param(
    [Parameter(Mandatory)][string]$ObsRoot,
    [Parameter(Mandatory)][string]$BuildDir,
    [string]$OutDir = '',
    [int]$Port = 19350,
    [int]$LiveSeconds = 10
)

$ErrorActionPreference = 'Stop'
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot '..\output\custom-rtmp' }
Import-Module (Join-Path $PSScriptRoot 'ObsTestHarness.psm1') -Force

$paths = Get-BuildPaths -BuildDir $BuildDir
$obsVersion = (Get-Item (Join-Path $ObsRoot 'bin\64bit\obs64.exe')).VersionInfo.ProductVersion
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

# A made-up key. The sink accepts any key that does not start with a behaviour prefix.
$key = 'ok-rdtest-7f3a9c1e5b2d4860'

$scenario = @{
    steps = @(
        @{ op = 'clear' },
        @{ op = 'set'; performance_mode = 'balanced' },
        @{ op = 'add_destination'; ref = 'a'; provider = 'custom_rtmp'; stream_key = $key
           config = @{ name = 'Sink A'; server_url = "rtmp://127.0.0.1:$Port/live" } },
        @{ op = 'snapshot'; label = 'before' },
        @{ op = 'start'; ref = 'a' },
        @{ op = 'wait_phase'; ref = 'a'; phase = 'live'; timeout_sec = 30 },
        @{ op = 'wait'; seconds = $LiveSeconds },
        @{ op = 'snapshot'; label = 'live' },
        @{ op = 'stop'; ref = 'a' },
        @{ op = 'wait_phase'; ref = 'a'; phase = 'idle'; timeout_sec = 40 },
        @{ op = 'wait'; seconds = 2 },
        @{ op = 'snapshot'; label = 'stopped' },
        @{ op = 'quit' }
    )
}

$report = New-TestReport -Title "Custom RTMP streaming on OBS $obsVersion"
$sink = Start-RtmpSink -SinkPath $paths.Sink -ReportPath (Join-Path $OutDir 'sink-report.json') -Port $Port
try {
    $run = Invoke-ObsScenario -ObsRoot $ObsRoot -PluginRunDir $paths.PluginRunDir -Scenario $scenario `
        -OutDir $OutDir -Name 'custom-rtmp' -TimeoutSec (120 + $LiveSeconds)
} finally {
    $sinkReport = Stop-RtmpSink -Sink $sink
}

$result = $run.Result
$report.Check('OBS ran the scenario and closed by itself', (-not $run.TimedOut))
$report.Check('OBS exited cleanly', $run.CleanExit, "exit code $($run.ExitCode)")
$report.Check('OBS wrote no crash report', ($run.Crashes.Count -eq 0))
$report.Check('The scenario completed', ($null -ne $result -and $result.completed), [string]$result.failure)

if ($null -ne $result -and $result.snapshots.live) {
    $before = $result.snapshots.before.destinations.a
    $live = $result.snapshots.live.destinations.a
    $stopped = $result.snapshots.stopped.destinations.a
    $effective = $live.effective

    $report.Check('The destination starts idle', ($before.phase -eq 'idle'))
    $report.Check('The destination is live', ($live.phase -eq 'live'))
    $report.Check('The destination reports no error while live', ([string]$live.error -eq ''))
    $report.Check('RelayDock keeps the PC awake while the destination is live, and not before',
        ($result.snapshots.live.keeps_awake -eq $true -and $result.snapshots.before.keeps_awake -eq $false))
    $report.Check('A video encoder and an audio encoder run', ($result.snapshots.live.encoders.video_live -eq 1 -and $result.snapshots.live.encoders.audio_live -eq 1))
    $report.Check('RelayDock measures a bitrate', ($live.stats.bitrate_kbps -gt 0), "$($live.stats.bitrate_kbps) Kbps")
    $report.Check('No frames were dropped on a local connection', ($live.stats.dropped_frames -eq 0), "$($live.stats.dropped_frames) dropped")

    $stream = $sinkReport.streams.$key
    $report.Check('The server received a publish for the stream key', ($null -ne $stream -and $stream.sessions -eq 1))
    if ($null -ne $stream) {
        $report.Check('The server received video', ($stream.video_bytes -gt 100000), "$($stream.video_bytes) bytes in $($stream.video_messages) messages")
        # The test scene is silent, so AAC frames are tiny. Count frames instead of bytes:
        # 48 kHz AAC produces about 47 frames per second.
        $report.Check('The server received audio', ($stream.audio_messages -gt $LiveSeconds * 35), "$($stream.audio_bytes) bytes in $($stream.audio_messages) messages")
        $report.Check('The video is H.264', ($stream.video_codec -eq 'avc1'), [string]$stream.video_codec)

        # One keyframe every keyframe interval, give or take one at the edges.
        $expectedKeyframes = [math]::Floor($LiveSeconds / $effective.keyframe_interval_sec)
        $report.Check('Keyframes arrive at the configured interval',
            ($stream.keyframes -ge ($expectedKeyframes - 1) -and $stream.keyframes -le ($expectedKeyframes + 3)),
            "$($stream.keyframes) keyframes, about $expectedKeyframes expected")

        $report.Check('The stream metadata carries the effective size',
            ([int]$stream.metadata.width -eq $effective.width -and [int]$stream.metadata.height -eq $effective.height),
            "$([int]$stream.metadata.width)x$([int]$stream.metadata.height) sent, $($effective.width)x$($effective.height) expected")
        $report.Check('Frames arrive at the effective frame rate',
            ([math]::Abs([double]$stream.measured_fps - [double]$effective.fps) -lt 1.5),
            "$($stream.measured_fps) FPS measured, $($effective.fps) expected")

        $record = @($stream.session_records)[0]
        $report.Check('The stream ended because the client stopped it', ($null -ne $record -and $record.ended -eq 'client'), [string]$record.ended)
        $report.Check('The server no longer sees a publisher', (-not $stream.publishing))

        # Measured throughput against the target, from the server's own byte count.
        $seconds = ($record.end_ms - $record.start_ms) / 1000.0
        $measuredKbps = if ($seconds -gt 0) { ($stream.video_bytes * 8 / 1000) / $seconds } else { 0 }
        $report.Check('The video bitrate at the server is within 35 percent of the target',
            ($measuredKbps -gt $effective.bitrate_kbps * 0.65 -and $measuredKbps -lt $effective.bitrate_kbps * 1.35),
            "$([int]$measuredKbps) Kbps measured, $($effective.bitrate_kbps) Kbps target")
    }

    $report.Check('The destination is idle after Stop', ($stopped.phase -eq 'idle'))
    $report.Check('Stop left no error behind', ([string]$stopped.error -eq ''))
    $report.Check('The output was released after Stop', (-not $stopped.has_output))
    $report.Check('Windows may sleep again after Stop', ($result.snapshots.stopped.keeps_awake -eq $false))
    $report.Check('The encoders were destroyed after Stop', ($result.snapshots.stopped.encoders.video_live -eq 0 -and $result.snapshots.stopped.encoders.audio_live -eq 0))
}

# Security: the stream key must not appear anywhere in the OBS log, and neither may the
# saved settings contain it.
$report.Check('The stream key is not in the OBS log', ($run.Log.Length -gt 0 -and -not $run.Log.Contains($key)))
$configFile = Join-Path (Get-ObsConfigDir -ObsRoot $ObsRoot) 'plugin_config\relaydock\config.json'
$configText = if (Test-Path $configFile) { Get-Content -LiteralPath $configFile -Raw } else { '' }
$report.Check('RelayDock saved its settings', ($configText.Length -gt 0))
$report.Check('The stream key is not in the settings file', (-not $configText.Contains($key)))
$leftover = @(cmdkey /list | Select-String 'RelayDockTest-').Count
$report.Check('No test credentials remain in Windows Credential Manager', ($leftover -eq 0), "$leftover entries")

$logLines = @($run.Log -split "`n" | Where-Object { $_ -match '\[RelayDock\]' } | ForEach-Object { $_.Trim() })
Set-Content -LiteralPath (Join-Path $OutDir 'relaydock-log-lines.txt') -Value $logLines
Write-Host ''
Write-Host "RelayDock log lines: $(Join-Path $OutDir 'relaydock-log-lines.txt')"
exit $report.Finish()
