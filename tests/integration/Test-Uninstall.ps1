# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Uninstalls RelayDock from inside RelayDock, the way a user does it under Settings, Updates.

.DESCRIPTION
The test installs a test build of Setup into a scratch folder, puts the test build of the plugin
in the place of the installed file, and starts a portable OBS that loads RelayDock from that
folder. RelayDock then finds itself installed, and the test drives its own windows:

  - keep: ask for the uninstall, see that it waits, change your mind with Keep RelayDock, close
    OBS. Nothing may be removed.
  - remove: ask again and close OBS. The uninstaller that RelayDock started removes the plugin
    and its entry under Installed apps.

The box "Also remove my destinations, settings and saved stream keys" is never ticked here. That
part of the uninstaller works on the real settings folder of the PC, and Test-Installer.ps1
covers it under its own guard.

A test build of Setup has its own identity in Windows (scripts\package.ps1 -TestInstallerDir), so
this test runs on a PC that has RelayDock installed and leaves that install alone.

Needs a build with RELAYDOCK_TEST_HOOKS=ON (preset windows-hooks-x64).

.EXAMPLE
.\Test-Uninstall.ps1 -SetupPath C:\out\test\RelayDock-1.0.0-windows-x64-Setup-test.exe -ObsRoot C:\obs-32.2.2 -BuildDir C:\RelayDock\build_hooks_x64
#>
param(
    [Parameter(Mandatory)][string]$SetupPath,
    [Parameter(Mandatory)][string]$ObsRoot,
    [Parameter(Mandatory)][string]$BuildDir,
    [string]$OutDir = ''
)

$ErrorActionPreference = 'Stop'
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot '..\output\uninstall' }
Import-Module (Join-Path $PSScriptRoot 'ObsTestHarness.psm1') -Force

$paths = Get-BuildPaths -BuildDir $BuildDir
$SetupPath = (Resolve-Path -LiteralPath $SetupPath).Path
$obsVersion = (Get-Item (Join-Path $ObsRoot 'bin\64bit\obs64.exe')).VersionInfo.ProductVersion
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$report = New-TestReport -Title "Uninstall from inside RelayDock on OBS $obsVersion"

# Only a test build of Setup. A release Setup would install for real.
if ((Get-Item -LiteralPath $SetupPath).VersionInfo.FileDescription -notmatch 'test build') {
    throw "$SetupPath is not a test build of Setup. Build one with scripts\package.ps1 -TestInstallerDir."
}
if (Get-Process obs64 -ErrorAction SilentlyContinue) { throw 'Close OBS Studio first. Setup refuses to run while it is open.' }

$testKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{6D1F3C52-8B0A-4E7D-A3C9-52E0B7F41D69}_is1'
$realKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{6D1F3C52-8B0A-4E7D-A3C9-52E0B7F41D68}_is1'
$realDll = Join-Path $env:ProgramData 'obs-studio\plugins\relaydock\bin\64bit\relaydock.dll'
$target = Join-Path $OutDir 'plugins\relaydock'
$dll = Join-Path $target 'bin\64bit\relaydock.dll'
$uninstaller = Join-Path $target 'unins000.exe'

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

# The uninstaller hands over to a copy of itself that carries the same command line.
function Get-WaitingUninstall {
    return @(Get-CimInstance Win32_Process | Where-Object {
            $_.CommandLine -and $_.CommandLine.Contains('/WAITFOROBS=1') -and $_.CommandLine.Contains($uninstaller) })
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

# ---- Install, and put the test build of the plugin in place ---------------------------------------
Write-Host ''
Write-Host 'install: a test install that RelayDock can find itself in'
$log = Join-Path $OutDir 'install.log'
$setup = Start-Process -FilePath $SetupPath -Wait -PassThru -ArgumentList @('/CURRENTUSER', "/DIR=`"$target`"", "/OBSDIR=`"$ObsRoot`"",
    '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$log`"")
$report.Check('install: Setup finished and Windows lists the test install', ($setup.ExitCode -eq 0 -and (Test-Path $dll) -and (Test-Path $testKey)),
    "exit code $($setup.ExitCode)")
if (-not (Test-Path $dll)) { exit $report.Finish() }
# The installed plugin is a release build. The test build can be driven from a scenario.
Copy-Item -LiteralPath (Join-Path $paths.PluginRunDir 'bin\relaydock.dll') -Destination $dll -Force

# OBS loads RelayDock from the installed folder. Its texts come from the build.
$environment = @{
    OBS_PLUGINS_PATH      = (Join-Path $target 'bin\64bit')
    OBS_PLUGINS_DATA_PATH = (Join-Path $paths.PluginRunDir 'data')
}

# ---- Ask for it, then keep RelayDock ---------------------------------------------------------------
Write-Host ''
Write-Host 'keep: ask for the uninstall, change your mind, close OBS'
$run = Invoke-ObsScenario -ObsRoot $ObsRoot -PluginRunDir $paths.PluginRunDir -ExtraEnvironment $environment -OutDir (Join-Path $OutDir 'keep') `
    -Name 'keep' -TimeoutSec 180 -Visible -ResetConfig -Scenario @{ steps = @(
        @{ op = 'clear' }, @{ op = 'accept_legal' },
        @{ op = 'ui_show_dock'; width = 400; height = 700 },
        @{ op = 'snapshot'; label = 'start' },
        @{ op = 'ui_open'; what = 'settings'; page = 'updates' },
        @{ op = 'ui_wait'; target = 'dialog' },
        @{ op = 'wait'; seconds = 0.6 },
        @{ op = 'ui_state'; label = 'page' },
        @{ op = 'ui_grab'; file = 'updates-uninstall.png' },
        @{ op = 'ui_click'; text = 'Uninstall RelayDock...' },
        @{ op = 'ui_wait'; target = 'message' },
        @{ op = 'ui_state'; target = 'message'; label = 'question' },
        @{ op = 'ui_click'; target = 'message'; text = 'Yes' },
        @{ op = 'ui_wait'; target = 'message'; present = $false },
        @{ op = 'wait'; seconds = 4 },
        @{ op = 'ui_state'; label = 'pending' },
        @{ op = 'ui_grab'; file = 'updates-uninstall-pending.png' },
        @{ op = 'snapshot'; label = 'pending' },
        @{ op = 'ui_click'; text = 'Keep RelayDock' },
        @{ op = 'wait'; seconds = 1 },
        @{ op = 'ui_state'; label = 'kept' },
        @{ op = 'snapshot'; label = 'kept' },
        @{ op = 'ui_close' },
        @{ op = 'ui_wait'; target = 'dialog'; present = $false },
        @{ op = 'quit' }) }
Add-ObsRunChecks -Report $report -Run $run -Label 'keep'

function Test-Label($State, [string]$Pattern) { return @($State.labels | Where-Object { $_.text -match $Pattern }).Count -gt 0 }
function Get-Button($State, [string]$Text) { return @($State.buttons | Where-Object { $_.text -eq $Text })[0] }

if ($run.Result -and $run.Result.ui.kept) {
    $ui = $run.Result.ui
    $snapshots = $run.Result.snapshots
    $plan = $snapshots.start.uninstall
    $report.Check('keep: RelayDock finds the uninstaller of the folder it runs from',
        ($plan.kind -eq 'installer' -and $plan.program -eq $uninstaller), "$($plan.kind) $($plan.program)")
    $report.Check('keep: the uninstaller will wait for OBS, ask nothing and keep settings and keys',
        ($plan.arguments -like '/WAITFOROBS=1 /SILENT /REMOVEDATA=0*'), $plan.arguments)
    $button = Get-Button $ui.page 'Uninstall RelayDock...'
    $box = @($ui.page.checks | Where-Object { $_.text -like 'Also remove my destinations*' })[0]
    $report.Check('keep: Settings, Updates offers the uninstall, with keeping settings and keys as the default',
        ($null -ne $button -and $button.enabled -and $null -ne $box -and -not $box.checked -and (Test-Label $ui.page '^Uninstall$')))
    $report.Check('keep: RelayDock asks first and says what stays',
        ((Test-Label $ui.question 'Remove RelayDock from this PC\?') -and (Test-Label $ui.question 'stay for a later install')))
    $report.Check('keep: after Yes, the page says RelayDock goes when OBS closes, and offers Keep RelayDock',
        ((Test-Label $ui.pending 'RelayDock is removed when you close OBS Studio') -and $null -ne (Get-Button $ui.pending 'Keep RelayDock') -and
         -not (Get-Button $ui.pending 'Uninstall RelayDock...').enabled -and $snapshots.pending.uninstall.pending))
    $requestFile = [string]$snapshots.pending.uninstall.request_file
    $report.Check('keep: Keep RelayDock withdraws the request, and the page says that RelayDock stays',
        ((Test-Label $ui.kept 'The uninstall is cancelled\. RelayDock stays\.') -and (Get-Button $ui.kept 'Uninstall RelayDock...').enabled -and
         -not $snapshots.kept.uninstall.pending -and $requestFile -and -not (Test-Path $requestFile)), $requestFile)
}
# OBS has closed. An uninstaller that had not understood the cancel would remove RelayDock now.
$ended = Wait-Until { (Get-WaitingUninstall).Count -eq 0 } 30
Start-Sleep -Seconds 3
$report.Check('keep: the waiting uninstaller ended, and closing OBS afterwards removed nothing',
    ($ended -and (Test-Path $dll) -and (Test-Path $uninstaller) -and (Test-Path $testKey)))

# ---- Ask for it and close OBS -----------------------------------------------------------------------
Write-Host ''
Write-Host 'remove: ask for the uninstall and close OBS'
if ((Test-Path $dll) -and (Test-Path $testKey)) {
    $run = Invoke-ObsScenario -ObsRoot $ObsRoot -PluginRunDir $paths.PluginRunDir -ExtraEnvironment $environment -OutDir (Join-Path $OutDir 'remove') `
        -Name 'remove' -TimeoutSec 180 -Visible -Scenario @{ steps = @(
            @{ op = 'ui_show_dock'; width = 400; height = 700 },
            @{ op = 'ui_open'; what = 'settings'; page = 'updates' },
            @{ op = 'ui_wait'; target = 'dialog' },
            @{ op = 'wait'; seconds = 0.6 },
            @{ op = 'ui_click'; text = 'Uninstall RelayDock...' },
            @{ op = 'ui_wait'; target = 'message' },
            @{ op = 'ui_click'; target = 'message'; text = 'Yes' },
            @{ op = 'ui_wait'; target = 'message'; present = $false },
            @{ op = 'wait'; seconds = 3 },
            @{ op = 'snapshot'; label = 'pending' },
            @{ op = 'ui_close' },
            @{ op = 'ui_wait'; target = 'dialog'; present = $false },
            @{ op = 'quit' }) }
    Add-ObsRunChecks -Report $report -Run $run -Label 'remove'
    $requestFile = if ($run.Result) { [string]$run.Result.snapshots.pending.uninstall.request_file } else { '' }
    $report.Check('remove: with the uninstall asked for, the files are still there while OBS runs',
        ($run.Result -and $run.Result.snapshots.pending.uninstall.pending -and $requestFile))
    $gone = Wait-Until { -not (Test-Path $target) -and -not (Test-Path $testKey) } 60
    $report.Check('remove: once OBS has closed, the uninstaller RelayDock started removes the plugin folder and the entry under Installed apps', $gone)
    $report.Check('remove: the request file is gone and no uninstaller is left waiting',
        ((Wait-Until { (Get-WaitingUninstall).Count -eq 0 } 30) -and $requestFile -and -not (Test-Path $requestFile)))
} else {
    $report.Skip('remove: the uninstaller RelayDock started removes the plugin', 'the test install did not survive the first part')
}

# ---- Leave nothing behind ----------------------------------------------------------------------------
if (Test-Path $uninstaller) {
    Start-Process -FilePath $uninstaller -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/REMOVEDATA=0' -Wait
    [void](Wait-Until { -not (Test-Path $target) } 30)
}
$report.Check('nothing of the test install is left on this PC', (-not (Test-Path $testKey) -and -not (Test-Path $target)))
$report.Check('a RelayDock that is installed for real on this PC is exactly as it was', ((Get-RealState) -eq $realBefore), $realBefore)
exit $report.Finish()
