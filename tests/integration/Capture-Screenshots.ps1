# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Makes the pictures in the README and the docs from the real plugin running in a real OBS.

.DESCRIPTION
Starts a portable OBS with the test build, sets up a scene of coloured rectangles and a few
destinations, streams them to the test server on this PC and saves pictures of the windows.

Nothing in the pictures is drawn by hand or edited afterwards. The destinations carry platform
names but stream to 127.0.0.1, which the cards show. The stream keys are made up.

Windows provides each picture the way it composes the window, so the OBS preview and the
vertical layout preview are in them.

Needs a build with RELAYDOCK_TEST_HOOKS=ON (preset windows-hooks-x64).

.EXAMPLE
.\Capture-Screenshots.ps1 -ObsRoot C:\obs-32.2.2 -BuildDir C:\RelayDock\build_hooks_x64
#>
param(
    [Parameter(Mandatory)][string]$ObsRoot,
    [Parameter(Mandatory)][string]$BuildDir,
    [string]$OutDir = (Join-Path $PSScriptRoot '..\..\docs\screenshots'),
    [string]$WorkDir = (Join-Path $PSScriptRoot '..\output\screenshots'),
    [int]$Port = 19400
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ObsTestHarness.psm1') -Force

$paths = Get-BuildPaths -BuildDir $BuildDir
New-Item -ItemType Directory -Force -Path $OutDir, $WorkDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$WorkDir = (Resolve-Path $WorkDir).Path
$server = "rtmp://127.0.0.1:$Port/live"
$report = New-TestReport -Title 'Screenshots'

function Invoke-Capture([string]$Name, [object[]]$Steps, [int]$TimeoutSec = 240) {
    $dir = Join-Path $WorkDir $Name
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $sink = Start-RtmpSink -SinkPath $paths.Sink -ReportPath (Join-Path $dir 'sink-report.json') -Port $Port
    try {
        $run = Invoke-ObsScenario -ObsRoot $ObsRoot -PluginRunDir $paths.PluginRunDir -Scenario @{ steps = $Steps } -OutDir $dir `
            -Name $Name -TimeoutSec $TimeoutSec -Visible -ResetConfig
    } finally {
        Stop-RtmpSink -Sink $sink | Out-Null
    }
    Add-ObsRunChecks -Report $report -Run $run -Label $Name
    return $dir
}

function Publish-Picture([string]$From, [string]$Name) {
    $ok = Test-Path $From
    if ($ok) { Copy-Item -LiteralPath $From -Destination (Join-Path $OutDir $Name) -Force }
    $size = if ($ok) { "$([math]::Round((Get-Item $From).Length / 1KB)) KB" } else { 'missing' }
    $report.Check("picture $Name", $ok, $size)
}

# A scene of plain rectangles: a background, a main picture, a camera and a side panel.
$scene = @(
    @{ op = 'clear' },
    @{ op = 'obs_video'; base_width = 1280; base_height = 720; output_width = 1280; output_height = 720; fps = '30' },
    @{ op = 'accept_legal' },
    @{ op = 'add_color_source'; name = 'Background'; color = '#1F2A44'; width = 1280; height = 720 },
    @{ op = 'add_color_source'; name = 'Game'; color = '#3B6E8F'; width = 860; height = 484; x = 40; y = 40 },
    @{ op = 'add_color_source'; name = 'Side panel'; color = '#E0B96A'; width = 320; height = 400; x = 920; y = 40 },
    @{ op = 'add_color_source'; name = 'Camera'; color = '#B56576'; width = 320; height = 200; x = 920; y = 480 }
)

$layoutId = [guid]::NewGuid().ToString()
$layout = @(@{ id = $layoutId; name = 'Game and camera'; items = @(
            @{ id = [guid]::NewGuid().ToString(); kind = 'program'; x = 0; y = 120; width = 1080; height = 608; fit = 'fit' },
            @{ id = [guid]::NewGuid().ToString(); kind = 'source'; source_name = 'Camera'; x = 90; y = 860; width = 900; height = 900; fit = 'fill' }) })

function Add-Platform([string]$Ref, [string]$Provider, [string]$Name, [hashtable]$Extra = @{}) {
    $config = @{ name = $Name; server_url = $server }
    if ($Provider -ne 'custom_rtmp' -and $Provider -ne 'tiktok') { $config['server_id'] = 'custom' }
    foreach ($entry in $Extra.GetEnumerator()) { $config[$entry.Key] = $entry.Value }
    return @{ op = 'add_destination'; ref = $Ref; provider = $Provider; stream_key = "ok-shot-$Ref-3f9a6d"; config = $config }
}

# ---- Main run: the dock in OBS, the editor, the layout editor, preflight, settings, themes ----
Write-Host ''
Write-Host 'main: dock, editor, vertical layout, preflight, settings, themes'
$main = $scene + @(
    @{ op = 'vertical_layout'; layouts = $layout },
    @{ op = 'set'; performance_mode = 'balanced'; upload_kbps = 40000 },
    (Add-Platform 'twitch' 'twitch' 'Twitch'),
    (Add-Platform 'youtube' 'youtube' 'YouTube'),
    (Add-Platform 'tiktok' 'tiktok' 'TikTok'),
    (Add-Platform 'backup' 'custom_rtmp' 'Backup server' @{ enabled = $false }),
    @{ op = 'ui_main_window'; width = 1400; height = 820 },
    @{ op = 'ui_show_dock'; area = 'right'; width = 400 },
    @{ op = 'wait'; seconds = 2 },
    @{ op = 'start_all' },
    @{ op = 'wait_phase'; ref = 'twitch'; phase = 'live'; timeout_sec = 40 },
    @{ op = 'wait_phase'; ref = 'youtube'; phase = 'live'; timeout_sec = 40 },
    @{ op = 'wait_phase'; ref = 'tiktok'; phase = 'live'; timeout_sec = 40 },
    @{ op = 'wait'; seconds = 14 },
    @{ op = 'ui_capture'; target = 'main'; file = 'obs-with-dock.png' },
    @{ op = 'ui_grab'; target = 'dock'; file = 'dock-live.png' },

    @{ op = 'ui_open'; what = 'add'; provider = 'youtube' },
    @{ op = 'ui_wait'; target = 'dialog' },
    @{ op = 'wait'; seconds = 1 },
    @{ op = 'ui_text'; name = 'stream key'; value = 'ok-shot-editor-not-a-real-key' },
    @{ op = 'wait'; seconds = 1 },
    @{ op = 'ui_capture'; target = 'dialog'; file = 'editor.png' },
    @{ op = 'ui_click'; text = 'Cancel' },
    @{ op = 'ui_wait'; target = 'dialog'; present = $false },

    @{ op = 'ui_open'; what = 'vertical' },
    @{ op = 'ui_wait'; target = 'dialog' },
    @{ op = 'wait'; seconds = 2 },
    @{ op = 'ui_select_row'; name = 'Layout items'; value = 'Camera' },
    @{ op = 'wait'; seconds = 1.5 },
    @{ op = 'ui_capture'; target = 'dialog'; file = 'vertical-editor.png' },
    @{ op = 'ui_click'; text = 'Cancel' },
    @{ op = 'ui_wait'; target = 'dialog'; present = $false },

    @{ op = 'ui_open'; what = 'settings'; page = 'performance' },
    @{ op = 'ui_wait'; target = 'dialog' },
    @{ op = 'wait'; seconds = 2.5 },
    @{ op = 'ui_capture'; target = 'dialog'; file = 'settings-performance.png' },
    @{ op = 'ui_open'; what = 'settings'; page = 'appearance' },
    @{ op = 'wait'; seconds = 1 },
    @{ op = 'ui_combo'; name = 'Theme'; value = 'Light' },
    @{ op = 'wait'; seconds = 2 },
    @{ op = 'ui_grab'; target = 'dock'; file = 'dock-light.png' },
    @{ op = 'ui_combo'; name = 'Theme'; value = 'Follow OBS' },
    @{ op = 'wait'; seconds = 1 },
    @{ op = 'ui_close' },
    @{ op = 'ui_wait'; target = 'dialog'; present = $false },

    @{ op = 'stop_all' },
    @{ op = 'wait_idle'; timeout_sec = 40 },
    @{ op = 'add_destination'; ref = 'facebook'; provider = 'facebook'; config = @{ name = 'Facebook' } },
    @{ op = 'wait'; seconds = 1 },
    @{ op = 'ui_open'; what = 'preflight' },
    @{ op = 'ui_wait'; target = 'dialog' },
    @{ op = 'wait'; seconds = 1 },
    @{ op = 'ui_capture'; target = 'dialog'; file = 'preflight.png' },
    @{ op = 'ui_close' },
    @{ op = 'ui_wait'; target = 'dialog'; present = $false },
    @{ op = 'quit' })
$dir = Invoke-Capture 'main' $main 300
foreach ($name in 'obs-with-dock.png', 'dock-live.png', 'editor.png', 'vertical-editor.png', 'settings-performance.png', 'dock-light.png', 'preflight.png') {
    Publish-Picture (Join-Path $dir $name) $name
}

# ---- Second run: a suggestion, from a destination whose server reads too slowly ---------------------
Write-Host ''
Write-Host 'suggestion: automatic optimisation proposes a lower bitrate'
$suggest = $scene + @(
    @{ op = 'set'; performance_mode = 'custom'; optimizer = @{ mode = 'suggest' } },
    @{ op = 'optimizer_tuning'; sustain_ms = 4000; cooldown_ms = 6000; recover_after_ms = 600000 },
    @{ op = 'add_destination'; ref = 'slow'; provider = 'custom_rtmp'; stream_key = 'slow2000-shot-7b1e4c'
       config = @{ name = 'My server'; server_url = $server; video = @{ bitrate_kbps = 3000 } } },
    @{ op = 'ui_show_dock'; width = 420; height = 720 },
    @{ op = 'start'; ref = 'slow' },
    @{ op = 'wait_phase'; ref = 'slow'; phase = 'live'; timeout_sec = 40 },
    @{ op = 'wait_suggestion'; timeout_sec = 90 },
    @{ op = 'wait'; seconds = 2 },
    @{ op = 'ui_grab'; target = 'dock'; file = 'suggestion.png' },
    @{ op = 'stop_all' },
    @{ op = 'wait_idle'; timeout_sec = 40 },
    @{ op = 'quit' })
$dir = Invoke-Capture 'suggestion' $suggest 240
Publish-Picture (Join-Path $dir 'suggestion.png') 'suggestion.png'

Write-Host ''
Write-Host "Pictures are in $OutDir"
exit $report.Finish()
