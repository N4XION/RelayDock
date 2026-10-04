# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 RelayDock contributors
<#
.SYNOPSIS
Runs every integration suite against one OBS and writes a results table.

.DESCRIPTION
Runs the suites one after the other, each in its own PowerShell process, and collects their
verdicts. The table goes to the console and, with -ResultsFile, to a Markdown file. Each
suite's full output is kept in the output folder.

The performance and endurance suites take long and measure rather than check, so they are not
part of this run. Start them by themselves.

Needs a build with RELAYDOCK_TEST_HOOKS=ON (preset windows-hooks-x64).

.EXAMPLE
.\Run-All.ps1 -ObsRoot C:\obs-32.2.2 -BuildDir C:\RelayDock\build_hooks_x64 -ResultsFile results.md
#>
param(
    [Parameter(Mandatory)][string]$ObsRoot,
    [Parameter(Mandatory)][string]$BuildDir,
    [string]$OutDir = '',
    [string]$ResultsFile = ''
)

$ErrorActionPreference = 'Stop'
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot '..\output\all' }
Import-Module (Join-Path $PSScriptRoot 'ObsTestHarness.psm1') -Force
$paths = Get-BuildPaths -BuildDir $BuildDir
$obsVersion = (Get-Item (Join-Path $ObsRoot 'bin\64bit\obs64.exe')).VersionInfo.ProductVersion
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

$suites = @(
    @{ Name = 'Plugin load and unload'; Script = 'Test-PluginLoad.ps1'; Arguments = @('-ObsRoot', $ObsRoot, '-PluginRunDir', $paths.PluginRunDir, '-ResetConfig') },
    @{ Name = 'Custom RTMP streaming'; Script = 'Test-CustomRtmp.ps1'; Arguments = @('-ObsRoot', $ObsRoot, '-BuildDir', $BuildDir, '-OutDir', (Join-Path $OutDir 'custom-rtmp')) },
    @{ Name = 'Several destinations'; Script = 'Test-MultiDestination.ps1'; Arguments = @('-ObsRoot', $ObsRoot, '-BuildDir', $BuildDir, '-OutDir', (Join-Path $OutDir 'multi-destination')) },
    @{ Name = 'Vertical canvas'; Script = 'Test-Vertical.ps1'; Arguments = @('-ObsRoot', $ObsRoot, '-BuildDir', $BuildDir, '-OutDir', (Join-Path $OutDir 'vertical')) },
    @{ Name = 'Automatic optimisation'; Script = 'Test-Optimizer.ps1'; Arguments = @('-ObsRoot', $ObsRoot, '-BuildDir', $BuildDir, '-OutDir', (Join-Path $OutDir 'optimizer')) },
    @{ Name = 'Interface'; Script = 'Test-Ui.ps1'; Arguments = @('-ObsRoot', $ObsRoot, '-BuildDir', $BuildDir, '-OutDir', (Join-Path $OutDir 'ui')) }
)

$rows = New-Object System.Collections.Generic.List[object]
foreach ($suite in $suites) {
    Write-Host ''
    Write-Host "==== $($suite.Name) ===="
    $log = Join-Path $OutDir (($suite.Script -replace '\.ps1$', '') + '.log')
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot $suite.Script)) + $suite.Arguments
    & powershell @arguments 2>&1 | Tee-Object -FilePath $log | ForEach-Object { Write-Host $_ }
    $exitCode = $LASTEXITCODE
    $watch.Stop()

    $text = Get-Content -LiteralPath $log -Raw
    $passed = @([regex]::Matches($text, '(?m)^\s*\[PASS\]')).Count
    $failed = @([regex]::Matches($text, '(?m)^\s*\[FAIL\]')).Count
    $skipped = @([regex]::Matches($text, '(?m)^\s*\[SKIP\]')).Count
    # A suite that died before printing a verdict counts as failed.
    $verdict = if ($exitCode -eq 0 -and $failed -eq 0 -and $passed -gt 0) { 'pass' } else { 'FAIL' }
    $rows.Add([pscustomobject]@{ Suite = $suite.Name; Script = $suite.Script; Passed = $passed; Failed = $failed; Skipped = $skipped
            Seconds = [int]$watch.Elapsed.TotalSeconds; Verdict = $verdict })
}

$totalPassed = ($rows | Measure-Object Passed -Sum).Sum
$totalFailed = ($rows | Measure-Object Failed -Sum).Sum
$totalSkipped = ($rows | Measure-Object Skipped -Sum).Sum
$failedSuites = @($rows | Where-Object { $_.Verdict -ne 'pass' }).Count

$spec = Get-Content (Join-Path $PSScriptRoot '..\..\buildspec.json') -Raw | ConvertFrom-Json
$version = if ($spec.versionSuffix) { "$($spec.version)-$($spec.versionSuffix)" } else { $spec.version }
$commit = (& git -C (Join-Path $PSScriptRoot '..\..') rev-parse --short HEAD 2>$null)
$dirty = if (& git -C (Join-Path $PSScriptRoot '..\..') status --porcelain 2>$null) { ' with uncommitted changes' } else { '' }

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("# Integration test results, OBS Studio $obsVersion")
$lines.Add('')
$lines.Add("Run on $(Get-Date -Format 'yyyy-MM-dd') with ``tests/integration/Run-All.ps1``. RelayDock $version, commit $commit$dirty. Windows build $([Environment]::OSVersion.Version.Build).")
$lines.Add('')
$lines.Add('| Suite | Script | Checks passed | Failed | Skipped | Time | Result |')
$lines.Add('| --- | --- | ---: | ---: | ---: | ---: | --- |')
foreach ($row in $rows) {
    $lines.Add("| $($row.Suite) | ``$($row.Script)`` | $($row.Passed) | $($row.Failed) | $($row.Skipped) | $($row.Seconds) s | $($row.Verdict) |")
}
$lines.Add("| Total | | $totalPassed | $totalFailed | $totalSkipped | | $(if ($failedSuites -eq 0) { 'pass' } else { 'FAIL' }) |")
$lines.Add('')
$lines.Add('A skipped check could not run in the session that ran the tests. It is not counted as passed. The log of each suite names it and says why.')

Write-Host ''
$lines | ForEach-Object { Write-Host $_ }
if ($ResultsFile) {
    [System.IO.File]::WriteAllLines($ResultsFile, $lines, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host ''
    Write-Host "Results: $ResultsFile"
}
exit $failedSuites
