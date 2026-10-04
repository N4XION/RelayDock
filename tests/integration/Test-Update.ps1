# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Updates RelayDock from inside RelayDock, the way a user does it with Update now.

.DESCRIPTION
The test installs a test build of Setup into a scratch folder, puts the test build of the plugin
in the place of the installed file, and starts a portable OBS that loads RelayDock from that
folder. RelayDock then finds itself installed. A stand-in for the release pages of the project
on this PC (rd-release-fake.exe) serves a second test build of Setup with a higher version
number, and its checksum list. The test opens the window that announces a newer version, with
that made-up release, and drives it:

  - refused: the stand-in serves an installer with one byte changed. RelayDock must not keep it
    and must not start it, and says so.
  - stopped: Cancel while the download runs. Nothing stays behind.
  - cancelled: Update now, then Cancel update, then close OBS. Nothing may be installed.
  - installed: Update now, then close OBS. The installer that RelayDock started puts the newer
    version over the old one.

Both builds of Setup are test builds with their own identity in Windows (scripts\package.ps1
-TestInstallerDir), so this test runs on a PC that has RelayDock installed and leaves that
install alone.

A test build of Setup counts only a portable OBS Studio, so your own OBS Studio may stay open.

Needs a build with RELAYDOCK_TEST_HOOKS=ON (preset windows-hooks-x64).

.EXAMPLE
.\Test-Update.ps1 -SetupPath C:\out\test\RelayDock-1.0.0-windows-x64-Setup-test.exe -NewSetupPath C:\out\test\update\RelayDock-9.9.9-windows-x64-Setup.exe -ObsRoot C:\obs-32.2.2 -BuildDir C:\RelayDock\build_hooks_x64
#>
param(
    [Parameter(Mandatory)][string]$SetupPath,
    [Parameter(Mandatory)][string]$NewSetupPath,
    [Parameter(Mandatory)][string]$ObsRoot,
    [Parameter(Mandatory)][string]$BuildDir,
    [string]$OutDir = ''
)

$ErrorActionPreference = 'Stop'
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot '..\output\update' }
Import-Module (Join-Path $PSScriptRoot 'ObsTestHarness.psm1') -Force

$paths = Get-BuildPaths -BuildDir $BuildDir
$SetupPath = (Resolve-Path -LiteralPath $SetupPath).Path
$NewSetupPath = (Resolve-Path -LiteralPath $NewSetupPath).Path
$obsVersion = (Get-Item (Join-Path $ObsRoot 'bin\64bit\obs64.exe')).VersionInfo.ProductVersion
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$report = New-TestReport -Title "Update from inside RelayDock on OBS $obsVersion"

# Only test builds of Setup. A release Setup would install for real.
foreach ($file in $SetupPath, $NewSetupPath) {
    if ((Get-Item -LiteralPath $file).VersionInfo.FileDescription -notmatch 'test build') {
        throw "$file is not a test build of Setup. Build one with scripts\package.ps1 -TestInstallerDir."
    }
}
$newName = Split-Path $NewSetupPath -Leaf
if ($newName -notmatch '^RelayDock-(\d+\.\d+\.\d+)-windows-x64-Setup\.exe$') { throw "$newName is not named like the installer of a release." }
$newVersion = $Matches[1]
$tag = "v$newVersion"
# A test build of Setup counts only an OBS Studio that was started with --portable, which is what
# this test starts. The OBS Studio of the PC's owner may stay open.
if (@(Get-CimInstance Win32_Process -Filter "Name = 'obs64.exe'" | Where-Object { $_.CommandLine -like '*--portable*' }).Count -gt 0) {
    throw 'A portable OBS Studio is running, probably from another test. Close it first.'
}

$spec = Get-Content (Join-Path $PSScriptRoot '..\..\buildspec.json') -Raw | ConvertFrom-Json
$repository = [string]$spec.repository
$testKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{6D1F3C52-8B0A-4E7D-A3C9-52E0B7F41D69}_is1'
$realKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{6D1F3C52-8B0A-4E7D-A3C9-52E0B7F41D68}_is1'
$realDll = Join-Path $env:ProgramData 'obs-studio\plugins\relaydock\bin\64bit\relaydock.dll'
$target = Join-Path $OutDir 'plugins\relaydock'
$dll = Join-Path $target 'bin\64bit\relaydock.dll'
$uninstaller = Join-Path $target 'unins000.exe'
$testDll = Join-Path $paths.PluginRunDir 'bin\relaydock.dll'

function Get-RealState {
    $file = if (Test-Path $realDll) { "$((Get-FileHash -LiteralPath $realDll -Algorithm SHA256).Hash) $((Get-Item $realDll).LastWriteTimeUtc.Ticks)" } else { 'no file' }
    $entry = if (Test-Path $realKey) { [string](Get-ItemProperty $realKey).DisplayVersion } else { 'no entry' }
    return "$file, $entry"
}
$realBefore = Get-RealState

function Wait-Until([scriptblock]$Condition, [int]$TimeoutSec = 60) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if (& $Condition) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return [bool](& $Condition)
}

function Test-Label($State, [string]$Pattern) { return @($State.labels | Where-Object { $_.text -match $Pattern }).Count -gt 0 }
function Get-Button($State, [string]$Text) { return @($State.buttons | Where-Object { $_.text -eq $Text })[0] }
function Get-InstalledVersion { if (Test-Path $testKey) { return [string](Get-ItemProperty $testKey).DisplayVersion } else { return '' } }
function Get-Hash([string]$Path) { if (Test-Path -LiteralPath $Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash } else { return '' } }

# A Setup that RelayDock started and that waits for OBS. Setup hands over to a copy of itself that
# carries the same command line.
function Get-WaitingSetup {
    return @(Get-CimInstance Win32_Process | Where-Object {
            $_.CommandLine -and $_.CommandLine.Contains('/WAITFOROBS=1') -and $_.CommandLine.Contains($newName) })
}

# What Update now keeps in the temp folder: one folder per download.
function Get-DownloadFolders { return @(Get-ChildItem -LiteralPath $env:TEMP -Directory -Filter 'RelayDock-update-*' -ErrorAction SilentlyContinue) }

# ---- The stand-in for the release pages -------------------------------------------------------------
$script:fake = $null
$script:fakeUrl = ''
function Start-Fake {
    $portFile = Join-Path $OutDir 'release-fake.port'
    Remove-Item -LiteralPath $portFile -ErrorAction SilentlyContinue
    $script:fake = Start-Process -FilePath $paths.ReleaseFake -ArgumentList "`"$portFile`"", "`"$NewSetupPath`"", $tag, $repository -PassThru -WindowStyle Hidden
    $deadline = (Get-Date).AddSeconds(15)
    while (-not (Test-Path $portFile) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100 }
    if (-not (Test-Path $portFile)) { throw 'rd-release-fake did not start.' }
    Start-Sleep -Milliseconds 200
    $script:fakeUrl = "http://127.0.0.1:$((Get-Content -LiteralPath $portFile -Raw).Trim())"
}
function Send-Fake([string]$Path) { return Invoke-RestMethod -Uri "$($script:fakeUrl)$Path" -TimeoutSec 10 }
function Stop-Fake {
    if ($script:fake -and -not $script:fake.HasExited) {
        try { Send-Fake '/control/quit' | Out-Null } catch { }
        if (-not $script:fake.WaitForExit(5000)) { $script:fake.Kill() }
    }
    $script:fake = $null
}

# A run of this test that broke off can leave its scratch install registered. Remove that one,
# and only that one: its folder lies inside this test's output folder.
if (Test-Path $testKey) {
    $location = [string](Get-ItemProperty $testKey).InstallLocation
    if (-not $location.StartsWith($OutDir, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Windows lists a test install of RelayDock that this test did not make. Remove it first.'
    }
    $old = Join-Path $location 'unins000.exe'
    if (Test-Path $old) { Start-Process -FilePath $old -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/REMOVEDATA=0' -Wait }
    [void](Wait-Until { -not (Test-Path $testKey) } 30)
    if (Test-Path $testKey) { Remove-Item -LiteralPath $testKey -Recurse -Force }
}
Get-ChildItem -LiteralPath $OutDir | Remove-Item -Recurse -Force

try {
    # ---- Install, and put the test build of the plugin in place -----------------------------------
    Write-Host ''
    Write-Host 'install: a test install that RelayDock can find itself in'
    $log = Join-Path $OutDir 'install.log'
    $setup = Start-Process -FilePath $SetupPath -Wait -PassThru -ArgumentList @('/CURRENTUSER', "/DIR=`"$target`"", "/OBSDIR=`"$ObsRoot`"",
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$log`"")
    $report.Check('install: Setup finished and Windows lists the test install', ($setup.ExitCode -eq 0 -and (Test-Path $dll) -and (Test-Path $testKey)),
        "exit code $($setup.ExitCode)")
    if (-not (Test-Path $dll)) { exit $report.Finish() }
    $oldVersion = Get-InstalledVersion
    # The installed plugin is a release build. The test build can be driven from a scenario.
    Copy-Item -LiteralPath $testDll -Destination $dll -Force
    $testHash = Get-Hash $testDll

    Start-Fake
    $release = "$($script:fakeUrl)/$repository/releases/download/$tag"
    $offer = @{ op = 'ui_open'; what = 'update'; tag = $tag
                url = "https://github.com/$repository/releases/tag/$tag"
                installer_url = "$release/$newName"; installer_name = $newName
                installer_size = (Get-Item -LiteralPath $NewSetupPath).Length
                checksums_url = "$release/SHA256SUMS.txt" }

    # OBS loads RelayDock from the installed folder. Its texts come from the build.
    $environment = @{
        OBS_PLUGINS_PATH      = (Join-Path $target 'bin\64bit')
        OBS_PLUGINS_DATA_PATH = (Join-Path $paths.PluginRunDir 'data')
    }

    # A run of this test that broke off can leave downloads in the temp folder. A file that is
    # still in use, by a Setup that runs, stays.
    foreach ($folder in Get-DownloadFolders) { Remove-Item -LiteralPath $folder.FullName -Recurse -Force -ErrorAction SilentlyContinue }

    # Something an earlier update left behind: a folder with an installer nobody runs.
    $leftover = Join-Path $env:TEMP 'RelayDock-update-00000000-test-left-over'
    New-Item -ItemType Directory -Force -Path $leftover | Out-Null
    Set-Content -LiteralPath (Join-Path $leftover 'RelayDock-0.0.1-windows-x64-Setup.exe') -Value 'not a program, made by Test-Update.ps1'
    Set-Content -LiteralPath (Join-Path $leftover 'update.request') -Value ''

    # ---- An installer that was changed on the way ---------------------------------------------------
    Write-Host ''
    Write-Host 'refused: an installer that does not match its checksum'
    Send-Fake '/control/tamper?on=1' | Out-Null
    $run = Invoke-ObsScenario -ObsRoot $ObsRoot -PluginRunDir $paths.PluginRunDir -ExtraEnvironment $environment -OutDir (Join-Path $OutDir 'refused') `
        -Name 'refused' -TimeoutSec 180 -Visible -ResetConfig -Scenario @{ steps = @(
            @{ op = 'clear' }, @{ op = 'accept_legal' },
            @{ op = 'ui_show_dock'; width = 400; height = 700 },
            @{ op = 'snapshot'; label = 'start' },
            $offer,
            @{ op = 'ui_wait'; target = 'dialog' },
            @{ op = 'wait'; seconds = 1 },
            @{ op = 'ui_state'; label = 'offer' },
            @{ op = 'ui_grab'; file = 'update-now.png' },
            @{ op = 'ui_click'; text = 'Update now' },
            @{ op = 'update_wait'; state = 'failed'; timeout_sec = 60 },
            @{ op = 'wait'; seconds = 0.5 },
            @{ op = 'ui_state'; label = 'refused' },
            @{ op = 'ui_grab'; file = 'update-refused.png' },
            @{ op = 'snapshot'; label = 'refused' },
            @{ op = 'ui_click'; text = 'Later' },
            @{ op = 'ui_wait'; target = 'dialog'; present = $false },
            @{ op = 'quit' }) }
    Add-ObsRunChecks -Report $report -Run $run -Label 'refused'
    if ($run.Result -and $run.Result.ui.refused) {
        $ui = $run.Result.ui
        $snapshots = $run.Result.snapshots
        $report.Check('refused: RelayDock, loaded from an installed folder, can update itself', [bool]$snapshots.start.update.installed_by_installer)
        $report.Check('refused: at its start RelayDock removed what an earlier update left in the temp folder',
            ((-not (Test-Path $leftover)) -and $run.Log -match 'Removed 1 downloaded installer'))
        $report.Check('refused: the window offers Update now and says what it does, in three steps',
            ($null -ne (Get-Button $ui.offer 'Update now') -and (Test-Label $ui.offer '^Update now does this:$') -and
             (Test-Label $ui.offer '^1\. Downloads the installer') -and (Test-Label $ui.offer '^2\. Checks it against the checksums') -and
             (Test-Label $ui.offer '^3\. Installs it when you close OBS Studio')))
        $report.Check('refused: it says that everything stays and that nothing is installed by itself',
            ((Test-Label $ui.offer 'destinations, settings and stream keys stay') -and (Test-Label $ui.offer 'installs nothing by itself')))
        $report.Check('refused: RelayDock says that the installer does not match its checksum and that nothing was installed',
            ((Test-Label $ui.refused 'does not match its checksum') -and (Test-Label $ui.refused 'Nothing was installed')))
        $report.Check('refused: it offers to try again, or to download in the browser',
            ($null -ne (Get-Button $ui.refused 'Try again') -and $null -ne (Get-Button $ui.refused 'Download installer')))
        $report.Check('refused: no installer was started and no request was made', ($snapshots.refused.update.state -eq 'failed' -and
            -not $snapshots.refused.update.request_file -and -not $snapshots.refused.update.installer_file))
    }
    $report.Check('refused: no Setup runs, and the changed file is not on the disk', ((Get-WaitingSetup).Count -eq 0 -and (Get-DownloadFolders).Count -eq 0))
    $report.Check('refused: the installed RelayDock is what it was', ((Get-InstalledVersion) -eq $oldVersion -and (Get-Hash $dll) -eq $testHash))
    Send-Fake '/control/tamper?on=0' | Out-Null

    # ---- Cancel while the download runs ---------------------------------------------------------------
    Write-Host ''
    Write-Host 'stopped: Cancel while the download runs'
    Send-Fake '/control/slow?ms=8000' | Out-Null
    $run = Invoke-ObsScenario -ObsRoot $ObsRoot -PluginRunDir $paths.PluginRunDir -ExtraEnvironment $environment -OutDir (Join-Path $OutDir 'stopped') `
        -Name 'stopped' -TimeoutSec 180 -Visible -Scenario @{ steps = @(
            @{ op = 'ui_show_dock'; width = 400; height = 700 },
            $offer,
            @{ op = 'ui_wait'; target = 'dialog' },
            @{ op = 'ui_click'; text = 'Update now' },
            @{ op = 'update_wait'; state = 'downloading'; timeout_sec = 10 },
            @{ op = 'wait'; seconds = 1.5 },
            @{ op = 'ui_state'; label = 'downloading' },
            @{ op = 'ui_grab'; file = 'update-downloading.png' },
            @{ op = 'ui_click'; text = 'Cancel' },
            @{ op = 'update_wait'; state = 'idle'; timeout_sec = 10 },
            @{ op = 'wait'; seconds = 0.5 },
            @{ op = 'ui_state'; label = 'stopped' },
            @{ op = 'snapshot'; label = 'stopped' },
            @{ op = 'ui_click'; text = 'Later' },
            @{ op = 'ui_wait'; target = 'dialog'; present = $false },
            @{ op = 'quit' }) }
    Add-ObsRunChecks -Report $report -Run $run -Label 'stopped'
    if ($run.Result -and $run.Result.ui.stopped) {
        $ui = $run.Result.ui
        $report.Check('stopped: while it downloads, the window shows the progress and offers only Cancel',
            ((Test-Label $ui.downloading '^Downloading the installer') -and $null -ne (Get-Button $ui.downloading 'Cancel') -and
             $null -eq (Get-Button $ui.downloading 'Update now') -and $null -eq (Get-Button $ui.downloading 'Later')))
        $report.Check('stopped: Cancel ends the download at once and the window offers Update now again',
            ($run.Result.snapshots.stopped.update.state -eq 'idle' -and $null -ne (Get-Button $ui.stopped 'Update now')))
    }
    $report.Check('stopped: no Setup runs and nothing of the download is on the disk', ((Get-WaitingSetup).Count -eq 0 -and (Get-DownloadFolders).Count -eq 0))
    Send-Fake '/control/slow?ms=0' | Out-Null

    # ---- Update now, then change your mind -------------------------------------------------------------
    Write-Host ''
    Write-Host 'cancelled: Update now, Cancel update, close OBS'
    $run = Invoke-ObsScenario -ObsRoot $ObsRoot -PluginRunDir $paths.PluginRunDir -ExtraEnvironment $environment -OutDir (Join-Path $OutDir 'cancelled') `
        -Name 'cancelled' -TimeoutSec 180 -Visible -Scenario @{ steps = @(
            @{ op = 'ui_show_dock'; width = 400; height = 700 },
            $offer,
            @{ op = 'ui_wait'; target = 'dialog' },
            @{ op = 'ui_click'; text = 'Update now' },
            @{ op = 'update_wait'; state = 'waiting'; timeout_sec = 60 },
            @{ op = 'wait'; seconds = 3 },
            @{ op = 'ui_state'; label = 'ready' },
            @{ op = 'ui_grab'; file = 'update-ready.png' },
            @{ op = 'ui_state'; target = 'dock'; label = 'dock_ready' },
            @{ op = 'snapshot'; label = 'ready' },
            @{ op = 'ui_click'; text = 'Cancel update' },
            @{ op = 'wait'; seconds = 1 },
            @{ op = 'ui_state'; label = 'cancelled' },
            @{ op = 'ui_state'; target = 'dock'; label = 'dock_cancelled' },
            @{ op = 'snapshot'; label = 'cancelled' },
            @{ op = 'ui_click'; text = 'Later' },
            @{ op = 'ui_wait'; target = 'dialog'; present = $false },
            @{ op = 'quit' }) }
    Add-ObsRunChecks -Report $report -Run $run -Label 'cancelled'
    if ($run.Result -and $run.Result.ui.cancelled) {
        $ui = $run.Result.ui
        $snapshots = $run.Result.snapshots
        $requestFile = [string]$snapshots.ready.update.request_file
        $installerFile = [string]$snapshots.ready.update.installer_file
        $report.Check('cancelled: after the download the window says that the update installs when OBS closes',
            ((Test-Label $ui.ready "RelayDock $([regex]::Escape($newVersion)) is downloaded and checked\. It installs when you close OBS Studio\.") -and
             $null -ne (Get-Button $ui.ready 'Cancel update') -and $null -ne (Get-Button $ui.ready 'Close OBS and install') -and
             $null -ne (Get-Button $ui.ready 'Close')))
        $report.Check('cancelled: the dock says it too', (Test-Label $ui.dock_ready "RelayDock $([regex]::Escape($newVersion)) installs when you close OBS Studio"))
        $report.Check('cancelled: the installer is the file of the release, saved under its own name in a folder of its own',
            ($snapshots.ready.update.state -eq 'waiting' -and $installerFile -like "$env:TEMP\RelayDock-update-*\$newName" -and $requestFile))
        $report.Check('cancelled: Cancel update takes the request back, and the window offers Update now again',
            ($snapshots.cancelled.update.state -eq 'idle' -and $null -ne (Get-Button $ui.cancelled 'Update now') -and
             $requestFile -and -not (Test-Path $requestFile)), $requestFile)
        $report.Check('cancelled: the dock no longer says that an update waits',
            (-not (Test-Label $ui.dock_cancelled 'installs when you close OBS Studio')))
    }
    # OBS has closed. A Setup that had not understood the cancel would install now.
    $ended = Wait-Until { (Get-WaitingSetup).Count -eq 0 } 30
    Start-Sleep -Seconds 3
    $report.Check('cancelled: the waiting Setup ended, and closing OBS afterwards installed nothing',
        ($ended -and (Get-InstalledVersion) -eq $oldVersion -and (Get-Hash $dll) -eq $testHash), (Get-InstalledVersion))

    # ---- Update now, and close OBS -----------------------------------------------------------------------
    Write-Host ''
    Write-Host 'installed: Update now, close OBS'
    $before = Send-Fake '/control/state'
    $run = Invoke-ObsScenario -ObsRoot $ObsRoot -PluginRunDir $paths.PluginRunDir -ExtraEnvironment $environment -OutDir (Join-Path $OutDir 'installed') `
        -Name 'installed' -TimeoutSec 180 -Visible -Scenario @{ steps = @(
            @{ op = 'ui_show_dock'; width = 400; height = 700 },
            $offer,
            @{ op = 'ui_wait'; target = 'dialog' },
            @{ op = 'ui_click'; text = 'Update now' },
            @{ op = 'update_wait'; state = 'waiting'; timeout_sec = 60 },
            @{ op = 'wait'; seconds = 3 },
            @{ op = 'snapshot'; label = 'ready' },
            @{ op = 'ui_click'; text = 'Close' },
            @{ op = 'ui_wait'; target = 'dialog'; present = $false },
            # Settings, Updates says the same, and the uninstall waits its turn.
            @{ op = 'ui_open'; what = 'settings'; page = 'updates' },
            @{ op = 'ui_wait'; target = 'dialog' },
            @{ op = 'wait'; seconds = 0.6 },
            @{ op = 'ui_state'; label = 'page' },
            @{ op = 'ui_grab'; file = 'updates-pending.png' },
            @{ op = 'ui_close' },
            @{ op = 'ui_wait'; target = 'dialog'; present = $false },
            # The window comes back as it was left, and its button closes OBS.
            $offer,
            @{ op = 'ui_wait'; target = 'dialog' },
            @{ op = 'wait'; seconds = 0.5 },
            @{ op = 'ui_state'; label = 'again' },
            @{ op = 'quit'; with = 'Close OBS and install' }) }
    Add-ObsRunChecks -Report $report -Run $run -Label 'installed'
    if ($run.Result -and $run.Result.ui.again) {
        $report.Check('installed: opened again, the window still says that the update waits, and Close OBS and install closes OBS',
            ((Test-Label $run.Result.ui.again 'is downloaded and checked') -and $null -ne (Get-Button $run.Result.ui.again 'Close OBS and install') -and
             $run.Log -match 'closing OBS with the button "Close OBS and install"'))
    }
    $requestFile = if ($run.Result) { [string]$run.Result.snapshots.ready.update.request_file } else { '' }
    if ($run.Result -and $run.Result.ui.page) {
        $page = $run.Result.ui.page
        $report.Check('installed: Settings, Updates says that the update installs when OBS closes, and offers Cancel update',
            ((Test-Label $page "RelayDock $([regex]::Escape($newVersion)) installs when you close OBS Studio") -and $null -ne (Get-Button $page 'Cancel update')))
        $report.Check('installed: Uninstall waits its turn while an update is under way, and the page says why',
        (-not (Get-Button $page 'Uninstall RelayDock...').enabled -and (Test-Label $page 'An update is under way')))
    }
    $after = Send-Fake '/control/state'
    $report.Check('installed: RelayDock asked for the checksum list and for the installer, and was handed on to the file',
        ($after.list_asked -gt $before.list_asked -and $after.installer_asked -gt $before.installer_asked -and $after.file_sent -gt $before.file_sent))
    $report.Check('installed: the log says what happened, step by step',
        ($run.Log -match "Update now: downloading RelayDock $([regex]::Escape($newVersion))" -and $run.Log -match 'is downloaded and checked' -and
         $run.Log -match "OBS closes\. The installer of RelayDock $([regex]::Escape($newVersion)) goes ahead"))
    $installed = Wait-Until { (Get-InstalledVersion) -eq $newVersion } 90
    $report.Check("installed: once OBS has closed, the Setup that RelayDock started installs $newVersion", $installed, (Get-InstalledVersion))
    $report.Check('installed: the plugin file was replaced by the one from the newer Setup',
        ((Wait-Until { (Get-WaitingSetup).Count -eq 0 } 60) -and (Test-Path $dll) -and (Get-Hash $dll) -ne $testHash))
    $report.Check('installed: the request file is gone and no Setup is left waiting',
        ((Get-WaitingSetup).Count -eq 0 -and $requestFile -and -not (Test-Path $requestFile)))
} finally {
    Stop-Fake
    # ---- Leave nothing behind ------------------------------------------------------------------------
    [void](Wait-Until { (Get-WaitingSetup).Count -eq 0 } 30)
    if (Test-Path $uninstaller) {
        Start-Process -FilePath $uninstaller -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/REMOVEDATA=0' -Wait
        [void](Wait-Until { -not (Test-Path $target) } 30)
    }
    # The download of the last run. Its Setup has ended, so the file can go.
    foreach ($folder in Get-DownloadFolders) { Remove-Item -LiteralPath $folder.FullName -Recurse -Force -ErrorAction SilentlyContinue }
}
$report.Check('nothing of the test install is left on this PC', (-not (Test-Path $testKey) -and -not (Test-Path $target) -and (Get-DownloadFolders).Count -eq 0))
$report.Check('a RelayDock that is installed for real on this PC is exactly as it was', ((Get-RealState) -eq $realBefore), $realBefore)
exit $report.Finish()
