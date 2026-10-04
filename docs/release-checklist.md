# Release checklist

A release is published when every gate on this page is met, or when the release notes say plainly which gate is open and why. Version 1.0.0 was published with open gates, on the decision of the project owner. Its release notes and the table at the end of this page list them.

The status of the current version is at the end of this page.

## 1. Automated checks

GitHub Actions runs these on every push. All must pass on the commit that gets tagged.

- [ ] No secrets in the repository (`scripts/scan-secrets.ps1`)
- [ ] The locale file matches the source (`scripts/update-locale.ps1 -Check`)
- [ ] The licence notice matches the vendored licences (`scripts/update-third-party-licenses.ps1 -Check`)
- [ ] Documentation links and wording (`scripts/check-docs.ps1`)
- [ ] The versions page matches the changelog (`scripts/update-versions.ps1 -Check`)
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

`tests/integration/Test-Installer.ps1 -DefaultFolder` covers these on a PC that never had RelayDock:

- [ ] The installer runs without asking for administrator rights
- [ ] It refuses to continue while OBS runs
- [ ] Installing over an existing version works
- [ ] Uninstall removes the plugin folder and keeps settings and keys
- [ ] Uninstall with "also remove settings and keys" removes `plugin_config\relaydock` and the `RelayDock:` credentials, and no other credential
- [ ] Without OBS, the installer says so

These need a person, on a PC that has OBS Studio installed:

- [ ] After the installer ran, RelayDock appears under Docks in OBS
- [ ] Installing a newer version over it keeps destinations and keys
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

The release workflow publishes by itself, so finish the gates above before you tag.

1. Set `version` and `versionSuffix` in `buildspec.json`.
2. Give the version its section in `CHANGELOG.md`, with the release date in the heading, and run `scripts/update-versions.ps1`.
3. Write `docs/release-notes/<version>.md` from `CHANGELOG.md`. Name every open gate in it.
4. Read the release page before it exists: `scripts/publish-release.ps1 -Tag v<version> -Repository <owner>/RelayDock -DryRun`.
5. Commit, push, and wait for the build of that commit to pass.
6. Tag the commit `v<version>` and push the tag.
7. GitHub Actions builds the files, checks that the build is reproducible, and publishes the release: the notes, the files, their hashes, the commit and the compiler version. A version with a suffix, such as `1.0.0-rc.1`, becomes a pre-release.
8. Download the files from the release and run `Test-ZipInstall.ps1` on them, and `Test-Installer.ps1` on a PC that has no RelayDock installed.
9. Point the download links at the top of `README.md` at the new version, and commit.

## Version numbers

RelayDock follows semantic versioning. See `CHANGELOG.md`.

A document version in `src/legal/legal_documents.cpp` changes whenever a legal text changes in a way users must review. RelayDock then asks every user to review it again.

RelayDock is built against the oldest supported OBS version. Raising `obs.minimumVersion` in `buildspec.json` is a MINOR change at least, and the release notes must say so.

## Status of 1.0.0-rc.2

Checked on 5 October 2026. "Open" means not done yet. Nothing on this list is assumed.

The unit tests, the integration suites and the release build checks ran on build `1.0.0-rc.2+19.1a1d81c20`. GitHub Actions built the release from the next commit, `d1f6e4f1f`, as build `1.0.0-rc.2+20.d1f6e4f1f`. That commit added result files, new screenshots, this table and one entry in the known limitations, and corrected two checks of the interface test that still expected the old vertical layout and the old document versions. It changed nothing that is built, apart from the build number and the commit id that every build carries.

The screenshots are from build `1.0.0-rc.2+19.1a1d81c20` too. The performance measurements and the 30 minute endurance run are from build `1.0.0-rc.1+9.54512d94d`. Since that build, these parts changed: the update check and its window, the standard vertical layout, one step of the settings migration, the project page setting, the installer script, and two legal texts. The code that connects, encodes and sends did not change.

The id `54512d94d` is from before the first push to GitHub. That push corrected the commit author and replaced a Windows user name in one test line, which gave every commit a new id. Build `9.54512d94d` has the files of commit `8efd158aa`, apart from that one line in `tests/security/test_diagnostics_leaks.cpp`.

| Gate | Status | Evidence, or what is missing |
| --- | --- | --- |
| 1. Automated checks | Passed, on the development PC and on GitHub Actions | The five scripts, the unit and security tests (310 test cases), the plugin build, the installer and the package. GitHub Actions passed on the pushed commit, and the release workflow passed for the tag `v1.0.0-rc.2`. The Actions tab of the repository shows every run. |
| 1. Two builds give the same DLL | Passed | `scripts/check-reproducible.ps1`: two builds in one folder matched byte for byte. Against the packaged DLL from another folder, all code and data matched, and 72 bytes of time stamp and debug file identifier differed. The release workflow ran the same check on GitHub and passed. |
| 2. Integration tests, OBS 32.0.4 | Passed | 478 checks, none failed, none skipped. [test-results/integration-obs-32.0.4.md](test-results/integration-obs-32.0.4.md) |
| 2. Integration tests, OBS 32.2.2 | Passed | 478 checks, none failed, none skipped. [test-results/integration-obs-32.2.2.md](test-results/integration-obs-32.2.2.md) |
| 2. Clipboard checks | Passed | Both ran on both OBS versions: the key reached the clipboard and was gone 30 seconds later. |
| 3. Performance | Measured | [performance-results.md](performance-results.md), on build `9.54512d94d`. There is no earlier release to compare with. |
| 4. Endurance, 30 minutes | Passed | 28 of 28 checks, on build `9.54512d94d`. [test-results/endurance-30-min.md](test-results/endurance-30-min.md) |
| 4. Endurance, 2 hours | Open | Two runs were stopped before their end, because the installer refuses to run while any OBS is open: one after a few minutes, one after 118 of 120 minutes. The second one recorded 118 measurements, one per minute: three destinations live, 212,672 frames each, none dropped, no reconnect, one session each at the server, and OBS memory in RAM steady between 174 and 178 MB from minute 7 on. It did not finish, so it does not count. |
| 4. Endurance, 6 hours | Open | It follows a finished 2 hour run. |
| 5. TikTok | Partly | The project owner streamed a game to TikTok LIVE with 1.0.0-rc.1 on 4 October 2026, and the picture arrived in the TikTok app. Sound, the numbers TikTok reports, stopping and reconnecting were not checked. |
| 5. Twitch, YouTube, Facebook | Open | Nobody has streamed to them with RelayDock in the project's own testing. It needs a person with accounts. |
| 5. Custom RTMPS against a real server | Open | Local tests use plain RTMP. |
| 6. Installer | Passed for 1.0.0-rc.1. Not run for this version. | `tests/integration/Test-Installer.ps1 -DefaultFolder` passed 27 checks on the 1.0.0-rc.1 installer, also on the file downloaded from the release page. The test only runs on a PC without RelayDock, and the development PC has it installed now. The installer script has not changed since. |
| 6. A person's install | Done once, with one finding | The project owner installed 1.0.0-rc.1 with the wizard and streamed with it. On the development PC the files were in place after the wizard, and a few minutes later the entry under Installed apps was missing. It came back when the installer ran again. What removed it is not known, and it has not happened in any test run. |
| 6. ZIP install in a portable OBS | Passed | `tests/integration/Test-ZipInstall.ps1`, 18 checks, with the ZIP downloaded from the release page, on OBS 32.0.4 and 32.2.2. |
| 6. The release build loads and unloads | Passed | `tests/integration/Test-PluginLoad.ps1` with the release build, 13 checks, OBS 32.2.2. |
| 6. Windows SmartScreen | Open | Nobody has written down what SmartScreen showed when the installer was opened from a browser download. |
| 7. Keys in the OBS log and the diagnostics report | Passed, automated | Every integration suite searches the OBS log for its test keys. The security tests plant a key in every field of the report. A person has not read a report after a real stream yet. |
| 7. Copy Key and the clipboard history | Open | The automated checks cover the clipboard. Nobody has looked at Win+V after a copy yet. |
| 7. Private vulnerability reporting | Done | Switched on in the repository settings. |
| 7. The release DLL contains no path from the build PC | Passed | `scripts/package.ps1` checks it and refuses to package otherwise. |
| 8. Platform limits | Checked 2026-10-04 | [research/platform-requirements.md](research/platform-requirements.md) |
| 8. Screenshots | Made from the running plugin | `docs/screenshots`, by `tests/integration/Capture-Screenshots.ps1`, on build `19.1a1d81c20`, OBS 32.2.2. |
| 8. Legal documents | Not reviewed by a lawyer | Each document says so in its first paragraph. The Privacy Policy and the Third-Party Services Notice are at version 1.1 since this release. |
| 9. Publish | Published as a pre-release | On 4 October 2026 at 11:07 UTC, by the release workflow: github.com/N4XION/RelayDock/releases/tag/v1.0.0-rc.2. The four files on that page match `SHA256SUMS.txt`. The update check of this version answers "RelayDock 1.0.0-rc.2 is the newest release". |

Hardware that has not been tested on the development PC: NVIDIA and Intel graphics, and Windows 10. See [testing.md](testing.md).
