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
    } else {
        # A test can switch the OBS exit warning on, and OBS saves that. Every run starts with
        # it off, or a scenario that ends with a destination live would wait for an answer.
        $text = [System.IO.File]::ReadAllText($userIni)
        if ($text -match '(?m)^ConfirmOnExit=') {
            $fixed = [regex]::Replace($text, '(?m)^ConfirmOnExit=[^\r\n]*', 'ConfirmOnExit=false')
        } else {
            $fixed = [regex]::Replace($text, '(?m)^\[General\]', "[General]`nConfirmOnExit=false")
        }
        if ($fixed -ne $text) {
            [System.IO.File]::WriteAllText($userIni, $fixed, (New-Object System.Text.UTF8Encoding($false)))
        }
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
    # By creation time, not by last write: the log of a run that ended a moment ago was also
    # written to a moment ago, and it must not be taken for this run's log.
    $file = Get-ChildItem -LiteralPath $logs -Filter '*.txt' |
        Where-Object { $_.CreationTime -ge $Session.StartedAt.AddSeconds(-2) } |
        Sort-Object CreationTime -Descending |
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

function Get-BuildPaths {
    <#
    .SYNOPSIS
    Paths inside a RelayDock CMake build folder that the tests need.
    #>
    param(
        [Parameter(Mandatory)][string]$BuildDir,
        [string]$Configuration = 'RelWithDebInfo'
    )

    $paths = [pscustomobject]@{
        PluginRunDir = Join-Path (Join-Path $BuildDir 'rundir') $Configuration
        Sink         = Join-Path (Join-Path (Join-Path $BuildDir 'tests') $Configuration) 'rd-rtmp-sink.exe'
        UnitTests    = Join-Path (Join-Path (Join-Path $BuildDir 'tests') $Configuration) 'relaydock-tests.exe'
    }
    if (-not (Test-Path (Join-Path (Join-Path $paths.PluginRunDir 'bin') 'relaydock.dll'))) {
        throw "No plugin in '$($paths.PluginRunDir)'. Build the preset first."
    }
    return $paths
}

function Start-RtmpSink {
    <#
    .SYNOPSIS
    Starts rd-rtmp-sink, the local RTMP ingest the tests stream to.
    #>
    param(
        [Parameter(Mandatory)][string]$SinkPath,
        [Parameter(Mandatory)][string]$ReportPath,
        [int]$Port = 19350,
        # A second port that accepts connections and never answers, like a server that hangs.
        [int]$BlackholePort = 0
    )

    if (-not (Test-Path $SinkPath)) { throw "No rd-rtmp-sink at '$SinkPath'. Build the tests first." }
    Remove-Item -LiteralPath $ReportPath, "$ReportPath.stop" -ErrorAction SilentlyContinue

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $SinkPath
    $psi.Arguments = "--port $Port --report `"$ReportPath`""
    if ($BlackholePort -gt 0) { $psi.Arguments += " --blackhole-port $BlackholePort" }
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $process = [System.Diagnostics.Process]::Start($psi)

    $deadline = (Get-Date).AddSeconds(10)
    while (-not (Test-Path $ReportPath)) {
        if ($process.HasExited) { throw "rd-rtmp-sink exited with code $($process.ExitCode). Port $Port may be in use." }
        if ((Get-Date) -gt $deadline) { throw 'rd-rtmp-sink did not start within 10 seconds.' }
        Start-Sleep -Milliseconds 100
    }

    return [pscustomobject]@{ Process = $process; ReportPath = $ReportPath; Port = $Port; SinkPath = $SinkPath }
}

function Get-RtmpSinkReport {
    <#
    .SYNOPSIS
    The sink's current report as an object. The sink rewrites the file four times a second.
    #>
    param([Parameter(Mandatory)]$Sink)

    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        try {
            return (Get-Content -LiteralPath $Sink.ReportPath -Raw -Encoding UTF8 -ErrorAction Stop | ConvertFrom-Json)
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    throw "Could not read the sink report at '$($Sink.ReportPath)'."
}

function Stop-RtmpSink {
    <#
    .SYNOPSIS
    Asks the sink to write its final report and exit. Returns the final report.
    #>
    param([Parameter(Mandatory)]$Sink)

    if (-not $Sink.Process.HasExited) {
        Set-Content -LiteralPath "$($Sink.ReportPath).stop" -Value 'stop'
        if (-not $Sink.Process.WaitForExit(10000)) { $Sink.Process.Kill() }
    }
    return (Get-Content -LiteralPath $Sink.ReportPath -Raw -Encoding UTF8 | ConvertFrom-Json)
}


function Stop-RtmpSinkAbruptly {
    <#
    .SYNOPSIS
    Kills the sink without a goodbye, the way a server or a network link fails.
    Start-RtmpSink brings it back on the same port.
    #>
    param([Parameter(Mandatory)]$Sink)

    if (-not $Sink.Process.HasExited) {
        $Sink.Process.Kill()
        $Sink.Process.WaitForExit(5000) | Out-Null
    }
}

function Start-ObsScenario {
    <#
    .SYNOPSIS
    Starts a RelayDock scenario inside a portable OBS and returns without waiting for it.
    .DESCRIPTION
    Needs a plugin build made with RELAYDOCK_TEST_HOOKS=ON. The scenario is a hashtable with a
    "steps" array. See tests/integration/README.md for the steps. OBS closes itself when the
    scenario ends. Stream keys and passwords used by the scenario go to a credential prefix of
    their own and are removed when the scenario ends.

    Use this with Complete-ObsScenario when the test has to act while OBS runs, for example to
    take the server away and bring it back. Use Invoke-ObsScenario otherwise.
    #>
    param(
        [Parameter(Mandatory)][string]$ObsRoot,
        [Parameter(Mandatory)][string]$PluginRunDir,
        [Parameter(Mandatory)][hashtable]$Scenario,
        [Parameter(Mandatory)][string]$OutDir,
        [string]$Name = 'scenario',
        [switch]$Visible,
        [switch]$ResetConfig,
        # Test keys live in Windows Credential Manager under this prefix. Every run gets its own
        # unless a test passes one, to check that keys survive an OBS restart.
        [string]$CredentialPrefix = "RelayDockTest-$([guid]::NewGuid().ToString())",
        # Extra environment variables for the OBS process. They win over the ones set here.
        [hashtable]$ExtraEnvironment = @{}
    )

    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    $scenarioPath = Join-Path $OutDir "$Name.scenario.json"
    $resultPath = Join-Path $OutDir "$Name.result.json"
    Remove-Item -LiteralPath $resultPath -ErrorAction SilentlyContinue
    $json = $Scenario | ConvertTo-Json -Depth 20
    [System.IO.File]::WriteAllText($scenarioPath, $json, (New-Object System.Text.UTF8Encoding($false)))

    Initialize-ObsTestConfig -ObsRoot $ObsRoot -Reset:$ResetConfig | Out-Null
    $environment = @{
        RELAYDOCK_SCENARIO               = $scenarioPath
        RELAYDOCK_SCENARIO_RESULT        = $resultPath
        RELAYDOCK_TEST_CREDENTIAL_PREFIX = $CredentialPrefix
    }
    foreach ($key in $ExtraEnvironment.Keys) { $environment[$key] = $ExtraEnvironment[$key] }
    $session = Start-ObsTest -ObsRoot $ObsRoot -PluginRunDir $PluginRunDir -Environment $environment -Visible:$Visible
    $session | Add-Member -NotePropertyName ResultPath -NotePropertyValue $resultPath
    return $session
}

function Complete-ObsScenario {
    <#
    .SYNOPSIS
    Waits for a scenario started with Start-ObsScenario and returns what happened.
    .DESCRIPTION
    Returns the scenario results, the OBS log, whether OBS exited by itself and cleanly, the
    crash reports it wrote and the memory leak count OBS printed at exit.
    #>
    param(
        [Parameter(Mandatory)]$Session,
        [int]$TimeoutSec = 180
    )

    $timedOut = -not $Session.Process.WaitForExit($TimeoutSec * 1000)
    if ($timedOut) {
        $stop = Stop-ObsTest -Session $Session -TimeoutSec 20
    } else {
        $stop = [pscustomobject]@{ Clean = ($Session.Process.ExitCode -eq 0); Killed = $false; ExitCode = $Session.Process.ExitCode }
    }

    $result = $null
    if (Test-Path $Session.ResultPath) {
        $result = Get-Content -LiteralPath $Session.ResultPath -Raw -Encoding UTF8 | ConvertFrom-Json
    }

    $log = Get-ObsLogText -Session $Session
    $leaks = $null
    $match = [regex]::Match($log, 'Number of memory leaks: (\d+)')
    if ($match.Success) { $leaks = [int]$match.Groups[1].Value }

    return [pscustomobject]@{
        Result      = $result
        Log         = $log
        LogPath     = Get-ObsLogPath -Session $Session
        TimedOut    = $timedOut
        CleanExit   = $stop.Clean
        ExitCode    = $stop.ExitCode
        Crashes     = @(Get-ObsCrashFiles -Session $Session)
        MemoryLeaks = $leaks
        ResultPath  = $Session.ResultPath
    }
}

function Invoke-ObsScenario {
    <#
    .SYNOPSIS
    Runs a RelayDock scenario inside a portable OBS, waits for it and returns what happened.
    #>
    param(
        [Parameter(Mandatory)][string]$ObsRoot,
        [Parameter(Mandatory)][string]$PluginRunDir,
        [Parameter(Mandatory)][hashtable]$Scenario,
        [Parameter(Mandatory)][string]$OutDir,
        [string]$Name = 'scenario',
        [int]$TimeoutSec = 180,
        [switch]$Visible,
        [switch]$ResetConfig,
        [string]$CredentialPrefix = "RelayDockTest-$([guid]::NewGuid().ToString())",
        [hashtable]$ExtraEnvironment = @{}
    )

    $session = Start-ObsScenario -ObsRoot $ObsRoot -PluginRunDir $PluginRunDir -Scenario $Scenario -OutDir $OutDir `
        -Name $Name -Visible:$Visible -ResetConfig:$ResetConfig -CredentialPrefix $CredentialPrefix -ExtraEnvironment $ExtraEnvironment
    return (Complete-ObsScenario -Session $session -TimeoutSec $TimeoutSec)
}

function Add-ObsRunChecks {
    <#
    .SYNOPSIS
    Adds the checks every scenario run must pass: OBS closed by itself, exited cleanly, wrote no
    crash report, finished the scenario and reported no more memory leaks than OBS does alone.
    #>
    param(
        [Parameter(Mandatory)]$Report,
        [Parameter(Mandatory)]$Run,
        [string]$Label = '',
        # What OBS reports with no third-party plugin. Test-PluginLoad.ps1 measures it. OBS 32
        # reports 1.
        [int]$BaselineLeaks = 1,
        # Secrets the scenario used. None of them may appear in the OBS log.
        [string[]]$Secrets = @()
    )

    $prefix = if ($Label) { "$Label`: " } else { '' }
    $Report.Check("${prefix}OBS closed by itself when the scenario ended", (-not $Run.TimedOut))
    $Report.Check("${prefix}OBS exited cleanly", $Run.CleanExit, "exit code $($Run.ExitCode)")
    $Report.Check("${prefix}OBS wrote no crash report", ($Run.Crashes.Count -eq 0))
    $Report.Check("${prefix}the scenario completed", ($null -ne $Run.Result -and $Run.Result.completed), [string]$Run.Result.failure)
    $Report.Check("${prefix}OBS reports no memory leak beyond its own baseline",
        ($null -ne $Run.MemoryLeaks -and $Run.MemoryLeaks -le $BaselineLeaks), "$($Run.MemoryLeaks) reported, baseline $BaselineLeaks")
    foreach ($secret in $Secrets) {
        $Report.Check("${prefix}the stream key '$($secret.Substring(0, [math]::Min(6, $secret.Length)))...' is not in the OBS log",
            ($Run.Log.Length -gt 0 -and -not $Run.Log.Contains($secret)))
    }
}

function New-TestReport {
    <#
    .SYNOPSIS
    Collects check results for one test script and prints a summary.
    #>
    param([Parameter(Mandatory)][string]$Title)

    $report = [pscustomobject]@{
        Title    = $Title
        Checks   = New-Object System.Collections.Generic.List[object]
        Failures = 0
        Skipped  = 0
    }
    # A check that cannot run here. It is reported, never counted as passed.
    $report | Add-Member -MemberType ScriptMethod -Name Skip -Value {
        param([string]$Description, [string]$Reason)
        $this.Skipped++
        Write-Host "  [SKIP] $Description ($Reason)"
    }
    $report | Add-Member -MemberType ScriptMethod -Name Check -Value {
        param([string]$Description, [bool]$Passed, [string]$Detail = '')
        $this.Checks.Add([pscustomobject]@{ Description = $Description; Passed = $Passed; Detail = $Detail })
        if (-not $Passed) { $this.Failures++ }
        $mark = if ($Passed) { 'PASS' } else { 'FAIL' }
        $line = "  [$mark] $Description"
        if ($Detail) { $line += " ($Detail)" }
        Write-Host $line
    }
    $report | Add-Member -MemberType ScriptMethod -Name Finish -Value {
        Write-Host ''
        if ($this.Failures -gt 0) {
            Write-Host "FAILED: $($this.Title). $($this.Failures) of $($this.Checks.Count) checks failed."
            return 1
        }
        $skipped = if ($this.Skipped -gt 0) { " $($this.Skipped) skipped." } else { '' }
        Write-Host "PASSED: $($this.Title). $($this.Checks.Count) checks.$skipped"
        return 0
    }
    Write-Host $Title
    return $report
}


function Test-ClipboardAvailable {
    <#
    .SYNOPSIS
    True when this session may open the Windows clipboard. Sandboxed and service sessions may not.
    Opens and closes the clipboard without reading or changing it.
    #>
    if (-not ('RelayDockTest.Clipboard' -as [type])) {
        Add-Type -Namespace RelayDockTest -Name Clipboard -MemberDefinition @'
[DllImport("user32.dll", SetLastError = true)] public static extern bool OpenClipboard(System.IntPtr owner);
[DllImport("user32.dll")] public static extern bool CloseClipboard();
'@
    }
    for ($i = 0; $i -lt 5; $i++) {
        if ([RelayDockTest.Clipboard]::OpenClipboard([IntPtr]::Zero)) {
            [RelayDockTest.Clipboard]::CloseClipboard() | Out-Null
            return $true
        }
        Start-Sleep -Milliseconds 100
    }
    return $false
}

Export-ModuleMember -Function Test-ClipboardAvailable, Get-RelayDockDevRoot, Get-ObsConfigDir, Initialize-ObsTestConfig, Start-ObsTest,
    Get-ObsLogPath, Get-ObsLogText, Wait-ObsLogLine, Stop-ObsTest, Get-ObsCrashFiles, Get-ObsMainWindow,
    Get-BuildPaths, Start-RtmpSink, Get-RtmpSinkReport, Stop-RtmpSink, Stop-RtmpSinkAbruptly, Start-ObsScenario,
    Complete-ObsScenario, Invoke-ObsScenario, Add-ObsRunChecks, New-TestReport
