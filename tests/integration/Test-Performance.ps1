# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Measures what RelayDock costs on this PC and writes the numbers to a report.

.DESCRIPTION
Every number in the report is measured in a real OBS on the PC that runs this script. Nothing
is estimated. The picture is scrolling random noise, which is hard to compress, so encoders
work as hard as they do on a busy game.

  baseline        OBS alone, idle, no RelayDock.
  idle            OBS with RelayDock loaded, nothing streaming.
  one .. four     One to four destinations with identical settings. They share one encoder.
  four-separate   Four destinations with different bitrates. Each needs its own encoder.
  mixed           Two horizontal destinations and one vertical one.
  potato, quality Three destinations in Potato Mode and in Quality mode.
  x264-shared, x264-separate
                  Three destinations on the software encoder, shared and separate.

Each streaming case warms up for 10 seconds and is then measured for -MeasureSec seconds.
Streams go to a test server on this PC, so the numbers do not include your internet line.

Needs a build with RELAYDOCK_TEST_HOOKS=ON (preset windows-hooks-x64).

.EXAMPLE
.\Test-Performance.ps1 -ObsRoot C:\obs-32.2.2 -BuildDir C:\RelayDock\build_hooks_x64
#>
param(
    [Parameter(Mandatory)][string]$ObsRoot,
    [Parameter(Mandatory)][string]$BuildDir,
    [string]$OutDir = (Join-Path $PSScriptRoot '..\output\performance'),
    [string[]]$Only = @(),
    [int]$Port = 19380,
    [int]$MeasureSec = 30,
    [int]$Width = 1920,
    [int]$Height = 1080,
    [int]$Fps = 60
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ObsTestHarness.psm1') -Force

$paths = Get-BuildPaths -BuildDir $BuildDir
$obsVersion = (Get-Item (Join-Path $ObsRoot 'bin\64bit\obs64.exe')).VersionInfo.ProductVersion
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$server = "rtmp://127.0.0.1:$Port/live"
$report = New-TestReport -Title "Performance on OBS $obsVersion, ${Width}x${Height} at $Fps FPS"
$cores = [Environment]::ProcessorCount
$rows = New-Object System.Collections.Generic.List[object]

function Test-Selected([string]$Name) { return ($Only.Count -eq 0) -or ($Only -contains $Name) }

# ---- The test picture: random noise ---------------------------------------------------------
$noise = Join-Path $OutDir 'noise.png'
if (-not (Test-Path $noise)) {
    Add-Type -AssemblyName System.Drawing
    $bitmap = New-Object System.Drawing.Bitmap 960, 540, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $area = New-Object System.Drawing.Rectangle 0, 0, 960, 540
    $data = $bitmap.LockBits($area, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bitmap.PixelFormat)
    $bytes = New-Object byte[] ($data.Stride * 540)
    (New-Object System.Random 20261004).NextBytes($bytes)   # A fixed seed, so every run uses the same picture
    [System.Runtime.InteropServices.Marshal]::Copy($bytes, 0, $data.Scan0, $bytes.Length)
    $bitmap.UnlockBits($data)
    $bitmap.Save($noise, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
}

$video = @{ op = 'obs_video'; base_width = $Width; base_height = $Height; output_width = $Width; output_height = $Height; fps = "$Fps" }
$picture = @{ op = 'add_moving_picture'; file = $noise }

function Add-Destination([string]$Ref, [string]$Provider = 'custom_rtmp', [hashtable]$Config = @{}) {
    $merged = @{ name = "Sink $Ref"; server_url = $server }
    if ($Provider -ne 'custom_rtmp') { $merged['server_id'] = 'custom' }
    foreach ($entry in $Config.GetEnumerator()) { $merged[$entry.Key] = $entry.Value }
    return @{ op = 'add_destination'; ref = $Ref; provider = $Provider; stream_key = "ok-perf-$Ref-7c3e91"; config = $merged }
}

# Measures an OBS process from outside: processor time over the interval, and memory at its end.
function Measure-Process($Process, [int]$Seconds) {
    $Process.Refresh()
    $before = $Process.TotalProcessorTime
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    Start-Sleep -Seconds $Seconds
    $Process.Refresh()
    $cpu = ($Process.TotalProcessorTime - $before).TotalSeconds / $watch.Elapsed.TotalSeconds / $cores * 100.0
    return [pscustomobject]@{ Cpu = $cpu; MemoryMb = $Process.WorkingSet64 / 1MB }
}

function Add-Row([string]$Case, [string]$What, $Cpu, $Memory, $Encoders, $RenderLag, $EncodeLag, $Dropped, [string]$Bitrates, [string]$Encoder) {
    $rows.Add([pscustomobject]@{
            Case = $Case; What = $What; Cpu = $Cpu; MemoryMb = $Memory; Encoders = $Encoders; RenderLag = $RenderLag
            EncodeLag = $EncodeLag; Dropped = $Dropped; Bitrates = $Bitrates; Encoder = $Encoder
        })
}

function Format-Number($Value, [int]$Decimals = 1) {
    if ($null -eq $Value) { return '-' }
    return ([double]$Value).ToString("F$Decimals", [System.Globalization.CultureInfo]::InvariantCulture)
}

# ---- Idle cases, measured from outside --------------------------------------------------------
function Invoke-Idle([string]$Name, [string]$What, [switch]$NoPlugin) {
    Initialize-ObsTestConfig -ObsRoot $ObsRoot -Reset | Out-Null
    $session = if ($NoPlugin) { Start-ObsTest -ObsRoot $ObsRoot -NoPlugin } else { Start-ObsTest -ObsRoot $ObsRoot -PluginRunDir $paths.PluginRunDir -Environment @{ RELAYDOCK_TEST_CREDENTIAL_PREFIX = "RelayDockTest-$([guid]::NewGuid())" } }
    try {
        Wait-ObsLogLine -Session $session -Pattern 'Finished loading|==== Startup complete' -TimeoutSec 60 | Out-Null
        Start-Sleep -Seconds 25   # Let OBS settle. It is busy for a while after it starts.
        $measured = Measure-Process $session.Process $MeasureSec
    } finally {
        $stopped = Stop-ObsTest -Session $session
    }
    Add-Row $Name $What $measured.Cpu $measured.MemoryMb 0 $null $null $null '' ''
    $report.Check("${Name}: OBS ran and closed cleanly", $stopped.Clean,
        "processor $(Format-Number $measured.Cpu 2) percent, memory $(Format-Number $measured.MemoryMb 0) MB")
    return $measured
}

# ---- Streaming cases ----------------------------------------------------------------------------
function Invoke-Streaming([string]$Name, [string]$What, [object[]]$Setup, [string[]]$Refs) {
    $dir = Join-Path $OutDir $Name
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $steps = New-Object System.Collections.Generic.List[object]
    $steps.Add(@{ op = 'clear' }); $steps.Add($video); $steps.Add($picture)
    foreach ($step in $Setup) { $steps.Add($step) }
    $steps.Add(@{ op = 'start_all' })
    foreach ($ref in $Refs) { $steps.Add(@{ op = 'wait_phase'; ref = $ref; phase = 'live'; timeout_sec = 40 }) }
    $steps.Add(@{ op = 'wait'; seconds = 10 })
    $steps.Add(@{ op = 'snapshot'; label = 'start' })
    $steps.Add(@{ op = 'wait'; seconds = $MeasureSec })
    $steps.Add(@{ op = 'snapshot'; label = 'end' })
    $steps.Add(@{ op = 'stop_all' })
    $steps.Add(@{ op = 'wait_idle'; timeout_sec = 40 })
    $steps.Add(@{ op = 'quit' })

    $sink = Start-RtmpSink -SinkPath $paths.Sink -ReportPath (Join-Path $dir 'sink-report.json') -Port $Port
    try {
        $run = Invoke-ObsScenario -ObsRoot $ObsRoot -PluginRunDir $paths.PluginRunDir -Scenario @{ steps = $steps.ToArray() } `
            -OutDir $dir -Name $Name -TimeoutSec ($MeasureSec + 150) -ResetConfig
    } finally {
        Stop-RtmpSink -Sink $sink | Out-Null
    }
    Add-ObsRunChecks -Report $report -Run $run -Label $Name
    if (-not ($run.Result -and $run.Result.snapshots.end)) { return }

    $a = $run.Result.snapshots.start
    $b = $run.Result.snapshots.end
    $renderTotal = $b.obs.render_total_frames - $a.obs.render_total_frames
    $renderLag = if ($renderTotal -gt 0) { 100.0 * ($b.obs.render_lagged_frames - $a.obs.render_lagged_frames) / $renderTotal } else { $null }
    $encodeTotal = $b.obs.encode_total_frames - $a.obs.encode_total_frames
    $encodeLag = if ($encodeTotal -gt 0) { 100.0 * ($b.obs.encode_skipped_frames - $a.obs.encode_skipped_frames) / $encodeTotal } else { $null }

    $dropped = 0; $frames = 0; $bitrates = @(); $allLive = $true; $encoderName = ''
    foreach ($ref in $Refs) {
        $da = $a.destinations.$ref; $db = $b.destinations.$ref
        if ($db.phase -ne 'live' -or $db.reconnects -ne 0) { $allLive = $false }
        $dropped += $db.stats.dropped_frames - $da.stats.dropped_frames
        $frames += $db.stats.total_frames - $da.stats.total_frames
        $seconds = ($b.time_ms - $a.time_ms) / 1000.0
        $bitrates += [int](($db.stats.total_bytes - $da.stats.total_bytes) * 8 / 1000 / $seconds)
        $encoderName = $db.effective.encoder
    }
    $droppedPercent = if ($frames -gt 0) { 100.0 * $dropped / $frames } else { $null }
    Add-Row $Name $What $b.obs.cpu_percent $b.obs.memory_mb $b.encoders.video_live $renderLag $encodeLag $droppedPercent ($bitrates -join ', ') $encoderName
    $report.Check("${Name}: every destination stayed live for the whole measurement", $allLive)
}

if (Test-Selected 'baseline') { Write-Host ''; Write-Host 'baseline: OBS alone'; $baseline = Invoke-Idle 'baseline' 'OBS alone, idle' -NoPlugin }
if (Test-Selected 'idle') {
    Write-Host ''; Write-Host 'idle: RelayDock loaded, nothing streaming'
    $idle = Invoke-Idle 'idle' 'OBS with RelayDock, idle'
    if ($baseline -and $idle) {
        Write-Host ("  RelayDock adds {0} percent processor and {1} MB memory while idle." -f (Format-Number ($idle.Cpu - $baseline.Cpu) 2), (Format-Number ($idle.MemoryMb - $baseline.MemoryMb) 0))
    }
}

$balanced = @{ op = 'set'; performance_mode = 'balanced'; optimizer = @{ mode = 'off' } }
$custom = @{ op = 'set'; performance_mode = 'custom'; optimizer = @{ mode = 'off' } }
$names = @('a', 'b', 'c', 'd')
$caseNames = @('one', 'two', 'three', 'four')
for ($count = 1; $count -le 4; $count++) {
    $case = $caseNames[$count - 1]
    if (-not (Test-Selected $case)) { continue }
    Write-Host ''; Write-Host "${case}: $count destination(s), one shared encoder"
    $refs = $names[0..($count - 1)]
    $setup = @($balanced) + @($refs | ForEach-Object { Add-Destination $_ })
    Invoke-Streaming $case "$count destination(s), shared encoder" $setup $refs
}

if (Test-Selected 'four-separate') {
    Write-Host ''; Write-Host 'four-separate: four destinations, four encoders'
    $setup = @($custom) + @(0..3 | ForEach-Object { Add-Destination $names[$_] 'custom_rtmp' @{ video = @{ bitrate_kbps = 6000 - 500 * $_ } } })
    Invoke-Streaming 'four-separate' '4 destinations, 4 encoders' $setup $names
}

if (Test-Selected 'mixed') {
    Write-Host ''; Write-Host 'mixed: two horizontal and one vertical'
    $setup = @($balanced, (Add-Destination 'a'), (Add-Destination 'b'), (Add-Destination 'c' 'custom_rtmp' @{ video = @{ orientation = 'vertical' } }))
    Invoke-Streaming 'mixed' '2 horizontal and 1 vertical' $setup @('a', 'b', 'c')
}

foreach ($mode in 'potato', 'quality') {
    if (-not (Test-Selected $mode)) { continue }
    Write-Host ''; Write-Host "${mode}: three destinations in $mode mode"
    $setup = @(@{ op = 'set'; performance_mode = $mode; optimizer = @{ mode = 'off' } },
        (Add-Destination 'a' 'twitch'), (Add-Destination 'b' 'youtube'), (Add-Destination 'c'))
    Invoke-Streaming $mode "3 destinations, $mode mode" $setup @('a', 'b', 'c')
}

$x264 = @{ video = @{ encoder = 'obs_x264' }; locks = @{ encoder = $true } }
if (Test-Selected 'x264-shared') {
    Write-Host ''; Write-Host 'x264-shared: three destinations on one software encoder'
    $setup = @($balanced) + @('a', 'b', 'c' | ForEach-Object { Add-Destination $_ 'custom_rtmp' $x264 })
    Invoke-Streaming 'x264-shared' '3 destinations, x264, shared' $setup @('a', 'b', 'c')
}
if (Test-Selected 'x264-separate') {
    Write-Host ''; Write-Host 'x264-separate: three destinations on three software encoders'
    $setup = @($custom) + @(0..2 | ForEach-Object {
            Add-Destination $names[$_] 'custom_rtmp' @{ video = @{ encoder = 'obs_x264'; bitrate_kbps = 6000 - 500 * $_ }; locks = @{ encoder = $true } } })
    Invoke-Streaming 'x264-separate' '3 destinations, x264, separate' $setup @('a', 'b', 'c')
}

# ---- Report ----------------------------------------------------------------------------------
$system = Get-CimInstance Win32_Processor | Select-Object -First 1
$gpus = (Get-CimInstance Win32_VideoController | ForEach-Object { $_.Name }) -join ', '
$memoryGb = [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('# RelayDock performance measurements')
$lines.Add('')
$lines.Add("Measured on $(Get-Date -Format 'yyyy-MM-dd') with ``tests/integration/Test-Performance.ps1``. Every number comes from a real run. Nothing is estimated.")
$lines.Add('')
$lines.Add("- PC: $($system.Name.Trim()), $($system.NumberOfCores) cores, $cores threads, $memoryGb GB memory")
$lines.Add("- Graphics: $gpus")
$lines.Add("- OBS Studio $obsVersion, canvas ${Width}x${Height} at $Fps FPS")
$lines.Add('- Picture: scrolling random noise, which is hard to compress')
$lines.Add("- Each streaming case: 10 seconds of warm-up, then $MeasureSec seconds measured")
$lines.Add('- Streams went to a test server on the same PC')
$lines.Add('')
$lines.Add('| Case | OBS processor | OBS memory | Video encoders | Rendering lag | Encoder lag | Dropped frames | Bitrate sent (Kbps) | Encoder |')
$lines.Add('| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |')
foreach ($row in $rows) {
    $lines.Add(("| {0} | {1}% | {2} MB | {3} | {4} | {5} | {6} | {7} | {8} |" -f $row.What, (Format-Number $row.Cpu 1), (Format-Number $row.MemoryMb 0),
            $row.Encoders, $(if ($null -eq $row.RenderLag) { '-' } else { (Format-Number $row.RenderLag 1) + '%' }),
            $(if ($null -eq $row.EncodeLag) { '-' } else { (Format-Number $row.EncodeLag 1) + '%' }),
            $(if ($null -eq $row.Dropped) { '-' } else { (Format-Number $row.Dropped 1) + '%' }), $row.Bitrates, $row.Encoder))
}
$lines.Add('')
$lines.Add('OBS processor is the share of all processor threads that the OBS process used. Rendering lag, encoder lag and dropped frames are shares of the frames in the measured interval.')
$reportPath = Join-Path $OutDir 'performance-results.md'
[System.IO.File]::WriteAllLines($reportPath, $lines, (New-Object System.Text.UTF8Encoding($false)))
$rows | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $OutDir 'performance-results.json') -Encoding UTF8

Write-Host ''
$lines | Where-Object { $_ -match '^\|' } | ForEach-Object { Write-Host $_ }
Write-Host ''
Write-Host "Report: $reportPath"
exit $report.Finish()
