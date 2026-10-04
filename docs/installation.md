# Install RelayDock

## What you need

- Windows 11, 64-bit. Windows 10 64-bit works with the same OBS versions but is not part of the test matrix.
- OBS Studio 32.0.0 or newer. RelayDock is tested with 32.0.4 and 32.2.2.

RelayDock is free. It needs no account.

## Install with the installer

1. Close OBS Studio.
2. Download `RelayDock-<version>-windows-x64-Setup.exe` from the Releases page of this repository.
3. Check the file. See "Check your download" below.
4. Run it and follow the steps.
5. Start OBS Studio.
6. Open the Docks menu and choose RelayDock.

The installer puts RelayDock where OBS looks for plugins:

```
C:\ProgramData\obs-studio\plugins\relaydock
```

It asks for administrator rights only when Windows does not let your account write to that folder. It changes nothing else: no registry settings for OBS, no start-up entries, no services, no files in the OBS program folder.

## Install from the ZIP

Use the ZIP when you run a portable OBS or prefer to copy the files yourself. See [manual-installation.md](manual-installation.md).

## Windows SmartScreen

RelayDock's release files are not signed with a code-signing certificate. Certificates that Windows trusts cost money every year, and RelayDock has no budget. So Windows SmartScreen may show "Windows protected your PC" the first time you run the installer.

That message means Windows does not know the file yet. It does not mean Windows found anything wrong with it.

To go ahead: choose More info, then Run anyway. Do that only after you checked the file's hash.

## Check your download

Each release lists the SHA-256 hash of every file in `SHA256SUMS.txt`.

1. Download `SHA256SUMS.txt` from the same release.
2. Open PowerShell in your Downloads folder.
3. Run:

```powershell
Get-FileHash .\RelayDock-1.0.0-windows-x64-Setup.exe -Algorithm SHA256
```

4. Compare the hash with the line for that file in `SHA256SUMS.txt`. They must be identical.

If they differ, delete the file and download it again from the Releases page. Do not run it.

The release files are built by GitHub Actions from the tagged source code. You can read the build steps in `.github/workflows` and rebuild the same version yourself with [building-from-source.md](building-from-source.md).

## First start

The first time you open the dock, RelayDock asks you to review six short documents: Terms of Use, Privacy Policy, Security and Credentials Notice, Third-Party Services Notice, Streaming Disclaimer and Open Source Licenses. You scroll each one to its end and tick a box. Adding and starting destinations stays locked until you finish. RelayDock asks once, and again only when a document changes.

Continue with [getting-started.md](getting-started.md).

## Update

Install the new version over the old one. Close OBS first. Your destinations, settings and saved stream keys stay.

RelayDock never updates itself. Settings, Updates has a Check for updates button. It asks GitHub for the newest release and shows a link. Nothing is downloaded.

## Uninstall

Close OBS. Open Windows Settings, Apps, Installed apps, find RelayDock and choose Uninstall.

The uninstaller removes the plugin files. It asks whether to remove your RelayDock settings and saved stream keys too. Without that step they stay, so a later install picks them up.

To remove them by hand:

- Settings: delete the folder `%APPDATA%\obs-studio\plugin_config\relaydock`.
- Stream keys: open Control Panel, Credential Manager, Windows Credentials, and remove the entries whose names start with `RelayDock:`. Inside RelayDock, Settings, Security, Remove all does the same.

Vertical layouts are stored in your OBS scene collection. They stay there as unused data until you save the collection without RelayDock installed.
