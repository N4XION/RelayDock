# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Tests live chat inside a real OBS: reading Twitch and YouTube chat into one list, the Twitch
sign-in, and what RelayDock says when a platform refuses.

.DESCRIPTION
The platforms are stand-ins on this PC (rd-chat-fake.exe). They speak the parts of the Twitch
and YouTube interfaces that RelayDock uses, and the script tells them what happens next: a viewer
writes, a gift arrives, a connection breaks. No real platform is contacted, and every token, key
and comment is made up.

Cases:
  merged    A saved Twitch sign-in and a YouTube key: both chats arrive in one list in the chat
            dock, gifts and paid messages stand out, and a broken Twitch connection comes back.
  signin    The Twitch sign-in from Settings, Chat: the code, the wait, the result, and sign out.
  problems  YouTube: a link that is no stream, a stream that ends, and a key that ran out of its
            daily amount.

Needs a build with RELAYDOCK_TEST_HOOKS=ON (preset windows-hooks-x64).

.EXAMPLE
.\Test-Chat.ps1 -ObsRoot C:\obs-32.2.2 -BuildDir C:\RelayDock\build_hooks_x64
#>
param(
    [Parameter(Mandatory)][string]$ObsRoot,
    [Parameter(Mandatory)][string]$BuildDir,
    [string]$OutDir = '',
    [string[]]$Only = @()
)

$ErrorActionPreference = 'Stop'
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot '..\output\chat' }
Import-Module (Join-Path $PSScriptRoot 'ObsTestHarness.psm1') -Force

$paths = Get-BuildPaths -BuildDir $BuildDir
$obsVersion = (Get-Item (Join-Path $ObsRoot 'bin\64bit\obs64.exe')).VersionInfo.ProductVersion
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$report = New-TestReport -Title "Live chat on OBS $obsVersion"

function Test-Selected([string]$Name) { return ($Only.Count -eq 0) -or ($Only -contains $Name) }
function Test-Label($State, [string]$Pattern) { return @($State.labels | Where-Object { $_.text -match $Pattern }).Count -gt 0 }
function Get-Button($State, [string]$Text) { return @($State.buttons | Where-Object { $_.text -eq $Text })[0] }
function Get-ChatLines($State) { return @(@($State.texts | Where-Object { $_.name -eq 'Live chat' })[0].lines) }

# ---- The stand-ins for the platforms ----------------------------------------------------------------
$script:fake = $null
$script:ports = $null

function Start-Fake([string]$Name) {
    $portsFile = Join-Path $OutDir "$Name.ports.json"
    Remove-Item -LiteralPath $portsFile -ErrorAction SilentlyContinue
    $script:fake = Start-Process -FilePath $paths.ChatFake -ArgumentList "`"$portsFile`"" -PassThru -WindowStyle Hidden
    $deadline = (Get-Date).AddSeconds(15)
    while (-not (Test-Path $portsFile) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100 }
    if (-not (Test-Path $portsFile)) { throw 'rd-chat-fake did not start.' }
    Start-Sleep -Milliseconds 200
    $script:ports = Get-Content -LiteralPath $portsFile -Raw | ConvertFrom-Json
}

function Stop-Fake {
    if ($script:fake -and -not $script:fake.HasExited) {
        try { Invoke-RestMethod -Uri "$($script:ports.control)/quit" -TimeoutSec 5 | Out-Null } catch { }
        if (-not $script:fake.WaitForExit(5000)) { $script:fake.Kill() }
    }
    $script:fake = $null
}

# Tells a stand-in to do something. Values are put into the address the way a form does it.
function Send-Fake([string]$Path, [hashtable]$Values = @{}) {
    $query = ($Values.Keys | ForEach-Object { "$_=$([uri]::EscapeDataString([string]$Values[$_]))" }) -join '&'
    $uri = "$($script:ports.control)$Path"
    if ($query) { $uri += "?$query" }
    return Invoke-RestMethod -Uri $uri -TimeoutSec 10
}

function Get-FakeState { return Send-Fake '/state' }

function Wait-Fake([scriptblock]$Condition, [int]$TimeoutSec = 40) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        $state = Get-FakeState
        if (& $Condition $state) { return $true }
        Start-Sleep -Milliseconds 200
    }
    return $false
}

function Get-SetupStep {
    return @{ op = 'chat_setup'
        twitch_auth = $script:ports.twitch.auth; twitch_api = $script:ports.twitch.api; twitch_eventsub = $script:ports.twitch.eventsub
        youtube_api = $script:ports.youtube.api
        retry_first_ms = 300; retry_max_ms = 800; sign_in_poll_ms = 250; keepalive_grace_ms = 3000
        youtube_min_poll_ms = 250; youtube_not_live_retry_ms = 500 }
}

# What must never be in the OBS log: the saved sign-in, every token the stand-in hands out, and the key.
function Get-Secrets {
    return @($script:ports.twitch.refresh_token, 'access-token-', 'refresh-token-', $script:ports.youtube.api_key, 'device-code-')
}

try {
    # -----------------------------------------------------------------------------------------
    if (Test-Selected 'merged') {
        Write-Host ''
        Write-Host 'merged: Twitch and YouTube chat in one list'
        Start-Fake 'merged'
        $dir = Join-Path $OutDir 'merged'
        $session = Start-ObsScenario -ObsRoot $ObsRoot -PluginRunDir $paths.PluginRunDir -OutDir $dir -Name 'merged' -Visible -ResetConfig -Scenario @{ steps = @(
                @{ op = 'clear' }, @{ op = 'accept_legal' }, (Get-SetupStep),
                @{ op = 'ui_show_dock'; dock = 'chat'; width = 400; height = 560 },
                @{ op = 'wait'; seconds = 1 },
                @{ op = 'ui_state'; target = 'chat'; label = 'empty' },
                @{ op = 'ui_grab'; target = 'chat'; file = 'chat-not-set-up.png' },
                @{ op = 'chat_twitch_saved'; refresh_token = $script:ports.twitch.refresh_token; client_id = $script:ports.twitch.client_id; login = $script:ports.twitch.login },
                @{ op = 'chat_youtube_key'; key = $script:ports.youtube.api_key },
                @{ op = 'chat_youtube_connect'; video = "https://www.youtube.com/watch?v=$($script:ports.youtube.video_id)" },
                @{ op = 'chat_wait'; platform = 'twitch'; state = 'connected'; timeout_sec = 30 },
                @{ op = 'chat_wait'; platform = 'youtube'; state = 'connected'; timeout_sec = 30 },
                @{ op = 'snapshot'; label = 'connected' },
                @{ op = 'chat_wait_events'; count = 6; timeout_sec = 40 },
                @{ op = 'wait'; seconds = 1 },
                @{ op = 'ui_state'; target = 'chat'; label = 'chat' },
                @{ op = 'ui_grab'; target = 'chat'; file = 'chat-merged.png' },
                @{ op = 'snapshot'; label = 'events' },
                @{ op = 'chat_wait_events'; count = 7; timeout_sec = 40 },
                @{ op = 'snapshot'; label = 'reconnected' },
                @{ op = 'quit' }) }

        # The viewers write once both readers are in place.
        $ready = Wait-Fake { param($s) $s.twitch.subscriptions.Count -ge 2 -and $s.youtube.message_requests -ge 1 }
        if ($ready) {
            Send-Fake '/twitch/chat' @{ id = 't1'; author = 'viewer_one'; text = 'hello from Twitch' } | Out-Null
            Send-Fake '/youtube/comment' @{ id = 'y1'; author = 'Viewer Two'; text = 'hello from YouTube' } | Out-Null
            Start-Sleep -Milliseconds 600
            Send-Fake '/twitch/chat' @{ id = 't2'; author = 'viewer_one'; text = 'Cheer100 nice one'; bits = 100 } | Out-Null
            Send-Fake '/youtube/superchat' @{ id = 'y2'; author = 'Viewer Two'; amount = 'NZ$5.00'; text = 'great stream' } | Out-Null
            Start-Sleep -Milliseconds 600
            Send-Fake '/twitch/notice' @{ id = 't3'; type = 'community_sub_gift'; text = 'viewer_three is gifting 5 Tier 1 Subs to the community!' } | Out-Null
            Send-Fake '/twitch/chat' @{ id = 't4'; author = 'evil'; text = '<img src=x onerror=alert(1)> <b>bold</b> &amp;' } | Out-Null
            # The Twitch connection breaks. RelayDock opens it again, and the next comment arrives.
            Start-Sleep -Seconds 3
            Send-Fake '/twitch/drop' | Out-Null
            if (Wait-Fake { param($s) $s.twitch.connections -ge 2 -and $s.twitch.subscriptions.Count -ge 4 }) {
                Send-Fake '/twitch/chat' @{ id = 't5'; author = 'viewer_one'; text = 'are you back?' } | Out-Null
            }
        }
        $run = Complete-ObsScenario -Session $session -TimeoutSec 180
        $state = Get-FakeState
        Add-ObsRunChecks -Report $report -Run $run -Label 'merged' -Secrets (Get-Secrets)

        $report.Check('merged: both stand-ins saw RelayDock connect and subscribe', $ready)
        if ($run.Result -and $run.Result.snapshots.events) {
            $ui = $run.Result.ui
            $connected = $run.Result.snapshots.connected.chat.platforms
            $report.Check('merged: before anything is set up, the dock says so and offers Set up chat',
                ((Test-Label $ui.empty 'Twitch: not set up') -and (Test-Label $ui.empty 'YouTube: not set up') -and $null -ne (Get-Button $ui.empty 'Set up chat')))
            $report.Check('merged: Twitch reads the chat of the signed-in user',
                ($connected.twitch.state -eq 'connected' -and $connected.twitch.account -eq $script:ports.twitch.login), "$($connected.twitch.state) $($connected.twitch.account)")
            $report.Check('merged: YouTube reads the chat of the stream behind the link',
                ($connected.youtube.state -eq 'connected' -and $connected.youtube.account -eq $script:ports.youtube.title), "$($connected.youtube.state) $($connected.youtube.account)")

            $events = @($run.Result.snapshots.events.chat.events)
            $order = ($events | ForEach-Object { "$($_.platform):$($_.id)" }) -join ' '
            $report.Check('merged: the comments of both platforms are in one list', (@($events | Where-Object { $_.platform -eq 'twitch' }).Count -eq 4 -and
                    @($events | Where-Object { $_.platform -eq 'youtube' }).Count -eq 2), $order)
            $byId = @{}
            foreach ($event in $events) { $byId[$event.id] = $event }
            $report.Check('merged: Bits, a Super Chat and gifted subscriptions are told apart from comments',
                ($byId['t1'].kind -eq 'message' -and $byId['t2'].kind -eq 'paid' -and $byId['t2'].headline -eq '100 Bits' -and
                 $byId['y2'].kind -eq 'paid' -and $byId['y2'].headline -eq 'NZ$5.00 Super Chat' -and $byId['t3'].kind -eq 'membership'),
                "$($byId['t2'].headline), $($byId['y2'].headline), $($byId['t3'].kind)")

            $lines = Get-ChatLines $ui.chat
            $report.Check('merged: the dock shows one line per event, with who wrote what',
                ($lines.Count -eq 6 -and @($lines | Where-Object { $_ -match 'viewer_one: hello from Twitch$' }).Count -eq 1 -and
                 @($lines | Where-Object { $_ -match 'Viewer Two: hello from YouTube$' }).Count -eq 1), ($lines -join ' | '))
            $report.Check('merged: a paid message shows its amount and its text',
                (@($lines | Where-Object { $_ -match '100 Bits' -and $_ -match 'Cheer100 nice one' }).Count -eq 1 -and
                 @($lines | Where-Object { $_ -match 'NZ\$5\.00 Super Chat' -and $_ -match 'great stream' }).Count -eq 1))
            $report.Check('merged: markup in a comment is shown as the text it is',
                (@($lines | Where-Object { $_.Contains('evil: <img src=x onerror=alert(1)> <b>bold</b> &amp;') }).Count -eq 1))
            $report.Check('merged: with both readers connected the dock reports no problem',
                ((Test-Label $ui.chat "Twitch: reading the chat of $($script:ports.twitch.login)") -and (Test-Label $ui.chat 'YouTube: reading the chat of')))

            $after = @($run.Result.snapshots.reconnected.chat.events)
            $report.Check('merged: after the Twitch connection broke, RelayDock opened it again and the next comment arrived',
                ($state.twitch.connections -ge 2 -and @($after | Where-Object { $_.id -eq 't5' }).Count -eq 1), "connections $($state.twitch.connections)")
        }
        $report.Check('merged: RelayDock sent Twitch no client secret', (-not $state.twitch.client_secret_sent))
        $report.Check('merged: the YouTube key travelled in a header and never in an address',
            ($state.youtube.message_requests -ge 2 -and -not $state.youtube.key_in_address))
        Stop-Fake
    }

    # -----------------------------------------------------------------------------------------
    if (Test-Selected 'signin') {
        Write-Host ''
        Write-Host 'signin: the Twitch sign-in from Settings, Chat'
        Start-Fake 'signin'
        # The stand-in answers "not yet" for a few seconds, like a user who is still on twitch.tv.
        Send-Fake '/twitch/set' @{ pending_polls = 14; refuse = 0 } | Out-Null
        $dir = Join-Path $OutDir 'signin'
        $run = Invoke-ObsScenario -ObsRoot $ObsRoot -PluginRunDir $paths.PluginRunDir -OutDir $dir -Name 'signin' -TimeoutSec 180 -Visible -ResetConfig -Scenario @{ steps = @(
                @{ op = 'clear' }, @{ op = 'accept_legal' }, (Get-SetupStep),
                @{ op = 'ui_show_dock'; width = 400; height = 700 },
                @{ op = 'ui_open'; what = 'settings'; page = 'chat' },
                @{ op = 'ui_wait'; target = 'dialog' },
                @{ op = 'wait'; seconds = 0.6 },
                @{ op = 'ui_state'; label = 'no_id' },
                @{ op = 'chat_twitch_client'; client_id = $script:ports.twitch.client_id },
                @{ op = 'wait'; seconds = 0.6 },
                @{ op = 'ui_state'; label = 'before' },
                @{ op = 'ui_grab'; file = 'settings-chat.png' },
                @{ op = 'ui_click'; text = 'Sign in with Twitch...' },
                @{ op = 'wait'; seconds = 2 },
                @{ op = 'ui_state'; label = 'code' },
                @{ op = 'ui_grab'; file = 'chat-sign-in.png' },
                @{ op = 'chat_wait'; platform = 'twitch'; state = 'connected'; timeout_sec = 40 },
                @{ op = 'wait'; seconds = 2.5 },
                @{ op = 'ui_state'; label = 'signed_in' },
                @{ op = 'snapshot'; label = 'signed_in' },
                @{ op = 'ui_click'; text = 'Sign out' },
                @{ op = 'ui_wait'; target = 'message' },
                @{ op = 'ui_state'; target = 'message'; label = 'sign_out_question' },
                @{ op = 'ui_click'; target = 'message'; text = 'Yes' },
                @{ op = 'ui_wait'; target = 'message'; present = $false },
                @{ op = 'wait'; seconds = 0.8 },
                @{ op = 'ui_state'; label = 'signed_out' },
                @{ op = 'snapshot'; label = 'signed_out' },
                @{ op = 'ui_close' },
                @{ op = 'ui_wait'; target = 'dialog'; present = $false },
                @{ op = 'quit' }) }
        $state = Get-FakeState
        Add-ObsRunChecks -Report $report -Run $run -Label 'signin' -Secrets (Get-Secrets)

        if ($run.Result -and $run.Result.ui.signed_out) {
            $ui = $run.Result.ui
            $report.Check('signin: without an application id, Sign in is switched off and the page says what is missing',
                (-not (Get-Button $ui.no_id 'Sign in with Twitch...').enabled -and (Test-Label $ui.no_id 'comes without an application id')))
            $report.Check('signin: with an application id, the page offers the sign-in',
                ((Get-Button $ui.before 'Sign in with Twitch...').enabled -and (Test-Label $ui.before 'Twitch: not set up')))
            $report.Check('signin: the window shows the code from Twitch and waits for the user',
                ((Test-Label $ui.code "^$($script:ports.twitch.user_code)$") -and (Test-Label $ui.code 'Waiting for you to confirm on twitch\.tv') -and
                 (Get-Button $ui.code 'Open twitch.tv').enabled -and (Test-Label $ui.code 'RelayDock never sees it')), $ui.code.title)
            $report.Check('signin: RelayDock asked again until Twitch granted the sign-in', ($state.twitch.token_polls -ge 15), "$($state.twitch.token_polls) questions")
            $signedIn = $run.Result.snapshots.signed_in.chat
            $report.Check('signin: afterwards the sign-in is saved and RelayDock reads the chat',
                ($signedIn.twitch_signed_in -and $signedIn.platforms.twitch.state -eq 'connected' -and
                 (Test-Label $ui.signed_in "Twitch: reading the chat of $($script:ports.twitch.login)") -and $null -ne (Get-Button $ui.signed_in 'Sign out')))
            $report.Check('signin: Sign out asks first and says how to end the permission on Twitch too',
                ((Test-Label $ui.sign_out_question 'Forget the Twitch sign-in on this PC\?') -and (Test-Label $ui.sign_out_question 'Connections')))
            $signedOut = $run.Result.snapshots.signed_out.chat
            $report.Check('signin: after Sign out nothing is saved and nothing is read',
                (-not $signedOut.twitch_signed_in -and $signedOut.platforms.twitch.state -eq 'not_set_up' -and
                 $null -ne (Get-Button $ui.signed_out 'Sign in with Twitch...')))
        }
        $report.Check('signin: the sign-in sent no client secret', (-not $state.twitch.client_secret_sent))
        Stop-Fake
    }

    # -----------------------------------------------------------------------------------------
    if (Test-Selected 'problems') {
        Write-Host ''
        Write-Host 'problems: what RelayDock says when YouTube refuses'
        Start-Fake 'problems'
        $dir = Join-Path $OutDir 'problems'
        $link = "https://youtu.be/$($script:ports.youtube.video_id)"
        $session = Start-ObsScenario -ObsRoot $ObsRoot -PluginRunDir $paths.PluginRunDir -OutDir $dir -Name 'problems' -Visible -ResetConfig -Scenario @{ steps = @(
                @{ op = 'clear' }, @{ op = 'accept_legal' }, (Get-SetupStep),
                @{ op = 'ui_show_dock'; dock = 'chat'; width = 400; height = 520 },
                # Without a key, and with a link that is no stream, nothing is sent.
                @{ op = 'chat_youtube_connect'; video = $link; expect_refused = $true },
                @{ op = 'chat_youtube_key'; key = $script:ports.youtube.api_key },
                @{ op = 'chat_youtube_connect'; video = 'my stream on YouTube'; expect_refused = $true },
                @{ op = 'snapshot'; label = 'refused' },
                @{ op = 'chat_youtube_connect'; video = $link },
                @{ op = 'chat_wait'; platform = 'youtube'; state = 'connected'; timeout_sec = 30 },
                @{ op = 'chat_wait_events'; count = 1; timeout_sec = 30 },
                # The key runs out of its daily amount.
                @{ op = 'chat_wait'; platform = 'youtube'; state = 'stopped'; timeout_sec = 40 },
                @{ op = 'wait'; seconds = 0.5 },
                @{ op = 'ui_state'; target = 'chat'; label = 'quota' },
                @{ op = 'ui_grab'; target = 'chat'; file = 'chat-quota.png' },
                @{ op = 'snapshot'; label = 'quota' },
                @{ op = 'wait'; seconds = 2 },
                @{ op = 'snapshot'; label = 'quota_later' },
                @{ op = 'chat_youtube_disconnect' },
                @{ op = 'wait'; seconds = 0.5 },
                @{ op = 'snapshot'; label = 'disconnected' },
                @{ op = 'quit' }) }
        if (Wait-Fake { param($s) $s.youtube.message_requests -ge 1 }) {
            Send-Fake '/youtube/comment' @{ id = 'y1'; author = 'Viewer'; text = 'one comment' } | Out-Null
            Start-Sleep -Seconds 2
            Send-Fake '/youtube/fail' @{ status = 403; reason = 'quotaExceeded'; times = 1000 } | Out-Null
        }
        $run = Complete-ObsScenario -Session $session -TimeoutSec 180
        $state = Get-FakeState
        Add-ObsRunChecks -Report $report -Run $run -Label 'problems' -Secrets (Get-Secrets)

        if ($run.Result -and $run.Result.snapshots.disconnected) {
            $snapshots = $run.Result.snapshots
            $report.Check('problems: without a key, or with a link that is no stream, RelayDock contacts nobody',
                ($snapshots.refused.chat.youtube_requests -eq 0 -and $snapshots.refused.chat.platforms.youtube.state -ne 'connected'))
            $quota = $snapshots.quota.chat.platforms.youtube
            $report.Check('problems: when the key has used its daily amount, the reading stops and says when it comes back',
                ($quota.state -eq 'stopped' -and $quota.message -match 'used its requests for today' -and $quota.message -match 'midnight Pacific Time'), $quota.message)
            $report.Check('problems: the dock shows that problem and leads to the chat settings',
                ((Test-Label $run.Result.ui.quota 'YouTube: stopped') -and (Test-Label $run.Result.ui.quota 'used its requests for today') -and
                 $null -ne (Get-Button $run.Result.ui.quota 'Chat settings')))
            $report.Check('problems: RelayDock does not keep asking a platform that said no',
                ($snapshots.quota_later.chat.youtube_requests -eq $snapshots.quota.chat.youtube_requests),
                "$($snapshots.quota.chat.youtube_requests) then $($snapshots.quota_later.chat.youtube_requests) requests")
            $report.Check('problems: the comment that arrived before stays in the list', (@($snapshots.quota.chat.events).Count -eq 1))
            $report.Check('problems: Disconnect switches the reading off',
                ($snapshots.disconnected.chat.platforms.youtube.state -eq 'off'), $snapshots.disconnected.chat.platforms.youtube.state)
        }
        Stop-Fake
    }
} finally {
    Stop-Fake
}

exit $report.Finish()
