// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "update/update_check.h"

#include "utils/i18n.h"
#include "utils/strings.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <format>
#include <vector>

namespace rd {

namespace {

bool parseNumber(std::string_view text, int &out)
{
	if (text.empty() || text.size() > 9)
		return false;
	for (char c : text) {
		if (c < '0' || c > '9')
			return false;
	}
	const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
	return result.ec == std::errc();
}

bool isNumeric(std::string_view text)
{
	if (text.empty())
		return false;
	for (char c : text) {
		if (c < '0' || c > '9')
			return false;
	}
	return true;
}

} // namespace

// ---- Versions ----------------------------------------------------------------------------------

bool parseSemVer(std::string_view text, SemVer &out)
{
	const std::string trimmed = trim(text);
	std::string_view rest = trimmed;
	if (!rest.empty() && (rest.front() == 'v' || rest.front() == 'V'))
		rest.remove_prefix(1);

	// Build metadata does not take part in ordering.
	if (const size_t plus = rest.find('+'); plus != std::string_view::npos)
		rest = rest.substr(0, plus);

	std::string_view prerelease;
	if (const size_t dash = rest.find('-'); dash != std::string_view::npos) {
		prerelease = rest.substr(dash + 1);
		rest = rest.substr(0, dash);
		if (prerelease.empty())
			return false;
	}

	const std::vector<std::string> parts = split(rest, '.', true);
	if (parts.size() != 3)
		return false;

	SemVer version;
	if (!parseNumber(parts[0], version.major) || !parseNumber(parts[1], version.minor) ||
	    !parseNumber(parts[2], version.patch))
		return false;

	if (!prerelease.empty()) {
		for (const std::string &identifier : split(prerelease, '.', true)) {
			if (identifier.empty())
				return false;
			for (char c : identifier) {
				const bool valid = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
						   c == '-';
				if (!valid)
					return false;
			}
		}
	}
	version.prerelease = std::string(prerelease);
	out = version;
	return true;
}

int compareSemVer(const SemVer &a, const SemVer &b)
{
	if (a.major != b.major)
		return a.major < b.major ? -1 : 1;
	if (a.minor != b.minor)
		return a.minor < b.minor ? -1 : 1;
	if (a.patch != b.patch)
		return a.patch < b.patch ? -1 : 1;

	// A release is newer than any of its pre-releases.
	if (a.prerelease.empty() || b.prerelease.empty()) {
		if (a.prerelease.empty() && b.prerelease.empty())
			return 0;
		return a.prerelease.empty() ? 1 : -1;
	}

	const std::vector<std::string> left = split(a.prerelease, '.');
	const std::vector<std::string> right = split(b.prerelease, '.');
	for (size_t i = 0; i < left.size() && i < right.size(); ++i) {
		const bool leftNumeric = isNumeric(left[i]);
		const bool rightNumeric = isNumeric(right[i]);
		if (leftNumeric && rightNumeric) {
			// Compare as numbers without overflow: longer means larger once leading zeros are gone.
			const std::string l = left[i].substr(std::min(left[i].find_first_not_of('0'), left[i].size() - 1));
			const std::string r = right[i].substr(std::min(right[i].find_first_not_of('0'), right[i].size() - 1));
			if (l.size() != r.size())
				return l.size() < r.size() ? -1 : 1;
			if (l != r)
				return l < r ? -1 : 1;
		} else if (leftNumeric != rightNumeric) {
			return leftNumeric ? -1 : 1; // Numbers sort before words
		} else if (left[i] != right[i]) {
			return left[i] < right[i] ? -1 : 1;
		}
	}
	if (left.size() != right.size())
		return left.size() < right.size() ? -1 : 1;
	return 0;
}

// ---- Releases ----------------------------------------------------------------------------------

bool parseGitHubRepository(const std::string &url, std::string &owner, std::string &name)
{
	std::string rest = trim(url);
	const std::string prefix = "https://github.com/";
	if (rest.size() <= prefix.size() || !equalsNoCase(rest.substr(0, prefix.size()), prefix))
		return false;
	rest = rest.substr(prefix.size());
	while (!rest.empty() && rest.back() == '/')
		rest.pop_back();
	if (rest.size() > 4 && equalsNoCase(rest.substr(rest.size() - 4), ".git"))
		rest.resize(rest.size() - 4);

	const std::vector<std::string> parts = split(rest, '/', true);
	if (parts.size() != 2 || parts[0].empty() || parts[1].empty())
		return false;
	for (const std::string &part : parts) {
		for (char c : part) {
			const bool valid = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' ||
					   c == '_' || c == '.';
			if (!valid)
				return false;
		}
	}
	owner = parts[0];
	name = parts[1];
	return true;
}

namespace {
bool readRelease(const nlohmann::json &object, ReleaseInfo &release);
} // namespace

bool parseLatestRelease(const std::string &json, ReleaseInfo &out, std::string &error)
{
	const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
	if (root.is_discarded() || !root.is_object()) {
		error = "The answer from GitHub was not valid JSON.";
		return false;
	}
	const auto tag = root.find("tag_name");
	if (tag == root.end() || !tag->is_string() || tag->get<std::string>().empty()) {
		// GitHub answers errors with {"message": "..."}.
		const auto message = root.find("message");
		error = message != root.end() && message->is_string()
				? "GitHub answered: " + message->get<std::string>().substr(0, 200)
				: std::string("The answer from GitHub names no release.");
		return false;
	}

	ReleaseInfo release;
	if (!readRelease(root, release)) {
		error = "The answer from GitHub names no release.";
		return false;
	}
	out = std::move(release);
	return true;
}

namespace {

// Fills a ReleaseInfo from one release object of the GitHub API. Returns false when the object
// names no tag.
bool readRelease(const nlohmann::json &object, ReleaseInfo &release)
{
	const auto tag = object.find("tag_name");
	if (tag == object.end() || !tag->is_string() || tag->get<std::string>().empty())
		return false;
	release.tag = tag->get<std::string>();
	if (const auto name = object.find("name"); name != object.end() && name->is_string())
		release.name = name->get<std::string>();
	if (const auto url = object.find("html_url"); url != object.end() && url->is_string())
		release.url = url->get<std::string>();
	if (const auto pre = object.find("prerelease"); pre != object.end() && pre->is_boolean())
		release.prerelease = pre->get<bool>();

	// Only a link to github.com is ever shown or opened.
	if (release.url.rfind("https://github.com/", 0) != 0)
		release.url.clear();

	// The installer among the files of the release.
	release.installerUrl.clear();
	if (const auto assets = object.find("assets"); assets != object.end() && assets->is_array()) {
		for (const nlohmann::json &asset : *assets) {
			if (!asset.is_object())
				continue;
			const auto name = asset.find("name");
			const auto link = asset.find("browser_download_url");
			if (name == asset.end() || !name->is_string() || link == asset.end() || !link->is_string())
				continue;
			const std::string file = name->get<std::string>();
			const std::string address = link->get<std::string>();
			const std::string suffix = "-Setup.exe";
			const bool isInstaller = file.size() > suffix.size() &&
						 file.compare(file.size() - suffix.size(), suffix.size(), suffix) == 0;
			if (isInstaller && address.rfind("https://github.com/", 0) == 0 &&
			    address.find("/releases/download/") != std::string::npos) {
				release.installerUrl = address;
				break;
			}
		}
	}
	return true;
}

} // namespace

bool parseReleaseList(const std::string &json, std::vector<ReleaseInfo> &out, std::string &error)
{
	out.clear();
	const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
	if (root.is_discarded()) {
		error = "The answer from GitHub was not valid JSON.";
		return false;
	}
	if (!root.is_array()) {
		// GitHub answers errors with {"message": "..."}.
		const auto message = root.find("message");
		error = message != root.end() && message->is_string()
				? "GitHub answered: " + message->get<std::string>().substr(0, 200)
				: std::string("The answer from GitHub is not a list of releases.");
		return false;
	}

	for (const nlohmann::json &entry : root) {
		if (!entry.is_object())
			continue;
		if (const auto draft = entry.find("draft"); draft != entry.end() && draft->is_boolean() && draft->get<bool>())
			continue;
		ReleaseInfo release;
		if (readRelease(entry, release))
			out.push_back(std::move(release));
	}
	return true;
}

bool newestRelease(const std::vector<ReleaseInfo> &releases, bool includePrereleases, ReleaseInfo &out)
{
	bool found = false;
	SemVer best;
	for (const ReleaseInfo &release : releases) {
		SemVer version;
		if (!parseSemVer(release.tag, version))
			continue;
		// The tag decides, not the flag on GitHub: 1.0.0-rc.1 is a candidate whatever the flag says.
		if (!includePrereleases && (release.prerelease || !version.prerelease.empty()))
			continue;
		if (!found || compareSemVer(version, best) > 0) {
			best = version;
			out = release;
			found = true;
		}
	}
	return found;
}

UpdateResult evaluateRelease(const std::string &currentVersion, const ReleaseInfo &release)
{
	UpdateResult result;
	result.release = release;

	SemVer current;
	SemVer latest;
	if (!parseSemVer(currentVersion, current)) {
		result.status = UpdateStatus::Failed;
		result.error = std::format("The running version \"{}\" is not a version number.", currentVersion);
		return result;
	}
	if (!parseSemVer(release.tag, latest)) {
		result.status = UpdateStatus::Failed;
		result.error = std::format("The release tag \"{}\" is not a version number.", release.tag);
		return result;
	}
	result.status = compareSemVer(latest, current) > 0 ? UpdateStatus::UpdateAvailable : UpdateStatus::UpToDate;
	return result;
}

std::string versionOfTag(const std::string &tag)
{
	SemVer version;
	if (!parseSemVer(tag, version))
		return tag;
	return !tag.empty() && (tag.front() == 'v' || tag.front() == 'V') ? tag.substr(1) : tag;
}

bool shouldAnnounceUpdate(const UpdateResult &result, const std::string &skippedVersion)
{
	if (result.status != UpdateStatus::UpdateAvailable)
		return false;

	SemVer offered;
	SemVer skipped;
	if (parseSemVer(result.release.tag, offered) && parseSemVer(skippedVersion, skipped) &&
	    compareSemVer(offered, skipped) == 0)
		return false;
	return true;
}

UserMessage describeUpdate(const UpdateResult &result, const std::string &currentVersion)
{
	UserMessage message;
	switch (result.status) {
	case UpdateStatus::NotConfigured:
		message.what = loc("Update.NotConfigured", "This build has no project page, so it cannot check for updates.");
		break;
	case UpdateStatus::UpToDate:
		message.what = locf("Update.UpToDate", "RelayDock {0} is the newest release.", currentVersion);
		break;
	case UpdateStatus::UpdateAvailable:
		message.what = locf("Update.Available", "RelayDock {0} is available. You run {1}.", result.release.tag, currentVersion);
		message.action = loc("Update.Available.Action",
				     "Open the release page to read what changed and download it. RelayDock never installs updates by itself.");
		break;
	case UpdateStatus::Failed:
		if (result.rateLimited) {
			message.what = loc("Update.RateLimited", "GitHub takes no more update checks from your internet address this hour.");
			message.action = loc("Update.RateLimited.Action",
					     "That happens on an address many people share, such as a VPN. Try again within the hour.");
			break;
		}
		message.what = loc("Update.Failed", "The update check did not finish.");
		message.detail = result.error;
		message.action = loc("Update.Failed.Action", "Check your internet connection and try again later.");
		break;
	case UpdateStatus::Cancelled:
		message.what = loc("Update.Cancelled", "The update check was cancelled.");
		break;
	}
	return message;
}

// ---- Network -----------------------------------------------------------------------------------

bool isRateLimited(const HttpResponse &response)
{
	// GitHub answers 403 or 429 and says "API rate limit exceeded" in the message.
	return response.ok && (response.status == 403 || response.status == 429) && containsNoCase(response.body, "rate limit");
}

UpdateResult checkForUpdate(const std::string &repositoryUrl, const std::string &currentVersion,
			    const std::atomic<bool> &cancel)
{
	UpdateResult result;
	std::string owner;
	std::string name;
	if (!parseGitHubRepository(repositoryUrl, owner, name)) {
		result.status = UpdateStatus::NotConfigured;
		return result;
	}

	// GitHub rejects requests without a User-Agent. The version is the only thing it says.
	SemVer current;
	const std::string agent = parseSemVer(currentVersion, current)
					  ? std::format("RelayDock/{}.{}.{}", current.major, current.minor, current.patch)
					  : std::string("RelayDock");

	// "latest" on GitHub means the newest finished release. A release candidate asks for the
	// list instead, which includes candidates.
	const bool candidate = !current.prerelease.empty();
	const std::string path = candidate ? std::format("/repos/{}/{}/releases?per_page=10", owner, name)
					   : std::format("/repos/{}/{}/releases/latest", owner, name);
	// The list carries the notes and the files of every release in it, so it gets more room.
	const size_t maxBytes = candidate ? 2 * 1024 * 1024 : 512 * 1024;

	HttpRequest request;
	request.url = "https://api.github.com" + path;
	request.headers = {{"Accept", "application/vnd.github+json"}, {"X-GitHub-Api-Version", "2022-11-28"}};
	request.userAgent = agent;
	request.timeoutMs = 10000;
	request.maxBytes = maxBytes;

	const HttpResponse response = httpRequest(request, cancel);
	if (cancel.load() || response.cancelled()) {
		result.status = UpdateStatus::Cancelled;
		return result;
	}
	if (!response.ok) {
		result.status = UpdateStatus::Failed;
		result.error = response.error;
		return result;
	}
	if (response.status == 404 && !candidate) {
		// The project exists but has published no finished release yet.
		result.status = UpdateStatus::UpToDate;
		return result;
	}
	if (response.status != 200) {
		result.status = UpdateStatus::Failed;
		result.rateLimited = isRateLimited(response);
		result.error = std::format("GitHub answered with status {}.", response.status);
		return result;
	}

	ReleaseInfo release;
	std::string error;
	if (candidate) {
		std::vector<ReleaseInfo> releases;
		if (!parseReleaseList(response.body, releases, error)) {
			result.status = UpdateStatus::Failed;
			result.error = error;
			return result;
		}
		if (!newestRelease(releases, true, release)) {
			// Nothing is published yet.
			result.status = UpdateStatus::UpToDate;
			return result;
		}
		return evaluateRelease(currentVersion, release);
	}

	if (!parseLatestRelease(response.body, release, error)) {
		result.status = UpdateStatus::Failed;
		result.error = error;
		return result;
	}
	return evaluateRelease(currentVersion, release);
}

} // namespace rd
