# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Runs the RelayDock installer and its uninstaller and checks what they did.

.DESCRIPTION
Everything runs silently and without administrator rights, into a scratch folder:

  - a first install: the files, their layout, and the entry under Installed apps
  - an install over an existing one
  - an install while OBS runs, which Setup must refuse
  - an install on a PC without OBS, which Setup must warn about
  - an uninstall that keeps settings and saved keys
  - an uninstall that removes them (/REMOVEDATA=1)

Also covered: the uninstall that RelayDock starts itself (/WAITFOROBS=1). It waits while OBS is
open, removes RelayDock once OBS has closed, and removes nothing when RelayDock has deleted its
request file, whether OBS still runs at that moment or has closed right after.

The test never touches a real RelayDock. With the release Setup it stops at once when RelayDock
is installed on this PC. With -TestBuild and a test build of Setup (scripts\package.ps1
-TestInstallerDir), which Windows knows under another identity, it also runs on such a PC. The part that removes settings and keys is skipped when this PC has RelayDock settings or
saved keys, because that part would delete them. It works on a folder and a credential it makes
up itself.

With -DefaultFolder the test also installs into the folder OBS reads, which is
C:\ProgramData\obs-studio\plugins\relaydock, and removes it again. OBS is never started from
there. It is skipped when the account cannot write to that folder, because Setup would then ask
for administrator rights and wait for a person.

What this test cannot show: an installed OBS loading RelayDock from that folder. That takes an
installed OBS and its real settings, and tests do not touch those.

.EXAMPLE
.\Test-Installer.ps1 -SetupPath .\release\RelayDock-1.0.0-windows-x64-Setup.exe -ObsRoot C:\obs-test -ZipPath .\release\RelayDock-1.0.0-windows-x64.zip
#>
param(
    [Parameter(Mandatory)][string]$SetupPath,
    # A portable OBS. Started once to check that Setup refuses while OBS runs.
    [Parameter(Mandatory)][string]$ObsRoot,
    # The ZIP of the same release. The installed files must match its files.
    [string]$ZipPath = '',
    [string]$OutDir = '',
    [switch]$DefaultFolder,
    # The Setup is a test build with its own identity in Windows.
    [switch]$TestBuild
)

$ErrorActionPreference = 'Stop'
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot '..\output\installer' }
Import-Module (Join-Path $PSScriptRoot 'ObsTestHarness.psm1') -Force

$SetupPath = (Resolve-Path -LiteralPath $SetupPath).Path
if ($ZipPath) { $ZipPath = (Resolve-Path -LiteralPath $ZipPath).Path }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

$report = New-TestReport -Title "Installer $(Split-Path -Leaf $SetupPath)"

# A test build of Setup says so in its file description. Check before anything runs: a release
# Setup started the way this test starts a test build would install for real.
$isTestBuild = (Get-Item -LiteralPath $SetupPath).VersionInfo.FileDescription -match 'test build'
if ($TestBuild -and -not $isTestBuild) { throw "-TestBuild was given, and $SetupPath is not a test build of Setup." }
if ($isTestBuild -and -not $TestBuild) { throw "$SetupPath is a test build of Setup. Run this test with -TestBuild." }
if ($TestBuild -and $DefaultFolder) { throw 'A test build of Setup never installs into the folder OBS reads. Leave -DefaultFolder out.' }

$appId = if ($TestBuild) { '{6D1F3C52-8B0A-4E7D-A3C9-52E0B7F41D69}' } else { '{6D1F3C52-8B0A-4E7D-A3C9-52E0B7F41D68}' }
$displayName = if ($TestBuild) { 'RelayDock test install (OBS Studio plugin)' } else { 'RelayDock (OBS Studio plugin)' }
$uninstallKeys = @(
    "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\${appId}_is1",
    "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\${appId}_is1")
$userKey = $uninstallKeys[0]
$realFolder = Join-Path $env:ProgramData 'obs-studio\plugins\relaydock'
$settingsFolder = Join-Path $env:APPDATA 'obs-studio\plugin_config\relaydock'

if (Get-Process obs64 -ErrorAction SilentlyContinue) { throw 'Close OBS Studio first. Setup refuses to run while it is open.' }

# An earlier run of this test that broke off can leave its scratch install registered. Remove
# that one, and only that one: its folder lies inside this test's output folder.
if (Test-Path $userKey) {
    $location = [string](Get-ItemProperty $userKey).InstallLocation
    if ($location.StartsWith($OutDir, [System.StringComparison]::OrdinalIgnoreCase)) {
        $old = Join-Path $location 'unins000.exe'
        if (Test-Path $old) { Start-Process -FilePath $old -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait }
        if (Test-Path $userKey) { Remove-Item -LiteralPath $userKey -Recurse -Force }
    }
}

# ---- Never on a PC that has RelayDock, unless Setup is a test build ------------------------------
if (@($uninstallKeys | Where-Object { Test-Path $_ }).Count -gt 0) {
    throw 'Windows lists an install with the identity this Setup uses. This test installs and removes it, so it does not run here.'
}
if (-not $TestBuild -and (Test-Path $realFolder)) {
    throw 'RelayDock is installed on this PC. This test installs and removes RelayDock, so it does not run here. Use a test build of Setup and -TestBuild.'
}
# What a real RelayDock on this PC looks like before the test, to show that the test left it alone.
$realDll = Join-Path $realFolder 'bin\64bit\relaydock.dll'
$realBefore = if (Test-Path $realDll) { "$((Get-FileHash -LiteralPath $realDll -Algorithm SHA256).Hash) $((Get-Item $realDll).LastWriteTimeUtc.Ticks)" } else { '' }
$realKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{6D1F3C52-8B0A-4E7D-A3C9-52E0B7F41D68}_is1'
$realEntryBefore = if ($TestBuild -and (Test-Path $realKey)) { [string](Get-ItemProperty $realKey).DisplayVersion } else { '' }
Get-ChildItem -LiteralPath $OutDir | Remove-Item -Recurse -Force

function Invoke-Program([string]$File, [string[]]$Arguments, [string]$LogName) {
    $log = Join-Path $OutDir $LogName
    $all = @($Arguments) + @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$log`"")
    $process = Start-Process -FilePath $File -ArgumentList $all -Wait -PassThru
    $text = if (Test-Path $log) { Get-Content -LiteralPath $log -Raw } else { '' }
    return [pscustomobject]@{ ExitCode = $process.ExitCode; Log = $text }
}

# The uninstaller hands over to a copy of itself and returns before that copy is done.
function Wait-Until([scriptblock]$Condition, [int]$TimeoutSec = 60) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if (& $Condition) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return [bool](& $Condition)
}

function Get-FileSha([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }

$target = Join-Path $OutDir 'plugins\relaydock'
$installArguments = @('/CURRENTUSER', "/DIR=`"$target`"", "/OBSDIR=`"$ObsRoot`"")
$dll = Join-Path $target 'bin\64bit\relaydock.dll'
$expected = @('bin\64bit\relaydock.dll', 'data\locale\en-US.ini', 'LICENSE.txt', 'THIRD_PARTY_LICENSES.txt')

# ---- A first install ----------------------------------------------------------------------------
Write-Host ''
Write-Host 'install: a first install into a scratch folder'
$run = Invoke-Program $SetupPath $installArguments 'install.log'
$report.Check('install: Setup finished', ($run.ExitCode -eq 0), "exit code $($run.ExitCode)")
$report.Check('install: it ran without administrator rights', ($run.Log -match 'Administrative install mode: No'))
$report.Check('install: it found the OBS it was pointed at and named its version', ($run.Log -match 'RelayDock Setup: found OBS Studio \d+\.\d+'))
$missing = @($expected | Where-Object { -not (Test-Path (Join-Path $target $_)) })
$report.Check('install: the plugin, its text and both licence files are in place', ($missing.Count -eq 0), ($missing -join ', '))
$report.Check('install: Windows lists RelayDock under Installed apps, for this user only',
    ((Test-Path $userKey) -and -not (Test-Path $uninstallKeys[1])))
if (Test-Path $userKey) {
    $entry = Get-ItemProperty $userKey
    $report.Check('install: the entry names RelayDock, its version and its folder',
        ($entry.DisplayName -eq $displayName -and [string]$entry.DisplayVersion -ne '' -and
         ([string]$entry.InstallLocation).TrimEnd('\') -eq $target),
        "$($entry.DisplayName) $($entry.DisplayVersion)")
}
if ($TestBuild) {
    $realNow = if (Test-Path $realDll) { "$((Get-FileHash -LiteralPath $realDll -Algorithm SHA256).Hash) $((Get-Item $realDll).LastWriteTimeUtc.Ticks)" } else { '' }
    $report.Check('install: the folder OBS reads is as it was, because the test named another one', ($realNow -eq $realBefore))

    # A test build without /DIR must refuse, before it does anything.
    $run = Invoke-Program $SetupPath @('/CURRENTUSER', "/OBSDIR=`"$ObsRoot`"") 'test-build-no-dir.log'
    $realNow = if (Test-Path $realDll) { "$((Get-FileHash -LiteralPath $realDll -Algorithm SHA256).Hash) $((Get-Item $realDll).LastWriteTimeUtc.Ticks)" } else { '' }
    $report.Check('test-build: without /DIR a test build of Setup refuses to run and changes nothing',
        ($run.ExitCode -ne 0 -and $realNow -eq $realBefore), "exit code $($run.ExitCode)")
} else {
    $report.Check('install: nothing went into the folder OBS reads, because the test named another one', (-not (Test-Path $realFolder)))
}

if ($ZipPath) {
    $unzipped = Join-Path $OutDir 'zip'
    Expand-Archive -LiteralPath $ZipPath -DestinationPath $unzipped
    $different = @($expected | Where-Object {
            -not (Test-Path (Join-Path $target $_)) -or (Get-FileSha (Join-Path $target $_)) -ne (Get-FileSha (Join-Path $unzipped "relaydock\$_")) })
    $report.Check('install: every installed file is identical to the file in the ZIP', ($different.Count -eq 0), ($different -join ', '))
} else {
    $report.Skip('install: every installed file is identical to the file in the ZIP', 'no -ZipPath given')
}

# ---- Over an existing install -------------------------------------------------------------------
Write-Host ''
Write-Host 'upgrade: an install over an existing one'
$before = if (Test-Path $dll) { Get-FileSha $dll } else { '' }
$run = Invoke-Program $SetupPath $installArguments 'upgrade.log'
$report.Check('upgrade: Setup finished', ($run.ExitCode -eq 0), "exit code $($run.ExitCode)")
$report.Check('upgrade: the files are in place and there is still one entry under Installed apps',
    ((Test-Path $dll) -and (Get-FileSha $dll) -eq $before -and (Test-Path $userKey)))

# ---- While OBS runs -----------------------------------------------------------------------------
Write-Host ''
Write-Host 'obs-running: Setup refuses while OBS is open'
Initialize-ObsTestConfig -ObsRoot $ObsRoot | Out-Null
$session = Start-ObsTest -ObsRoot $ObsRoot -NoPlugin
try {
    [void](Wait-ObsLogLine -Session $session -Pattern '==== Startup complete' -TimeoutSec 90)
    $stamp = (Get-Item $dll).LastWriteTimeUtc
    $run = Invoke-Program $SetupPath $installArguments 'obs-running.log'
    $report.Check('obs-running: Setup stopped without installing', ($run.ExitCode -ne 0), "exit code $($run.ExitCode)")
    $report.Check('obs-running: it said that OBS Studio is running', ($run.Log -match 'OBS Studio is running'))
    $report.Check('obs-running: the installed file was not touched', ((Get-Item $dll).LastWriteTimeUtc -eq $stamp))
} finally {
    $stopped = Stop-ObsTest -Session $session
}
$report.Check('obs-running: OBS itself was not disturbed and closed cleanly', $stopped.Clean, "exit code $($stopped.ExitCode)")

# ---- The uninstall RelayDock starts itself ------------------------------------------------------
# RelayDock makes a request file and the uninstaller waits for OBS to close. Deleting the file is
# how the user changes their mind. The test stands in for RelayDock: it makes the file and starts
# the uninstaller the same way.
Write-Host ''
Write-Host 'wait: the uninstaller that RelayDock starts waits for OBS to close'
$uninstaller = Join-Path $target 'unins000.exe'
$request = Join-Path $OutDir 'uninstall.request'
function Start-WaitingUninstall([string]$LogName) {
    $log = Join-Path $OutDir $LogName
    Set-Content -LiteralPath $request -Value 'made by Test-Installer.ps1'
    Start-Process -FilePath $uninstaller -ArgumentList @('/WAITFOROBS=1', '/SILENT', '/REMOVEDATA=0', "/REQUESTFILE=`"$request`"",
        '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$log`"")
}
function Get-LogText([string]$LogName) {
    $log = Join-Path $OutDir $LogName
    if (Test-Path $log) { return (Get-Content -LiteralPath $log -Raw) }
    return ''
}
# The uninstaller hands over to a copy of itself in the temp folder, which carries the same
# command line. These are the copies of this test's uninstaller that are still waiting.
function Get-WaitingUninstall {
    return @(Get-CimInstance Win32_Process | Where-Object {
            $_.CommandLine -and $_.CommandLine.Contains('/WAITFOROBS=1') -and $_.CommandLine.Contains($uninstaller) })
}
$session = Start-ObsTest -ObsRoot $ObsRoot -NoPlugin
try {
    [void](Wait-ObsLogLine -Session $session -Pattern '==== Startup complete' -TimeoutSec 90)

    # The user changes their mind while OBS still runs.
    Start-WaitingUninstall 'wait-cancelled.log'
    Start-Sleep -Seconds 4
    $report.Check('wait: with OBS open, the uninstaller waits and removes nothing',
        ((Get-WaitingUninstall).Count -ge 1 -and (Test-Path $dll) -and (Test-Path $userKey)))
    Remove-Item -LiteralPath $request -Force
    $ended = Wait-Until { (Get-WaitingUninstall).Count -eq 0 } 20
    $report.Check('wait: when RelayDock withdraws the request while OBS still runs, the uninstaller ends and removes nothing',
        ($ended -and (Test-Path $dll) -and (Test-Path $userKey) -and (Get-LogText 'wait-cancelled.log') -match 'cancelled from RelayDock'))

    # The user changes their mind and closes OBS in the same moment.
    Start-WaitingUninstall 'wait-cancelled-at-close.log'
    Start-Sleep -Seconds 3
    Remove-Item -LiteralPath $request -Force
} finally {
    $stopped = Stop-ObsTest -Session $session
}
$ended = Wait-Until { (Get-WaitingUninstall).Count -eq 0 } 30
$report.Check('wait: a request withdrawn in the moment OBS closes removes nothing either',
    ($ended -and (Test-Path $dll) -and (Test-Path $userKey) -and (Get-LogText 'wait-cancelled-at-close.log') -match 'cancelled from RelayDock'))
$report.Check('wait: OBS closed cleanly with the uninstaller waiting on it', $stopped.Clean, "exit code $($stopped.ExitCode)")

# The user goes through with it.
$session = Start-ObsTest -ObsRoot $ObsRoot -NoPlugin
try {
    [void](Wait-ObsLogLine -Session $session -Pattern '==== Startup complete' -TimeoutSec 90)
    Start-WaitingUninstall 'wait-removed.log'
    Start-Sleep -Seconds 4
    $report.Check('wait: a new request waits again while OBS is open', ((Get-WaitingUninstall).Count -ge 1 -and (Test-Path $dll)))
} finally {
    $stopped = Stop-ObsTest -Session $session
}
$gone = Wait-Until { -not (Test-Path $target) -and -not (Test-Path $userKey) } 60
$report.Check('wait: once OBS has closed, RelayDock is removed without another question, and the request file is gone',
    ($gone -and -not (Test-Path $request)))

# Back in place for the cases below.
$run = Invoke-Program $SetupPath $installArguments 'install-after-wait.log'
$report.Check('wait: RelayDock installs again afterwards', ($run.ExitCode -eq 0 -and (Test-Path $dll)), "exit code $($run.ExitCode)")

# ---- No OBS on the PC ---------------------------------------------------------------------------
Write-Host ''
Write-Host 'no-obs: Setup warns when it finds no OBS'
$empty = Join-Path $OutDir 'no-obs-here'
New-Item -ItemType Directory -Force -Path $empty | Out-Null
$run = Invoke-Program $SetupPath @('/CURRENTUSER', "/DIR=`"$target`"", "/OBSDIR=`"$empty`"") 'no-obs.log'
$report.Check('no-obs: Setup says that it found no OBS Studio', ($run.Log -match 'Setup found no OBS Studio on this PC'))
$report.Check('no-obs: a silent install goes ahead, as the default answer says', ($run.ExitCode -eq 0), "exit code $($run.ExitCode)")

# ---- Uninstall ----------------------------------------------------------------------------------
# Made-up data to see what the uninstaller removes. Only on a PC that has no RelayDock data.
$ownCredentials = @(cmdkey /list | Select-String 'target=RelayDock:').Count
$dataTest = (-not (Test-Path $settingsFolder)) -and ($ownCredentials -eq 0)
$testCredential = 'RelayDock:installer-test:stream_key'
$otherCredential = 'RelayDockTest-installer:keep:stream_key'
if ($dataTest) {
    New-Item -ItemType Directory -Force -Path $settingsFolder | Out-Null
    Set-Content -LiteralPath (Join-Path $settingsFolder 'config.json') -Value '{"note":"made by Test-Installer.ps1"}'
    cmdkey /generic:$testCredential /user:relaydock /pass:made-up-value-for-a-test | Out-Null   # NOT-REAL
    cmdkey /generic:$otherCredential /user:relaydock /pass:made-up-value-for-a-test | Out-Null   # NOT-REAL
}

try {
    Write-Host ''
    Write-Host 'uninstall: the default keeps settings and keys'
    $uninstaller = Join-Path $target 'unins000.exe'
    $report.Check('uninstall: the uninstaller is in the plugin folder', (Test-Path $uninstaller))
    $run = Invoke-Program $uninstaller @() 'uninstall.log'
    $gone = Wait-Until { -not (Test-Path $target) -and -not (Test-Path $userKey) }
    $report.Check('uninstall: the plugin folder and the entry under Installed apps are gone', $gone)
    if ($dataTest) {
        $report.Check('uninstall: settings and saved keys are still there',
            ((Test-Path (Join-Path $settingsFolder 'config.json')) -and @(cmdkey /list | Select-String ([regex]::Escape($testCredential))).Count -eq 1))
    } else {
        $report.Skip('uninstall: settings and saved keys are still there', 'this PC has RelayDock settings or keys, and the test leaves them alone')
    }

    Write-Host ''
    Write-Host 'remove-data: /REMOVEDATA=1 removes settings and keys'
    if ($dataTest) {
        $run = Invoke-Program $SetupPath $installArguments 'install-again.log'
        $report.Check('remove-data: RelayDock is installed again', ($run.ExitCode -eq 0 -and (Test-Path $dll)))
        $run = Invoke-Program (Join-Path $target 'unins000.exe') @('/REMOVEDATA=1') 'uninstall-remove-data.log'
        $gone = Wait-Until { -not (Test-Path $target) -and -not (Test-Path $userKey) -and -not (Test-Path $settingsFolder) }
        $report.Check('remove-data: the plugin folder, the entry and the settings folder are gone', $gone)
        $report.Check('remove-data: the saved key is gone', (@(cmdkey /list | Select-String ([regex]::Escape($testCredential))).Count -eq 0))
        $report.Check('remove-data: a credential that is not RelayDock''s is still there',
            (@(cmdkey /list | Select-String ([regex]::Escape($otherCredential))).Count -eq 1))
    } else {
        $report.Skip('remove-data: the settings folder and the saved keys are gone', 'this PC has RelayDock settings or keys, and the test leaves them alone')
    }
} finally {
    # Whatever happened above, the made-up data does not stay.
    if ($dataTest) {
        cmdkey /delete:$testCredential 2>$null | Out-Null
        cmdkey /delete:$otherCredential 2>$null | Out-Null
        if (Test-Path $settingsFolder) { Remove-Item -LiteralPath $settingsFolder -Recurse -Force }
    }
}

# ---- The folder OBS reads -----------------------------------------------------------------------
if ($DefaultFolder) {
    Write-Host ''
    Write-Host 'default-folder: install into the folder OBS reads, and remove it again'
    # Folders above the plugin folder that this test creates. They go again at the end.
    $created = @()
    $folder = $realFolder
    while ($folder -and -not (Test-Path $folder)) { $created += $folder; $folder = Split-Path -Parent $folder }

    $writable = $false
    try {
        New-Item -ItemType Directory -Force -Path $realFolder | Out-Null
        $probe = Join-Path $realFolder '.relaydock-test-probe'
        Set-Content -LiteralPath $probe -Value 'probe'
        Remove-Item -LiteralPath $probe -Force
        Remove-Item -LiteralPath $realFolder -Force
        $writable = $true
    } catch {
        $writable = $false
    }

    if (-not $writable) {
        $report.Skip('default-folder: Setup installs into the folder OBS reads without administrator rights',
            'this account cannot write there, so Setup would ask for administrator rights and wait for a person')
    } else {
        $run = Invoke-Program $SetupPath @('/CURRENTUSER') 'default-install.log'
        try {
            $report.Check('default-folder: Setup finished without administrator rights',
                ($run.ExitCode -eq 0 -and $run.Log -match 'Administrative install mode: No'), "exit code $($run.ExitCode)")
            $missing = @($expected | Where-Object { -not (Test-Path (Join-Path $realFolder $_)) })
            $report.Check('default-folder: the files are in the folder OBS reads, in the layout OBS expects', ($missing.Count -eq 0), $realFolder)
        } finally {
            $uninstaller = Join-Path $realFolder 'unins000.exe'
            if (Test-Path $uninstaller) { [void](Invoke-Program $uninstaller @() 'default-uninstall.log') }
            $gone = Wait-Until { -not (Test-Path $realFolder) -and -not (Test-Path $userKey) }
        }
        $report.Check('default-folder: the uninstaller removed the folder and the entry again', $gone)
    }

    # Deepest first, and only what is empty.
    foreach ($folder in $created) {
        if ((Test-Path $folder) -and @(Get-ChildItem -LiteralPath $folder -Force).Count -eq 0) { Remove-Item -LiteralPath $folder -Force }
    }
}

$left = @($uninstallKeys | Where-Object { Test-Path $_ }).Count + [int](Test-Path $target)
if (-not $TestBuild) { $left += [int](Test-Path $realFolder) }
$report.Check('nothing of the test install is left on this PC', ($left -eq 0))
if ($TestBuild) {
    $realNow = if (Test-Path $realDll) { "$((Get-FileHash -LiteralPath $realDll -Algorithm SHA256).Hash) $((Get-Item $realDll).LastWriteTimeUtc.Ticks)" } else { '' }
    $realEntryNow = if (Test-Path $realKey) { [string](Get-ItemProperty $realKey).DisplayVersion } else { '' }
    $report.Check('a RelayDock that is installed for real on this PC is exactly as it was: its file and its entry under Installed apps',
        ($realNow -eq $realBefore -and $realEntryNow -eq $realEntryBefore), "entry $realEntryBefore")
}
exit $report.Finish()
