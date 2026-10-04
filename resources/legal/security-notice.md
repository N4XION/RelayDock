# Security and Credentials Notice

Version 1.1. Last updated 5 October 2026.

This document is a draft written by the RelayDock contributors. No lawyer has reviewed it. Have it reviewed by a qualified lawyer before you rely on it for commercial use.

## 1. Stream keys are secrets

A stream key lets anyone stream to your channel. Treat it like a password. Never show it on stream, never paste it into chat, and never post it in a bug report.

## 2. How RelayDock stores your keys

RelayDock saves stream keys and RTMP passwords in Windows Credential Manager. Windows encrypts these entries with keys tied to your Windows account. RelayDock does not write stream keys or passwords to its settings file, to the OBS settings files or to any log.

You can see the entries in Windows under Control Panel, Credential Manager, Windows Credentials. Their names start with "RelayDock:". You can delete them there at any time.

If Windows cannot save a key, RelayDock keeps the key in memory until OBS closes and tells you so. It never falls back to saving the key in plain text.

When you set up chat, two more secrets are kept the same way: the Twitch sign-in, which lets RelayDock read your Twitch chat and nothing else, and your YouTube API key. RelayDock never sees your Twitch password. Treat the API key like a password as well: anyone who has it can use up the requests Google allows your key.

## 3. What this protection covers, and what it does not

Windows Credential Manager protects your keys from other Windows accounts on the computer and from someone who copies your OBS settings folder.

It does not protect your keys from:

- malicious software that runs under your own Windows account,
- another person who uses your unlocked Windows session,
- someone who has your Windows password.

No software on a general-purpose computer can prevent those. Keep Windows and OBS Studio updated, lock your computer when you leave it, and install software only from sources you trust.

## 4. Keys in memory and on the network

While a destination streams, OBS Studio holds its stream key in memory, because OBS sends it to the server.

Over plain RTMP (`rtmp://`), the stream key and the stream travel unencrypted. Anyone who can observe your network traffic can read the key. Over RTMPS (`rtmps://`), the connection is encrypted. Use RTMPS whenever the platform offers it. RelayDock picks an RTMPS server by default for platforms that publish one.

## 5. Logs, diagnostics and screenshots

RelayDock removes every stream key, password, chat sign-in and API key it has handled from its log lines and from the diagnostic export. It also removes text that looks like a credential in a server address.

OBS Studio writes the server address of each stream to its own log. RelayDock cannot change that. For this reason, put secrets in the Stream key field, never in the Server URL.

RelayDock shows a saved key as dots and never displays it. Copying a key is something only you can start.

## 6. Copying a key

Copy Key places the key on the Windows clipboard. RelayDock asks Windows not to keep it in clipboard history or sync it to other devices, and it clears the clipboard after 30 seconds if the key is still there. Other programs can read the clipboard while the key is on it.

## 7. What RelayDock cannot promise

The contributors work to keep RelayDock secure. They do not claim it is free of vulnerabilities. If a key may have been exposed, reset it on the platform straight away. Every platform lets you issue a new key.

## 8. Reporting a security problem

Report security problems privately. The file SECURITY.md in the project explains how. Do not open a public issue for a vulnerability, and do not include real stream keys in any report.
