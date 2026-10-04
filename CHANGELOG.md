# Changelog

RelayDock follows semantic versioning. A version is MAJOR.MINOR.PATCH:

- MAJOR changes when an update breaks something you rely on, such as saved settings a newer version can no longer read.
- MINOR adds features and keeps everything working.
- PATCH fixes problems.

A version with a suffix, such as `1.0.0-rc.1`, is a pre-release.

Each version has a heading with its release date. `scripts/update-versions.ps1` builds `docs/versions.md` from this file: the table of versions, their files and their changes.

## 1.0.0-rc.3 (2026-10-05)

The third release candidate. It adds a chat dock for Twitch and YouTube, the logo of each platform, and an uninstall from inside RelayDock.

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

## 1.0.0-rc.2 (2026-10-04)

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

## 1.0.0-rc.1 (2026-10-04)

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
