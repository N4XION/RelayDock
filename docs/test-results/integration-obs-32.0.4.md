# Integration test results, OBS Studio 32.0.4

Run on 2026-10-05 with `tests/integration/Run-All.ps1`. RelayDock build 1.1.0+36.89739be7c. Windows build 26200.

| Suite | Script | Checks passed | Failed | Skipped | Time | Result |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| Plugin load and unload | `Test-PluginLoad.ps1` | 7 | 0 | 0 | 12 s | pass |
| Custom RTMP streaming | `Test-CustomRtmp.ps1` | 30 | 0 | 0 | 17 s | pass |
| Several destinations | `Test-MultiDestination.ps1` | 152 | 0 | 0 | 229 s | pass |
| Vertical canvas | `Test-Vertical.ps1` | 65 | 0 | 0 | 56 s | pass |
| Automatic optimisation | `Test-Optimizer.ps1` | 60 | 0 | 0 | 310 s | pass |
| Interface | `Test-Ui.ps1` | 167 | 0 | 0 | 179 s | pass |
| Live chat | `Test-Chat.ps1` | 57 | 0 | 0 | 33 s | pass |
| Total | | 538 | 0 | 0 | | pass |

A skipped check could not run in the session that ran the tests. It is not counted as passed. The log of each suite names it and says why.
