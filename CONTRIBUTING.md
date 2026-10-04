# Contributing to RelayDock

Thank you for helping. This page explains how to report a problem, how to build and test, and what a change needs before it can merge.

## Report a problem

Use the issue templates. A good report has:

- the RelayDock version and build (Settings, About),
- the OBS Studio and Windows versions,
- what you did, what you expected and what happened,
- the diagnostics report (Settings, Diagnostics, Save report). It contains no stream key and no password. Read it before you attach it.

Never post a stream key, password or token. If your report is about security, follow `SECURITY.md` and do not open a public issue.

## Build

`docs/building-from-source.md` has the full steps. In short:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
```

The first configure downloads the OBS Studio sources and dependencies that `buildspec.json` pins by hash. Every tool the build needs is free.

## Test

A change is ready when these pass:

```powershell
# Unit and security tests. No OBS needed.
ctest --preset core-tests

# Integration tests in a real OBS. Needs the test build.
cmake --preset windows-hooks-x64
cmake --build --preset windows-hooks-x64
.\tests\integration\Test-PluginLoad.ps1 -ObsRoot <portable OBS> -BuildDir build_hooks_x64
```

`docs/testing.md` lists every test script, what it checks and how to set up a portable OBS for it.

The rules for tests:

- A test proves something that was observed. No test may assert a number nobody measured.
- A bug fix comes with a test that fails without the fix.
- Tests use made-up stream keys with obvious names. No test may contain a real key.
- Integration tests stream to the local test server in `tests/tools/rtmp-sink`. They never connect to a real platform.

## Code

- C++20, built with MSVC. `.clang-format` and `.editorconfig` define the layout.
- Code under `src` that needs neither OBS nor Qt belongs in the core library and gets unit tests. See `docs/architecture.md` for the layers.
- Never block the OBS interface thread. Network and disk waits run on worker threads.
- Own OBS objects with the RAII types from `obs.hpp`. Release them before OBS shuts down.
- Say which thread owns a class in a comment at its declaration.
- Comments explain why. Leave out comments that repeat the code.

### Secrets

- A stream key or password is a `SecretString`. Never copy one into a `std::string` that outlives the call.
- Secrets go through `SecretVault` and nowhere else. No field of `AppConfig` may hold one.
- Log through `rd::log...`. It redacts. Never call `blog` directly with data that came from the user.
- A new place where text leaves RelayDock (a log, a file, the clipboard, the screen) needs a test in `tests/security`.

### Interface text

Write interface text with a key and the English text:

```cpp
uiText("Card.Status.Live", "LIVE")
uiTextF("Card.Stop.Tip", "Stop {0}", name)
```

Then run `scripts/update-locale.ps1`. It rebuilds `data/locale/en-US.ini` from the code. CI fails when the file is out of date.

Write the way the rest of RelayDock does: active voice, short sentences, "you" and "your". Say what happened, what RelayDock knows and what to do next. No exclamation marks, no jargon, no blame.

### Add a platform

1. Add a folder under `src/providers` with a class derived from `ProviderBase`. Fill in the name, servers, limits and setup notes.
2. Give every limit a source: the platform's own page and the date you read it. Record them in `docs/research/platform-requirements.md`.
3. Register the provider in `registerBuiltInProviders()`.
4. Add tests to `tests/unit/test_providers.cpp`.
5. Add a guide under `docs`.

Nothing else in RelayDock knows the list of platforms.

RelayDock connects to platforms the way OBS Studio does, with a server address and a stream key. A change that needs a paid service, an account with the RelayDock project or a server run by the project does not fit.

## Pull requests

- One topic per pull request.
- Describe what changed and how you tested it. Name the OBS versions you tested with.
- Update the docs and `CHANGELOG.md` when users can notice the change.
- CI must pass.

By contributing you agree that your contribution is licensed under the GNU General Public License, version 2 or later, the licence of RelayDock.

## Releases

`docs/release-checklist.md` lists what a release needs. A maintainer tags the version, GitHub Actions builds the files and their checksums, and the maintainer publishes the draft release after the checklist is complete.
