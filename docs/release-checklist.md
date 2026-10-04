# Release checklist

A release is published when every gate on this page is met, or when the release notes say plainly which gate is open and why. A release candidate may have open gates. A final release may not.

The status of the current version is at the end of this page.

## 1. Automated checks

GitHub Actions runs these on every push. All must pass on the commit that gets tagged.

- [ ] No secrets in the repository (`scripts/scan-secrets.ps1`)
- [ ] The locale file matches the source (`scripts/update-locale.ps1 -Check`)
- [ ] The licence notice matches the vendored licences (`scripts/update-third-party-licenses.ps1 -Check`)
- [ ] Documentation links and wording (`scripts/check-docs.ps1`)
- [ ] Unit tests
- [ ] Security tests
- [ ] The plugin builds with warnings treated as errors
- [ ] The ZIP, the installer and `SHA256SUMS.txt` are produced
- [ ] Two builds give the same DLL (`scripts/check-reproducible.ps1`, in the release workflow)

## 2. Integration tests

On a real PC, for every OBS Studio version in the test matrix.

- [ ] `tests/integration/Run-All.ps1` passes
- [ ] The results file is committed under `docs/test-results`
- [ ] The run was made from a normal desktop session, so the clipboard checks ran

## 3. Performance

- [ ] `tests/integration/Test-Performance.ps1` ran on the release build's code
- [ ] `docs/performance-results.md` holds its report
- [ ] Idle cost and the cost per destination did not get worse than in the previous release without a known reason

## 4. Endurance

`tests/integration/Test-Endurance.ps1`, each with every check passing:

- [ ] 30 minutes
- [ ] 2 hours
- [ ] 6 hours

## 5. Platforms

These need a person with an account. Enter the stream key in the RelayDock dock only. Never put it in a file, a test, a chat or an issue.

For each of Twitch, YouTube, Facebook and TikTok:

- [ ] Set the stream to private, unlisted, "Only me" or a test mode on the platform, where the platform has one
- [ ] The destination goes LIVE in RelayDock
- [ ] The platform's dashboard shows video and audio
- [ ] The size, frame rate and bitrate the platform reports match the card
- [ ] Ten minutes of streaming with no dropped connection
- [ ] Stop in RelayDock ends the stream on the platform, or the guide says what else is needed
- [ ] A wrong key gives the message the guide describes
- [ ] With the network cable pulled for 20 seconds, the destination reconnects

And together:

- [ ] Two platforms at once, with a shared encoder
- [ ] One horizontal and one vertical platform at once
- [ ] Twitch test stream (`Start test stream`) is accepted and stays off the channel
- [ ] A Custom RTMPS destination against a real RTMPS server

Record each run with the platform test report issue template, and update the table in [testing.md](testing.md).

## 6. Installer

On a PC or virtual machine that has OBS Studio installed and never had RelayDock.

- [ ] The installer starts without asking for administrator rights
- [ ] It refuses to continue while OBS runs
- [ ] RelayDock appears under Docks in OBS
- [ ] Installing a newer version over it keeps destinations and keys
- [ ] Uninstall removes the plugin folder
- [ ] Uninstall with "also remove settings and keys" removes `plugin_config\relaydock` and the `RelayDock:` credentials
- [ ] On a PC without OBS, the installer says so
- [ ] The ZIP install works in a portable OBS, following [manual-installation.md](manual-installation.md) (`tests/integration/Test-ZipInstall.ps1`)
- [ ] The release build loads and unloads cleanly (`tests/integration/Test-PluginLoad.ps1` with the release build)
- [ ] What Windows SmartScreen shows is noted in the release notes

## 7. Security

- [ ] Copy Key: the key is not in clipboard history (Win+V) and the clipboard is empty after 30 seconds
- [ ] A diagnostics report made after streaming was read by a person and contains no key
- [ ] The OBS log after streaming contains no key
- [ ] Private vulnerability reporting is switched on in the repository settings
- [ ] `SHA256SUMS.txt` matches the uploaded files
- [ ] The release DLL contains no path from the build PC

## 8. Documents

- [ ] Platform limits were checked against the platforms' pages again, and the dates updated
- [ ] Screenshots were made again with `tests/integration/Capture-Screenshots.ps1`
- [ ] `CHANGELOG.md` describes the release
- [ ] `docs/known-limitations.md` is current
- [ ] The legal documents were reviewed by a qualified lawyer, or still say that they were not

## 9. Publish

1. Set `version` and `versionSuffix` in `buildspec.json`.
2. Commit, then tag the commit `v<version>`.
3. Push the tag. GitHub Actions builds the files and creates a draft release.
4. Download the files from the draft and check their hashes against `SHA256SUMS.txt`.
5. Write the release notes from `CHANGELOG.md`. Name every open gate. Name the compiler version from the build log (the line "The CXX compiler identification is MSVC ..."), so others can reproduce the build.
6. Publish the draft.

## Version numbers

RelayDock follows semantic versioning. See `CHANGELOG.md`.

A document version in `src/legal/legal_documents.cpp` changes whenever a legal text changes in a way users must review. RelayDock then asks every user to review it again.

RelayDock is built against the oldest supported OBS version. Raising `obs.minimumVersion` in `buildspec.json` is a MINOR change at least, and the release notes must say so.
