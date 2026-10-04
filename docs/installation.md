# Install RelayDock

## What you need

- Windows 10 (version 2004 or newer) or Windows 11, 64-bit. All tests ran on Windows 11. Nobody has tested Windows 10 yet.
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

The installer refuses to copy files while OBS Studio runs, and it warns when it finds no OBS Studio or one that is too old.

To install without any question, for example from a script, run it with `/VERYSILENT`.

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

RelayDock looks for a newer version each time OBS starts. When there is one, a window says so and offers to download the installer in your browser.

1. Choose Download installer in that window, or download the installer from [versions.md](versions.md).
2. Close OBS Studio.
3. Open the downloaded file and follow its steps.

The installer replaces the old version. Your destinations, settings and saved stream keys stay.

In the window, Later asks again at the next start, and Skip this version stays quiet until a newer one exists. To look by hand, or to switch the check at start-up off, open Settings, Updates.

RelayDock never updates itself. It downloads nothing and installs nothing. A plugin cannot replace its own file while OBS runs, and a program that fetches and starts other programs is what security software looks for.

## Uninstall

There are two ways. Both remove the plugin files and keep your destinations, settings and saved stream keys, unless you say otherwise. A later install then picks them up.

### From RelayDock

1. In OBS, open the RelayDock settings and choose Updates.
2. Under Uninstall, tick "Also remove my destinations, settings and saved stream keys" if you want those gone too.
3. Choose Uninstall RelayDock and confirm.
4. Close OBS Studio.

OBS holds the plugin open while it runs, so the uninstaller starts at step 3 and waits. It removes RelayDock when OBS has closed, and then says so. Until you close OBS, Keep RelayDock on the same page takes the request back.

A RelayDock that you copied from the ZIP has no uninstaller. The Updates page then names the files to delete.

### From Windows

Close OBS. Open Windows Settings, Apps, Installed apps, find RelayDock and choose Uninstall. The uninstaller asks whether to remove your RelayDock settings and saved stream keys too.

### From a script

`unins000.exe /VERYSILENT` in the plugin folder uninstalls and keeps settings and keys. Add `/REMOVEDATA=1` to remove them too.

To remove them by hand:

- Settings: delete the folder `%APPDATA%\obs-studio\plugin_config\relaydock`.
- Stream keys: open Control Panel, Credential Manager, Windows Credentials, and remove the entries whose names start with `RelayDock:`. Inside RelayDock, Settings, Security, Remove all does the same.

Vertical layouts are stored in your OBS scene collection. They stay there as unused data until you save the collection without RelayDock installed.
