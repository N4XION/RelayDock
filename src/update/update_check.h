// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "core/user_message.h"

#include <atomic>
#include <string>
#include <string_view>

namespace rd {

// The update check. It asks GitHub for the newest release of RelayDock and compares version
// numbers. That is all it does.
//
// It runs only when the user clicks "Check for updates", or at start-up when the user switched
// that on. It sends no data about the user or the PC: one HTTPS request to api.github.com with
// the RelayDock version in the User-Agent header, which GitHub requires. It never downloads or
// installs anything. The user opens the release page in a browser.
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
};

// Splits "https://github.com/owner/name" into its parts. Only github.com addresses are accepted.
bool parseGitHubRepository(const std::string &url, std::string &owner, std::string &name);

// Reads the answer of GET /repos/{owner}/{name}/releases/latest.
bool parseLatestRelease(const std::string &json, ReleaseInfo &out, std::string &error);

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
};

// Compares the running version with a release.
UpdateResult evaluateRelease(const std::string &currentVersion, const ReleaseInfo &release);

UserMessage describeUpdate(const UpdateResult &result, const std::string &currentVersion);

// ---- Network -----------------------------------------------------------------------------------

struct HttpResponse {
	bool ok = false;      // The request completed and a status code arrived
	int status = 0;       // HTTP status code
	std::string body;
	std::string error;
};

// One HTTPS GET with Windows' own HTTP stack and certificate checks. Blocks, so call it from a
// worker thread. Gives up when `cancel` becomes true, after `timeoutMs`, or once the body
// exceeds `maxBytes`.
HttpResponse httpsGet(const std::string &host, const std::string &path, const std::string &userAgent,
		      int timeoutMs, size_t maxBytes, const std::atomic<bool> &cancel);

// The whole check. `repositoryUrl` is the project page, `currentVersion` the running version.
UpdateResult checkForUpdate(const std::string &repositoryUrl, const std::string &currentVersion,
			    const std::atomic<bool> &cancel);

} // namespace rd
