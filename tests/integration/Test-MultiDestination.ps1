# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Runs several destinations at once in a real OBS and checks sharing, isolation and recovery.

.DESCRIPTION
Each test below is one OBS run against the local RTMP sink.

  sharing            Three compatible destinations use one video encoder. One stops and rejoins
                     while the others keep streaming.
  independent        Two destinations with different size and frame rate get an encoder each.
  isolation          A rejected key, an unreachable server and a dropped connection fail on their
                     own while a healthy destination keeps streaming.
  reconnect          A dropped connection reconnects by itself. The other destination is untouched.
  manual-reconnect   Reconnect on request cuts and restores one destination.
  recovery           The server goes away and comes back. The destination recovers.
  stop-connecting    Stop while a connection attempt hangs. The OBS window must stay responsive.
  video-settings     OBS refuses new video settings while a destination connects or waits to
                     reconnect, and accepts them again when nothing is active.
  shutdown-live      OBS closes while two destinations are live.
  shutdown-connecting   OBS closes while a connection attempt hangs.
  shutdown-reconnecting OBS closes while a destination waits to reconnect.

Needs a build with RELAYDOCK_TEST_HOOKS=ON (preset windows-hooks-x64).

.EXAMPLE
.\Test-MultiDestination.ps1 -ObsRoot C:\obs-32.2.2 -BuildDir C:\RelayDock\build_hooks_x64

.EXAMPLE
.\Test-MultiDestination.ps1 -ObsRoot C:\obs-32.2.2 -BuildDir C:\RelayDock\build_hooks_x64 -Only sharing,isolation
#>
param(
    [Parameter(Mandatory)][string]$ObsRoot,
    [Parameter(Mandatory)][string]$BuildDir,
    [string]$OutDir = '',
    [string[]]$Only = @(),
    [int]$Port = 19350
)

$ErrorActionPreference = 'Stop'
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot '..\output\multi-destination' }
Import-Module (Join-Path $PSScriptRoot 'ObsTestHarness.psm1') -Force

$paths = Get-BuildPaths -BuildDir $BuildDir
$obsVersion = (Get-Item (Join-Path $ObsRoot 'bin\64bit\obs64.exe')).VersionInfo.ProductVersion
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

$server = "rtmp://127.0.0.1:$Port/live"
$blackholePort = $Port + 1
$closedPort = $Port + 2
$report = New-TestReport -Title "Multiple destinations on OBS $obsVersion"

# A fixed canvas for every test: 1080p canvas, 720p output, 60 FPS. Tests then do not depend
# on the monitor of the PC they run on.
$video = @{ op = 'obs_video'; base_width = 1920; base_height = 1080; output_width = 1280; output_height = 720; fps = '60' }

function Add-Destination([string]$Ref, [string]$Key, [hashtable]$Config = @{}, [string]$Url = $server) {
    $merged = @{ name = "Sink $Ref"; server_url = $Url }
    foreach ($entry in $Config.GetEnumerator()) { $merged[$entry.Key] = $entry.Value }
    return @{ op = 'add_destination'; ref = $Ref; provider = 'custom_rtmp'; stream_key = $Key; config = $merged }
}

function Test-Selected([string]$Name) {
    return ($Only.Count -eq 0) -or ($Only -contains $Name)
}

function Invoke-WithSink([string]$Name, [hashtable]$Scenario, [int]$TimeoutSec = 180) {
    $dir = Join-Path $OutDir $Name
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $sink = Start-RtmpSink -SinkPath $paths.Sink -ReportPath (Join-Path $dir 'sink-report.json') -Port $Port -BlackholePort $blackholePort
    try {
        $run = Invoke-ObsScenario -ObsRoot $ObsRoot -PluginRunDir $paths.PluginRunDir -Scenario $Scenario -OutDir $dir `
            -Name $Name -TimeoutSec $TimeoutSec
    } finally {
        $sinkReport = Stop-RtmpSink -Sink $sink
    }
    return [pscustomobject]@{ Run = $run; Sink = $sinkReport }
}

# ---------------------------------------------------------------------------------------------
if (Test-Selected 'sharing') {
    Write-Host ''
    Write-Host 'sharing: three compatible destinations, one video encoder'
    $keys = @{ a = 'ok-share-a-91c2f07d'; b = 'ok-share-b-3e8a55b1'; c = 'ok-share-c-c40d7e26' }
    $outcome = Invoke-WithSink 'sharing' @{ steps = @(
            @{ op = 'clear' }, $video, @{ op = 'set'; performance_mode = 'balanced' },
            (Add-Destination 'a' $keys.a), (Add-Destination 'b' $keys.b), (Add-Destination 'c' $keys.c),
            @{ op = 'start_all' },
            @{ op = 'wait_phase'; ref = 'a'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait_phase'; ref = 'b'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait_phase'; ref = 'c'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait'; seconds = 8 },
            @{ op = 'snapshot'; label = 'all_live' },
            @{ op = 'stop'; ref = 'b' },
            @{ op = 'wait_phase'; ref = 'b'; phase = 'idle'; timeout_sec = 30 },
            @{ op = 'wait'; seconds = 4 },
            @{ op = 'snapshot'; label = 'b_stopped' },
            @{ op = 'start'; ref = 'b' },
            @{ op = 'wait_phase'; ref = 'b'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait'; seconds = 5 },
            @{ op = 'snapshot'; label = 'b_rejoined' },
            @{ op = 'stop_all' },
            @{ op = 'wait_idle'; timeout_sec = 40 },
            @{ op = 'wait'; seconds = 2 },
            @{ op = 'snapshot'; label = 'all_stopped' },
            @{ op = 'quit' }) }
    $run = $outcome.Run
    Add-ObsRunChecks -Report $report -Run $run -Label 'sharing' -Secrets $keys.Values

    if ($run.Result -and $run.Result.snapshots.all_stopped) {
        $s = $run.Result.snapshots
        $live = $s.all_live.destinations
        $report.Check('sharing: all three destinations are live', ($live.a.phase -eq 'live' -and $live.b.phase -eq 'live' -and $live.c.phase -eq 'live'))
        $report.Check('sharing: OBS runs one video encoder for three destinations', ($s.all_live.encoders.video_live -eq 1), "$($s.all_live.encoders.video_live) video encoder(s)")
        $report.Check('sharing: all three use the same OBS encoder object',
            ($live.a.video_encoder -ne '' -and $live.a.video_encoder -eq $live.b.video_encoder -and $live.b.video_encoder -eq $live.c.video_encoder),
            "$($live.a.video_encoder) / $($live.b.video_encoder) / $($live.c.video_encoder)")
        $report.Check('sharing: each destination lists the two it shares with', (@($live.a.shared_with).Count -eq 2 -and @($live.b.shared_with).Count -eq 2))

        $streams = $outcome.Sink.streams
        $va = $streams.($keys.a).video_bytes; $vb = $streams.($keys.b).video_bytes; $vc = $streams.($keys.c).video_bytes
        $report.Check('sharing: the server received video on all three keys', ($va -gt 100000 -and $vb -gt 100000 -and $vc -gt 100000), "$va / $vb / $vc bytes")
        # a and c ran the whole time on the same encoder, so they carry the same data.
        $difference = if ($va -gt 0) { [math]::Abs($va - $vc) / $va } else { 1 }
        $report.Check('sharing: destinations on one encoder receive the same video', ($difference -lt 0.05), "$([math]::Round($difference * 100, 2)) percent apart")

        $report.Check('sharing: stopping one destination leaves the others live',
            ($s.b_stopped.destinations.a.phase -eq 'live' -and $s.b_stopped.destinations.c.phase -eq 'live' -and $s.b_stopped.destinations.b.phase -eq 'idle'))
        $report.Check('sharing: the others kept sending while one was stopped',
            ($s.b_stopped.destinations.a.stats.total_bytes -gt $s.all_live.destinations.a.stats.total_bytes))
        $report.Check('sharing: the shared encoder stayed alive for the others', ($s.b_stopped.encoders.video_live -eq 1))
        $report.Check('sharing: a destination started later joins the running encoder',
            ($s.b_rejoined.destinations.b.phase -eq 'live' -and $s.b_rejoined.encoders.video_live -eq 1 -and
             $s.b_rejoined.destinations.b.video_encoder -eq $s.b_rejoined.destinations.a.video_encoder),
            "$($s.b_rejoined.encoders.video_live) video encoder(s)")
        $report.Check('sharing: the stopped destination has two sessions at the server, the others one',
            ($streams.($keys.b).sessions -eq 2 -and $streams.($keys.a).sessions -eq 1 -and $streams.($keys.c).sessions -eq 1),
            "a=$($streams.($keys.a).sessions) b=$($streams.($keys.b).sessions) c=$($streams.($keys.c).sessions)")
        $report.Check('sharing: Stop All stops every destination and frees every encoder',
            ($s.all_stopped.destinations.a.phase -eq 'idle' -and $s.all_stopped.destinations.b.phase -eq 'idle' -and
             $s.all_stopped.destinations.c.phase -eq 'idle' -and $s.all_stopped.encoders.video_live -eq 0 -and $s.all_stopped.encoders.audio_live -eq 0))
        $report.Check('sharing: starting and stopping never blocked the OBS window',
            ($s.all_live.max_ui_gap_ms -lt 2500 -and $s.b_stopped.max_ui_gap_ms -lt 1500 -and $s.all_stopped.max_ui_gap_ms -lt 1500),
            "longest pauses $($s.all_live.max_ui_gap_ms) / $($s.b_stopped.max_ui_gap_ms) / $($s.all_stopped.max_ui_gap_ms) ms")
    }
}

# ---------------------------------------------------------------------------------------------
if (Test-Selected 'independent') {
    Write-Host ''
    Write-Host 'independent: different settings, one encoder each'
    $keys = @{ a = 'ok-indep-a-5b17c9e0'; b = 'ok-indep-b-0f62ad34' }
    $outcome = Invoke-WithSink 'independent' @{ steps = @(
            @{ op = 'clear' }, $video, @{ op = 'set'; performance_mode = 'custom' },
            (Add-Destination 'a' $keys.a @{ video = @{ width = 1280; height = 720; fps = 30; bitrate_kbps = 2500; encoder = 'obs_x264'; preset = 'veryfast' } }),
            (Add-Destination 'b' $keys.b @{ video = @{ width = 854; height = 480; fps = 15; bitrate_kbps = 1000; encoder = 'obs_x264'; preset = 'veryfast' } }),
            @{ op = 'start_all' },
            @{ op = 'wait_phase'; ref = 'a'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait_phase'; ref = 'b'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait'; seconds = 10 },
            @{ op = 'snapshot'; label = 'live' },
            @{ op = 'stop_all' },
            @{ op = 'wait_idle'; timeout_sec = 40 },
            @{ op = 'quit' }) }
    $run = $outcome.Run
    Add-ObsRunChecks -Report $report -Run $run -Label 'independent' -Secrets $keys.Values

    if ($run.Result -and $run.Result.snapshots.live) {
        $live = $run.Result.snapshots.live
        $report.Check('independent: both destinations are live', ($live.destinations.a.phase -eq 'live' -and $live.destinations.b.phase -eq 'live'))
        $report.Check('independent: OBS runs two video encoders', ($live.encoders.video_live -eq 2), "$($live.encoders.video_live) video encoder(s)")
        $report.Check('independent: the destinations use different encoder objects', ($live.destinations.a.video_encoder -ne $live.destinations.b.video_encoder))

        # The frame rate is measured from the frames that arrive. OBS writes the canvas frame
        # rate into the stream's "framerate" metadata field even when a destination encodes
        # every second or fourth frame, so that field is not checked here.
        $sa = $outcome.Sink.streams.($keys.a); $sb = $outcome.Sink.streams.($keys.b)
        $report.Check('independent: destination a arrives as 1280x720',
            ([int]$sa.metadata.width -eq 1280 -and [int]$sa.metadata.height -eq 720), "$([int]$sa.metadata.width)x$([int]$sa.metadata.height)")
        $report.Check('independent: destination a arrives at 30 FPS', ([math]::Abs([double]$sa.measured_fps - 30) -lt 1.5), "$($sa.measured_fps) FPS measured")
        $report.Check('independent: destination b arrives as 854x480',
            ([int]$sb.metadata.width -eq 854 -and [int]$sb.metadata.height -eq 480), "$([int]$sb.metadata.width)x$([int]$sb.metadata.height)")
        $report.Check('independent: destination b arrives at 15 FPS', ([math]::Abs([double]$sb.measured_fps - 15) -lt 1.0), "$($sb.measured_fps) FPS measured")

        foreach ($ref in 'a', 'b') {
            $stream = $outcome.Sink.streams.($keys.$ref)
            $record = @($stream.session_records)[0]
            $seconds = ($record.end_ms - $record.start_ms) / 1000.0
            $kbps = if ($seconds -gt 0) { ($stream.video_bytes * 8 / 1000) / $seconds } else { 0 }
            $target = $live.destinations.$ref.effective.bitrate_kbps
            $report.Check("independent: destination $ref sends its own bitrate", ($kbps -gt $target * 0.6 -and $kbps -lt $target * 1.4), "$([int]$kbps) Kbps measured, $target Kbps target")
        }
    }
}

# ---------------------------------------------------------------------------------------------
if (Test-Selected 'isolation') {
    Write-Host ''
    Write-Host 'isolation: failing destinations do not stop a healthy one'
    $keys = @{ good = 'ok-iso-good-a71e03c4'; rejected = 'reject-iso-58d20b9f'; dropped = 'drop4-iso-e9c1476a'; unreachable = 'ok-iso-nowhere-2b6f8d10' }
    $outcome = Invoke-WithSink 'isolation' @{ steps = @(
            @{ op = 'clear' }, $video, @{ op = 'set'; performance_mode = 'balanced' },
            (Add-Destination 'good' $keys.good),
            (Add-Destination 'rejected' $keys.rejected),
            (Add-Destination 'dropped' $keys.dropped @{ connection = @{ auto_reconnect = $false } }),
            (Add-Destination 'unreachable' $keys.unreachable @{} "rtmp://127.0.0.1:$closedPort/live"),
            @{ op = 'start_all' },
            @{ op = 'wait_phase'; ref = 'good'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait_phase'; ref = 'rejected'; phase = 'failed'; timeout_sec = 40 },
            @{ op = 'wait_phase'; ref = 'unreachable'; phase = 'failed'; timeout_sec = 60 },
            @{ op = 'wait_phase'; ref = 'dropped'; phase = 'failed'; timeout_sec = 60 },
            @{ op = 'snapshot'; label = 'failures' },
            @{ op = 'wait'; seconds = 5 },
            @{ op = 'snapshot'; label = 'later' },
            @{ op = 'stop_all' },
            @{ op = 'wait_idle'; timeout_sec = 40 },
            @{ op = 'quit' }) } 240
    $run = $outcome.Run
    Add-ObsRunChecks -Report $report -Run $run -Label 'isolation' -Secrets $keys.Values

    if ($run.Result -and $run.Result.snapshots.later) {
        $failures = $run.Result.snapshots.failures.destinations
        $later = $run.Result.snapshots.later.destinations

        $report.Check('isolation: the rejected key fails as "rejected the connection"',
            ($failures.rejected.phase -eq 'failed' -and $failures.rejected.last_stop -eq 'invalid_stream' -and $failures.rejected.error -match 'rejected the connection'),
            [string]$failures.rejected.error)
        $report.Check('isolation: the unreachable server fails as "could not connect"',
            ($failures.unreachable.phase -eq 'failed' -and $failures.unreachable.last_stop -eq 'connect_failed' -and $failures.unreachable.error -match 'could not connect'),
            [string]$failures.unreachable.error)
        $report.Check('isolation: the dropped connection fails as "lost its connection"',
            ($failures.dropped.phase -eq 'failed' -and $failures.dropped.last_stop -eq 'disconnected' -and $failures.dropped.error -match 'lost its connection'),
            [string]$failures.dropped.error)
        foreach ($ref in 'rejected', 'unreachable', 'dropped') {
            $report.Check("isolation: the error for '$ref' names the destination and says what to check",
                ($failures.$ref.error -match "Sink $ref" -and $failures.$ref.error -match 'Check'))
            $report.Check("isolation: the error for '$ref' does not contain its stream key", (-not ([string]$failures.$ref.error).Contains($keys.$ref)))
        }

        $report.Check('isolation: the healthy destination is still live after the others failed', ($later.good.phase -eq 'live' -and [string]$later.good.error -eq ''))
        $report.Check('isolation: the healthy destination kept sending',
            ($later.good.stats.total_bytes -gt $failures.good.stats.total_bytes), "$($failures.good.stats.total_bytes) then $($later.good.stats.total_bytes) bytes")
        $good = $outcome.Sink.streams.($keys.good)
        $report.Check('isolation: the server saw one uninterrupted session for the healthy destination', ($good.sessions -eq 1 -and @($good.session_records)[0].ended -eq 'client'))
        $report.Check('isolation: the server refused the rejected key', ($outcome.Sink.streams.($keys.rejected).rejected -ge 1))
        $report.Check('isolation: failed destinations hold no output', (-not $later.rejected.has_output -and -not $later.unreachable.has_output -and -not $later.dropped.has_output))
        $report.Check('isolation: the PC is kept awake for the one destination that is live', ($run.Result.snapshots.later.keeps_awake -eq $true))
    }
}

# ---------------------------------------------------------------------------------------------
if (Test-Selected 'reconnect') {
    Write-Host ''
    Write-Host 'reconnect: a dropped connection reconnects by itself'
    $keys = @{ steady = 'ok-rec-steady-4c8e1a72'; flaky = 'droponce4-rec-b3d960f5' }
    $outcome = Invoke-WithSink 'reconnect' @{ steps = @(
            @{ op = 'clear' }, $video, @{ op = 'set'; performance_mode = 'balanced' },
            (Add-Destination 'steady' $keys.steady),
            (Add-Destination 'flaky' $keys.flaky @{ connection = @{ auto_reconnect = $true; reconnect_attempts = 5; reconnect_delay_sec = 1 } }),
            @{ op = 'start_all' },
            @{ op = 'wait_phase'; ref = 'steady'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait_phase'; ref = 'flaky'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait_reconnects'; ref = 'flaky'; count = 1; timeout_sec = 60 },
            @{ op = 'wait'; seconds = 5 },
            @{ op = 'snapshot'; label = 'recovered' },
            @{ op = 'stop_all' },
            @{ op = 'wait_idle'; timeout_sec = 40 },
            @{ op = 'quit' }) }
    $run = $outcome.Run
    Add-ObsRunChecks -Report $report -Run $run -Label 'reconnect' -Secrets $keys.Values

    if ($run.Result -and $run.Result.snapshots.recovered) {
        $d = $run.Result.snapshots.recovered.destinations
        $flaky = $outcome.Sink.streams.($keys.flaky); $steady = $outcome.Sink.streams.($keys.steady)
        $report.Check('reconnect: the dropped destination is live again', ($d.flaky.phase -eq 'live' -and $d.flaky.reconnects -eq 1), "$($d.flaky.reconnects) reconnect(s)")
        $report.Check('reconnect: the server saw the drop and then a second session',
            ($flaky.sessions -eq 2 -and @($flaky.session_records)[0].ended -eq 'dropped'), "$($flaky.sessions) session(s)")
        $report.Check('reconnect: data flows again after the reconnect', (@($flaky.session_records)[1].bytes -gt 100000), "$(@($flaky.session_records)[1].bytes) bytes in the second session")
        $report.Check('reconnect: the other destination never disconnected', ($steady.sessions -eq 1 -and $d.steady.reconnects -eq 0 -and $d.steady.phase -eq 'live'))
    }
}

# ---------------------------------------------------------------------------------------------
if (Test-Selected 'manual-reconnect') {
    Write-Host ''
    Write-Host 'manual-reconnect: Reconnect cuts and restores one destination'
    $keys = @{ a = 'ok-manual-a-7d05e2c9'; b = 'ok-manual-b-1f9b64a3' }
    $outcome = Invoke-WithSink 'manual-reconnect' @{ steps = @(
            @{ op = 'clear' }, $video, @{ op = 'set'; performance_mode = 'balanced' },
            (Add-Destination 'a' $keys.a), (Add-Destination 'b' $keys.b),
            @{ op = 'start_all' },
            @{ op = 'wait_phase'; ref = 'a'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait_phase'; ref = 'b'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait'; seconds = 4 },
            @{ op = 'reconnect'; ref = 'a' },
            @{ op = 'wait'; seconds = 1 },
            @{ op = 'wait_phase'; ref = 'a'; phase = 'live'; timeout_sec = 40 },
            @{ op = 'wait'; seconds = 4 },
            @{ op = 'snapshot'; label = 'after' },
            @{ op = 'stop_all' },
            @{ op = 'wait_idle'; timeout_sec = 40 },
            @{ op = 'quit' }) }
    $run = $outcome.Run
    Add-ObsRunChecks -Report $report -Run $run -Label 'manual-reconnect' -Secrets $keys.Values

    if ($run.Result -and $run.Result.snapshots.after) {
        $d = $run.Result.snapshots.after.destinations
        $a = $outcome.Sink.streams.($keys.a); $b = $outcome.Sink.streams.($keys.b)
        $report.Check('manual-reconnect: the destination is live again', ($d.a.phase -eq 'live' -and [string]$d.a.error -eq ''))
        $report.Check('manual-reconnect: the server saw two sessions for it', ($a.sessions -eq 2), "$($a.sessions) session(s)")
        $report.Check('manual-reconnect: the other destination kept its single session', ($b.sessions -eq 1 -and $d.b.phase -eq 'live'))
        $report.Check('manual-reconnect: both share one encoder again', ($run.Result.snapshots.after.encoders.video_live -eq 1 -and $d.a.video_encoder -eq $d.b.video_encoder))
    }
}

# ---------------------------------------------------------------------------------------------
if (Test-Selected 'recovery') {
    Write-Host ''
    Write-Host 'recovery: the server goes away and comes back'
    $key = 'ok-recovery-6a2c91d8'
    $dir = Join-Path $OutDir 'recovery'
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $scenario = @{ steps = @(
            @{ op = 'clear' }, $video, @{ op = 'set'; performance_mode = 'balanced' },
            (Add-Destination 'a' $key @{ connection = @{ auto_reconnect = $true; reconnect_attempts = 20; reconnect_delay_sec = 1 } }),
            @{ op = 'start'; ref = 'a' },
            @{ op = 'wait_phase'; ref = 'a'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait_phase'; ref = 'a'; phase = 'reconnecting'; timeout_sec = 60 },
            @{ op = 'snapshot'; label = 'outage' },
            @{ op = 'wait_reconnects'; ref = 'a'; count = 1; timeout_sec = 90 },
            @{ op = 'wait'; seconds = 5 },
            @{ op = 'snapshot'; label = 'recovered' },
            @{ op = 'stop'; ref = 'a' },
            @{ op = 'wait_idle'; timeout_sec = 40 },
            @{ op = 'quit' }) }

    $sink = Start-RtmpSink -SinkPath $paths.Sink -ReportPath (Join-Path $dir 'sink-report-before.json') -Port $Port
    $session = Start-ObsScenario -ObsRoot $ObsRoot -PluginRunDir $paths.PluginRunDir -Scenario $scenario -OutDir $dir -Name 'recovery'
    $wentLive = Wait-ObsLogLine -Session $session -Pattern '\[RelayDock\] Sink a is live' -TimeoutSec 60
    Start-Sleep -Seconds 4
    Stop-RtmpSinkAbruptly -Sink $sink          # The server is gone.
    Start-Sleep -Seconds 7                      # Several retries fail in this time.
    $sink = Start-RtmpSink -SinkPath $paths.Sink -ReportPath (Join-Path $dir 'sink-report-after.json') -Port $Port
    $run = Complete-ObsScenario -Session $session -TimeoutSec 200
    $after = Stop-RtmpSink -Sink $sink

    $report.Check('recovery: the destination went live before the outage', $wentLive)
    Add-ObsRunChecks -Report $report -Run $run -Label 'recovery' -Secrets @($key)
    if ($run.Result -and $run.Result.snapshots.recovered) {
        $outage = $run.Result.snapshots.outage.destinations.a
        $recovered = $run.Result.snapshots.recovered.destinations.a
        $report.Check('recovery: the destination shows Reconnecting during the outage', ($outage.phase -eq 'reconnecting' -and $outage.reconnect_attempt -ge 1))
        $report.Check('recovery: the PC is kept awake while the destination waits to reconnect', ($run.Result.snapshots.outage.keeps_awake -eq $true))
        $report.Check('recovery: the destination is live again after the server returns', ($recovered.phase -eq 'live' -and $recovered.reconnects -ge 1), "$($recovered.reconnects) reconnect(s)")
        $report.Check('recovery: the returned server receives video', ($after.streams.$key.video_bytes -gt 100000), "$($after.streams.$key.video_bytes) bytes")
        $retries = @($run.Log -split "`n" | Where-Object { $_ -match 'Sink a lost its connection\. Retry' }).Count
        $report.Check('recovery: retries are spaced out, not a tight loop', ($retries -ge 1 -and $retries -le 12), "$retries retries during a 7 second outage")
    }
}

# ---------------------------------------------------------------------------------------------
if (Test-Selected 'stop-connecting') {
    Write-Host ''
    Write-Host 'stop-connecting: Stop while a connection attempt hangs'
    $keys = @{ hang = 'ok-hang-93e7b1c5'; good = 'ok-hang-good-d48a2f60' }
    $outcome = Invoke-WithSink 'stop-connecting' @{ steps = @(
            @{ op = 'clear' }, $video, @{ op = 'set'; performance_mode = 'balanced' },
            (Add-Destination 'good' $keys.good),
            (Add-Destination 'hang' $keys.hang @{} "rtmp://127.0.0.1:$blackholePort/live"),
            @{ op = 'start_all' },
            @{ op = 'wait_phase'; ref = 'good'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait'; seconds = 3 },
            @{ op = 'snapshot'; label = 'connecting' },
            @{ op = 'stop'; ref = 'hang' },
            @{ op = 'wait'; seconds = 3 },
            @{ op = 'snapshot'; label = 'stop_requested' },
            @{ op = 'wait_phase'; ref = 'hang'; phase = 'idle'; timeout_sec = 90 },
            @{ op = 'snapshot'; label = 'stopped' },
            @{ op = 'stop_all' },
            @{ op = 'wait_idle'; timeout_sec = 40 },
            @{ op = 'quit' }) } 300
    $run = $outcome.Run
    Add-ObsRunChecks -Report $report -Run $run -Label 'stop-connecting' -Secrets $keys.Values

    if ($run.Result -and $run.Result.snapshots.stopped) {
        $s = $run.Result.snapshots
        $report.Check('stop-connecting: the destination is still connecting when Stop is pressed', ($s.connecting.destinations.hang.phase -eq 'starting'))
        $report.Check('stop-connecting: the OBS window stayed responsive while the stop waited on the network',
            ($s.stop_requested.max_ui_gap_ms -lt 1000 -and $s.stopped.max_ui_gap_ms -lt 1000),
            "longest pauses $($s.stop_requested.max_ui_gap_ms) and $($s.stopped.max_ui_gap_ms) ms")
        $report.Check('stop-connecting: the destination ends idle with no error', ($s.stopped.destinations.hang.phase -eq 'idle' -and [string]$s.stopped.destinations.hang.error -eq ''))
        $report.Check('stop-connecting: the healthy destination stayed live throughout', ($s.stopped.destinations.good.phase -eq 'live'))
    }
}

# ---------------------------------------------------------------------------------------------
if (Test-Selected 'video-settings') {
    Write-Host ''
    Write-Host 'video-settings: OBS video settings cannot change under a destination that holds encoders'
    # An encoder that is not encoding does not stop OBS from replacing its video. Without
    # RelayDock's video guard, OBS accepts the change in both cases below and crashes at the
    # next connection attempt.
    $keys = @{ a = 'droponce4-vid-a-51c0de77'; hang = 'ok-vid-hang-b82e19f4' }
    $change = @{ op = 'obs_video'; base_width = 1280; base_height = 720; output_width = 852; output_height = 480; fps = '30' }
    $outcome = Invoke-WithSink 'video-settings' @{ steps = @(
            @{ op = 'clear' }, $video, @{ op = 'set'; performance_mode = 'balanced' },
            @{ op = 'snapshot'; label = 'idle' },
            (Add-Destination 'a' $keys.a @{ connection = @{ auto_reconnect = $true; reconnect_attempts = 5; reconnect_delay_sec = 6 } }),
            (Add-Destination 'hang' $keys.hang @{} "rtmp://127.0.0.1:$blackholePort/live"),
            # 1. While a destination waits to reconnect.
            @{ op = 'start'; ref = 'a' },
            @{ op = 'wait_phase'; ref = 'a'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait_phase'; ref = 'a'; phase = 'reconnecting'; timeout_sec = 40 },
            @{ op = 'wait'; seconds = 1 },
            @{ op = 'snapshot'; label = 'waiting' },
            ($change + @{ expect = 'refused' }),
            @{ op = 'wait_reconnects'; ref = 'a'; count = 1; timeout_sec = 60 },
            @{ op = 'wait'; seconds = 5 },
            @{ op = 'snapshot'; label = 'recovered' },
            @{ op = 'stop'; ref = 'a' },
            @{ op = 'wait_idle'; timeout_sec = 40 },
            # 2. While a destination is still connecting.
            @{ op = 'start'; ref = 'hang' },
            @{ op = 'wait'; seconds = 3 },
            @{ op = 'snapshot'; label = 'connecting' },
            ($change + @{ expect = 'refused' }),
            @{ op = 'stop'; ref = 'hang' },
            @{ op = 'wait'; seconds = 2 },
            @{ op = 'stop'; ref = 'hang' },
            @{ op = 'wait_idle'; timeout_sec = 90 },
            @{ op = 'wait'; seconds = 1 },
            # 3. With nothing active, OBS accepts the change.
            @{ op = 'snapshot'; label = 'stopped' },
            ($change + @{ expect = 'applied' }),
            @{ op = 'snapshot'; label = 'changed' },
            $video,
            @{ op = 'quit' }) } 300
    $run = $outcome.Run
    Add-ObsRunChecks -Report $report -Run $run -Label 'video-settings' -Secrets $keys.Values

    if ($run.Result -and $run.Result.snapshots.changed) {
        $s = $run.Result.snapshots
        # In order: the canvas for this test, the two changes OBS must refuse, the one it must
        # accept, and the canvas again.
        $changes = @($run.Result.video_changes)
        $stream = $outcome.Sink.streams.($keys.a)

        $report.Check('video-settings: with nothing active, OBS does not count video as in use', ($s.idle.obs.video_active -eq $false))
        $report.Check('video-settings: the destination was waiting to reconnect', ($s.waiting.destinations.a.phase -eq 'reconnecting'))
        $report.Check('video-settings: OBS counts video as in use while a destination waits to reconnect', ($s.waiting.obs.video_active -eq $true))
        $report.Check('video-settings: OBS refused new video settings while the destination waited to reconnect',
            ($changes[1].applied -eq $false), [string]$changes[1].video)
        $report.Check('video-settings: the destination reconnected and is live', ($s.recovered.destinations.a.phase -eq 'live' -and $s.recovered.destinations.a.reconnects -eq 1))
        $report.Check('video-settings: OBS still has the video settings the stream started with',
            ($s.recovered.obs.base_width -eq 1920 -and $s.recovered.obs.output_width -eq 1280),
            "$($s.recovered.obs.base_width)x$($s.recovered.obs.base_height) canvas, $($s.recovered.obs.output_width)x$($s.recovered.obs.output_height) output")
        $report.Check('video-settings: the server received video again after the reconnect',
            ($stream.sessions -eq 2 -and @($stream.session_records)[1].bytes -gt 100000), "$(@($stream.session_records)[1].bytes) bytes in the second session")
        $report.Check('video-settings: the other destination was still connecting', ($s.connecting.destinations.hang.phase -eq 'starting'))
        $report.Check('video-settings: OBS counts video as in use while a destination connects', ($s.connecting.obs.video_active -eq $true))
        $report.Check('video-settings: OBS refused new video settings while the destination was connecting',
            ($changes[2].applied -eq $false), [string]$changes[2].video)
        $report.Check('video-settings: after the last destination stopped, video is free again',
            ($s.stopped.obs.video_active -eq $false -and $s.stopped.keeps_awake -eq $false))
        $report.Check('video-settings: OBS then accepts new video settings',
            ($changes[3].applied -eq $true -and $s.changed.obs.base_width -eq 1280 -and $s.changed.obs.output_width -eq 852), [string]$changes[3].video)
    }
}

# ---------------------------------------------------------------------------------------------
if (Test-Selected 'shutdown-live') {
    Write-Host ''
    Write-Host 'shutdown-live: OBS closes while two destinations are live'
    $keys = @{ a = 'ok-quit-a-0c5d8e71'; b = 'ok-quit-b-f27a3b94' }
    $outcome = Invoke-WithSink 'shutdown-live' @{ steps = @(
            @{ op = 'clear' }, $video, @{ op = 'set'; performance_mode = 'balanced' },
            (Add-Destination 'a' $keys.a), (Add-Destination 'b' $keys.b),
            @{ op = 'start_all' },
            @{ op = 'wait_phase'; ref = 'a'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait_phase'; ref = 'b'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait'; seconds = 4 },
            @{ op = 'snapshot'; label = 'live' },
            @{ op = 'quit' }) }
    Add-ObsRunChecks -Report $report -Run $outcome.Run -Label 'shutdown-live' -Secrets $keys.Values
    $report.Check('shutdown-live: RelayDock stopped its outputs for shutdown', ($outcome.Run.Log -match '\[RelayDock\] Stopped 2 output\(s\) for shutdown'))
    $report.Check('shutdown-live: RelayDock unloaded', ($outcome.Run.Log -match '\[RelayDock\] Unloaded\.'))
    $report.Check('shutdown-live: the server saw both streams end', (-not $outcome.Sink.streams.($keys.a).publishing -and -not $outcome.Sink.streams.($keys.b).publishing))
}

if (Test-Selected 'shutdown-connecting') {
    Write-Host ''
    Write-Host 'shutdown-connecting: OBS closes while a connection attempt hangs'
    $key = 'ok-quit-hang-8b3e6f15'
    $outcome = Invoke-WithSink 'shutdown-connecting' @{ steps = @(
            @{ op = 'clear' }, $video, @{ op = 'set'; performance_mode = 'balanced' },
            (Add-Destination 'hang' $key @{} "rtmp://127.0.0.1:$blackholePort/live"),
            @{ op = 'start'; ref = 'hang' },
            @{ op = 'wait'; seconds = 3 },
            @{ op = 'snapshot'; label = 'connecting' },
            @{ op = 'quit' }) } 240
    Add-ObsRunChecks -Report $report -Run $outcome.Run -Label 'shutdown-connecting' -Secrets @($key)
    if ($outcome.Run.Result -and $outcome.Run.Result.snapshots.connecting) {
        $report.Check('shutdown-connecting: the destination was still connecting at shutdown', ($outcome.Run.Result.snapshots.connecting.destinations.hang.phase -eq 'starting'))
    }
    $report.Check('shutdown-connecting: RelayDock unloaded', ($outcome.Run.Log -match '\[RelayDock\] Unloaded\.'))
}

if (Test-Selected 'shutdown-reconnecting') {
    Write-Host ''
    Write-Host 'shutdown-reconnecting: OBS closes while a destination waits to reconnect'
    $key = 'drop3-quit-rec-a6d1c0e9'
    $outcome = Invoke-WithSink 'shutdown-reconnecting' @{ steps = @(
            @{ op = 'clear' }, $video, @{ op = 'set'; performance_mode = 'balanced' },
            (Add-Destination 'a' $key @{ connection = @{ auto_reconnect = $true; reconnect_attempts = 5; reconnect_delay_sec = 30 } }),
            @{ op = 'start'; ref = 'a' },
            @{ op = 'wait_phase'; ref = 'a'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait_phase'; ref = 'a'; phase = 'reconnecting'; timeout_sec = 40 },
            @{ op = 'wait'; seconds = 1 },
            @{ op = 'snapshot'; label = 'reconnecting' },
            @{ op = 'quit' }) }
    Add-ObsRunChecks -Report $report -Run $outcome.Run -Label 'shutdown-reconnecting' -Secrets @($key)
    if ($outcome.Run.Result -and $outcome.Run.Result.snapshots.reconnecting) {
        $report.Check('shutdown-reconnecting: the destination was waiting to reconnect at shutdown', ($outcome.Run.Result.snapshots.reconnecting.destinations.a.phase -eq 'reconnecting'))
    }
    $report.Check('shutdown-reconnecting: RelayDock unloaded', ($outcome.Run.Log -match '\[RelayDock\] Unloaded\.'))
}

$leftover = @(cmdkey /list | Select-String 'RelayDockTest-').Count
$report.Check('No test credentials remain in Windows Credential Manager', ($leftover -eq 0), "$leftover entries")
exit $report.Finish()
