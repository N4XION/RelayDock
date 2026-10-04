// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "core/user_message.h"
#include "network/http_client.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rd {

// The update check. It asks GitHub for the newest release of RelayDock and compares version
// numbers. That is all it does.
//
// It runs only when the user clicks "Check for updates", or at start-up when the user switched
// that on. It sends no data about the user or the PC: one HTTPS request to api.github.com with
// the RelayDock version in the User-Agent header, which GitHub requires. The check itself never
// downloads or installs anything. The user opens the release page in a browser, or chooses
// Update now, which is in update_download.h.
//
// A build with no repository configured has no update check at all.

// ---- Versions ----------------------------------------------------------------------------------

// A semantic version: 1.2.3 or 1.2.3-rc.1. Build metadata after "+" is ignored.
struct SemVer {
	int major = 0;
	int minor = 0;
	int patch = 0;
	std::string prerelease; // "rc.1". Empty for a release.

	bool operator==(const SemVer &other) const = default;
};

// Accepts an optional leading "v". Returns false for text that is not a version.
bool parseSemVer(std::string_view text, SemVer &out);

// Negative when a is older than b, 0 when equal, positive when newer. A pre-release is older
// than the release with the same numbers, as the semantic versioning rules say.
int compareSemVer(const SemVer &a, const SemVer &b);

// ---- Releases ----------------------------------------------------------------------------------

struct ReleaseInfo {
	std::string tag;  // "v1.0.1"
	std::string name; // Title of the release
	std::string url;  // Release page on github.com
	bool prerelease = false;
	// Direct link to the installer of this release on github.com. Empty when the release has
	// no file whose name ends in "-Setup.exe".
	std::string installerUrl;
	// What GitHub lists about that file, for Update now. The size is 0 and the checksum empty
	// when GitHub did not list them.
	std::string installerName;   // "RelayDock-1.0.1-windows-x64-Setup.exe"
	uint64_t installerSize = 0;  // Bytes
	std::string installerSha256; // 64 hex digits, lower case
	// Direct link to SHA256SUMS.txt of this release on github.com. Empty when it has none.
	std::string checksumsUrl;
};

// Splits "https://github.com/owner/name" into its parts. Only github.com addresses are accepted.
bool parseGitHubRepository(const std::string &url, std::string &owner, std::string &name);

// Reads the answer of GET /repos/{owner}/{name}/releases/latest.
bool parseLatestRelease(const std::string &json, ReleaseInfo &out, std::string &error);

// Reads the answer of GET /repos/{owner}/{name}/releases, which is a list and includes release
// candidates. Drafts and entries without a tag are left out. An empty list is a valid answer.
bool parseReleaseList(const std::string &json, std::vector<ReleaseInfo> &out, std::string &error);

// The release with the highest version number. Returns false when the list holds no release
// whose tag is a version, or only release candidates while `includePrereleases` is false.
bool newestRelease(const std::vector<ReleaseInfo> &releases, bool includePrereleases, ReleaseInfo &out);

enum class UpdateStatus {
	NotConfigured,   // This build has no repository
	UpToDate,
	UpdateAvailable,
	Failed,          // The request or its answer could not be used
	Cancelled,
};

struct UpdateResult {
	UpdateStatus status = UpdateStatus::NotConfigured;
	ReleaseInfo release;
	std::string error; // For Failed. Never contains anything secret.
	// For Failed: GitHub turned the request away because too many came from this internet
	// address. GitHub answers 60 an hour for one address, and a VPN shares its address.
	bool rateLimited = false;
};

// Compares the running version with a release.
UpdateResult evaluateRelease(const std::string &currentVersion, const ReleaseInfo &release);

UserMessage describeUpdate(const UpdateResult &result, const std::string &currentVersion);

// Whether the check at start-up should tell the user about this result: only a newer version,
// and not the one the user chose to skip. A failed check stays quiet, because nobody asked.
bool shouldAnnounceUpdate(const UpdateResult &result, const std::string &skippedVersion);

// "v1.0.1" and "1.0.1" both give "1.0.1". Text that is not a version comes back unchanged.
std::string versionOfTag(const std::string &tag);

// Whether GitHub's answer says that this internet address has asked too often.
bool isRateLimited(const HttpResponse &response);

// ---- Network -----------------------------------------------------------------------------------

// The whole check. It blocks, so call it from a worker thread. It ends at once when `cancel`
// becomes true. `repositoryUrl` is the project page, `currentVersion` the running version.
//
// A finished release is only told about finished releases. A release candidate is also told
// about newer release candidates, because whoever tests one wants the next one.
UpdateResult checkForUpdate(const std::string &repositoryUrl, const std::string &currentVersion,
			    const std::atomic<bool> &cancel);

} // namespace rd
