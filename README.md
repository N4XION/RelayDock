# RelayDock

A free and open-source OBS Studio multistream plugin that lets you stream to Twitch, TikTok, YouTube, Facebook, and custom RTMP or RTMPS destinations from one OBS session.

Your streams go straight from your PC to each platform. RelayDock has no server, no account, no subscription and no telemetry.

## Download and install

You need Windows 10 or 11 (64-bit) and OBS Studio 32.0.0 or newer. All tests ran on Windows 11.

1. Download the installer: [RelayDock-1.0.0-windows-x64-Setup.exe](https://github.com/N4XION/RelayDock/releases/download/v1.0.0/RelayDock-1.0.0-windows-x64-Setup.exe)
2. Close OBS Studio.
3. Open the downloaded file and follow its steps. If Windows shows "Windows protected your PC", choose More info, then Run anyway. Windows shows that because the file is not code-signed.
4. Start OBS Studio, open the Docks menu and choose RelayDock.

To update, choose Update now when RelayDock tells you about a newer version. It downloads the installer, checks it and installs it when you close OBS Studio. Your destinations, settings and stream keys stay. Running the newer installer yourself works too.

You do not pick a folder. The installer puts RelayDock into `C:\ProgramData\obs-studio\plugins\relaydock`, the folder where OBS Studio looks for plugins. To remove it, open the RelayDock settings, choose Updates and then Uninstall RelayDock. Windows Settings, Apps, Installed apps works too.

For a portable OBS Studio, download [RelayDock-1.0.0-windows-x64.zip](https://github.com/N4XION/RelayDock/releases/download/v1.0.0/RelayDock-1.0.0-windows-x64.zip) and follow [docs/manual-installation.md](docs/manual-installation.md).

This is version 1.0.0. Its [release page](https://github.com/N4XION/RelayDock/releases/tag/v1.0.0) has the notes and the checksums, and [docs/installation.md](docs/installation.md) shows how to check your download, update and uninstall. [docs/versions.md](docs/versions.md) lists every version with its files and what changed.

![The RelayDock dock inside OBS Studio with three destinations live](docs/screenshots/obs-with-dock.png)

## Status

This is version 1.0.0. The project owner decided to publish it while some release checks are still open.

- Built and tested on Windows 11 with OBS Studio 32.0.4 and 32.2.2.
- Every feature below is covered by automated tests on one PC. Streams go to a test server on that PC, and chat comes from stand-ins for Twitch and YouTube.
- TikTok accepted a stream from 1.0.0-rc.1. Twitch, YouTube and Facebook have not received a stream in the project's own testing yet. That needs real accounts.
- The ZIP install is tested for this version. The installer and the uninstall from inside RelayDock were tested on the build before the version was renamed to 1.0.0. The installer script and the uninstall code did not change in between.
- [docs/testing.md](docs/testing.md) lists what is verified, and [docs/release-checklist.md](docs/release-checklist.md) lists what is still open.

If you try it with a real platform, a [platform test report](../../issues/new/choose) helps the next person.

## What it does

- Streams to several destinations at once. Each has its own connection, so one failing or reconnecting never stops the others.
- Encodes once for destinations with identical settings. Three destinations can cost one encode.
- Streams horizontal 16:9 and vertical 9:16 at the same time. The vertical picture is cropped or fitted, never stretched.
- Four performance modes: Potato, Balanced, Quality and Custom.
- Rule-based automatic optimisation that suggests or applies a lower bitrate, frame rate or resolution when frames drop, and restores them later. You can lock any setting.
- A preflight check that says READY, WARNING or FAILED, with a reason and a fix for each finding.
- Protects a running stream. It asks before OBS closes, keeps the PC awake, and keeps the OBS video settings locked while a destination connects or waits to reconnect.
- Shows the comments of your Twitch and YouTube streams in one list, the RelayDock Chat dock, with Bits, Super Chats, gifts and new subscribers. [docs/chat.md](docs/chat.md) shows how to set it up.
- Keeps stream keys in Windows Credential Manager. Never in a file, never in a log, never on screen.
- Looks like part of OBS, or the way you set it: themes, accent colour, background image, spacing, saved layouts.

## What it does not do

- It cannot fetch your stream key, set a title or press Go live for you. It signs in to Twitch only to read chat, and only when you ask.
- It reads no chat from TikTok or Facebook. TikTok publishes no way to do that, and Facebook's needs an approval RelayDock does not have.
- It streams over RTMP and RTMPS only.
- It does not record.
- It runs on 64-bit Windows 10 and 11 only, in OBS Studio 32 or newer. OBS Studio itself ended 32-bit Windows and Windows 7 and 8 with its version 28.

[docs/known-limitations.md](docs/known-limitations.md) has the full list.

## First stream

1. Open the dock. Review the six short documents RelayDock shows once.
2. Choose + Add Platform, pick a platform and paste your stream key.
3. Add a second destination the same way.
4. Choose the stethoscope button to run the preflight check.
5. Choose Start All Enabled.

[docs/getting-started.md](docs/getting-started.md) walks through it.

| Platform | Guide |
| --- | --- |
| Twitch | [docs/twitch.md](docs/twitch.md) |
| YouTube | [docs/youtube.md](docs/youtube.md) |
| Facebook | [docs/facebook.md](docs/facebook.md) |
| TikTok | [docs/tiktok.md](docs/tiktok.md) |
| Any RTMP or RTMPS server | [docs/custom-rtmp.md](docs/custom-rtmp.md) |

## Screenshots

Every picture here is a capture of the real plugin running in OBS Studio, made by `tests/integration/Capture-Screenshots.ps1`. The destinations in them stream to a test server on the same PC.

### Destinations

![Destination cards in the dock: three live, one switched off](docs/screenshots/dock-live.png)

### Adding a destination

The stream key field shows dots as you type. A saved key is never shown again.

![The destination editor with a hidden stream key](docs/screenshots/editor.png)

### Horizontal and vertical at once

![The vertical layout editor with a live preview](docs/screenshots/vertical-editor.png)

### Preflight check

![The preflight check reporting a missing stream key](docs/screenshots/preflight.png)

### Automatic optimisation

![A suggestion to lower the bitrate, with Apply, Ignore and Lock Setting](docs/screenshots/suggestion.png)

### Settings

![The settings window on the Performance page](docs/screenshots/settings-performance.png)

### Themes

![The dock in the Light theme](docs/screenshots/dock-light.png)

## Performance

RelayDock adds work only for what you stream. Destinations with identical settings share one encoder, and the vertical canvas is rendered only while a vertical destination uses it.

[docs/performance-results.md](docs/performance-results.md) has numbers measured on the development PC with `tests/integration/Test-Performance.ps1`: idle cost, one to four destinations on a shared encoder, four separate encoders, and each performance mode. You can run the same script on your own PC.

[docs/performance.md](docs/performance.md) explains the modes, locks and automatic optimisation.

## Security and privacy

- Stream keys and RTMP passwords are saved in Windows Credential Manager, encrypted by Windows for your account. So are the Twitch chat sign-in and your YouTube API key, when you set up chat.
- The settings file has no field that can hold a key.
- Keys are removed from every log line and from the diagnostics report.
- RelayDock opens network connections only for your streams, for Test connection, for the update check, for chat once you set it up, and for Update now when you choose it. The check runs once when OBS starts, unless you switch that off, and when you ask for it.

[docs/security.md](docs/security.md) describes the design and what it cannot protect against. Report security problems privately, as [SECURITY.md](SECURITY.md) explains.

## Documentation

| Topic | Page |
| --- | --- |
| Install, update, uninstall | [docs/installation.md](docs/installation.md) |
| Every version, its files and its changes | [docs/versions.md](docs/versions.md) |
| Install by hand, portable OBS | [docs/manual-installation.md](docs/manual-installation.md) |
| First steps | [docs/getting-started.md](docs/getting-started.md) |
| Modes, locks, optimisation | [docs/performance.md](docs/performance.md) |
| Themes and layouts | [docs/appearance.md](docs/appearance.md) |
| Keys, logs, network | [docs/security.md](docs/security.md) |
| When something fails | [docs/troubleshooting.md](docs/troubleshooting.md) |
| What is not supported | [docs/known-limitations.md](docs/known-limitations.md) |
| Build it yourself | [docs/building-from-source.md](docs/building-from-source.md) |
| How it is built inside | [docs/architecture.md](docs/architecture.md) |
| Tests and their results | [docs/testing.md](docs/testing.md) |
| Release steps | [docs/release-checklist.md](docs/release-checklist.md) |

## Build from source

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
```

You need Visual Studio 2022 with the C++ workload. Everything else is fetched by the build. See [docs/building-from-source.md](docs/building-from-source.md).

## Contributing

Bug reports, platform test reports, translations and code are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) and the [Code of Conduct](CODE_OF_CONDUCT.md) first.

## Licence

RelayDock is free software under the [GNU General Public License, version 2 or later](LICENSE). It comes with no warranty.

Third-party software and its licences are listed in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md). The privacy policy is in [PRIVACY.md](PRIVACY.md).

RelayDock is not affiliated with Twitch, TikTok, YouTube, Facebook or the OBS Project. Their names and logos are trademarks of their owners. RelayDock shows a logo only to mark which platform a destination or a chat message belongs to. [resources/brands/NOTICE.txt](resources/brands/NOTICE.txt) says where the logo files come from, and Settings, Appearance switches the logos off.
