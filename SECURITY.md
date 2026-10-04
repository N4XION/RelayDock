# Security

RelayDock handles stream keys. A leaked key lets someone else stream to your channel, so security problems get priority over everything else.

## Report a vulnerability

Report it privately. Do not open a public issue.

1. Open the Security tab of this repository.
2. Choose Report a vulnerability.
3. Describe what you found.

GitHub delivers the report to the maintainers only. If the Security tab shows no report button, open a public issue that says only "I have a security report" and a maintainer will open a private channel. Put no details in that issue.

Include:

- the RelayDock version (Settings, About),
- the OBS Studio and Windows versions,
- the steps that show the problem,
- what an attacker gains from it.

Never include a real stream key, password or token. Replace it with a made-up value of the same shape. If a real key was exposed while you investigated, reset it on the platform first.

The maintainers are volunteers. They aim to answer within seven days and to tell you what they plan to do. They credit reporters in the release notes unless you ask them not to.

## Supported versions

Security fixes go into the newest release. Update to it before you report a problem.

## What counts

In scope:

- a stream key or password that reaches a log, the settings file, the diagnostics report, a crash report or the screen,
- a way to make RelayDock send a key or a stream to a server the user did not choose,
- a crash or memory error that input from a streaming server or a settings file can trigger,
- a flaw in the installer that changes files outside the RelayDock plugin folder,
- a build or release step that lets someone replace the published files.

Out of scope:

- malware that already runs under your Windows account. It can read the same secrets you can. Windows Credential Manager does not protect against that, and the Security and Credentials Notice says so.
- problems in OBS Studio, Qt or a streaming platform. Report those to their owners.
- a platform rejecting or limiting a stream.

## How RelayDock protects keys

- Keys and RTMP passwords are saved in Windows Credential Manager, never in a file.
- The settings file has no field that can hold a secret.
- Every key RelayDock handles is registered with a redactor. Log lines and the diagnostics report pass through it. Patterns catch keys that were never registered.
- The interface shows a saved key as dots and has no way to reveal it. Copy Key excludes the clipboard entry from Windows clipboard history and cloud sync and clears it after 30 seconds.
- RelayDock contacts no server of its own. It has no telemetry and no analytics.
- Automated tests check the points above on every build. See `tests/security` and `docs/testing.md`.

`docs/security.md` describes the design in detail and lists what RelayDock cannot protect against.

## Check your download

Each release has a `SHA256SUMS.txt` file. Compare it with the file you downloaded:

```powershell
Get-FileHash .\RelayDock-1.0.0-windows-x64-Setup.exe -Algorithm SHA256
```

The hash must match the line for that file. Download RelayDock only from the Releases page of this repository.

Release files are built by GitHub Actions from the tagged source. The workflow is in `.github/workflows`. The files are not signed with a paid code-signing certificate, so Windows SmartScreen may warn about them. `docs/installation.md` explains what that warning means and how to verify the file.
