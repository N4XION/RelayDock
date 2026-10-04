# Build RelayDock from source

Everything you need is free.

## Tools

| Tool | Version | Notes |
| --- | --- | --- |
| Windows | 11, 64-bit | |
| Visual Studio | 2022 Community, or the Build Tools | With the "Desktop development with C++" workload. It includes the MSVC compiler, the Windows SDK and CMake. |
| Git | Any recent version | The build number comes from the commit history. |
| PowerShell | 5.1 or newer | Ships with Windows. Used by the test scripts. |
| Inno Setup | 6 | Only to build the installer. |

You do not install OBS Studio's sources, Qt or any library by hand. The build fetches them.

## Get the code

```powershell
git clone <this repository's URL>
cd RelayDock
```

## Build the plugin

Open a "Developer PowerShell for VS 2022", or any PowerShell where `cmake` is on the PATH.

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
```

The first configure:

1. downloads the OBS Studio 32.0.4 sources, the OBS dependency package and its Qt 6 build, each checked against the SHA-256 hash in `buildspec.json`,
2. builds the two OBS libraries RelayDock links to, `libobs` and `obs-frontend-api`.

That takes about ten minutes once. Later builds take seconds.

The result is in `build_x64\rundir\RelWithDebInfo`:

```
bin\relaydock.dll
data\relaydock\locale\en-US.ini
```

RelayDock builds against OBS 32.0.4 on purpose. OBS loads a plugin when the plugin's OBS major and minor version is not newer than its own, so a build against 32.0 loads in 32.0 and everything after it.

`scripts\dev.ps1` finds Visual Studio's CMake for you when it is not on the PATH:

```powershell
.\scripts\dev.ps1 -Preset windows-x64 -Steps configure,build,test
```

### Keep build output out of a synced folder

If your checkout is inside OneDrive or a similar folder, put the build elsewhere. Create `CMakeUserPresets.json` (Git ignores it) with a preset that inherits `windows-x64` and sets `binaryDir` and the cache variable `RELAYDOCK_DEPS_DIR` to folders outside the synced tree.

## Run the unit tests

The core of RelayDock needs neither OBS nor Qt. Build and test it by itself in under a minute:

```powershell
cmake --preset core-tests
cmake --build --preset core-tests
ctest --preset core-tests
```

## Try your build in OBS

Use a portable copy of OBS, so your real OBS and its settings stay untouched.

1. Download the OBS Studio ZIP (not the installer) from the OBS project's releases and extract it, for example to `C:\obs-test`.
2. Start it with your build:

```powershell
$env:OBS_PLUGINS_PATH = "$PWD\build_x64\rundir\RelWithDebInfo\bin"
$env:OBS_PLUGINS_DATA_PATH = "$PWD\build_x64\rundir\RelWithDebInfo\data"
& C:\obs-test\bin\64bit\obs64.exe --portable --multi
```

Start `obs64.exe` from its own folder, or pass the full path as above with the working directory set to `C:\obs-test\bin\64bit`.

## Integration tests

The integration tests drive a real OBS through a test-only scenario runner. That runner exists only in a test build:

```powershell
cmake --preset windows-hooks-x64
cmake --build --preset windows-hooks-x64
.\tests\integration\Run-All.ps1 -ObsRoot C:\obs-test -BuildDir build_hooks_x64
```

A test build logs a warning at start-up and must never be released. Release builds do not contain the runner. [testing.md](testing.md) describes every suite.

## Build the release files

```powershell
.\scripts\package.ps1
```

This builds the release preset, then writes to `release\`:

- `RelayDock-<version>-windows-x64.zip`
- `RelayDock-<version>-windows-x64-Setup.exe` (when Inno Setup is installed)
- `THIRD_PARTY_LICENSES.txt`
- `SHA256SUMS.txt`

GitHub Actions runs the same script for a tagged release.

## After you change interface text

```powershell
.\scripts\update-locale.ps1
```

It rebuilds `data\locale\en-US.ini` from the strings in the code. CI fails when that file is out of date.

## Options

| CMake option | Default | Meaning |
| --- | --- | --- |
| `RELAYDOCK_BUILD_PLUGIN` | ON | Build the OBS plugin. OFF builds the core library only. |
| `RELAYDOCK_BUILD_TESTS` | ON | Build the unit and security tests and the RTMP test server. |
| `RELAYDOCK_TEST_HOOKS` | OFF | Include the scenario runner for integration tests. Never for a release. |
| `RELAYDOCK_DEPS_DIR` | `.deps` in the source tree | Where downloaded dependencies go. |
