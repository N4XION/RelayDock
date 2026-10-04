# Security and privacy

This page describes what RelayDock does with your stream keys and your data, how you can check it, and what it cannot protect against.

## Where things are stored

| What | Where | Encrypted |
| --- | --- | --- |
| Stream keys and RTMP passwords | Windows Credential Manager, entries named `RelayDock:<id>:stream-key` and `RelayDock:<id>:password` | Yes, by Windows, for your Windows account |
| The Twitch chat sign-in and the YouTube API key | Windows Credential Manager, entries named `RelayDock:<id>:chat-sign-in` and `RelayDock:<id>:api-key` | Yes, by Windows, for your Windows account |
| Destinations and settings | `%APPDATA%\obs-studio\plugin_config\relaydock\config.json` | No. It holds no secret. |
| Chat | Memory only, the newest 500 comments and events, until OBS closes | Not stored |
| Vertical layouts | Your OBS scene collection file | No. It holds no secret. |
| Log lines | The OBS log | No. Keys are removed before a line is written. |

You can see the saved secrets in Control Panel, Credential Manager, Windows Credentials. RelayDock's Settings, Security page lists which ones exist, never their values, and lets you remove them.

When Windows cannot save a key, RelayDock keeps it in memory until OBS closes, tells you so, and marks the destination in the preflight check. It never falls back to a file.

## Why Windows Credential Manager

Two free options exist on Windows for keeping a secret between sessions.

| | Windows Credential Manager | DPAPI with a file |
| --- | --- | --- |
| Encryption | By Windows, with keys tied to your Windows account | The same |
| Where the secret lives | In the Windows credential vault | In a file RelayDock would have to write |
| Can you see and delete it without RelayDock | Yes, in Control Panel | Only by finding and deleting the file |
| Risk of the file ending up in a backup, a sync folder or a bug report | None. There is no file. | Real. OBS settings folders get copied and shared. |

RelayDock uses Credential Manager. Both protect against the same attackers, and Credential Manager keeps secrets out of the settings folder altogether. The code talks to an interface (`ICredentialStore`), so another store can be added without touching anything else.

## The settings file cannot hold a key

`config.json` has no field for a stream key or a password. The code that writes it works from a structure that has no such member, so a programming mistake cannot put one there. The integration tests stream with test keys and then search the file for them.

## Keys never appear on screen

A saved key shows as dots. The editor has no "show" button. To use the key elsewhere, choose Copy.

Copy puts the key on the Windows clipboard and:

- marks the entry so Windows leaves it out of clipboard history (Win+V) and does not sync it to your other devices,
- clears the clipboard after 30 seconds if the key is still on it.

Other programs can read the clipboard while the key is there. Nothing can prevent that.

## Logs and the diagnostics report

Every key and password RelayDock handles is registered with a redactor for the rest of the session. Every RelayDock log line passes through it before OBS sees it. On top of the exact values, patterns remove text that looks like a credential:

- a user name and password inside a URL,
- everything after the application name in an RTMP address,
- URL query strings,
- Twitch, YouTube and Facebook key formats,
- `key=`, `password=`, `token=` and similar pairs.

The diagnostics report (Settings, Diagnostics) is built from data that has no key in it, then passes through the same redactor, and then has Windows user names removed from file paths. It is saved only when you choose where, and sent nowhere.

OBS Studio writes each stream's server address to its own log. RelayDock cannot change that. Put secrets in the Stream key field, never in the Server URL.

## What RelayDock sends

RelayDock opens network connections in these cases, each started by you:

1. A stream. It goes from your PC straight to the platform's server, over RTMPS where the platform offers it. Nothing passes through a RelayDock server. There is none.
2. Test connection. One TCP connection to the destination's server and port. No data is sent.
3. The update check, once each time OBS starts unless you switch that off under Settings, Updates, and when you choose Check for updates. One HTTPS request to `api.github.com` for the newest release. It carries the RelayDock version number and nothing about you. A build with no project page configured has no update check at all.
4. Twitch chat, after you signed in under Settings, Chat. HTTPS requests to `id.twitch.tv` and `api.twitch.tv` and one WebSocket to `eventsub.wss.twitch.tv`. RelayDock holds a permission to read chat and nothing else. It has no client secret, because Twitch gives a program on a PC none.
5. YouTube chat, after you saved an API key of your own and connected a stream. HTTPS requests to `www.googleapis.com`, with the key in a header, never in the address.

Every one of these requests can be cancelled within about a tenth of a second, so closing OBS never waits for a server.

RelayDock has no analytics, no telemetry, no crash upload and no account.

## What RelayDock cannot protect against

- Malware running under your Windows account can read what you can read, including Windows Credential Manager.
- Someone using your unlocked Windows session can start OBS and stream with your keys.
- Plain RTMP (`rtmp://`) sends the key unencrypted. Anyone who can observe your network can read it. Use RTMPS.
- While a destination streams, OBS holds its key in memory.
- If you show the OBS log, the destination editor of another tool, or a platform's key page on stream, RelayDock cannot stop that.

If a key may have leaked, reset it on the platform. Every platform lets you issue a new one.

## How the release files are made

- GitHub Actions builds the release from the tagged source. The steps are in `.github/workflows`.
- The build downloads OBS Studio's sources and dependencies pinned by SHA-256 hash in `buildspec.json`.
- RelayDock contains no packer, no obfuscation, no self-updater and no code that downloads or runs other code.
- Every release file has its SHA-256 hash in `SHA256SUMS.txt`.
- The build is reproducible. Two builds of one commit in one folder give the same `relaydock.dll`, byte for byte. A build in another folder differs only in the time stamp fields and in the identifier of the debug file. `scripts/check-reproducible.ps1` checks both, and [building-from-source.md](building-from-source.md) shows how to compare your own build with a release.
- The files are not code-signed. Code-signing certificates that Windows trusts cost money. [installation.md](installation.md) explains the SmartScreen message and how to verify a download.
- The SignPath Foundation signs releases of open-source projects for free when a project meets its conditions, among them an OSI-approved licence and release builds made from a public repository. RelayDock can apply once its repository and first releases are public. Until a certificate is granted, releases stay unsigned and the checksums are the way to verify them.
- The debug file path stored in the DLL is the file name only, so a release carries no path from the PC that built it. `scripts/package.ps1` refuses to package a DLL that contains the builder's user name or the test scenario runner.

## Tests that guard this

`tests/security` runs on every build:

- redaction of registered keys and of key-shaped text in logs,
- the diagnostics report with a key planted in every text field it has,
- Windows Credential Manager round trips, and what happens when the store refuses a key.

The integration tests check, in a real OBS, that test keys appear neither in the OBS log nor in `config.json` after streaming with them, and that no settings page or editor shows a saved key. [testing.md](testing.md) has the list.

## Report a problem

See `SECURITY.md` in the repository root.
