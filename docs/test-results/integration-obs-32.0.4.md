# Integration test results, OBS Studio 32.0.4

Run on 2026-10-04 with `tests/integration/Run-All.ps1`. RelayDock build 1.0.0-rc.1+9.54512d94d. Windows build 26200.

| Suite | Script | Checks passed | Failed | Skipped | Time | Result |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| Plugin load and unload | `Test-PluginLoad.ps1` | 7 | 0 | 0 | 11 s | pass |
| Custom RTMP streaming | `Test-CustomRtmp.ps1` | 30 | 0 | 0 | 16 s | pass |
| Several destinations | `Test-MultiDestination.ps1` | 152 | 0 | 0 | 224 s | pass |
| Vertical canvas | `Test-Vertical.ps1` | 61 | 0 | 0 | 51 s | pass |
| Automatic optimisation | `Test-Optimizer.ps1` | 60 | 0 | 0 | 293 s | pass |
| Interface | `Test-Ui.ps1` | 152 | 0 | 0 | 162 s | pass |
| Total | | 462 | 0 | 0 | | pass |

A skipped check could not run in the session that ran the tests. It is not counted as passed. The log of each suite names it and says why.
