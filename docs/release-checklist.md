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
2. Write `docs/release-notes/<version>.md` from `CHANGELOG.md`. Name every open gate in it.
3. Read the release page before it exists: `scripts/publish-release.ps1 -Tag v<version> -Repository <owner>/RelayDock -DryRun`.
4. Commit, push, and wait for the build of that commit to pass.
5. Tag the commit `v<version>` and push the tag.
6. GitHub Actions builds the files, checks that the build is reproducible, and publishes the release: the notes, the files, their hashes, the commit and the compiler version. A version with a suffix, such as `1.0.0-rc.1`, becomes a pre-release.
7. Download the files from the release and run `Test-ZipInstall.ps1` and `Test-Installer.ps1` on them.

## Version numbers

RelayDock follows semantic versioning. See `CHANGELOG.md`.

A document version in `src/legal/legal_documents.cpp` changes whenever a legal text changes in a way users must review. RelayDock then asks every user to review it again.

RelayDock is built against the oldest supported OBS version. Raising `obs.minimumVersion` in `buildspec.json` is a MINOR change at least, and the release notes must say so.

## Status of 1.0.0-rc.1

Checked on 4 October 2026. "Open" means not done yet. Nothing on this list is assumed.

The integration, performance and endurance tests ran on build `1.0.0-rc.1+9.54512d94d`. The commits after it changed documents, test scripts and result files, and no file under `src`. The release build checks ran on build `1.0.0-rc.1+10.378f8dc6e`.

Those two ids are from before the first push to GitHub. That push corrected the commit author and replaced a Windows user name in one test line, which gave every commit a new id. Build `9.54512d94d` is commit `8efd158aa`, and build `10.378f8dc6e` is commit `4409f26ca`. Apart from that one line in `tests/security/test_diagnostics_leaks.cpp`, their files are the same.

| Gate | Status | Evidence, or what is missing |
| --- | --- | --- |
| 1. Automated checks | Passed on the development PC. Open in CI. | The four scripts pass, and so do 302 unit and security test cases. GitHub Actions has not run, because the repository is not on GitHub yet. |
| 1. Two builds give the same DLL | Passed on the development PC | `scripts/check-reproducible.ps1`: two builds in one folder matched byte for byte. Against the packaged DLL from another folder, all code and data matched, and 73 bytes of time stamp and debug file identifier differed. |
| 2. Integration tests, OBS 32.0.4 | Passed | 462 checks. [test-results/integration-obs-32.0.4.md](test-results/integration-obs-32.0.4.md) |
| 2. Integration tests, OBS 32.2.2 | Passed | 462 checks. [test-results/integration-obs-32.2.2.md](test-results/integration-obs-32.2.2.md) |
| 2. Clipboard checks | Passed | Both ran: the key reached the clipboard and was gone 30 seconds later. |
| 3. Performance | Measured | [performance-results.md](performance-results.md). There is no earlier release to compare with. |
| 4. Endurance, 30 minutes | Passed | 28 of 28 checks. [test-results/endurance-30-min.md](test-results/endurance-30-min.md) |
| 4. Endurance, 2 hours | Open | The run had not finished when this was written. |
| 4. Endurance, 6 hours | Open | The run had not finished when this was written. |
| 5. Platforms | Open | Nobody has streamed to Twitch, YouTube, Facebook or TikTok with this version. It needs a person with accounts. |
| 5. Custom RTMPS against a real server | Open | Local tests use plain RTMP. |
| 6. Installer | Built. Installing is open. | `scripts/package.ps1` built it with Inno Setup 6.7.3. Started while OBS was running, it refused, installed nothing and left no uninstall entry. Nobody has installed, upgraded or uninstalled with it yet. |
| 6. ZIP install in a portable OBS | Passed | `tests/integration/Test-ZipInstall.ps1`, 18 checks, OBS 32.2.2. |
| 6. The release build loads and unloads | Passed | `tests/integration/Test-PluginLoad.ps1` with the release build, 13 checks, OBS 32.2.2. |
| 7. Keys in the OBS log and the diagnostics report | Passed, automated | Every integration suite searches the OBS log for its test keys. The security tests plant a key in every field of the report. A person has not read a report after a real stream yet. |
| 7. Copy Key and the clipboard history | Open | The automated checks cover the clipboard. Nobody has looked at Win+V after a copy yet. |
| 7. Private vulnerability reporting | Open | GitHub offers the setting for public repositories. The repository is private. |
| 7. The release DLL contains no path from the build PC | Passed | `scripts/package.ps1` checks it and refuses to package otherwise. |
| 8. Platform limits | Checked 2026-10-04 | [research/platform-requirements.md](research/platform-requirements.md) |
| 8. Screenshots | Made from the running plugin | `docs/screenshots`, by `tests/integration/Capture-Screenshots.ps1`, on build `54512d94d`. |
| 8. Legal documents | Not reviewed by a lawyer | Each document says so in its first paragraph. |
| 9. Publish | Open | The source is at github.com/N4XION/RelayDock, in a private repository. No release is published. `repository` in `buildspec.json` is empty, so this build has no update check. |

Hardware that has not been tested: NVIDIA and Intel graphics, and Windows 10. See [testing.md](testing.md).
