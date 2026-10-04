# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Checks the vertical canvas in a real OBS: what the picture looks like and how it streams.

.DESCRIPTION
  picture     Puts coloured rectangles of known size in the OBS scene, renders the vertical
              canvas to an image and measures where they land. This proves a 16:9 scene is
              centre-cropped or fitted and never stretched: a square stays a square.
  streaming   Streams 16:9 and 9:16 at the same time, then checks sizes at the server, encoder
              sharing between two vertical destinations, and that the vertical canvas leaves the
              OBS render loop when no destination uses it.
  scaled      A vertical destination at 720x1280 and 30 FPS from a 1080x1920, 60 FPS canvas.
  shutdown    OBS closes while a vertical destination is live.

Needs a build with RELAYDOCK_TEST_HOOKS=ON (preset windows-hooks-x64).

.EXAMPLE
.\Test-Vertical.ps1 -ObsRoot C:\obs-32.2.2 -BuildDir C:\RelayDock\build_hooks_x64
#>
param(
    [Parameter(Mandatory)][string]$ObsRoot,
    [Parameter(Mandatory)][string]$BuildDir,
    [string]$OutDir = '',
    [string[]]$Only = @(),
    [int]$Port = 19350
)

$ErrorActionPreference = 'Stop'
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot '..\output\vertical' }
Import-Module (Join-Path $PSScriptRoot 'ObsTestHarness.psm1') -Force

$paths = Get-BuildPaths -BuildDir $BuildDir
$obsVersion = (Get-Item (Join-Path $ObsRoot 'bin\64bit\obs64.exe')).VersionInfo.ProductVersion
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$server = "rtmp://127.0.0.1:$Port/live"
$report = New-TestReport -Title "Vertical canvas on OBS $obsVersion"
$video = @{ op = 'obs_video'; base_width = 1920; base_height = 1080; output_width = 1280; output_height = 720; fps = '60' }

function Test-Selected([string]$Name) { return ($Only.Count -eq 0) -or ($Only -contains $Name) }

function Add-Destination([string]$Ref, [string]$Key, [hashtable]$Config = @{}) {
    $merged = @{ name = "Sink $Ref"; server_url = $server }
    foreach ($entry in $Config.GetEnumerator()) { $merged[$entry.Key] = $entry.Value }
    return @{ op = 'add_destination'; ref = $Ref; provider = 'custom_rtmp'; stream_key = $Key; config = $merged }
}

function Invoke-WithSink([string]$Name, [hashtable]$Scenario, [int]$TimeoutSec = 180) {
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

# True when a measured box matches the expected one within a few pixels.
function Test-Box($Box, [double]$X, [double]$Y, [double]$W, [double]$H, [double]$Tolerance = 4) {
    if ($null -eq $Box -or $Box.pixels -le 0) { return $false }
    return ([math]::Abs($Box.x - $X) -le $Tolerance) -and ([math]::Abs($Box.y - $Y) -le $Tolerance) -and
           ([math]::Abs($Box.width - $W) -le $Tolerance) -and ([math]::Abs($Box.height - $H) -le $Tolerance)
}

function Format-Box($Box) {
    if ($null -eq $Box -or $Box.pixels -le 0) { return 'not visible' }
    return "x=$($Box.x) y=$($Box.y) $($Box.width)x$($Box.height)"
}

# ---------------------------------------------------------------------------------------------
if (Test-Selected 'picture') {
    Write-Host ''
    Write-Host 'picture: what the vertical canvas shows'
    $dir = Join-Path $OutDir 'picture'
    New-Item -ItemType Directory -Force -Path $dir | Out-Null

    $programFit = @(@{ id = [guid]::NewGuid().ToString(); name = 'Fit'; items = @(
                @{ id = [guid]::NewGuid().ToString(); kind = 'program'; x = 0; y = 0; width = 1080; height = 1920; fit = 'fit' }) })
    $programFill = @(@{ id = [guid]::NewGuid().ToString(); name = 'Fill'; items = @(
                @{ id = [guid]::NewGuid().ToString(); kind = 'program'; x = 0; y = 0; width = 1080; height = 1920; fit = 'fill' }) })
    $twoItems = @(@{ id = [guid]::NewGuid().ToString(); name = 'Two'; items = @(
                @{ id = [guid]::NewGuid().ToString(); kind = 'program'; x = 0; y = 0; width = 1080; height = 960; fit = 'fill' },
                @{ id = [guid]::NewGuid().ToString(); kind = 'source'; source_name = 'RD yellow'; x = 0; y = 960; width = 1080; height = 960; fit = 'fit' }) })
    $fillBox = @(@{ id = [guid]::NewGuid().ToString(); name = 'FillBox'; items = @(
                @{ id = [guid]::NewGuid().ToString(); kind = 'source'; source_name = 'RD yellow'; x = 60; y = 1000; width = 400; height = 800; fit = 'fill' }) })
    $cropped = @(@{ id = [guid]::NewGuid().ToString(); name = 'Crop'; items = @(
                @{ id = [guid]::NewGuid().ToString(); kind = 'program'; x = 0; y = 0; width = 1080; height = 1920; fit = 'fit'
                   crop_left = 656; crop_right = 656 }) })
    $missing = @(@{ id = [guid]::NewGuid().ToString(); name = 'Missing'; items = @(
                @{ id = [guid]::NewGuid().ToString(); kind = 'program'; x = 0; y = 0; width = 1080; height = 1920; fit = 'fill' },
                @{ id = [guid]::NewGuid().ToString(); kind = 'source'; source_name = 'No such source'; x = 0; y = 0; width = 200; height = 200; fit = 'fit' }) })

    $find = @{ red = '#FF0000'; green = '#00FF00'; blue = '#0000FF'; yellow = '#FFFF00' }
    $scenario = @{ steps = @(
            @{ op = 'clear' }, $video, @{ op = 'clear_scene' },
            # A 16:9 picture with known geometry: blue background, a red square in the middle,
            # a green square near the top left, and a yellow 2:1 bar parked outside the canvas.
            @{ op = 'add_color_source'; name = 'RD blue'; color = '#0000FF'; width = 1920; height = 1080; x = 0; y = 0 },
            @{ op = 'add_color_source'; name = 'RD red'; color = '#FF0000'; width = 400; height = 400; x = 760; y = 340 },
            @{ op = 'add_color_source'; name = 'RD green'; color = '#00FF00'; width = 200; height = 200; x = 100; y = 100 },
            @{ op = 'add_color_source'; name = 'RD yellow'; color = '#FFFF00'; width = 400; height = 200; x = 5000; y = 0 },
            @{ op = 'wait'; seconds = 2 },
            # What the first release saved for somebody who never opened the layout editor:
            # Program filling the canvas. Loaded from a scene collection, it becomes a fit.
            @{ op = 'vertical_load_saved'; saved = @{ version = 1; canvas_width = 1080; canvas_height = 1920; layouts = @(
                        @{ id = [guid]::NewGuid().ToString(); name = 'Default'; items = @(
                                @{ id = [guid]::NewGuid().ToString(); kind = 'program'; x = 0; y = 0; width = 1080; height = 1920; fit = 'fill' }) }) } },
            @{ op = 'wait'; seconds = 1 },
            @{ op = 'render_vertical'; label = 'upgraded'; find = $find; file = (Join-Path $dir 'upgraded.png') },
            # The layout RelayDock makes by itself, before anybody arranged anything.
            @{ op = 'vertical_layout'; layouts = @() },
            @{ op = 'wait'; seconds = 1 },
            @{ op = 'render_vertical'; label = 'default_layout'; find = $find; file = (Join-Path $dir 'default-layout.png') },
            @{ op = 'vertical_layout'; layouts = $programFill },
            @{ op = 'wait'; seconds = 1 },
            @{ op = 'render_vertical'; label = 'default_fill'; find = $find; file = (Join-Path $dir 'program-fill.png') },
            @{ op = 'vertical_layout'; layouts = $programFit },
            @{ op = 'wait'; seconds = 1 },
            @{ op = 'render_vertical'; label = 'program_fit'; find = $find; file = (Join-Path $dir 'program-fit.png') },
            @{ op = 'vertical_layout'; layouts = $twoItems },
            @{ op = 'wait'; seconds = 1 },
            @{ op = 'render_vertical'; label = 'two_items'; find = $find; file = (Join-Path $dir 'two-items.png') },
            @{ op = 'vertical_layout'; layouts = $fillBox },
            @{ op = 'wait'; seconds = 1 },
            @{ op = 'render_vertical'; label = 'fill_box'; find = $find; file = (Join-Path $dir 'fill-box.png') },
            @{ op = 'vertical_layout'; layouts = $cropped },
            @{ op = 'wait'; seconds = 1 },
            @{ op = 'render_vertical'; label = 'cropped'; find = $find; file = (Join-Path $dir 'cropped.png') },
            @{ op = 'vertical_layout'; layouts = $missing },
            @{ op = 'wait'; seconds = 1 },
            @{ op = 'render_vertical'; label = 'missing'; find = $find },
            @{ op = 'clear_scene' },
            @{ op = 'quit' }) }

    $run = Invoke-ObsScenario -ObsRoot $ObsRoot -PluginRunDir $paths.PluginRunDir -Scenario $scenario -OutDir $dir -Name 'picture' -TimeoutSec 120
    Add-ObsRunChecks -Report $report -Run $run -Label 'picture'

    if ($run.Result -and $run.Result.renders.missing) {
        $r = $run.Result.renders

        # The default layout shows all of the scene: a 16:9 band, 1080x607.5, in the middle.
        $c = $r.default_layout.colors
        $report.Check('picture: the canvas renders at 1080x1920', ($r.default_layout.width -eq 1080 -and $r.default_layout.height -eq 1920))
        $report.Check('picture: a new layout shows the whole scene, not a cropped part of it', (Test-Box $c.blue 0 656 1080 608 2), (Format-Box $c.blue))
        $report.Check('picture: in a new layout the square near the left edge is visible', (Test-Box $c.green 56.25 712.5 112.5 112.5), (Format-Box $c.green))

        $c = $r.upgraded.colors
        $report.Check('picture: the untouched cropped layout of the first release now shows the whole scene',
            ((Test-Box $c.blue 0 656 1080 608 2) -and $c.green.pixels -gt 0), (Format-Box $c.blue))
        $report.Check('picture: the OBS log says that the layout changed and how to crop again',
            ($run.Log -match 'Vertical layouts: The layout "Default" was the standard layout of an earlier version'))

        # Program with Fill covers the 1080x1920 canvas. Scale 1920/1080 = 1.778.
        $c = $r.default_fill.colors
        $report.Check('picture: Fill keeps a square a square', ($c.red.pixels -gt 0 -and [math]::Abs($c.red.width - $c.red.height) -le 2), (Format-Box $c.red))
        $report.Check('picture: Fill scales the scene by canvas height over scene height', (Test-Box $c.red 184.9 604.4 711.1 711.1), (Format-Box $c.red))
        $report.Check('picture: Fill centres the scene, so the middle stays in the middle',
            ([math]::Abs(($c.red.x + $c.red.width / 2) - 540) -le 3 -and [math]::Abs(($c.red.y + $c.red.height / 2) - 960) -le 3))
        $report.Check('picture: Fill crops what does not fit, here the square near the left edge', ($c.green.pixels -eq 0), (Format-Box $c.green))
        $report.Check('picture: Fill covers the whole canvas', (Test-Box $c.blue 0 0 1080 1920 1), (Format-Box $c.blue))

        # Program with Fit: the whole 16:9 picture, 1080x607.5, centred.
        $c = $r.program_fit.colors
        $report.Check('picture: Fit shows the whole scene as a 16:9 band across the canvas', (Test-Box $c.blue 0 656 1080 608 2), (Format-Box $c.blue))
        $report.Check('picture: Fit keeps a square a square', ([math]::Abs($c.red.width - $c.red.height) -le 2), (Format-Box $c.red))
        $report.Check('picture: Fit scales the scene by canvas width over scene width', (Test-Box $c.red 427.5 847.5 225 225), (Format-Box $c.red))
        $report.Check('picture: Fit keeps everything, including the square near the left edge', (Test-Box $c.green 56.25 712.5 112.5 112.5), (Format-Box $c.green))

        # Program in the top half, a 2:1 source fitted into the bottom half.
        $c = $r.two_items.colors
        $report.Check('picture: a source placed in the bottom half lands there with its 2:1 shape intact',
            (Test-Box $c.yellow 0 1170 1080 540 2), (Format-Box $c.yellow))
        $report.Check('picture: Program in the top half stays in the top half', ($c.blue.pixels -gt 0 -and ($c.blue.y + $c.blue.height) -le 961), (Format-Box $c.blue))
        $report.Check('picture: the square is still square inside the top half', ([math]::Abs($c.red.width - $c.red.height) -le 2 -and $c.red.pixels -gt 0), (Format-Box $c.red))

        # A 2:1 source filling a tall 400x800 box is cropped to the box.
        $c = $r.fill_box.colors
        $report.Check('picture: Fill inside a box covers exactly that box', (Test-Box $c.yellow 60 1000 400 800 1), (Format-Box $c.yellow))

        # Crop 656 px from both sides of the scene, then Fit: a 608x1080 strip, which is 9:16.
        $c = $r.cropped.colors
        $report.Check('picture: crop cuts the scene before it is placed', ($c.green.pixels -eq 0), (Format-Box $c.green))
        $report.Check('picture: the cropped strip fits the canvas and the square stays square',
            ((Test-Box $c.red 184.7 603.9 710.5 710.5 5) -and [math]::Abs($c.red.width - $c.red.height) -le 2), (Format-Box $c.red))

        $report.Check('picture: an item whose source does not exist is skipped and reported', ($r.missing.missing_items -eq 1))
        $report.Check('picture: the rest of that layout still renders', ($r.missing.colors.blue.pixels -gt 0))
        $report.Check('picture: images were saved for review', (Test-Path (Join-Path $dir 'default-layout.png')))
    }
}

# ---------------------------------------------------------------------------------------------
if (Test-Selected 'streaming') {
    Write-Host ''
    Write-Host 'streaming: 16:9 and 9:16 at the same time'
    $keys = @{ wide = 'ok-vert-wide-3a91c7e2'; tall = 'ok-vert-tall-d05b68f4'; tall2 = 'ok-vert-tall2-7e2c14a9' }
    $vertical = @{ video = @{ orientation = 'vertical' } }
    $outcome = Invoke-WithSink 'streaming' @{ steps = @(
            @{ op = 'clear' }, $video, @{ op = 'set'; performance_mode = 'balanced' },
            (Add-Destination 'wide' $keys.wide),
            (Add-Destination 'tall' $keys.tall $vertical),
            (Add-Destination 'tall2' $keys.tall2 $vertical),
            @{ op = 'start_all' },
            @{ op = 'wait_phase'; ref = 'wide'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait_phase'; ref = 'tall'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait_phase'; ref = 'tall2'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait'; seconds = 8 },
            @{ op = 'snapshot'; label = 'all_live' },
            @{ op = 'stop'; ref = 'tall' },
            @{ op = 'stop'; ref = 'tall2' },
            @{ op = 'wait_phase'; ref = 'tall'; phase = 'idle'; timeout_sec = 30 },
            @{ op = 'wait_phase'; ref = 'tall2'; phase = 'idle'; timeout_sec = 30 },
            @{ op = 'wait'; seconds = 4 },
            @{ op = 'snapshot'; label = 'vertical_stopped' },
            @{ op = 'start'; ref = 'tall' },
            @{ op = 'wait_phase'; ref = 'tall'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait'; seconds = 4 },
            @{ op = 'snapshot'; label = 'vertical_again' },
            @{ op = 'stop_all' },
            @{ op = 'wait_idle'; timeout_sec = 40 },
            @{ op = 'wait'; seconds = 2 },
            @{ op = 'snapshot'; label = 'all_stopped' },
            @{ op = 'quit' }) } 240
    $run = $outcome.Run
    Add-ObsRunChecks -Report $report -Run $run -Label 'streaming' -Secrets $keys.Values

    if ($run.Result -and $run.Result.snapshots.all_stopped) {
        $s = $run.Result.snapshots
        $d = $s.all_live.destinations
        $streams = $outcome.Sink.streams
        $report.Check('streaming: the horizontal and both vertical destinations are live together',
            ($d.wide.phase -eq 'live' -and $d.tall.phase -eq 'live' -and $d.tall2.phase -eq 'live'))
        $report.Check('streaming: the horizontal stream arrives as 1280x720',
            ([int]$streams.($keys.wide).metadata.width -eq 1280 -and [int]$streams.($keys.wide).metadata.height -eq 720),
            "$([int]$streams.($keys.wide).metadata.width)x$([int]$streams.($keys.wide).metadata.height)")
        $report.Check('streaming: the vertical stream arrives as 1080x1920',
            ([int]$streams.($keys.tall).metadata.width -eq 1080 -and [int]$streams.($keys.tall).metadata.height -eq 1920),
            "$([int]$streams.($keys.tall).metadata.width)x$([int]$streams.($keys.tall).metadata.height)")
        $report.Check('streaming: both sizes carry video', ($streams.($keys.wide).video_bytes -gt 100000 -and $streams.($keys.tall).video_bytes -gt 100000),
            "$($streams.($keys.wide).video_bytes) and $($streams.($keys.tall).video_bytes) bytes")
        $report.Check('streaming: horizontal and vertical never share an encoder', ($d.wide.video_encoder -ne $d.tall.video_encoder))
        $report.Check('streaming: two vertical destinations with the same settings share one encoder',
            ($d.tall.video_encoder -eq $d.tall2.video_encoder -and $s.all_live.encoders.video_live -eq 2), "$($s.all_live.encoders.video_live) video encoders for 3 destinations")
        $report.Check('streaming: stopping the vertical destinations leaves the horizontal one live',
            ($s.vertical_stopped.destinations.wide.phase -eq 'live' -and $s.vertical_stopped.encoders.video_live -eq 1))
        $report.Check('streaming: the horizontal stream ran in one uninterrupted session', ($streams.($keys.wide).sessions -eq 1))
        $joined = @($run.Log -split "`n" | Where-Object { $_ -match 'Vertical canvas .* joined the OBS render loop' }).Count
        $left = @($run.Log -split "`n" | Where-Object { $_ -match 'Vertical canvas left the OBS render loop' }).Count
        $report.Check('streaming: the vertical canvas renders only while a destination uses it', ($joined -eq 2 -and $left -eq 2), "joined $joined times, left $left times")
        $report.Check('streaming: a vertical destination starts again after the canvas was released',
            ($s.vertical_again.destinations.tall.phase -eq 'live' -and $streams.($keys.tall).sessions -eq 2))
        $report.Check('streaming: everything stops and every encoder is freed', ($s.all_stopped.encoders.video_live -eq 0 -and -not $run.Result.snapshots.all_stopped.destinations.tall.has_output))
    }
}

# ---------------------------------------------------------------------------------------------
if (Test-Selected 'scaled') {
    Write-Host ''
    Write-Host 'scaled: a smaller, slower vertical stream from the same canvas'
    $key = 'ok-vert-small-b84f2d07'
    $outcome = Invoke-WithSink 'scaled' @{ steps = @(
            @{ op = 'clear' }, $video, @{ op = 'set'; performance_mode = 'custom' },
            (Add-Destination 'small' $key @{ video = @{ orientation = 'vertical'; width = 720; height = 1280; fps = 30; bitrate_kbps = 2500 } }),
            @{ op = 'start'; ref = 'small' },
            @{ op = 'wait_phase'; ref = 'small'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait'; seconds = 8 },
            @{ op = 'snapshot'; label = 'live' },
            @{ op = 'stop_all' },
            @{ op = 'wait_idle'; timeout_sec = 40 },
            @{ op = 'quit' }) }
    Add-ObsRunChecks -Report $report -Run $outcome.Run -Label 'scaled' -Secrets @($key)
    $stream = $outcome.Sink.streams.$key
    if ($null -ne $stream) {
        $report.Check('scaled: the stream arrives as 720x1280', ([int]$stream.metadata.width -eq 720 -and [int]$stream.metadata.height -eq 1280),
            "$([int]$stream.metadata.width)x$([int]$stream.metadata.height)")
        $report.Check('scaled: the stream arrives at 30 FPS', ([math]::Abs([double]$stream.measured_fps - 30) -lt 1.5), "$($stream.measured_fps) FPS measured")
    } else {
        $report.Check('scaled: the server received the stream', $false)
    }
}

# ---------------------------------------------------------------------------------------------
if (Test-Selected 'shutdown') {
    Write-Host ''
    Write-Host 'shutdown: OBS closes while a vertical destination is live'
    $keys = @{ wide = 'ok-vquit-wide-61d9e3b0'; tall = 'ok-vquit-tall-0c7a52f8' }
    $outcome = Invoke-WithSink 'shutdown' @{ steps = @(
            @{ op = 'clear' }, $video, @{ op = 'set'; performance_mode = 'balanced' },
            (Add-Destination 'wide' $keys.wide),
            (Add-Destination 'tall' $keys.tall @{ video = @{ orientation = 'vertical' } }),
            @{ op = 'start_all' },
            @{ op = 'wait_phase'; ref = 'wide'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait_phase'; ref = 'tall'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait'; seconds = 4 },
            @{ op = 'quit' }) }
    Add-ObsRunChecks -Report $report -Run $outcome.Run -Label 'shutdown' -Secrets $keys.Values
    $report.Check('shutdown: RelayDock stopped both outputs', ($outcome.Run.Log -match '\[RelayDock\] Stopped 2 output\(s\) for shutdown'))
    $report.Check('shutdown: RelayDock unloaded', ($outcome.Run.Log -match '\[RelayDock\] Unloaded\.'))
}

$leftover = @(cmdkey /list | Select-String 'RelayDockTest-').Count
$report.Check('No test credentials remain in Windows Credential Manager', ($leftover -eq 0), "$leftover entries")
exit $report.Finish()
