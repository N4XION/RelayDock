# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Streams for a long time and checks that nothing degrades: no crash, no reconnect, no growing
memory, no stalled interface.

.DESCRIPTION
Three destinations stream scrolling noise to a test server on this PC: two horizontal ones that
share an encoder and one vertical one. RelayDock takes a measurement every minute. At the end
the script writes a report with the numbers and checks them.

The release checklist asks for runs of 30 minutes, 2 hours and 6 hours.

The script copies the plugin build and the test server into its output folder first, so you
can keep building while a long run is in progress.

Needs a build with RELAYDOCK_TEST_HOOKS=ON (preset windows-hooks-x64).

.EXAMPLE
.\Test-Endurance.ps1 -ObsRoot C:\obs-32.2.2 -BuildDir C:\RelayDock\build_hooks_x64 -Minutes 30
#>
param(
    [Parameter(Mandatory)][string]$ObsRoot,
    [Parameter(Mandatory)][string]$BuildDir,
    [int]$Minutes = 30,
    [string]$OutDir = (Join-Path $PSScriptRoot "..\output\endurance-$Minutes"),
    [int]$Port = 19390,
    # Memory may grow by this share between minute 5 and the end before the run counts as failed.
    [double]$MaxMemoryGrowthPercent = 15.0
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ObsTestHarness.psm1') -Force

$paths = Get-BuildPaths -BuildDir $BuildDir
$obsVersion = (Get-Item (Join-Path $ObsRoot 'bin\64bit\obs64.exe')).VersionInfo.ProductVersion
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$report = New-TestReport -Title "Endurance, $Minutes minutes, on OBS $obsVersion"

# A private copy of what the run uses.
$plugin = Join-Path $OutDir 'plugin'
if (Test-Path $plugin) { Remove-Item -LiteralPath $plugin -Recurse -Force }
Copy-Item -LiteralPath $paths.PluginRunDir -Destination $plugin -Recurse
$sinkExe = Join-Path $OutDir 'rd-rtmp-sink.exe'
Copy-Item -LiteralPath $paths.Sink -Destination $sinkExe -Force

# The test picture: random noise from a fixed seed.
$noise = Join-Path $OutDir 'noise.png'
if (-not (Test-Path $noise)) {
    Add-Type -AssemblyName System.Drawing
    $bitmap = New-Object System.Drawing.Bitmap 960, 540, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $area = New-Object System.Drawing.Rectangle 0, 0, 960, 540
    $data = $bitmap.LockBits($area, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bitmap.PixelFormat)
    $bytes = New-Object byte[] ($data.Stride * 540)
    (New-Object System.Random 20261004).NextBytes($bytes)
    [System.Runtime.InteropServices.Marshal]::Copy($bytes, 0, $data.Scan0, $bytes.Length)
    $bitmap.UnlockBits($data)
    $bitmap.Save($noise, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
}

$server = "rtmp://127.0.0.1:$Port/live"
$keys = @{ a = 'ok-endure-a-2f81c6'; b = 'ok-endure-b-90d3e4'; tall = 'ok-endure-tall-5a7b19' }
$steps = New-Object System.Collections.Generic.List[object]
$steps.Add(@{ op = 'clear' })
$steps.Add(@{ op = 'obs_video'; base_width = 1280; base_height = 720; output_width = 1280; output_height = 720; fps = '30' })
$steps.Add(@{ op = 'add_moving_picture'; file = $noise })
$steps.Add(@{ op = 'set'; performance_mode = 'balanced'; optimizer = @{ mode = 'off' } })
$steps.Add(@{ op = 'add_destination'; ref = 'a'; provider = 'custom_rtmp'; stream_key = $keys.a; config = @{ name = 'Sink a'; server_url = $server } })
$steps.Add(@{ op = 'add_destination'; ref = 'b'; provider = 'custom_rtmp'; stream_key = $keys.b; config = @{ name = 'Sink b'; server_url = $server } })
$steps.Add(@{ op = 'add_destination'; ref = 'tall'; provider = 'custom_rtmp'; stream_key = $keys.tall
        config = @{ name = 'Sink tall'; server_url = $server; video = @{ orientation = 'vertical' } } })
$steps.Add(@{ op = 'start_all' })
foreach ($ref in 'a', 'b', 'tall') { $steps.Add(@{ op = 'wait_phase'; ref = $ref; phase = 'live'; timeout_sec = 40 }) }
for ($minute = 1; $minute -le $Minutes; $minute++) {
    $steps.Add(@{ op = 'wait'; seconds = 60 })
    $steps.Add(@{ op = 'snapshot'; label = ('m{0:D4}' -f $minute) })
}
$steps.Add(@{ op = 'stop_all' })
$steps.Add(@{ op = 'wait_idle'; timeout_sec = 60 })
$steps.Add(@{ op = 'wait'; seconds = 3 })
$steps.Add(@{ op = 'snapshot'; label = 'stopped' })
$steps.Add(@{ op = 'quit' })

Write-Host "Streaming for $Minutes minutes. Started $(Get-Date -Format 'HH:mm:ss')."
$sink = Start-RtmpSink -SinkPath $sinkExe -ReportPath (Join-Path $OutDir 'sink-report.json') -Port $Port
try {
    $run = Invoke-ObsScenario -ObsRoot $ObsRoot -PluginRunDir $plugin -Scenario @{ steps = $steps.ToArray() } -OutDir $OutDir `
        -Name 'endurance' -TimeoutSec ($Minutes * 60 + 300) -ResetConfig
} finally {
    $sinkReport = Stop-RtmpSink -Sink $sink
}
Add-ObsRunChecks -Report $report -Run $run -Label 'endurance' -Secrets $keys.Values

$summary = New-Object System.Collections.Generic.List[string]
if ($run.Result -and $run.Result.snapshots.stopped) {
    $shots = @(1..$Minutes | ForEach-Object { $run.Result.snapshots.('m{0:D4}' -f $_) } | Where-Object { $null -ne $_ })
    $last = $shots[-1]
    $report.Check('every minute was measured', ($shots.Count -eq $Minutes), "$($shots.Count) of $Minutes")

    foreach ($ref in 'a', 'b', 'tall') {
        $d = $last.destinations.$ref
        $stream = $sinkReport.streams.($keys.$ref)
        $dropped = if ($d.stats.total_frames -gt 0) { 100.0 * $d.stats.dropped_frames / $d.stats.total_frames } else { 0 }
        $report.Check("${ref}: live at the end with no reconnect", ($d.phase -eq 'live' -and $d.reconnects -eq 0), "$($d.stats.live_seconds) s live")
        $report.Check("${ref}: one unbroken session at the server", ($stream.sessions -eq 1), "$($stream.sessions) session(s)")
        $report.Check("${ref}: less than 0.5 percent of frames dropped", ($dropped -lt 0.5), ("{0:N3} percent" -f $dropped))
        $expectedFrames = $d.stats.live_seconds * 30
        $report.Check("${ref}: the server received the frames that were sent",
            ($stream.video_messages -ge 0.98 * ($d.stats.total_frames - $d.stats.dropped_frames) -and $d.stats.total_frames -ge 0.97 * $expectedFrames),
            "$($stream.video_messages) frames received, $($d.stats.total_frames) sent")
        $megabytes = [math]::Round($stream.video_bytes / 1MB)
        $summary.Add("| Sink $ref | $($d.effective.width)x$($d.effective.height) | $($d.stats.live_seconds) s | $megabytes MB | $($d.stats.dropped_frames) of $($d.stats.total_frames) | $($d.reconnects) | $($stream.sessions) |")
    }
    $report.Check('the two horizontal destinations shared one encoder throughout',
        ($last.destinations.a.video_encoder -eq $last.destinations.b.video_encoder -and $last.encoders.video_live -eq 2))

    # Memory. The first minutes include start-up allocations, so compare from minute 5.
    $memory = @($shots | ForEach-Object { [double]$_.obs.memory_mb })
    $from = [math]::Min(4, $memory.Count - 1)
    $growth = 100.0 * ($memory[-1] - $memory[$from]) / $memory[$from]
    $peak = ($memory | Measure-Object -Maximum).Maximum
    $report.Check("memory at the end is within $MaxMemoryGrowthPercent percent of minute $($from + 1)", ($growth -le $MaxMemoryGrowthPercent),
        ("{0:N0} MB at minute {1}, {2:N0} MB at the end, peak {3:N0} MB, change {4:N1} percent" -f $memory[$from], ($from + 1), $memory[-1], $peak, $growth))

    $cpu = @($shots | ForEach-Object { [double]$_.obs.cpu_percent })
    $cpuAverage = ($cpu | Measure-Object -Average).Average
    $half = [math]::Floor($cpu.Count / 2)
    $cpuFirst = ($cpu[0..([math]::Max(0, $half - 1))] | Measure-Object -Average).Average
    $cpuSecond = ($cpu[$half..($cpu.Count - 1)] | Measure-Object -Average).Average
    $report.Check('processor use did not creep up', ($cpuSecond -le $cpuFirst * 1.25 + 1.0),
        ("first half {0:N1} percent, second half {1:N1} percent" -f $cpuFirst, $cpuSecond))

    $renderLag = 100.0 * $last.obs.render_lagged_frames / [math]::Max(1, $last.obs.render_total_frames)
    $encodeLag = 100.0 * $last.obs.encode_skipped_frames / [math]::Max(1, $last.obs.encode_total_frames)
    $report.Check('encoders kept up', ($encodeLag -lt 1.0), ("{0:N2} percent of frames skipped" -f $encodeLag))
    $gap = ($shots | ForEach-Object { [int]$_.max_ui_gap_ms } | Measure-Object -Maximum).Maximum
    $report.Check('the OBS interface never stalled for a second', ($gap -lt 1000), "longest gap $gap ms")
    $report.Check('everything stopped and every encoder was freed',
        ($run.Result.snapshots.stopped.encoders.video_live -eq 0 -and -not $run.Result.snapshots.stopped.destinations.a.has_output))

    # ---- Report file ---------------------------------------------------------------------------
    $system = Get-CimInstance Win32_Processor | Select-Object -First 1
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# Endurance run, $Minutes minutes")
    $lines.Add('')
    $lines.Add("Run on $(Get-Date -Format 'yyyy-MM-dd') with ``tests/integration/Test-Endurance.ps1 -Minutes $Minutes``. Every number is measured.")
    $lines.Add('')
    $lines.Add("- PC: $($system.Name.Trim()), $([Environment]::ProcessorCount) threads")
    $lines.Add("- OBS Studio $obsVersion, canvas 1280x720 at 30 FPS, scrolling random noise")
    $lines.Add('- Three destinations to a test server on the same PC: two horizontal ones on one shared encoder, one vertical one')
    $lines.Add('')
    $lines.Add('| Destination | Size | Live | Video received | Frames dropped | Reconnects | Sessions at the server |')
    $lines.Add('| --- | --- | ---: | ---: | ---: | ---: | ---: |')
    foreach ($line in $summary) { $lines.Add($line) }
    $lines.Add('')
    $lines.Add(("- OBS memory: {0:N0} MB at minute {1}, {2:N0} MB at the end, peak {3:N0} MB ({4:N1} percent change)" -f $memory[$from], ($from + 1), $memory[-1], $peak, $growth))
    $lines.Add(("- OBS processor use: {0:N1} percent on average ({1:N1} in the first half, {2:N1} in the second)" -f $cpuAverage, $cpuFirst, $cpuSecond))
    $lines.Add(("- Rendering lag {0:N2} percent, encoder lag {1:N2} percent of all frames" -f $renderLag, $encodeLag))
    $lines.Add("- Longest stall of the OBS interface: $gap ms")
    $lines.Add("- OBS exit: code $($run.ExitCode), crash reports $($run.Crashes.Count), memory leaks reported $($run.MemoryLeaks) (OBS reports 1 without any plugin)")
    $lines.Add("- Checks: $($report.Checks.Count - $report.Failures) of $($report.Checks.Count) passed")
    [System.IO.File]::WriteAllLines((Join-Path $OutDir "endurance-$Minutes-min.md"), $lines, (New-Object System.Text.UTF8Encoding($false)))
}

exit $report.Finish()
