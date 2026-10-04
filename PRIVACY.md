# Privacy Policy

Version 1.1. Last updated 4 October 2026.

This document is a draft written by the RelayDock contributors. No lawyer has reviewed it. Have it reviewed by a qualified lawyer before you rely on it for commercial use.

## 1. Summary

RelayDock runs on your computer. It collects no analytics and no telemetry. It has no account system. The RelayDock contributors operate no server that RelayDock talks to, and they receive no data from your use of RelayDock.

## 2. What RelayDock does not collect

RelayDock does not collect, and does not send to the RelayDock contributors:

- your stream content,
- your stream keys, passwords or tokens,
- your name, email address or any account identifier,
- your OBS settings, scenes or sources,
- usage statistics, crash reports or tracking identifiers.

RelayDock contains no advertising and no tracking software.

## 3. What RelayDock stores on your computer

RelayDock keeps the following on your computer only.

- Settings. A file named config.json in the OBS plugin settings folder (on Windows: `%APPDATA%\obs-studio\plugin_config\relaydock`). It holds your destinations (name, platform, server address, video and audio settings), your performance, network, appearance and layout choices, the upload speed you entered, and your legal acceptance records. A backup copy named config.json.bak sits next to it. The file holds no stream keys and no passwords.
- Vertical layouts. Saved inside your OBS scene collection file, because they refer to your OBS sources.
- Stream keys and passwords. Stored in Windows Credential Manager, protected by Windows for your user account. See the Security and Credentials Notice.
- Legal acceptance records. For each document: its name, its version, the date and time you agreed, and the RelayDock version. Nothing in the record identifies you.
- Log lines. RelayDock writes status lines to the OBS log file. It removes stream keys and passwords from every line before writing it.

## 4. Network connections RelayDock makes

RelayDock connects to the internet only in these cases.

- Streaming. When you start a destination, RelayDock connects from your computer to the server of that destination and sends your stream and your stream key to it. The connection goes straight from your computer to the platform or server you chose. It does not pass through any server run by the RelayDock contributors.
- Test connection. When you click Test connection, RelayDock opens a network connection to the server you configured to see whether it answers, then closes it. It sends no stream and no stream key.
- Update check. Each time OBS Studio starts, and when you click Check for updates, RelayDock asks GitHub for the newest RelayDock release. GitHub receives your IP address and the RelayDock version, as it does for any web request. RelayDock sends nothing else. When a newer version exists, RelayDock tells you and offers a link to it. It downloads nothing and installs nothing by itself. You can switch the check at start-up off under Settings, Updates. Builds without a configured project page have no update check.

RelayDock makes no other network connections.

## 5. Third parties

The platforms you stream to receive your stream, your stream key and your IP address, and they handle them under their own privacy policies. GitHub handles update checks under its own privacy policy. OBS Studio has its own privacy practices, which RelayDock does not change.

## 6. Diagnostic exports

RelayDock creates a diagnostic report only when you choose Save report or Copy report under Settings, Diagnostics. Save report writes it to a file you choose. Copy report puts it on the Windows clipboard. RelayDock sends it nowhere. The report describes your system (processor, graphics chip, memory, Windows and OBS versions), your RelayDock settings, the state of your destinations and recent RelayDock log lines. It contains no stream keys and no passwords. Server addresses are shortened, and your Windows user name is removed from file paths. Read the report before you share it, because you decide who receives it.

## 7. Children

RelayDock is a tool for people who stream. It is not directed at children. Each platform sets its own minimum age.

## 8. Deleting your data

Uninstalling RelayDock removes the plugin. To remove what RelayDock stored, delete the relaydock folder in the OBS plugin settings folder and remove the saved keys under Settings, Security in RelayDock, or in Windows Credential Manager under Windows Credentials (entries that start with "RelayDock:").

## 9. Future telemetry

RelayDock has no telemetry. If a future version adds any, it will be off by default and will need your explicit opt-in, and this policy will describe it before it ships.

## 10. Changes

When this policy changes, RelayDock shows you the new version and asks you to review it.
