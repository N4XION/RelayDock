# Install RelayDock by hand

Use this when you run a portable OBS Studio, or when you want to copy the files yourself.

## From the ZIP

1. Close OBS Studio.
2. Download `RelayDock-<version>-windows-x64.zip` from the Releases page of this repository.
3. Check its hash against `SHA256SUMS.txt`. [installation.md](installation.md) shows how.
4. Open the ZIP. It contains one folder named `relaydock`:

```
relaydock
  LICENSE.txt
  THIRD_PARTY_LICENSES.txt
  bin
    64bit
      relaydock.dll
  data
    locale
      en-US.ini
```

5. Copy the `relaydock` folder to the OBS plugins folder.

For an installed OBS:

```
C:\ProgramData\obs-studio\plugins\relaydock
```

Create `C:\ProgramData\obs-studio\plugins` first if it does not exist. `ProgramData` is a hidden folder. Type the path into the File Explorer address bar.

6. Start OBS Studio and open Docks, RelayDock.

## Portable OBS

A portable OBS (started with `--portable`, or with a `portable_mode.txt` file in its folder) does not read `C:\ProgramData`. It only loads plugins from its own folder. Copy the files there instead:

```
<OBS folder>\obs-plugins\64bit\relaydock.dll
<OBS folder>\data\obs-plugins\relaydock\locale\en-US.ini
```

That is `relaydock\bin\64bit\relaydock.dll` from the ZIP into `obs-plugins\64bit`, and the contents of `relaydock\data` into a new folder `data\obs-plugins\relaydock`.

## Check that it loaded

Open the OBS log (Help, Log Files, View Current Log) and look for:

```
[RelayDock] Loading version 1.0.0 ...
[RelayDock] Loaded. Open the dock from the OBS Docks menu.
```

If those lines are missing, OBS did not find the plugin. Check the folder names. They must match the layout above exactly.

If the log says the plugin was built for a newer OBS, update OBS Studio. RelayDock needs 32.0.0 or newer.

## Remove it

Close OBS and delete the `relaydock` folder you copied. [installation.md](installation.md) lists where settings and saved keys live, if you want to remove those too.
