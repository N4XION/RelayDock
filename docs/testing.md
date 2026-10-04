# Testing

This page says how RelayDock is tested, what the tests prove, and what they do not.

Every result on this page and in the files it links to comes from a run that happened. Nothing is assumed to pass.

## The three kinds of test

| Kind | Where | Needs | Runs |
| --- | --- | --- | --- |
| Unit tests | `tests/unit` | Nothing. No OBS, no Qt. | On every build, in CI |
| Security tests | `tests/security` | Windows Credential Manager | On every build, in CI |
| Integration tests | `tests/integration` | A portable OBS Studio and a graphics chip | On a developer's PC before a release |

GitHub's free runners have no graphics chip, so OBS cannot run there. The integration tests therefore run on a real PC, and their results are committed under `docs/test-results`.

## Unit tests

`relaydock-tests.exe` covers the core library: every decision RelayDock makes, without OBS.

| Area | What is checked |
| --- | --- |
| Providers | Servers, limits and validation messages of every platform |
| Stream URLs | Parsing, and what is safe to show |
| Settings | Reading, writing, repair, backup, recovery from a damaged file, files from a newer version |
| Effective settings | Modes, locks, platform limits, aspect ratio, frame rate division |
| Encoder sharing | Which destinations share, and that settings are never bent to force it |
| Destination state | Every transition: start, stop, reconnect, failure, forced stop |
| Optimiser | Sustain timers, hysteresis, cooldown, recovery, flapping, locks, suggestions |
| Vertical layout | Fill, fit and crop geometry, presets, snapping, saving |
| Preflight | Every rule |
| Upload budget | Sums and thresholds |
| Legal documents | Versions match the files, acceptance records, re-review on change |
| Theme | Text contrast for every mode and many user colours |
| Update check | Version ordering, release parsing, cancellation |
| Connection test | Reachable, refused, unresolvable, cancelled |

## Security tests

| Test | What is checked |
| --- | --- |
| Redactor | Registered keys and key-shaped text are removed. Ordinary text is left alone. |
| Log redaction | A key passed to any log call never reaches the log sink or the recent-lines buffer. |
| Credentials | Round trips through the real Windows Credential Manager, and the failure path. Test entries use their own prefix and are deleted. |
| Diagnostics report | A key planted in every text field of the report's input never appears in the output. Server addresses are shortened. The Windows user name is removed. |

Run both:

```powershell
ctest --preset core-tests
```

## Integration tests

These run the real plugin inside a real OBS. Streams go to `rd-rtmp-sink`, a small RTMP server in this repository that runs on the same PC and can misbehave on request: reject a key, drop a connection, stall, or read too slowly.

| Suite | What it proves |
| --- | --- |
| Plugin load | RelayDock loads and unloads cleanly and adds no memory leak to what OBS reports by itself. |
| Custom RTMP | A stream starts, carries video and audio at the set bitrate, and stops. The key is in neither the OBS log nor the settings file. |
| Several destinations | Destinations with equal settings share one encoder and send identical video. Others get their own. A rejected or dropped destination does not disturb the rest. Reconnect, manual reconnect and recovery from a server outage work. Stop works while connecting. OBS refuses new video settings while a destination connects or waits to reconnect, which would otherwise crash it. The PC is kept awake while a destination is active. OBS closes cleanly while live, connecting and reconnecting. |
| Vertical canvas | Rendered pictures are measured: fill, fit and crop place rectangles where the geometry says, so nothing is stretched. 16:9 and 9:16 stream together. The canvas leaves the render loop when unused. |
| Automatic optimisation | Against a server that reads too slowly: Automatic mode lowers the bitrate on the running encoder with no reconnect, Suggest mode changes nothing until accepted, Lock Setting restores the saved value, and quality comes back after the problem ends. |
| Interface | The first-run review cannot be skipped or bypassed. The editor never shows a saved key. All 17 settings pages open, and changes made on the Performance, Network, Appearance and Layout pages apply and are saved. The card menu duplicates, moves, tests and removes. Preflight blocks Start All Enabled on a failure. The layout editor's preset is what the canvas renders. Closing OBS with a destination live brings up a question, and both answers do what they say. OBS closes cleanly while RelayDock windows are open. |

Run them all:

```powershell
.\tests\integration\Run-All.ps1 -ObsRoot C:\obs-test -BuildDir build_hooks_x64
```

`tests/integration/README.md` explains the setup and every scenario step.

## The release build

The suites above use a test build, which contains the scenario runner. A release build does not. Two checks use the files you download:

| Check | What it proves |
| --- | --- |
| `Test-PluginLoad.ps1` with the release build | The release DLL loads and unloads cleanly in OBS. |
| `Test-ZipInstall.ps1` | The ZIP has the layout the manual guide shows. Copied into a portable OBS by the guide's steps, RelayDock loads from the OBS folder, finds its text and closes cleanly. With the files removed, OBS starts without it. |
| `Test-Installer.ps1` | The installer runs without administrator rights and puts the same files as the ZIP into the plugin folder. It installs over an existing version, refuses while OBS runs, and warns when it finds no OBS. The uninstaller removes the plugin, keeps settings and keys by default, and removes them on request, leaving other credentials alone. |

## Results

| What | File |
| --- | --- |
| Integration suites on OBS Studio 32.0.4 | [test-results/integration-obs-32.0.4.md](test-results/integration-obs-32.0.4.md) |
| Integration suites on OBS Studio 32.2.2 | [test-results/integration-obs-32.2.2.md](test-results/integration-obs-32.2.2.md) |
| Performance measurements | [performance-results.md](performance-results.md) |
| Endurance runs | [test-results/](test-results/) |

## What the tests do not prove

### Real platforms

No automated test connects to Twitch, YouTube, Facebook or TikTok. That needs a real account and a real stream key, and neither belongs in a repository or a test.

| Platform | Server and limits from official sources | Stream accepted by the platform |
| --- | --- | --- |
| Twitch | Yes, checked 2026-10-04 | Not tested yet |
| YouTube | Yes, checked 2026-10-04 | Not tested yet |
| Facebook | Yes, checked 2026-10-04 | Not tested yet |
| TikTok | TikTok publishes no fixed server or limits | Not tested yet |
| Custom RTMP | Not applicable | Tested against RelayDock's own test server |
| Custom RTMPS | Not applicable | Not tested yet |

"Not tested yet" means nobody on the project has streamed to that platform with this version. A platform test needs someone with an account to enter their own key in the dock, stream privately, and note the result. [release-checklist.md](release-checklist.md) has the steps. If you run one, the platform test report issue template records it.

### Encrypted connections

RTMPS uses the same OBS output as RTMP, with TLS handled by OBS. The local tests use plain RTMP, because a local TLS server would need a certificate installed into Windows, and tests do not change security settings.

### The clipboard

Copy Key needs the Windows clipboard. A sandboxed or service session has none. The interface suite detects that, skips the two clipboard checks and says so. It still checks that RelayDock reports the refusal and copies nothing.

In the results linked above both checks ran: the key reached the clipboard, and the clipboard was empty 30 seconds later.

No test looks at the Windows clipboard history (Win+V) or the cloud clipboard. RelayDock marks a copied key so that Windows keeps it out of both. A person checks that by hand. The release checklist has the step.

### The installer

`Test-Installer.ps1` installs into a scratch folder, and with `-DefaultFolder` into the folder OBS reads. It never starts an installed OBS Studio, because that would use a real user's OBS settings. So one step stays with a person: run the installer, start OBS, and see RelayDock under Docks. The release checklist has it.

### Other PCs

All results come from one development PC, named in each result file. Other processors, graphics chips and drivers behave differently, especially hardware encoders.

## Test matrix

| | OBS 32.0.4 | OBS 32.2.2 |
| --- | --- | --- |
| Windows 11, AMD graphics | Tested | Tested |
| Windows 11, NVIDIA graphics | Not tested | Not tested |
| Windows 11, Intel graphics | Not tested | Not tested |
| Windows 10 | Not tested | Not tested |

RelayDock picks NVENC and Quick Sync through the same code path as AMF, and unit tests cover their settings with encoder descriptions modelled on what OBS reports. A run on real hardware is still missing.
