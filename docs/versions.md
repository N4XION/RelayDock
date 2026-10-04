# Versions

Every RelayDock version, its files and what changed in it. The newest is at the top.

`scripts/update-versions.ps1` makes this page from `CHANGELOG.md`. Change the text there, not here.

| Version | Released | Kind | Installer | ZIP for a portable OBS | More |
| --- | --- | --- | --- | --- | --- |
| 1.1.0 | 2026-10-05 | Release | [RelayDock-1.1.0-windows-x64-Setup.exe](https://github.com/N4XION/RelayDock/releases/download/v1.1.0/RelayDock-1.1.0-windows-x64-Setup.exe) | [RelayDock-1.1.0-windows-x64.zip](https://github.com/N4XION/RelayDock/releases/download/v1.1.0/RelayDock-1.1.0-windows-x64.zip) | [What changed](#version-110), [release page](https://github.com/N4XION/RelayDock/releases/tag/v1.1.0) |
| 1.0.0 | 2026-10-05 | Release | [RelayDock-1.0.0-windows-x64-Setup.exe](https://github.com/N4XION/RelayDock/releases/download/v1.0.0/RelayDock-1.0.0-windows-x64-Setup.exe) | [RelayDock-1.0.0-windows-x64.zip](https://github.com/N4XION/RelayDock/releases/download/v1.0.0/RelayDock-1.0.0-windows-x64.zip) | [What changed](#version-100), [release page](https://github.com/N4XION/RelayDock/releases/tag/v1.0.0) |
| 1.0.0-rc.2 | 2026-10-04 | Release candidate | [RelayDock-1.0.0-rc.2-windows-x64-Setup.exe](https://github.com/N4XION/RelayDock/releases/download/v1.0.0-rc.2/RelayDock-1.0.0-rc.2-windows-x64-Setup.exe) | [RelayDock-1.0.0-rc.2-windows-x64.zip](https://github.com/N4XION/RelayDock/releases/download/v1.0.0-rc.2/RelayDock-1.0.0-rc.2-windows-x64.zip) | [What changed](#version-100-rc2), [release page](https://github.com/N4XION/RelayDock/releases/tag/v1.0.0-rc.2) |
| 1.0.0-rc.1 | 2026-10-04 | Release candidate | [RelayDock-1.0.0-rc.1-windows-x64-Setup.exe](https://github.com/N4XION/RelayDock/releases/download/v1.0.0-rc.1/RelayDock-1.0.0-rc.1-windows-x64-Setup.exe) | [RelayDock-1.0.0-rc.1-windows-x64.zip](https://github.com/N4XION/RelayDock/releases/download/v1.0.0-rc.1/RelayDock-1.0.0-rc.1-windows-x64.zip) | [What changed](#version-100-rc1), [release page](https://github.com/N4XION/RelayDock/releases/tag/v1.0.0-rc.1) |

Every release also carries `SHA256SUMS.txt`, with the SHA-256 hash of each file, and `THIRD_PARTY_LICENSES.txt`. [installation.md](installation.md) shows how to check a download against the hashes.

The newest version needs Windows 10 or 11 (64-bit) and OBS Studio 32.0.0 or newer.

## Update from an older version

1. Download the installer of the newer version from the table.
2. Close OBS Studio.
3. Open the downloaded file and follow its steps.

The installer replaces the old version. Your destinations, settings and stream keys stay. For a portable OBS, copy the files from the ZIP over the old ones, as [manual-installation.md](manual-installation.md) describes.

RelayDock looks for a newer version each time OBS starts and tells you when there is one. From version 1.1.0 on, its window offers Update now, which downloads the installer, checks it and installs it when you close OBS. Settings, Updates has a Check for updates button and the switch for the check at start-up. RelayDock never downloads or installs anything by itself.

## Version 1.1.0

Released 2026-10-05. Release.

Files:

- [RelayDock-1.1.0-windows-x64-Setup.exe](https://github.com/N4XION/RelayDock/releases/download/v1.1.0/RelayDock-1.1.0-windows-x64-Setup.exe)
- [RelayDock-1.1.0-windows-x64.zip](https://github.com/N4XION/RelayDock/releases/download/v1.1.0/RelayDock-1.1.0-windows-x64.zip)
- [SHA256SUMS.txt](https://github.com/N4XION/RelayDock/releases/download/v1.1.0/SHA256SUMS.txt)
- [THIRD_PARTY_LICENSES.txt](https://github.com/N4XION/RelayDock/releases/download/v1.1.0/THIRD_PARTY_LICENSES.txt)

RelayDock can update itself when you ask it to.

### Added

- Update now. The window that announces a newer version downloads the installer from the release page, checks it against the checksums of the release and installs it when you close OBS Studio. Until then you can cancel. Close OBS and install does both steps at once, and the installer offers to start OBS again. It works for a RelayDock that the installer put on your PC. A RelayDock that you copied by hand still gets the download in your browser.
- Settings, Updates and the dock say when an update waits for OBS to close, and offer Cancel update.

### Changed

- An installer that RelayDock starts waits for OBS to close without a window. It asks Windows whether OBS still runs every two seconds, and no longer twice a second. The uninstall from inside RelayDock waits the same way.
- Three documents have a new version, and RelayDock asks you to review them again: the Privacy Policy, the Security and Credentials Notice and the Third-Party Services Notice describe Update now.

### Security

- Update now keeps a downloaded installer only when it is the file the release holds: it comes from the release pages of the RelayDock project on github.com, it has the size GitHub lists, and its SHA-256 checksum is the one `SHA256SUMS.txt` of that release names. A file that does not match is deleted and never started. `docs/security.md` says what this check proves and what it does not: the release files carry no code signature.

## Version 1.0.0

Released 2026-10-05. Release.

Files:

- [RelayDock-1.0.0-windows-x64-Setup.exe](https://github.com/N4XION/RelayDock/releases/download/v1.0.0/RelayDock-1.0.0-windows-x64-Setup.exe)
- [RelayDock-1.0.0-windows-x64.zip](https://github.com/N4XION/RelayDock/releases/download/v1.0.0/RelayDock-1.0.0-windows-x64.zip)
- [SHA256SUMS.txt](https://github.com/N4XION/RelayDock/releases/download/v1.0.0/SHA256SUMS.txt)
- [THIRD_PARTY_LICENSES.txt](https://github.com/N4XION/RelayDock/releases/download/v1.0.0/THIRD_PARTY_LICENSES.txt)

The first version without the release candidate label. It adds a chat dock for Twitch and YouTube, the logo of each platform, and an uninstall from inside RelayDock.

The project owner decided to publish it as 1.0.0 while some release checks are still open. The release notes list them.

### Added

- RelayDock Chat, a second dock. It shows the comments of your Twitch and YouTube streams in one list, with Bits, Super Chats, gifts, new subscribers and raids. `docs/chat.md` shows how to set it up. Twitch needs a sign-in on twitch.tv and an application id. YouTube needs an API key of your own and the link to your stream. TikTok and Facebook are not read, because neither offers RelayDock a way to do it.
- The badge of Twitch, TikTok, YouTube and Facebook shows the platform's logo. Settings, Appearance switches back to letters.
- Uninstall under Settings, Updates. RelayDock starts its uninstaller, which waits until you close OBS. Keep RelayDock takes the request back. Your destinations, settings and stream keys stay unless you tick the box.

### Changed

- Closing OBS no longer waits for a web request that hangs. Every request RelayDock makes can be cancelled within about a tenth of a second.
- When GitHub turns the update check away because too many requests came from your internet address, RelayDock says so. That happens on a shared address, such as a VPN.
- Four documents have a new version, and RelayDock asks you to review them again: the Privacy Policy and the Security and Credentials Notice describe chat, the Third-Party Services Notice describes chat and the logos, and the Open Source Licenses page names the source of the logos.

### Tested

- The installer test and the new uninstall test run on a PC that has RelayDock installed, with a test build of the installer that Windows knows under another identity.
- The chat readers are tested against stand-ins for Twitch and YouTube. Nobody has used them with the real platforms yet.

## Version 1.0.0-rc.2

Released 2026-10-04. Release candidate.

Files:

- [RelayDock-1.0.0-rc.2-windows-x64-Setup.exe](https://github.com/N4XION/RelayDock/releases/download/v1.0.0-rc.2/RelayDock-1.0.0-rc.2-windows-x64-Setup.exe)
- [RelayDock-1.0.0-rc.2-windows-x64.zip](https://github.com/N4XION/RelayDock/releases/download/v1.0.0-rc.2/RelayDock-1.0.0-rc.2-windows-x64.zip)
- [SHA256SUMS.txt](https://github.com/N4XION/RelayDock/releases/download/v1.0.0-rc.2/SHA256SUMS.txt)
- [THIRD_PARTY_LICENSES.txt](https://github.com/N4XION/RelayDock/releases/download/v1.0.0-rc.2/THIRD_PARTY_LICENSES.txt)

The second release candidate. It changes what a vertical stream looks like by default, and RelayDock now tells you when a newer version exists.

### Changed

- A new vertical layout shows your whole OBS picture, with empty space above and below. Before, it filled the 9:16 frame and cut off the sides. To crop on purpose, open the layout editor and set Scaling to Fill.
- A vertical layout that is still the untouched standard layout of 1.0.0-rc.1 changes to the new look by itself when OBS loads it. The OBS log says so. A layout you arranged stays as it is.
- RelayDock looks for a newer version each time OBS starts. Before, it looked only when you asked. Switch it off under Settings, Updates. The Privacy Policy and the Third-Party Services Notice describe the change, and RelayDock asks you to review both again.

### Added

- A window that tells you when a newer version exists. It downloads the installer in your browser, links to what changed, and offers Later and Skip this version. RelayDock still installs nothing by itself.
- `docs/versions.md`: every version with its files and its changes.

### Tested since 1.0.0-rc.1

- TikTok accepted a stream from 1.0.0-rc.1. `docs/testing.md` says what was checked and what was not.

## Version 1.0.0-rc.1

Released 2026-10-04. Release candidate.

Files:

- [RelayDock-1.0.0-rc.1-windows-x64-Setup.exe](https://github.com/N4XION/RelayDock/releases/download/v1.0.0-rc.1/RelayDock-1.0.0-rc.1-windows-x64-Setup.exe)
- [RelayDock-1.0.0-rc.1-windows-x64.zip](https://github.com/N4XION/RelayDock/releases/download/v1.0.0-rc.1/RelayDock-1.0.0-rc.1-windows-x64.zip)
- [SHA256SUMS.txt](https://github.com/N4XION/RelayDock/releases/download/v1.0.0-rc.1/SHA256SUMS.txt)
- [THIRD_PARTY_LICENSES.txt](https://github.com/N4XION/RelayDock/releases/download/v1.0.0-rc.1/THIRD_PARTY_LICENSES.txt)

The first release candidate. Everything listed here is built and covered by automated tests against a local test server. No real platform has received a stream from this version in the project's own testing yet. `docs/testing.md` and `docs/release-checklist.md` show what is verified and what is open.

### Streaming

- Stream to Twitch, YouTube, Facebook, TikTok and custom RTMP or RTMPS servers at the same time, straight from your PC.
- Each destination has its own connection. One failing, reconnecting or stopping does not affect the others.
- Destinations with identical video settings share one encoder.
- Horizontal 16:9 and vertical 9:16 streams at the same time. The vertical picture is never stretched.
- A vertical layout editor with a live preview.
- Automatic reconnect with growing waits, and a manual Reconnect.
- A hidden test stream for Twitch. Test connection for every destination.
- A question before OBS closes while a destination is active.
- The PC and the display stay awake while a destination is active.
- OBS keeps its video settings locked while a destination is connecting, live or waiting to reconnect. Without that lock, a change during a reconnect wait crashes OBS.

### Performance

- Four performance modes: Potato, Balanced, Quality and Custom.
- Setting locks for resolution, frame rate, bitrate and encoder.
- Rule-based automatic optimisation with Off, Suggest changes and Automatic modes. Bitrate changes apply while live.
- Prevent Game Lag.
- A preflight check that reports READY, WARNING or FAILED with a reason and a fix for each finding.
- An upload budget from the speed you enter. RelayDock runs no speed test.

### Security and privacy

- Stream keys and passwords are saved in Windows Credential Manager, never in a file.
- A saved key is never shown. Replace, Copy and Remove are the only actions.
- Keys are removed from log lines and from the diagnostics report.
- No telemetry, no analytics, no account, no RelayDock server.
- A first-run review of six legal documents.

### Interface

- A dock with destination cards in list or grid, compact or expanded, with drag to reorder.
- A settings window with 17 pages.
- Themes: Follow OBS, Light, Dark and Custom, with accent colour, background image, opacity, corner radius, text size and spacing.
- Saved dock layouts.
- A diagnostics report you can save or copy.
- A check for updates on request. It asks GitHub for the newest release and shows a link. It never downloads or installs anything.

### Known limitations

See `docs/known-limitations.md`.
