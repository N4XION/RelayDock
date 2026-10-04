# Integration tests

These scripts run RelayDock inside a real OBS Studio and check what it does. They stream to a small RTMP server on the same PC (`tests/tools/rtmp-sink`). They never connect to a real platform and never use a real stream key.

`docs/testing.md` describes what each suite proves. This page is the reference for writing and running them.

## Set up

1. Build with the test hooks:

```powershell
cmake --preset windows-hooks-x64
cmake --build --preset windows-hooks-x64
```

2. Get a portable OBS. Download the OBS Studio ZIP from the OBS project's releases and extract it, for example to `C:\obs-test`. The scripts start it with `--portable`, so it keeps its settings in its own folder and never touches an installed OBS.

3. Run everything:

```powershell
.\tests\integration\Run-All.ps1 -ObsRoot C:\obs-test -BuildDir build_hooks_x64
```

or one suite, or one case of a suite:

```powershell
.\tests\integration\Test-Vertical.ps1 -ObsRoot C:\obs-test -BuildDir build_hooks_x64
.\tests\integration\Test-Vertical.ps1 -ObsRoot C:\obs-test -BuildDir build_hooks_x64 -Only picture
```

Pass one name to `-Only` per run.

## The suites

| Script | What it drives |
| --- | --- |
| `Test-PluginLoad.ps1` | Loading and unloading in OBS, with a baseline of what OBS reports by itself. |
| `Test-CustomRtmp.ps1` | One stream from start to stop, and where the key ends up. |
| `Test-MultiDestination.ps1` | Shared and separate encoders, failure isolation, reconnect, recovery, stops and shutdowns. |
| `Test-Vertical.ps1` | The vertical canvas: what the picture looks like, and 16:9 with 9:16 at once. |
| `Test-Optimizer.ps1` | Automatic optimisation against a server that reads too slowly. |
| `Test-Ui.ps1` | The windows themselves: first-run review, editor, settings, preflight, layout editor. |
| `Test-Performance.ps1` | Measurements, not checks. Writes a report. |
| `Test-Endurance.ps1` | A long stream with a measurement every minute. |
| `Run-All.ps1` | The first six, with a results table. |

`Test-Ui.ps1` shows the OBS window, because a hidden window has no layout to check. The other suites keep OBS in the system tray.

## How a test works

A build with `RELAYDOCK_TEST_HOOKS=ON` contains a scenario runner. It reads a JSON file of steps named by the `RELAYDOCK_SCENARIO` environment variable, runs them on the OBS interface thread, and writes what it saw to the file named by `RELAYDOCK_SCENARIO_RESULT`. The steps call the same functions the interface calls.

A release build does not contain the runner, so an installed RelayDock cannot be scripted through an environment variable. `scripts/package.ps1` refuses to package a DLL that contains it.

`ObsTestHarness.psm1` starts OBS, starts the test server, waits for the scenario and collects the results, the OBS log and any crash report. Every scenario gets its own credential prefix in Windows Credential Manager (`RelayDockTest-<id>`), so test keys never mix with real ones. The runner deletes them when the scenario ends.

A test that restarts OBS to check that keys survive passes the same `-CredentialPrefix` to both runs and sets `keep_credentials` at the top of the first scenario. The second run then cleans up.

## The test server

`rd-rtmp-sink` accepts RTMP publishers and counts what arrives. The start of a stream key tells it how to behave:

| Key starts with | Behaviour |
| --- | --- |
| `ok-` | Accept and count. Any key without a known prefix does this too. |
| `reject-` | Refuse the publish request, like a platform does with a wrong key. |
| `drop<N>-` | Cut the connection N seconds after publishing starts, every time. |
| `droponce<N>-` | Cut the connection once. The next connection with that key is accepted. |
| `stall<N>-` | Stop reading after N seconds, like a congested network. |
| `slow<K>-` | Read at most K Kbps, like an upload link that is too narrow. |
| `slow<K>for<S>-` | The same, for the first S seconds only. |

Its report lists, per key: sessions, bytes, video and audio messages, keyframes, codec, the frame rate measured from arrival times, and the stream's metadata.

## Scenario steps

Destinations are named by a `ref` you choose when you add them.

### Setting up

| Step | Fields | What it does |
| --- | --- | --- |
| `clear` | | Removes every destination and every test credential. |
| `obs_video` | `base_width`, `base_height`, `output_width`, `output_height`, `fps` | Sets the OBS canvas, output size and frame rate. |
| `add_color_source` | `name`, `color`, `width`, `height`, `x`, `y` | Adds a coloured rectangle to the current scene. |
| `add_moving_picture` | `file`, `speed_x`, `speed_y` | Adds a picture that scrolls across the whole canvas. |
| `clear_scene` | | Removes every item from the current scene. |
| `add_destination` | `ref`, `provider`, `config`, `stream_key`, `password` | Adds a destination. `config` uses the names of the settings file. |
| `set` | `performance_mode`, `enabled`, `optimizer`, `upload_kbps` | Changes settings. |
| `config_patch` | `patch` | Merges JSON into the saved settings. |
| `accept_legal` | | Records the first-run review as done. |
| `vertical_layout` | `layouts` | Replaces the vertical layouts. |
| `optimizer_tuning` | `sustain_ms`, `cooldown_ms`, `recover_after_ms`, `probation_ms`, `drop_trigger` | Shortens the optimiser's timers for a test. |

### Streaming

| Step | Fields | What it does |
| --- | --- | --- |
| `start` | `ref`, `private_test` | Starts one destination. |
| `start_all` | | Starts every enabled destination. |
| `stop` | `ref` | Stops one. A second `stop` forces it. |
| `stop_all` | | Stops all. |
| `reconnect` | `ref` | Drops the connection and starts again. |
| `suggestion` | `action`: `apply`, `ignore`, `lock` | Answers the first suggestion. |

### Waiting

| Step | Fields | Waits until |
| --- | --- | --- |
| `wait` | `seconds` | The time has passed. |
| `wait_phase` | `ref`, `phase`, `timeout_sec` | The destination reaches the phase. |
| `wait_reconnects` | `ref`, `count`, `timeout_sec` | It has reconnected that often and is live. |
| `wait_idle` | `timeout_sec` | No destination is active. |
| `wait_suggestion` | `timeout_sec` | A suggestion exists. |
| `wait_adjustment` | `ref`, `state`: `reduced`, `none`, `timeout_sec` | The optimiser reduced something, or restored everything. |

### Looking

| Step | Fields | Records |
| --- | --- | --- |
| `snapshot` | `label` | Every destination's phase, error, statistics, effective settings and encoder, plus OBS and RelayDock measurements. |
| `render_vertical` | `label`, `layout`, `find`, `file` | Renders the vertical canvas to an image and measures where given colours are. |
| `preflight` | `label` | The preflight report. |
| `diagnostics` | `label` | The diagnostics report text. |
| `legal_state` | `label` | Which legal documents are accepted. |
| `save_config` | | Writes the settings file now. |
| `quit` | | Ends the scenario and closes OBS. |

### Interface

`target` is `dock`, `dialog` (the RelayDock dialog in front), `main` (the OBS window), `message` or `input`.

| Step | Fields | What it does |
| --- | --- | --- |
| `ui_show_dock` | `area`: `floating`, `left`, `right`, `width`, `height` | Shows the dock. |
| `ui_main_window` | `width`, `height`, `x`, `y` | Sizes and places the OBS window. |
| `ui_open` | `what`: `add`, `edit`, `settings`, `legal`, `vertical`, `preflight`, `start_all`, plus `provider`, `ref`, `page`, `document`, `layout` | Opens a window the way the dock does. |
| `ui_wait` | `target`, `present`, `timeout_sec` | Waits for a window to appear or close. |
| `ui_state` | `target`, `label` | Records the window's buttons, boxes, labels, fields, number fields and lists, in reading order. A hidden field reports that it is hidden, never its text. |
| `ui_click` | `target`, `text` | Presses the button with that text, name or tooltip. |
| `ui_menu` | `target`, `button`, `index`, `action` | Chooses an entry from a button's menu, such as the three dots on the card at place `index`. Without `action` it records the entries. |
| `ui_key` | `target`, `name`, `index`, `key` | Presses a key on a widget. `name`: `card` picks the card at place `index`. Keys: `Up`, `Down`, `Left`, `Right`, `Return`, with `Alt+` or `Shift+`. |
| `ui_drag` | `target`, `name`, `from_x`, `from_y`, `to_x`, `to_y` | Drags with the left mouse button on a widget. Positions are shares of its width and height. |
| `ui_check` | `target`, `text`, `checked` | Ticks or unticks the box whose text starts with `text`. |
| `ui_text` | `target`, `name`, `value` | Types into a text field. |
| `ui_combo` | `target`, `name`, `value` | Chooses the list entry that contains `value`. |
| `ui_spin` | `target`, `name`, `value` | Sets a number field. |
| `ui_select_row` | `target`, `name`, `value` | Selects a row in a list. |
| `ui_scroll_end` | `target` | Scrolls the window's text to its end. |
| `ui_grab` | `target`, `file` | Saves a picture of the widgets. Does not include what OBS draws. |
| `ui_capture` | `target`, `file` | Saves a picture of the window as Windows shows it, including OBS previews. |
| `ui_resize` | `target`, `width`, `height` | Resizes a window. |
| `ui_close` | `target` | Closes a window. |
| `ui_bind_ref` | `ref`, `name` | Names a destination that was created through the interface. |
| `ui_clipboard` | `expect`, `label` | Records whether the clipboard holds `expect`. Never records the text. |

## Rules

- A check states what was observed. Do not assert a number nobody measured.
- Use made-up keys with the prefixes above. `scripts/scan-secrets.ps1` rejects anything that looks like a real key.
- A check that cannot run in the current session is reported with `Skip`, never as passed.
- Keep OBS and RelayDock's real settings out of it. Always use a portable OBS.
