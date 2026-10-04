// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "core/user_message.h"
#include "update/update_check.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rd {

// "Update now": RelayDock fetches the installer of a newer release, checks it and hands it to
// Windows to run. It happens only when the user chooses Update now. Nothing here runs by itself.
//
// RelayDock downloads two files of the release from GitHub: the installer and SHA256SUMS.txt,
// the list of checksums that every release carries. It keeps the installer only when
//   - both addresses belong to the releases of the configured project on github.com,
//   - every redirect on the way stays on GitHub's own servers,
//   - the file has the size GitHub listed,
//   - its SHA-256 is the one SHA256SUMS.txt names for it,
//   - and the one GitHub listed itself, when GitHub listed one.
//
// What that proves: the file is the one the release holds, complete and unchanged on its way.
// What it does not prove: who made the release. The release files carry no code signature, and
// someone who controls the project's releases on GitHub controls both files.

// ---- Checksums ---------------------------------------------------------------------------------

struct ChecksumEntry {
	std::string sha256; // 64 hex digits, lower case
	std::string file;
};

// Reads SHA256SUMS.txt, which has the format of sha256sum: 64 hex digits, a space, a space or
// an asterisk, and the file name. Lines that do not fit are left out.
std::vector<ChecksumEntry> parseChecksumList(std::string_view text);

// The checksum the list names for `file`. Empty when the list does not name the file, or names
// it more than once with different checksums.
std::string checksumFor(const std::vector<ChecksumEntry> &list, std::string_view file);

// SHA-256 of `data` as 64 hex digits, lower case. Empty when Windows could not compute it.
std::string sha256Hex(std::string_view data);

// ---- Addresses ---------------------------------------------------------------------------------

struct DownloadRules {
	std::string repositoryUrl; // The project page, "https://github.com/owner/name"
	// Tests only: the release may also be served by this PC itself (127.0.0.1, localhost).
	bool allowThisPc = false;
};

// Whether `url` is a file of a release of the configured project:
// https://github.com/{owner}/{name}/releases/download/...
bool acceptableDownloadUrl(std::string_view url, const DownloadRules &rules);

// Whether a redirect may be followed. GitHub hands a download on to its file servers, whose
// names end in githubusercontent.com. Any other target ends the download.
bool acceptableRedirect(std::string_view url, const DownloadRules &rules);

// Whether `name` is the installer of `version`: "RelayDock-{version}-windows-x64-Setup.exe".
// The version is the only part that changes, so a release cannot hand over another file.
bool isInstallerName(std::string_view name, std::string_view version);

// ---- Download ----------------------------------------------------------------------------------

struct UpdateDownloadConfig {
	DownloadRules rules;
	std::string userAgent = "RelayDock";
	// The folder the installer is saved in. RelayDock creates it. It must not exist yet, so
	// nothing that was there before can be mistaken for the download.
	std::string folder;
	int timeoutMs = 15000;
	size_t maxInstallerBytes = 64 * 1024 * 1024;
};

enum class DownloadStatus { Ready, Failed, Cancelled };

struct UpdateDownload {
	DownloadStatus status = DownloadStatus::Failed;
	std::string file;    // Ready: the full path of the checked installer
	// Ready: holds the file open, so that no other program can change or replace it between
	// the check and the start. Keep it until the installer runs, then let it go.
	std::shared_ptr<void> lock;
	UserMessage problem; // Failed: what went wrong and what to do
};

// Bytes received so far and bytes expected. Called on the thread that downloads.
using DownloadProgress = std::function<void(uint64_t received, uint64_t total)>;

enum class ChecksumFetch { Found, Failed, Cancelled };

// The first step of a download, on its own: fetches the checksum list of the release and gives
// the checksum it names for the installer. Failed with a `problem` when the release cannot be
// served, when the list does not name the installer, and when GitHub lists another checksum for
// the file. It blocks, and saves nothing.
ChecksumFetch fetchInstallerChecksum(const ReleaseInfo &release, const UpdateDownloadConfig &config,
				     const std::atomic<bool> &cancel, std::string &sha256, UserMessage &problem);

// Whether Update now can work for this release at all: it names an installer for its own
// version, a checksum list, and both on the project's release pages.
bool canDownloadUpdate(const ReleaseInfo &release, const DownloadRules &rules);

// The whole download. It blocks, so call it from a worker thread. It ends at once when
// `cancel` becomes true, and leaves no file behind unless the result is Ready.
UpdateDownload downloadUpdate(const ReleaseInfo &release, const UpdateDownloadConfig &config,
			      const std::atomic<bool> &cancel, const DownloadProgress &progress = {});

// ---- Tidying up ---------------------------------------------------------------------------------

// The request file RelayDock puts next to a downloaded installer. See app/update_install.h.
inline constexpr const char *kUpdateRequestName = "update.request";

// A folder name for one download inside `parent`: "RelayDock-update-" and a random id.
std::string newUpdateFolder(const std::string &parent);

// Lets go of the lock and deletes a downloaded installer and its folder.
void discardUpdateDownload(UpdateDownload &download);

// Removes what earlier updates left in `parent`: folders named "RelayDock-update-..." with an
// installer and a request file in them. A folder whose installer cannot be deleted stays as
// it is, request file included: that installer runs, and may be waiting for OBS to close.
// A folder with anything else in it stays too. Returns the number of folders removed.
int removeUpdateLeftovers(const std::string &parent);

// ---- Starting the installer --------------------------------------------------------------------

struct UpdatePlan {
	std::string program;   // The downloaded installer
	// Its command line. The request file is added when the update starts, see withRequestFile.
	std::string arguments;
};

// `installFolder` is the folder this RelayDock was installed into. The installer puts the new
// version into the same folder and waits until OBS has closed.
UpdatePlan planUpdate(const std::string &installerFile, const std::string &installFolder);

} // namespace rd
