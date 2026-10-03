# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
#
# Helpers for tests that run a real OBS Studio with the RelayDock plugin loaded.
#
# The tests use a portable OBS folder (an extracted OBS ZIP). In portable mode OBS keeps its
# settings in <obs>\config and ignores %APPDATA%\obs-studio and %ProgramData%\obs-studio, so a
# test run never touches the OBS you stream with.
#
# Works in Windows PowerShell 5.1 and PowerShell 7.

Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'

if (-not ('RelayDockTest.Win32' -as [type])) {
    Add-Type -Namespace RelayDockTest -Name Win32 -MemberDefinition @'
        public delegate bool EnumWindowsProc(System.IntPtr hWnd, System.IntPtr lParam);

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        public static extern bool EnumWindows(EnumWindowsProc callback, System.IntPtr lParam);

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        public static extern uint GetWindowThreadProcessId(System.IntPtr hWnd, out uint processId);

        [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
        public static extern int GetWindowText(System.IntPtr hWnd, System.Text.StringBuilder text, int maxCount);

        [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
        public static extern int GetClassName(System.IntPtr hWnd, System.Text.StringBuilder text, int maxCount);

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        public static extern bool IsWindowVisible(System.IntPtr hWnd);

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        public static extern bool PostMessage(System.IntPtr hWnd, uint msg, System.IntPtr wParam, System.IntPtr lParam);

        public static System.Collections.Generic.List<System.IntPtr> WindowsOfProcess(uint processId)
        {
            var result = new System.Collections.Generic.List<System.IntPtr>();
            EnumWindows(delegate(System.IntPtr hWnd, System.IntPtr lParam) {
                uint owner;
                GetWindowThreadProcessId(hWnd, out owner);
                if (owner == processId) result.Add(hWnd);
                return true;
            }, System.IntPtr.Zero);
            return result;
        }

        public static string TitleOf(System.IntPtr hWnd)
        {
            var text = new System.Text.StringBuilder(512);
            GetWindowText(hWnd, text, text.Capacity);
            return text.ToString();
        }

        public static string ClassOf(System.IntPtr hWnd)
        {
            var text = new System.Text.StringBuilder(256);
            GetClassName(hWnd, text, text.Capacity);
            return text.ToString();
        }
'@
}

function Get-RelayDockDevRoot {
    <#
    .SYNOPSIS
    Folder that holds test copies of OBS and build output. Override with RELAYDOCK_DEV_ROOT.
    #>
    if ($env:RELAYDOCK_DEV_ROOT) { return $env:RELAYDOCK_DEV_ROOT }
    return (Join-Path $env:LOCALAPPDATA 'RelayDockDev')
}

function Get-ObsConfigDir {
    param([Parameter(Mandatory)][string]$ObsRoot)
    return (Join-Path $ObsRoot 'config\obs-studio')
}

function Initialize-ObsTestConfig {
    <#
    .SYNOPSIS
    Prepares the portable config so OBS starts without first-run dialogs.
    .PARAMETER Reset
    Deletes the whole portable config first. Use it for tests that need a clean OBS.
    #>
    param(
        [Parameter(Mandatory)][string]$ObsRoot,
        [switch]$Reset
    )

    if (-not (Test-Path (Join-Path $ObsRoot 'bin\64bit\obs64.exe'))) {
        throw "No obs64.exe under '$ObsRoot'. Extract an OBS Studio ZIP there first."
    }

    $config = Get-ObsConfigDir -ObsRoot $ObsRoot
    if ($Reset -and (Test-Path $config)) {
        Remove-Item -LiteralPath $config -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $config | Out-Null

    # A leftover sentinel makes OBS ask about safe mode, which would block the test.
    $sentinel = Join-Path $config '.sentinel'
    if (Test-Path $sentinel) {
        Remove-Item -LiteralPath $sentinel -Recurse -Force
    }

    $userIni = Join-Path $config 'user.ini'
    if (-not (Test-Path $userIni)) {
        # FirstRun=true skips the auto-configuration wizard and the default audio devices.
        $lines = @(
            '[General]',
            'FirstRun=true',
            'ConfirmOnExit=false',
            '',
            '[BasicWindow]',
            'SysTrayEnabled=true',
            'SysTrayWhenStarted=false',
            'SysTrayMinimizeToTray=false',
            ''
        )
        [System.IO.File]::WriteAllLines($userIni, $lines, (New-Object System.Text.UTF8Encoding($false)))
    }

    $globalIni = Join-Path $config 'global.ini'
    if (-not (Test-Path $globalIni)) {
        $lines = @(
            '[General]',
            'EnableAutoUpdates=false',
            ''
        )
        [System.IO.File]::WriteAllLines($globalIni, $lines, (New-Object System.Text.UTF8Encoding($false)))
    }

    return $config
}

function Start-ObsTest {
    <#
    .SYNOPSIS
    Starts portable OBS with the RelayDock build from a CMake rundir.
    .PARAMETER PluginRunDir
    Folder with bin\relaydock.dll and data\relaydock, for example <build>\rundir\RelWithDebInfo.
    .PARAMETER NoPlugin
    Starts OBS without RelayDock. Tests use it to measure what OBS does by itself.
    .PARAMETER Visible
    Shows the OBS window. Without it OBS starts hidden in the system tray.
    .PARAMETER Environment
    Extra environment variables for the OBS process.
    #>
    param(
        [Parameter(Mandatory)][string]$ObsRoot,
        [string]$PluginRunDir,
        [switch]$NoPlugin,
        [switch]$Visible,
        [hashtable]$Environment = @{},
        [string[]]$ExtraArguments = @()
    )

    $bin = Join-Path $ObsRoot 'bin\64bit'
    if (-not $NoPlugin) {
        if (-not $PluginRunDir) { throw 'Pass -PluginRunDir or -NoPlugin.' }
        $dll = Join-Path $PluginRunDir 'bin\relaydock.dll'
        if (-not (Test-Path $dll)) { throw "No plugin build at '$dll'. Build the plugin first." }
    }

    $arguments = @('--portable', '--multi', '--disable-updater', '--disable-missing-files-check')
    if (-not $Visible) { $arguments += '--minimize-to-tray' }
    $arguments += $ExtraArguments

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = Join-Path $bin 'obs64.exe'
    $psi.WorkingDirectory = $bin
    $psi.Arguments = ($arguments -join ' ')
    $psi.UseShellExecute = $false
    # OBS writes debug chatter to stderr. Read and drop it so it cannot fill the pipe.
    $psi.RedirectStandardError = $true
    $psi.RedirectStandardOutput = $true
    if (-not $NoPlugin) {
        $psi.EnvironmentVariables['OBS_PLUGINS_PATH'] = (Join-Path $PluginRunDir 'bin')
        $psi.EnvironmentVariables['OBS_PLUGINS_DATA_PATH'] = (Join-Path $PluginRunDir 'data')
    }
    foreach ($key in $Environment.Keys) {
        $psi.EnvironmentVariables[$key] = [string]$Environment[$key]
    }

    $startedAt = Get-Date
    $process = [System.Diagnostics.Process]::Start($psi)
    $process.BeginErrorReadLine()
    $process.BeginOutputReadLine()
    return [pscustomobject]@{
        Process   = $process
        ObsRoot   = $ObsRoot
        StartedAt = $startedAt
    }
}

function Get-ObsLogPath {
    <#
    .SYNOPSIS
    Path of the log file OBS created for this run, or $null when it does not exist yet.
    #>
    param([Parameter(Mandatory)]$Session)

    $logs = Join-Path (Get-ObsConfigDir -ObsRoot $Session.ObsRoot) 'logs'
    if (-not (Test-Path $logs)) { return $null }
    $file = Get-ChildItem -LiteralPath $logs -Filter '*.txt' |
        Where-Object { $_.LastWriteTime -ge $Session.StartedAt.AddSeconds(-2) } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($file) { return $file.FullName }
    return $null
}

function Get-ObsLogText {
    param([Parameter(Mandatory)]$Session)

    $path = Get-ObsLogPath -Session $Session
    if (-not $path) { return '' }
    # OBS keeps the file open for writing. Open it shared.
    $stream = New-Object System.IO.FileStream($path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::ReadWrite)
    try {
        $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
        return $reader.ReadToEnd()
    } finally {
        $stream.Dispose()
    }
}

function Wait-ObsLogLine {
    <#
    .SYNOPSIS
    Waits until the OBS log contains a line matching the regular expression.
    Returns $true when found, $false on timeout or when OBS exits first.
    #>
    param(
        [Parameter(Mandatory)]$Session,
        [Parameter(Mandatory)][string]$Pattern,
        [int]$TimeoutSec = 60
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if ((Get-ObsLogText -Session $Session) -match $Pattern) { return $true }
        if ($Session.Process.HasExited) {
            return ((Get-ObsLogText -Session $Session) -match $Pattern)
        }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Stop-ObsTest {
    <#
    .SYNOPSIS
    Closes OBS the way a user does (WM_CLOSE on the main window) and waits for it to exit.
    Returns an object with Clean = $true when OBS exited by itself with exit code 0.
    #>
    param(
        [Parameter(Mandatory)]$Session,
        [int]$TimeoutSec = 45
    )

    $process = $Session.Process
    $result = [pscustomobject]@{ Clean = $false; Killed = $false; ExitCode = $null }

    if (-not $process.HasExited) {
        $WM_CLOSE = 0x0010
        foreach ($window in [RelayDockTest.Win32]::WindowsOfProcess([uint32]$process.Id)) {
            $title = [RelayDockTest.Win32]::TitleOf($window)
            # The OBS main window title starts with "OBS " followed by the version.
            if ($title -match '^OBS \d') {
                [void][RelayDockTest.Win32]::PostMessage($window, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
            }
        }
        if (-not $process.WaitForExit($TimeoutSec * 1000)) {
            $process.Kill()
            $process.WaitForExit(10000) | Out-Null
            $result.Killed = $true
        }
    }

    $result.ExitCode = $process.ExitCode
    $result.Clean = (-not $result.Killed) -and ($process.ExitCode -eq 0)
    return $result
}

function Get-ObsCrashFiles {
    <#
    .SYNOPSIS
    Crash reports OBS wrote since the session started.
    #>
    param([Parameter(Mandatory)]$Session)

    $crashes = Join-Path (Get-ObsConfigDir -ObsRoot $Session.ObsRoot) 'crashes'
    if (-not (Test-Path $crashes)) { return @() }
    return @(Get-ChildItem -LiteralPath $crashes -File |
            Where-Object { $_.LastWriteTime -ge $Session.StartedAt.AddSeconds(-2) })
}

function Get-ObsMainWindow {
    <#
    .SYNOPSIS
    Handle of the OBS main window for a session, or [IntPtr]::Zero.
    #>
    param([Parameter(Mandatory)]$Session)

    foreach ($window in [RelayDockTest.Win32]::WindowsOfProcess([uint32]$Session.Process.Id)) {
        if ([RelayDockTest.Win32]::TitleOf($window) -match '^OBS \d') { return $window }
    }
    return [IntPtr]::Zero
}

Export-ModuleMember -Function Get-RelayDockDevRoot, Get-ObsConfigDir, Initialize-ObsTestConfig, Start-ObsTest,
    Get-ObsLogPath, Get-ObsLogText, Wait-ObsLogLine, Stop-ObsTest, Get-ObsCrashFiles, Get-ObsMainWindow
