# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Drives the real RelayDock windows inside a real OBS and checks what they show and do.

.DESCRIPTION
  onboarding    The first-run review: the dock is locked until all six documents are reviewed,
                the acknowledgement box unlocks only after scrolling to the end, Continue
                unlocks only after ticking it, quitting half-way records nothing, and the
                acceptance survives an OBS restart.
  destination   Adds a destination through the editor, checks that the stream key is never
                shown or written to the settings file, streams with it, then uses Copy Key and
                checks that the clipboard is cleared after 30 seconds.
  settings      Opens every settings page, changes settings through the controls and checks
                that they apply.
  tools         The preflight window and the vertical layout editor.

OBS opens visibly for these tests, because a hidden window has no layout to check.
Each scenario saves pictures of the windows next to its results.

Needs a build with RELAYDOCK_TEST_HOOKS=ON (preset windows-hooks-x64).

.EXAMPLE
.\Test-Ui.ps1 -ObsRoot C:\obs-32.2.2 -BuildDir C:\RelayDock\build_hooks_x64
#>
param(
    [Parameter(Mandatory)][string]$ObsRoot,
    [Parameter(Mandatory)][string]$BuildDir,
    [string]$OutDir = (Join-Path $PSScriptRoot '..\output\ui'),
    [string[]]$Only = @(),
    [int]$Port = 19370
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ObsTestHarness.psm1') -Force

$paths = Get-BuildPaths -BuildDir $BuildDir
$obsVersion = (Get-Item (Join-Path $ObsRoot 'bin\64bit\obs64.exe')).VersionInfo.ProductVersion
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$server = "rtmp://127.0.0.1:$Port/live"
$report = New-TestReport -Title "Interface on OBS $obsVersion"
$video = @{ op = 'obs_video'; base_width = 1280; base_height = 720; output_width = 1280; output_height = 720; fps = '30' }
$titles = @('Terms of Use', 'Privacy Policy', 'Security and Credentials Notice', 'Third-Party Services Notice',
    'Streaming Disclaimer', 'Open Source Licenses')

function Test-Selected([string]$Name) { return ($Only.Count -eq 0) -or ($Only -contains $Name) }

function Invoke-Ui([string]$Name, [hashtable]$Scenario, [int]$TimeoutSec = 240, [switch]$KeepConfig, [switch]$WithSink) {
    $dir = Join-Path $OutDir $Name
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $sink = $null
    if ($WithSink) { $sink = Start-RtmpSink -SinkPath $paths.Sink -ReportPath (Join-Path $dir 'sink-report.json') -Port $Port }
    try {
        $run = Invoke-ObsScenario -ObsRoot $ObsRoot -PluginRunDir $paths.PluginRunDir -Scenario $Scenario -OutDir $dir `
            -Name $Name -TimeoutSec $TimeoutSec -Visible -ResetConfig:(-not $KeepConfig)
    } finally {
        $sinkReport = if ($sink) { Stop-RtmpSink -Sink $sink } else { $null }
    }
    return [pscustomobject]@{ Run = $run; Sink = $sinkReport; Dir = $dir }
}

function Get-Button($State, [string]$Text) { return @($State.buttons | Where-Object { $_.text -eq $Text })[0] }
function Get-Check($State, [string]$Prefix) { return @($State.checks | Where-Object { $_.text -like "$Prefix*" })[0] }
function Test-Label($State, [string]$Pattern) { return @($State.labels | Where-Object { $_.text -match $Pattern }).Count -gt 0 }

# ---------------------------------------------------------------------------------------------
if (Test-Selected 'onboarding') {
    Write-Host ''
    Write-Host 'onboarding: the first-run review'

    $steps = New-Object System.Collections.Generic.List[object]
    $steps.Add(@{ op = 'clear' })
    $steps.Add(@{ op = 'ui_show_dock'; width = 400; height = 700 })
    $steps.Add(@{ op = 'wait'; seconds = 1 })
    $steps.Add(@{ op = 'ui_state'; target = 'dock'; label = 'dock_first' })
    $steps.Add(@{ op = 'ui_grab'; target = 'dock'; file = 'dock-first-run.png' })

    # Quit half-way: accept the first document, then close. Nothing may be recorded.
    $steps.Add(@{ op = 'ui_click'; target = 'dock'; text = 'Review now' })
    $steps.Add(@{ op = 'ui_wait'; target = 'dialog' })
    $steps.Add(@{ op = 'wait'; seconds = 1 })
    $steps.Add(@{ op = 'ui_state'; label = 'legal_open' })
    $steps.Add(@{ op = 'ui_grab'; file = 'legal-first.png' })
    $steps.Add(@{ op = 'ui_check'; text = 'I have reviewed'; allow_disabled = $true })
    $steps.Add(@{ op = 'wait'; seconds = 0.5 })
    $steps.Add(@{ op = 'ui_state'; label = 'legal_unscrolled' })
    $steps.Add(@{ op = 'ui_scroll_end' })
    $steps.Add(@{ op = 'wait'; seconds = 0.5 })
    $steps.Add(@{ op = 'ui_state'; label = 'legal_scrolled' })
    $steps.Add(@{ op = 'ui_check'; text = 'I have reviewed' })
    $steps.Add(@{ op = 'wait'; seconds = 0.5 })
    $steps.Add(@{ op = 'ui_state'; label = 'legal_checked' })
    $steps.Add(@{ op = 'ui_grab'; file = 'legal-ready.png' })
    $steps.Add(@{ op = 'ui_click'; text = 'Continue' })
    $steps.Add(@{ op = 'wait'; seconds = 1 })
    $steps.Add(@{ op = 'ui_state'; label = 'legal_second' })
    $steps.Add(@{ op = 'ui_click'; text = 'Not now' })
    $steps.Add(@{ op = 'ui_wait'; target = 'dialog'; present = $false })
    $steps.Add(@{ op = 'legal_state'; label = 'abandoned' })

    # The whole review.
    $steps.Add(@{ op = 'ui_click'; target = 'dock'; text = 'Review now' })
    $steps.Add(@{ op = 'ui_wait'; target = 'dialog' })
    for ($i = 0; $i -lt 6; $i++) {
        $steps.Add(@{ op = 'wait'; seconds = 0.8 })
        $steps.Add(@{ op = 'ui_state'; label = "page_$i" })
        $steps.Add(@{ op = 'ui_scroll_end' })
        $steps.Add(@{ op = 'wait'; seconds = 0.4 })
        $steps.Add(@{ op = 'ui_check'; text = 'I have reviewed' })
        $steps.Add(@{ op = 'wait'; seconds = 0.4 })
        $steps.Add(@{ op = 'ui_click'; text = $(if ($i -lt 5) { 'Continue' } else { 'Finish' }) })
    }
    $steps.Add(@{ op = 'ui_wait'; target = 'dialog'; present = $false })
    $steps.Add(@{ op = 'wait'; seconds = 1 })
    $steps.Add(@{ op = 'legal_state'; label = 'accepted' })
    $steps.Add(@{ op = 'ui_state'; target = 'dock'; label = 'dock_accepted' })
    $steps.Add(@{ op = 'ui_grab'; target = 'dock'; file = 'dock-empty.png' })
    $steps.Add(@{ op = 'save_config' })
    $steps.Add(@{ op = 'quit' })

    $outcome = Invoke-Ui 'onboarding' @{ steps = $steps.ToArray() }
    $run = $outcome.Run
    Add-ObsRunChecks -Report $report -Run $run -Label 'onboarding'

    if ($run.Result -and $run.Result.ui.dock_accepted) {
        $ui = $run.Result.ui
        $report.Check('onboarding: a new install shows the review panel in the dock', (Test-Label $ui.dock_first 'Before your first stream'))
        $report.Check('onboarding: adding and starting are locked until the review is done',
            (-not (Get-Button $ui.dock_first '+ Add Platform').enabled -and -not (Get-Button $ui.dock_first 'Start All Enabled').enabled))
        $report.Check('onboarding: the review opens on the first of six documents',
            ((Test-Label $ui.legal_open 'Document 1 of 6') -and (Test-Label $ui.legal_open '^Terms of Use$')))
        $box = Get-Check $ui.legal_open 'I have reviewed'
        $report.Check('onboarding: the acknowledgement box starts disabled and unticked', ($null -ne $box -and -not $box.enabled -and -not $box.checked))
        $report.Check('onboarding: Continue starts disabled', (-not (Get-Button $ui.legal_open 'Continue').enabled))
        $box = Get-Check $ui.legal_unscrolled 'I have reviewed'
        $report.Check('onboarding: the box cannot be ticked before scrolling to the end', (-not $box.enabled -and -not $box.checked))
        $box = Get-Check $ui.legal_scrolled 'I have reviewed'
        $report.Check('onboarding: scrolling to the end unlocks the box', ($box.enabled -and -not $box.checked))
        $report.Check('onboarding: Continue stays disabled until the box is ticked', (-not (Get-Button $ui.legal_scrolled 'Continue').enabled))
        $report.Check('onboarding: ticking the box enables Continue',
            ((Get-Check $ui.legal_checked 'I have reviewed').checked -and (Get-Button $ui.legal_checked 'Continue').enabled))
        $report.Check('onboarding: the box says reviewed and agree, with the document name',
            ((Get-Check $ui.legal_checked 'I have reviewed').text -eq 'I have reviewed and agree to the Terms of Use.'))
        $box = Get-Check $ui.legal_second 'I have reviewed'
        $report.Check('onboarding: the next document starts locked again',
            ((Test-Label $ui.legal_second 'Document 2 of 6') -and -not $box.enabled -and -not $box.checked -and -not (Get-Button $ui.legal_second 'Continue').enabled))
        $report.Check('onboarding: closing half-way records nothing',
            (-not $run.Result.legal.abandoned.complete -and @($run.Result.legal.abandoned.records).Count -eq 0))

        $allPages = $true
        for ($i = 0; $i -lt 6; $i++) {
            $page = $ui."page_$i"
            if (-not (Test-Label $page ("^" + [regex]::Escape($titles[$i]) + '$'))) { $allPages = $false }
            if ((Get-Check $page 'I have reviewed').enabled) { $allPages = $false }
        }
        $report.Check('onboarding: every document has its own page, each locked until scrolled', $allPages)
        $accepted = $run.Result.legal.accepted
        $records = @($accepted.records)
        $report.Check('onboarding: finishing records all six documents', ($accepted.complete -and $records.Count -eq 6))
        $report.Check('onboarding: each record holds the document version, the time and the RelayDock version',
            (@($records | Where-Object { $_.version -eq '1.0' -and $_.accepted_at -match '^\d{4}-\d\d-\d\dT' -and $_.app_version -match '^\d+\.\d+\.\d+' }).Count -eq 6))
        $report.Check('onboarding: the dock unlocks after the review',
            ((Get-Button $ui.dock_accepted '+ Add Platform').enabled -and -not (Test-Label $ui.dock_accepted 'Before your first stream')))
        $report.Check('onboarding: an empty dock says how to begin', (Test-Label $ui.dock_accepted 'No destinations yet'))
    }

    # A restart must not ask again.
    $restart = Invoke-Ui 'onboarding-restart' @{ steps = @(
            @{ op = 'ui_show_dock'; width = 400; height = 700 },
            @{ op = 'wait'; seconds = 1 },
            @{ op = 'legal_state'; label = 'restart' },
            @{ op = 'ui_state'; target = 'dock'; label = 'dock_restart' },
            @{ op = 'quit' }) } -KeepConfig
    Add-ObsRunChecks -Report $report -Run $restart.Run -Label 'onboarding restart'
    if ($restart.Run.Result -and $restart.Run.Result.legal.restart) {
        $report.Check('onboarding: the acceptance survives an OBS restart',
            ($restart.Run.Result.legal.restart.complete -and -not (Test-Label $restart.Run.Result.ui.dock_restart 'Before your first stream')))
    }
}

# ---------------------------------------------------------------------------------------------
if (Test-Selected 'destination') {
    Write-Host ''
    Write-Host 'destination: add, stream and copy the key through the interface'
    $key = 'ok-ui-editor-48c2e9b17d05'
    # Copy Key needs the Windows clipboard. A sandboxed session has none, and the wait for the
    # 30 second clear would then prove nothing.
    $clipboard = Test-ClipboardAvailable
    $clearWait = if ($clipboard) { 31 } else { 1 }
    $outcome = Invoke-Ui 'destination' @{ steps = @(
            @{ op = 'clear' }, $video, @{ op = 'accept_legal' },
            @{ op = 'ui_show_dock'; width = 400; height = 700 },
            @{ op = 'ui_open'; what = 'add'; provider = 'custom_rtmp' },
            @{ op = 'ui_wait'; target = 'dialog' },
            @{ op = 'wait'; seconds = 1 },
            @{ op = 'ui_state'; label = 'editor_new' },
            @{ op = 'ui_grab'; file = 'editor-new.png' },
            @{ op = 'ui_text'; name = 'Name'; value = 'My server' },
            @{ op = 'ui_text'; name = 'Server URL'; value = $server },
            @{ op = 'ui_text'; name = 'stream key'; value = $key },
            @{ op = 'wait'; seconds = 0.5 },
            @{ op = 'ui_state'; label = 'editor_filled' },
            @{ op = 'ui_grab'; file = 'editor-filled.png' },
            @{ op = 'ui_click'; text = 'Add' },
            @{ op = 'ui_wait'; target = 'dialog'; present = $false },
            @{ op = 'wait'; seconds = 1 },
            @{ op = 'ui_bind_ref'; ref = 'a'; name = 'My server' },
            @{ op = 'save_config' },
            @{ op = 'ui_state'; target = 'dock'; label = 'dock_ready' },
            @{ op = 'ui_grab'; target = 'dock'; file = 'dock-ready.png' },
            @{ op = 'ui_click'; target = 'dock'; text = 'Start My server' },
            @{ op = 'wait_phase'; ref = 'a'; phase = 'live'; timeout_sec = 30 },
            @{ op = 'wait'; seconds = 5 },
            @{ op = 'ui_state'; target = 'dock'; label = 'dock_live' },
            @{ op = 'ui_grab'; target = 'dock'; file = 'dock-live.png' },
            @{ op = 'snapshot'; label = 'live' },
            @{ op = 'ui_open'; what = 'edit'; ref = 'a' },
            @{ op = 'ui_wait'; target = 'dialog' },
            @{ op = 'wait'; seconds = 1 },
            @{ op = 'ui_state'; label = 'editor_saved' },
            @{ op = 'ui_grab'; file = 'editor-saved.png' },
            @{ op = 'ui_click'; text = 'Copy' },
            @{ op = 'wait'; seconds = 1 },
            @{ op = 'ui_clipboard'; expect = $key; label = 'copied' },
            @{ op = 'ui_state'; label = 'editor_copied' },
            @{ op = 'ui_click'; text = 'Cancel' },
            @{ op = 'ui_wait'; target = 'dialog'; present = $false },
            @{ op = 'ui_click'; target = 'dock'; text = 'Stop My server' },
            @{ op = 'wait_idle'; timeout_sec = 40 },
            @{ op = 'wait'; seconds = $clearWait },
            @{ op = 'ui_clipboard'; expect = $key; label = 'after_30s' },
            @{ op = 'ui_state'; target = 'dock'; label = 'dock_stopped' },
            @{ op = 'quit' }) } 300 -WithSink
    $run = $outcome.Run
    Add-ObsRunChecks -Report $report -Run $run -Label 'destination' -Secrets @($key)

    if ($run.Result -and $run.Result.ui.dock_stopped) {
        $ui = $run.Result.ui
        $report.Check('destination: the editor opens for a new Custom RTMP destination', ($ui.editor_new.title -eq 'Add Custom RTMP'))
        $keyField = @($ui.editor_filled.edits | Where-Object { $_.name -eq 'stream key' })[0]
        $report.Check('destination: the stream key field hides what is typed',
            ($null -ne $keyField -and $keyField.hidden -and $keyField.length -eq $key.Length -and $keyField.shown -notmatch 'ok-ui'))
        $report.Check('destination: the editor previews what the destination will stream',
            (Test-Label $ui.editor_filled 'Streams 1280x720 at 30 FPS'))
        $report.Check('destination: the new destination appears as a card that is ready',
            ((Test-Label $ui.dock_ready '^My server$') -and (Test-Label $ui.dock_ready '^READY$')))
        $configPath = Join-Path (Get-ObsConfigDir -ObsRoot $ObsRoot) 'plugin_config\relaydock\config.json'
        $configText = if (Test-Path $configPath) { Get-Content $configPath -Raw } else { '' }
        $report.Check('destination: the settings file holds the destination and not its key',
            ($configText -match 'My server' -and -not $configText.Contains($key)))
        $report.Check('destination: the Start button on the card starts the stream', ($run.Result.snapshots.live.destinations.a.phase -eq 'live'))
        $report.Check('destination: the live card shows LIVE with bitrate and time',
            ((Test-Label $ui.dock_live '^LIVE$') -and (Test-Label $ui.dock_live 'Kbps, .*% dropped, \d+:\d\d')))
        $report.Check('destination: the toolbar summary counts the live destination', (Test-Label $ui.dock_live '^1 live, '))
        $report.Check('destination: the stream arrived at the server with that key',
            ($null -ne $outcome.Sink.streams.$key -and $outcome.Sink.streams.$key.video_bytes -gt 100000))
        $report.Check('destination: the editor of a live destination says changes apply later', (Test-Label $ui.editor_saved 'This destination is live'))
        $report.Check('destination: a saved key shows as dots with Replace, Copy and Remove',
            ((Test-Label $ui.editor_saved '^\u2022+$') -and $null -ne (Get-Button $ui.editor_saved 'Replace') -and
             $null -ne (Get-Button $ui.editor_saved 'Copy') -and $null -ne (Get-Button $ui.editor_saved 'Remove')))
        $visibleKey = @($ui.editor_saved.labels | Where-Object { $_.text.Contains($key) }).Count +
                      @($ui.editor_saved.edits | Where-Object { $_.text.Contains($key) -or $_.shown.Contains($key) }).Count
        $report.Check('destination: the saved key appears nowhere in the editor', ($visibleKey -eq 0))
        if ($clipboard) {
            $report.Check('destination: Copy puts the key on the clipboard', ($run.Result.clipboard.copied.matches -eq $true))
            $report.Check('destination: the editor says the clipboard will be cleared', (Test-Label $ui.editor_copied 'clears the clipboard after 30 seconds'))
            $report.Check('destination: the key is gone from the clipboard after 30 seconds',
                ($run.Result.clipboard.after_30s.matches -eq $false -and $run.Result.clipboard.after_30s.empty -eq $true))
        } else {
            $reason = 'this session has no access to the Windows clipboard'
            $report.Skip('destination: Copy puts the key on the clipboard', $reason)
            $report.Skip('destination: the key is gone from the clipboard after 30 seconds', $reason)
            $report.Check('destination: when Windows refuses the clipboard, Copy says so and claims nothing',
                ((Test-Label $ui.editor_copied 'Windows did not accept the clipboard data') -and $run.Result.clipboard.copied.matches -eq $false))
        }
        $report.Check('destination: the Stop button on the card stops the stream', (Test-Label $ui.dock_stopped '^READY$'))
    }
}

# ---------------------------------------------------------------------------------------------
if (Test-Selected 'settings') {
    Write-Host ''
    Write-Host 'settings: every page opens and its controls apply'
    $pages = @('general', 'platforms', 'streaming', 'video', 'audio', 'encoder', 'vertical', 'optimization', 'performance',
        'network', 'appearance', 'layout', 'security', 'diagnostics', 'updates', 'advanced', 'about')
    $keys = @{ a = 'ok-ui-settings-a-1d6f'; b = 'ok-ui-settings-b-93ce' }
    $steps = New-Object System.Collections.Generic.List[object]
    $steps.Add(@{ op = 'clear' }); $steps.Add($video); $steps.Add(@{ op = 'accept_legal' })
    $steps.Add(@{ op = 'add_destination'; ref = 'a'; provider = 'twitch'; stream_key = $keys.a; config = @{ name = 'Twitch' } })
    $steps.Add(@{ op = 'add_destination'; ref = 'b'; provider = 'youtube'; stream_key = $keys.b; config = @{ name = 'YouTube' } })
    $steps.Add(@{ op = 'ui_show_dock'; width = 400; height = 760 })
    $steps.Add(@{ op = 'ui_open'; what = 'settings'; page = 'general' })
    $steps.Add(@{ op = 'ui_wait'; target = 'dialog' })
    foreach ($page in $pages) {
        $steps.Add(@{ op = 'ui_open'; what = 'settings'; page = $page })
        $steps.Add(@{ op = 'wait'; seconds = 0.6 })
        $steps.Add(@{ op = 'ui_state'; label = "page_$page" })
        $steps.Add(@{ op = 'ui_grab'; file = "settings-$page.png" })
    }
    # Change settings through the controls.
    $steps.Add(@{ op = 'ui_open'; what = 'settings'; page = 'performance' })
    $steps.Add(@{ op = 'wait'; seconds = 0.5 })
    $steps.Add(@{ op = 'ui_click'; text = 'Potato' })
    $steps.Add(@{ op = 'wait'; seconds = 2.5 })
    $steps.Add(@{ op = 'snapshot'; label = 'potato' })
    $steps.Add(@{ op = 'ui_click'; text = 'Balanced' })
    $steps.Add(@{ op = 'ui_open'; what = 'settings'; page = 'network' })
    $steps.Add(@{ op = 'wait'; seconds = 0.5 })
    $steps.Add(@{ op = 'ui_spin'; name = 'Your upload speed'; value = 5 })
    $steps.Add(@{ op = 'wait'; seconds = 1 })
    $steps.Add(@{ op = 'ui_state'; label = 'network_5' })
    $steps.Add(@{ op = 'ui_spin'; name = 'Your upload speed'; value = 50 })
    $steps.Add(@{ op = 'wait'; seconds = 1 })
    $steps.Add(@{ op = 'ui_state'; label = 'network_50' })
    $steps.Add(@{ op = 'ui_open'; what = 'settings'; page = 'appearance' })
    $steps.Add(@{ op = 'wait'; seconds = 0.5 })
    $steps.Add(@{ op = 'ui_combo'; name = 'Theme'; value = 'Light' })
    $steps.Add(@{ op = 'wait'; seconds = 1 })
    $steps.Add(@{ op = 'ui_grab'; file = 'settings-appearance-light.png' })
    $steps.Add(@{ op = 'ui_grab'; target = 'dock'; file = 'dock-light.png' })
    $steps.Add(@{ op = 'ui_combo'; name = 'Theme'; value = 'Dark' })
    $steps.Add(@{ op = 'wait'; seconds = 1 })
    $steps.Add(@{ op = 'ui_grab'; target = 'dock'; file = 'dock-dark.png' })
    $steps.Add(@{ op = 'ui_combo'; name = 'Theme'; value = 'Follow OBS' })
    $steps.Add(@{ op = 'ui_open'; what = 'settings'; page = 'layout' })
    $steps.Add(@{ op = 'wait'; seconds = 0.5 })
    $steps.Add(@{ op = 'ui_combo'; name = 'Cards'; value = 'Compact' })
    $steps.Add(@{ op = 'wait'; seconds = 1 })
    $steps.Add(@{ op = 'ui_grab'; target = 'dock'; file = 'dock-compact.png' })
    $steps.Add(@{ op = 'ui_state'; target = 'dock'; label = 'dock_compact' })
    $steps.Add(@{ op = 'ui_combo'; name = 'Cards'; value = 'Expanded' })
    $steps.Add(@{ op = 'wait'; seconds = 1 })
    $steps.Add(@{ op = 'ui_state'; target = 'dock'; label = 'dock_expanded' })
    $steps.Add(@{ op = 'ui_grab'; target = 'dock'; file = 'dock-two.png' })
    $steps.Add(@{ op = 'save_config' })
    $steps.Add(@{ op = 'ui_close' })
    $steps.Add(@{ op = 'ui_wait'; target = 'dialog'; present = $false })
    $steps.Add(@{ op = 'quit' })

    $outcome = Invoke-Ui 'settings' @{ steps = $steps.ToArray() } 300
    $run = $outcome.Run
    Add-ObsRunChecks -Report $report -Run $run -Label 'settings' -Secrets $keys.Values

    if ($run.Result -and $run.Result.ui.dock_expanded) {
        $ui = $run.Result.ui
        $opened = @($pages | Where-Object { $null -ne $ui."page_$_" -and @($ui."page_$_".labels).Count -gt 0 }).Count
        $report.Check('settings: all 17 pages open and show content', ($opened -eq 17), "$opened pages")
        $nav = @($ui.page_general.lists | Where-Object { $_.name -eq 'Settings pages' })[0]
        $report.Check('settings: the page list names all 17 pages', (@($nav.rows).Count -eq 17), (@($nav.rows) -join ', '))
        $report.Check('settings: Platforms lists every platform with its limits',
            ((Test-Label $ui.page_platforms '^Twitch$') -and (Test-Label $ui.page_platforms '^TikTok$') -and (Test-Label $ui.page_platforms '^YouTube$') -and
             (Test-Label $ui.page_platforms '^Facebook$') -and (Test-Label $ui.page_platforms 'Video up to 6000 Kbps')))
        $report.Check('settings: Streaming shows the encoder plan', (Test-Label $ui.page_streaming 'Enabled destinations: 2\. Video encoders: 1\.'))
        $report.Check('settings: Encoder lists the encoders of this PC', (Test-Label $ui.page_encoder '^Automatic picks '))
        $saved = @(@($ui.page_security.lists | Where-Object { $_.name -eq 'Saved keys and passwords' })[0].rows)
        $report.Check('settings: Security lists saved keys by name, never by value',
            ($saved -contains 'Twitch: stream key' -and $saved -contains 'YouTube: stream key'), ($saved -join ', '))
        $leaks = 0
        foreach ($page in $pages) {
            $json = $ui."page_$page" | ConvertTo-Json -Depth 8
            foreach ($secret in $keys.Values) { if ($json.Contains($secret)) { $leaks++ } }
        }
        $report.Check('settings: no page shows a stream key', ($leaks -eq 0))
        $report.Check('settings: About shows the version and the six legal documents',
            ((Test-Label $ui.page_about '^Version 1\.0\.0') -and @($ui.page_about.buttons | Where-Object { $_.text -eq 'View' }).Count -eq 6))
        $report.Check('settings: Updates says plainly that this build has no update check', (Test-Label $ui.page_updates 'no project page configured'))
        $report.Check('settings: choosing Potato on the Performance page slows measuring to every two seconds',
            ($run.Result.snapshots.potato.performance.tick_interval_ms -eq 2000))
        $report.Check('settings: an upload speed that is too low shows as exceeded',
            ((Test-Label $ui.network_5 'of your 5000 Kbps upload') -and (Test-Label $ui.network_5 'Streams will drop frames or disconnect')))
        $report.Check('settings: a sufficient upload speed shows the safe limit', (Test-Label $ui.network_50 'Safe limit 37\.5 Mbps'))
        $report.Check('settings: compact cards hide the detail lines', (-not (Test-Label $ui.dock_compact '^Server: ')))
        $report.Check('settings: expanded cards show server and encoder', ((Test-Label $ui.dock_expanded '^Server: rtmps://') -and (Test-Label $ui.dock_expanded '^Encoder: ')))
        $configPath = Join-Path (Get-ObsConfigDir -ObsRoot $ObsRoot) 'plugin_config\relaydock\config.json'
        $configText = if (Test-Path $configPath) { Get-Content $configPath -Raw } else { '' }
        $report.Check('settings: changes made in the window are saved', ($configText -match '"upload_kbps":\s*50000'))
    }
}

# ---------------------------------------------------------------------------------------------
if (Test-Selected 'tools') {
    Write-Host ''
    Write-Host 'tools: preflight window and vertical layout editor'
    $key = 'ok-ui-tools-5b7a21e0'
    $outcome = Invoke-Ui 'tools' @{ steps = @(
            @{ op = 'clear' }, $video, @{ op = 'accept_legal' },
            @{ op = 'add_color_source'; name = 'RD blue'; color = '#0000FF'; width = 1280; height = 720 },
            @{ op = 'add_destination'; ref = 'good'; provider = 'custom_rtmp'; stream_key = $key; config = @{ name = 'Good'; server_url = $server } },
            @{ op = 'add_destination'; ref = 'nokey'; provider = 'twitch'; config = @{ name = 'No key yet' } },
            @{ op = 'add_destination'; ref = 'tall'; provider = 'custom_rtmp'; stream_key = 'ok-ui-tools-tall-c41d';
               config = @{ name = 'Tall'; server_url = $server; video = @{ orientation = 'vertical' } } },
            @{ op = 'ui_show_dock'; width = 400; height = 760 },
            @{ op = 'ui_open'; what = 'preflight' },
            @{ op = 'ui_wait'; target = 'dialog' },
            @{ op = 'wait'; seconds = 1 },
            @{ op = 'ui_state'; label = 'preflight' },
            @{ op = 'ui_grab'; file = 'preflight-failed.png' },
            @{ op = 'ui_close' },
            @{ op = 'ui_wait'; target = 'dialog'; present = $false },
            @{ op = 'ui_open'; what = 'start_all' },
            @{ op = 'ui_wait'; target = 'dialog' },
            @{ op = 'wait'; seconds = 1 },
            @{ op = 'ui_state'; label = 'start_gate' },
            @{ op = 'ui_close' },
            @{ op = 'ui_wait'; target = 'dialog'; present = $false },
            @{ op = 'snapshot'; label = 'not_started' },
            @{ op = 'ui_open'; what = 'vertical' },
            @{ op = 'ui_wait'; target = 'dialog' },
            @{ op = 'wait'; seconds = 2 },
            @{ op = 'ui_state'; label = 'vertical_open' },
            @{ op = 'ui_grab'; file = 'vertical-editor.png' },
            @{ op = 'ui_capture'; file = 'vertical-editor-capture.png'; optional = $true },
            @{ op = 'ui_select_row'; name = 'Layout items'; value = 'OBS picture' },
            @{ op = 'wait'; seconds = 0.5 },
            @{ op = 'ui_combo'; name = 'Preset'; value = 'Top half' },
            @{ op = 'wait'; seconds = 0.5 },
            @{ op = 'ui_state'; label = 'vertical_preset' },
            @{ op = 'ui_click'; text = 'Save' },
            @{ op = 'ui_wait'; target = 'dialog'; present = $false },
            @{ op = 'render_vertical'; label = 'after_save'; find = @{ blue = '#0000FF' } },
            @{ op = 'quit' }) } 240
    $run = $outcome.Run
    Add-ObsRunChecks -Report $report -Run $run -Label 'tools'

    if ($run.Result -and $run.Result.ui.vertical_preset) {
        $ui = $run.Result.ui
        $report.Check('tools: the preflight window reports FAILED for a destination with no key',
            ((Test-Label $ui.preflight '^FAILED$') -and (Test-Label $ui.preflight 'No key yet: .*no stream key')))
        $report.Check('tools: it also lists what is ready', (Test-Label $ui.preflight 'Good: Good is ready'))
        $report.Check('tools: a finding about a destination offers Edit', ($null -ne (Get-Button $ui.preflight 'Edit')))
        $start = Get-Button $ui.start_gate 'Start'
        $report.Check('tools: Start All Enabled is blocked while the check fails', ($null -ne $start -and -not $start.enabled))
        $report.Check('tools: nothing started behind a failed check',
            ($run.Result.snapshots.not_started.destinations.good.phase -eq 'idle' -and $run.Result.snapshots.not_started.destinations.tall.phase -eq 'idle'))
        $report.Check('tools: the vertical editor opens with the default layout',
            (@($ui.vertical_open.lists | Where-Object { $_.name -eq 'Layout items' })[0].rows -contains 'OBS picture (Program)'))
        $position = @($ui.vertical_preset.labels | Where-Object { $_.text -match 'The source is' }).Count
        $report.Check('tools: the editor reports the size of the selected source', ($position -eq 1))
        $box = $run.Result.renders.after_save.colors.blue
        # Top half of 1080x1920 is 1080x960. A 16:9 picture filling it is cropped at the sides.
        $report.Check('tools: the Top half preset is what the canvas renders after Save',
            ($null -ne $box -and [math]::Abs($box.x) -le 4 -and [math]::Abs($box.y) -le 4 -and
             [math]::Abs($box.width - 1080) -le 4 -and [math]::Abs($box.height - 960) -le 4),
            "x=$($box.x) y=$($box.y) $($box.width)x$($box.height)")
    }
}

exit $report.Finish()
