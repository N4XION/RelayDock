# Architecture

RelayDock is one OBS plugin module, `relaydock.dll`. Inside, the code has two layers with a hard line between them.

```
+---------------------------------------------------------------+
| Plugin layer (needs OBS and Qt)                                |
|   ui/         dock, cards, editor, settings, onboarding        |
|   app/        AppContext, PerformanceMonitor, diagnostics      |
|   outputs/    OutputManager, vertical canvas, video guard      |
|   encoders/   EncoderCatalog, EncoderPool                      |
+---------------------------------------------------------------+
| Core library (plain C++20 and Win32, no OBS, no Qt)            |
|   providers/  platforms and their limits                       |
|   settings/   configuration, JSON, migrations, theme           |
|   security/   secrets, credential store, redactor              |
|   performance/ effective settings, optimiser                   |
|   encoders/   sharing plan, encoder settings                   |
|   core/       destination state machine, vertical layout       |
|   network/    URLs, bandwidth budget, reachability             |
|   diagnostics/ preflight, report                               |
|   legal/      documents and acceptance records                 |
|   update/     version comparison, release check                |
+---------------------------------------------------------------+
```

Every decision RelayDock makes lives in the core library: which settings a destination streams with, which destinations share an encoder, what the optimiser proposes, whether the preflight check passes, what a log line may contain. The plugin layer gathers facts from OBS, hands them to the core and carries out the answer.

That split is why most of RelayDock is tested without OBS. `relaydock-tests.exe` links only the core.

## Life of the plugin

| OBS event | RelayDock |
| --- | --- |
| `obs_module_load` | Creates `AppContext`: loads settings, registers providers, opens the credential store, registers saved keys with the redactor. Creates the dock. |
| Finished loading | Reads the encoder list, binds vertical layouts to sources, starts the performance monitor. |
| Scene collection cleanup | Releases every reference to the collection's sources. |
| Scene collection changed | Binds vertical layouts again. |
| The main window is asked to close | `ExitGuard` asks first when a destination is active, because OBS only asks for its own outputs. |
| Exit | Closes RelayDock's windows, stops every output and waits for it, releases every OBS object, saves settings. This happens while OBS is still intact. |
| `obs_module_unload` | Destroys `AppContext`. |

## Destinations and outputs

Each destination gets its own OBS output and service (`rtmp_output`, `rtmp_custom`). Nothing is shared between destinations except encoders. That is how one destination can fail, reconnect or stop without touching another.

`DestinationStateMachine` (core) holds the phase of one destination: Idle, Starting, Live, Reconnecting, Stopping, Failed. `OutputManager` feeds it the signals OBS raises and acts on its answers.

OBS raises output signals on its own threads. The handlers copy what they need and post it to the interface thread. They touch nothing else.

Stopping an output can block inside OBS while a connection attempt is in flight. So stops run on short-lived worker threads, and shutdown joins them.

From the moment the first destination starts until the last one has released its encoders, two things are in force:

- `SleepInhibitor` (core) asks Windows to keep the PC and the display awake. It uses a power request, not `SetThreadExecutionState`, because that state belongs to the interface thread and OBS sets and clears it for its own stream.
- `VideoGuard` keeps OBS from changing its video settings. OBS refuses that change only while an encoder is capturing. The encoder of a destination that is connecting, or that waits to reconnect, is not capturing. OBS would free the video the encoder was created for and crash at the next connection attempt. The guard is a raw video output of RelayDock's own on a view of 16 by 16 pixels. While it runs, OBS counts video as in use and greys out its Video settings, as it does during its own stream. `tests/integration/Test-MultiDestination.ps1 -Only video-settings` covers both cases.

## Effective settings and encoder sharing

A destination's saved settings are a starting point. `resolveEffectiveSettings` (core) computes what it streams with from:

- the performance mode,
- the settings the user locked,
- the platform's limits,
- the encoders this PC has,
- the OBS canvas and frame rate,
- reductions the optimiser applied.

A locked setting keeps its saved value. An unlocked one follows the mode, inside the platform's limits. Potato and Balanced give every unlocked destination on a canvas the same values, on purpose, so their settings match.

`planEncoders` (core) then groups destinations whose effective video settings are exactly equal. It never rounds one destination towards another. `EncoderPool` creates one OBS encoder per group and hands out references. OBS feeds every output attached to an encoder from one encode.

## Vertical video

A vertical destination reads from a vertical canvas: a private OBS scene rendered by an `obs_view_t` at the canvas size. RelayDock uses the view API because OBS documents it as stable.

The view joins the OBS render loop only while an encoder uses it. With no vertical destination active, it costs nothing. The layout editor draws the scene directly and needs no view.

`computePlacement` (core) decides where a source lands in its box: Fill scales it to cover the box and crops, Fit scales it to fit inside. Both use one scale factor for width and height, which is what "never stretched" means. `VerticalCanvas` turns that into OBS scene item bounds and crop.

Layouts refer to sources of the scene collection, so they are saved inside the collection through OBS's save callback.

## Automatic optimisation

`PerformanceMonitor` reads counters OBS already keeps (rendered and lagged frames, encoded and skipped frames, sent and dropped frames) once per second, and turns them into percentages over a ten second window. Processor and graphics load of the whole PC come from `SystemSampler` on its own thread.

`Optimizer` (core) is a set of fixed rules with sustain timers, hysteresis, a cooldown and slow recovery. It returns changes to apply and suggestions to show. A bitrate change is applied to the running encoder through `EncoderPool::retune`. A frame rate or resolution change needs a new encoder, so the destination reconnects.

## Secrets

- `SecretString` holds a secret in memory and zeroes it when it goes away.
- `ICredentialStore` is the storage interface. `WindowsCredentialStore` uses Windows Credential Manager. `MemoryCredentialStore` serves tests and the session-only fallback.
- `SecretVault` is the only way in and out. Every secret that passes through it is registered with the `Redactor`.
- `rd::log...` passes every line through the redactor before OBS's logger sees it.
- `AppConfig` has no member that can hold a secret.

## Threads

| Thread | What runs there |
| --- | --- |
| OBS interface thread | All of `AppContext`, `OutputManager`, the dock and dialogs. |
| OBS signal threads | The static signal handlers of `OutputManager`. They only post to the interface thread. |
| OBS graphics thread | The draw callback of the layout editor's preview. It reads a small state structure under a mutex. |
| Stop workers | `obs_output_stop` and `obs_output_force_stop` calls. Joined at shutdown. |
| `SystemSampler` | Processor and graphics load. |
| Background tasks | Test connection and the update check. Cancellable. |
| Chat readers | One thread for Twitch chat, one for YouTube chat, one for a Twitch sign-in. Each ends within about a tenth of a second when asked. They hand events to the interface thread. |

Each class says which thread owns it in a comment at its declaration.

## Chat

`src/chat` reads live chat and has no OBS and no Qt in it. `twitch_protocol` and `youtube_protocol` build requests and read answers without touching the network, so the tests cover every answer the platforms document. `TwitchChat` and `YouTubeChat` hold the connections, each on its own thread. `ChatHub` in `src/app` owns them, brings their events to the interface thread and keeps the one list the chat dock shows.

`src/network` has the request function and the WebSocket client they use. Both run WinHTTP in asynchronous mode, which is what makes them cancellable.

Chat is text from strangers. `cleanChatText` removes control characters and characters that reverse the reading direction, and the dock escapes every character before it draws.

## Add a platform

A platform is a class derived from `ProviderBase` that fills in data: name, servers, limits, setup notes. `CONTRIBUTING.md` has the steps. No other code lists platforms.

## Source tree

```
src/                 The plugin and the core library
tests/unit           Unit tests of the core
tests/security       Security tests of the core
tests/integration    Scripts that drive a real OBS
tests/tools          The RTMP test server, and stand-ins for Twitch and YouTube
tests/support        A small web server for the tests
resources/           Icons and legal documents, compiled into the DLL
data/locale          Interface strings for OBS's locale system
cmake/               Build framework, adapted from the OBS plugin template
installer/           Inno Setup script
scripts/             Build, locale and packaging scripts
docs/                Documentation
```
