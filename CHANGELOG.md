# Changelog

RelayDock follows semantic versioning. A version is MAJOR.MINOR.PATCH:

- MAJOR changes when an update breaks something you rely on, such as saved settings a newer version can no longer read.
- MINOR adds features and keeps everything working.
- PATCH fixes problems.

A version with a suffix, such as `1.0.0-rc.1`, is a pre-release.

## 1.0.0-rc.1

The first release candidate. Everything listed here is built and covered by automated tests against a local test server. No real platform has received a stream from this version in the project's own testing yet. `docs/testing.md` and `docs/release-checklist.md` show what is verified and what is open.

### Streaming

- Stream to Twitch, YouTube, Facebook, TikTok and custom RTMP or RTMPS servers at the same time, straight from your PC.
- Each destination has its own connection. One failing, reconnecting or stopping does not affect the others.
- Destinations with identical video settings share one encoder.
- Horizontal 16:9 and vertical 9:16 streams at the same time. The vertical picture is never stretched.
- A vertical layout editor with a live preview.
- Automatic reconnect with growing waits, and a manual Reconnect.
- A hidden test stream for Twitch. Test connection for every destination.

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

### Known limitations

See `docs/known-limitations.md`.
