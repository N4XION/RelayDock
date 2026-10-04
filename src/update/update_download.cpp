// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "update/update_download.h"

#include "network/http_client.h"
#include "utils/i18n.h"
#include "utils/strings.h"
#include "utils/uuid.h"

#include <windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <format>

namespace rd {

namespace {

constexpr size_t kMaxChecksumListBytes = 64 * 1024;
constexpr int kMaxRedirects = 5;
constexpr const char *kChecksumListName = "SHA256SUMS.txt";
constexpr const char *kUpdateFolderPrefix = "RelayDock-update-";

bool isHexDigit(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool isThisPc(std::string_view host)
{
	return host == "127.0.0.1" || host == "localhost";
}

bool endsWith(std::string_view text, std::string_view suffix)
{
	return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::wstring widen(const std::string &text)
{
	if (text.empty())
		return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring out(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
	return out;
}

// The path a file of this release has on the project's release pages.
std::string releaseFilePath(const DownloadRules &rules, const std::string &tag, std::string_view file)
{
	std::string owner;
	std::string name;
	if (!parseGitHubRepository(rules.repositoryUrl, owner, name))
		return {};
	return std::format("/{}/{}/releases/download/{}/{}", owner, name, tag, file);
}

// Whether `url` is exactly the address of `file` in the release `tag`.
bool isReleaseFile(const std::string &url, const DownloadRules &rules, const std::string &tag, std::string_view file)
{
	if (!acceptableDownloadUrl(url, rules))
		return false;
	HttpUrl parsed;
	if (!parseHttpUrl(url, parsed))
		return false;
	const std::string expected = releaseFilePath(rules, tag, file);
	return !expected.empty() && equalsNoCase(parsed.path, expected);
}

UserMessage problem(std::string what, std::string detail = {})
{
	UserMessage message;
	message.what = std::move(what);
	message.detail = std::move(detail);
	message.action = loc("UpdateNow.Error.Action",
			     "Nothing was installed. Try again later, or download the installer from the release page.");
	return message;
}

struct Fetched {
	bool ok = false;
	bool cancelled = false;
	std::string body;
	UserMessage problem;
};

// One file, following redirects by hand so that each target is checked before it is asked.
Fetched fetch(const std::string &firstUrl, size_t maxBytes, const UpdateDownloadConfig &config,
	      const std::atomic<bool> &cancel, const DownloadProgress &progress)
{
	Fetched fetched;
	std::string url = firstUrl;
	for (int hop = 0; hop <= kMaxRedirects; hop++) {
		const bool allowed = hop == 0 ? acceptableDownloadUrl(url, config.rules) : acceptableRedirect(url, config.rules);
		if (!allowed) {
			fetched.problem = problem(loc("UpdateNow.Error.Address",
						      "The download was sent to an address that does not belong to GitHub. RelayDock stopped it."));
			return fetched;
		}

		HttpRequest request;
		request.url = url;
		request.headers = {{"Accept", "application/octet-stream"}};
		request.userAgent = config.userAgent;
		request.timeoutMs = config.timeoutMs;
		request.maxBytes = maxBytes;
		request.followRedirects = false;
		request.progress = progress;

		HttpResponse response = httpRequest(request, cancel);
		if (cancel.load() || response.cancelled()) {
			fetched.cancelled = true;
			return fetched;
		}
		if (!response.ok) {
			fetched.problem = problem(loc("UpdateNow.Error.Network", "The download did not finish."), response.error);
			return fetched;
		}

		const bool redirect = response.status == 301 || response.status == 302 || response.status == 303 ||
				      response.status == 307 || response.status == 308;
		if (redirect) {
			if (response.location.empty()) {
				fetched.problem = problem(loc("UpdateNow.Error.Network", "The download did not finish."),
							  loc("UpdateNow.Error.NoTarget", "GitHub sent the download on without saying where to."));
				return fetched;
			}
			url = std::move(response.location);
			continue;
		}
		if (response.status != 200) {
			fetched.problem = problem(locf("UpdateNow.Error.Status", "GitHub answered the download with status {0}.", response.status));
			return fetched;
		}

		fetched.ok = true;
		fetched.body = std::move(response.body);
		return fetched;
	}

	fetched.problem = problem(loc("UpdateNow.Error.Network", "The download did not finish."),
				  loc("UpdateNow.Error.Redirects", "GitHub sent the download on too many times."));
	return fetched;
}

// Writes the installer into a folder that did not exist before, and reads it back through a
// handle that lets nobody else write to the file. That handle is `lock`.
bool saveInstaller(const std::string &folder, const std::string &name, const std::string &bytes, const std::string &sha256,
		   std::string &file, std::shared_ptr<void> &lock, DWORD &error)
{
	const std::wstring wideFolder = widen(folder);
	if (!CreateDirectoryW(wideFolder.c_str(), nullptr)) {
		error = GetLastError();
		return false;
	}

	file = folder + "\\" + name;
	const std::wstring wideFile = widen(file);
	const auto discard = [&] {
		DeleteFileW(wideFile.c_str());
		RemoveDirectoryW(wideFolder.c_str());
	};

	HANDLE handle = CreateFileW(wideFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle == INVALID_HANDLE_VALUE) {
		error = GetLastError();
		RemoveDirectoryW(wideFolder.c_str());
		return false;
	}
	size_t written = 0;
	while (written < bytes.size()) {
		const DWORD part = static_cast<DWORD>(std::min<size_t>(bytes.size() - written, 1024 * 1024));
		DWORD done = 0;
		if (!WriteFile(handle, bytes.data() + written, part, &done, nullptr) || done == 0) {
			error = GetLastError();
			CloseHandle(handle);
			discard();
			return false;
		}
		written += done;
	}
	FlushFileBuffers(handle);
	CloseHandle(handle);

	// What Windows will run is the file, not the bytes in memory. Check the file. Sharing only
	// reads means: while this handle is open, nobody can write to the file or delete it.
	handle = CreateFileW(wideFile.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle == INVALID_HANDLE_VALUE) {
		error = GetLastError();
		discard();
		return false;
	}
	std::string readBack(bytes.size(), '\0');
	size_t got = 0;
	bool readOk = true;
	while (got < readBack.size()) {
		const DWORD part = static_cast<DWORD>(std::min<size_t>(readBack.size() - got, 1024 * 1024));
		DWORD done = 0;
		if (!ReadFile(handle, readBack.data() + got, part, &done, nullptr) || done == 0) {
			readOk = false;
			break;
		}
		got += done;
	}
	LARGE_INTEGER size{};
	const bool sizeOk = GetFileSizeEx(handle, &size) && static_cast<unsigned long long>(size.QuadPart) == bytes.size();
	if (!readOk || !sizeOk || sha256Hex(readBack) != sha256) {
		CloseHandle(handle);
		error = ERROR_CRC;
		discard();
		return false;
	}
	lock = std::shared_ptr<void>(handle, [](void *held) { CloseHandle(static_cast<HANDLE>(held)); });
	return true;
}

} // namespace

// ---- Checksums ---------------------------------------------------------------------------------

std::vector<ChecksumEntry> parseChecksumList(std::string_view text)
{
	std::vector<ChecksumEntry> entries;
	size_t start = 0;
	while (start <= text.size()) {
		size_t end = text.find('\n', start);
		if (end == std::string_view::npos)
			end = text.size();
		std::string_view line = text.substr(start, end - start);
		start = end + 1;
		if (!line.empty() && line.back() == '\r')
			line.remove_suffix(1);

		// 64 digits, a space, then a space (text mode) or an asterisk (binary mode), then the name.
		if (line.size() < 67 || line[64] != ' ' || (line[65] != ' ' && line[65] != '*'))
			continue;
		const std::string_view digits = line.substr(0, 64);
		if (!std::all_of(digits.begin(), digits.end(), isHexDigit))
			continue;
		const std::string_view name = line.substr(66);
		if (name.empty() || name.find_first_of("/\\") != std::string_view::npos)
			continue;
		entries.push_back({toLower(digits), std::string(name)});
	}
	return entries;
}

std::string checksumFor(const std::vector<ChecksumEntry> &list, std::string_view file)
{
	std::string found;
	for (const ChecksumEntry &entry : list) {
		if (entry.file != file)
			continue;
		if (!found.empty() && found != entry.sha256)
			return {};
		found = entry.sha256;
	}
	return found;
}

std::string sha256Hex(std::string_view data)
{
	BCRYPT_ALG_HANDLE algorithm = nullptr;
	if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
		return {};
	unsigned char digest[32] = {};
	const NTSTATUS status = BCryptHash(algorithm, nullptr, 0,
					   reinterpret_cast<PUCHAR>(const_cast<char *>(data.data())),
					   static_cast<ULONG>(data.size()), digest, sizeof(digest));
	BCryptCloseAlgorithmProvider(algorithm, 0);
	if (status < 0)
		return {};

	static constexpr char digits[] = "0123456789abcdef";
	std::string hex;
	hex.reserve(64);
	for (const unsigned char byte : digest) {
		hex += digits[byte >> 4];
		hex += digits[byte & 0x0f];
	}
	return hex;
}

// ---- Addresses ---------------------------------------------------------------------------------

bool acceptableDownloadUrl(std::string_view url, const DownloadRules &rules)
{
	std::string owner;
	std::string name;
	if (!parseGitHubRepository(rules.repositoryUrl, owner, name))
		return false;
	HttpUrl parsed;
	if (!parseHttpUrl(url, parsed))
		return false;

	const std::string prefix = std::format("/{}/{}/releases/download/", owner, name);
	if (!startsWithNoCase(parsed.path, prefix) || parsed.path.size() == prefix.size())
		return false;
	// The rest names a tag and a file. Nothing in it may step out of the release.
	const std::string_view rest = std::string_view(parsed.path).substr(prefix.size());
	if (rest.find("..") != std::string_view::npos || rest.find_first_of("?%\\") != std::string_view::npos)
		return false;

	if (parsed.secure && parsed.port == 443 && parsed.host == "github.com")
		return true;
	return rules.allowThisPc && isThisPc(parsed.host);
}

bool acceptableRedirect(std::string_view url, const DownloadRules &rules)
{
	HttpUrl parsed;
	if (!parseHttpUrl(url, parsed))
		return false;
	if (parsed.secure && parsed.port == 443 &&
	    (parsed.host == "github.com" || endsWith(parsed.host, ".githubusercontent.com")))
		return true;
	return rules.allowThisPc && isThisPc(parsed.host);
}

bool isInstallerName(std::string_view name, std::string_view version)
{
	SemVer parsed;
	if (version.empty() || !parseSemVer(version, parsed))
		return false;
	for (const char c : version) {
		const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-';
		if (!ok)
			return false;
	}
	return name == std::format("RelayDock-{}-windows-x64-Setup.exe", version);
}

// ---- Download ----------------------------------------------------------------------------------

bool canDownloadUpdate(const ReleaseInfo &release, const DownloadRules &rules)
{
	if (release.tag.empty() || release.installerUrl.empty() || release.checksumsUrl.empty())
		return false;
	if (!isInstallerName(release.installerName, versionOfTag(release.tag)))
		return false;
	return isReleaseFile(release.installerUrl, rules, release.tag, release.installerName) &&
	       isReleaseFile(release.checksumsUrl, rules, release.tag, kChecksumListName);
}

ChecksumFetch fetchInstallerChecksum(const ReleaseInfo &release, const UpdateDownloadConfig &config,
				     const std::atomic<bool> &cancel, std::string &sha256, UserMessage &trouble)
{
	sha256.clear();
	if (!canDownloadUpdate(release, config.rules)) {
		trouble.what = loc("UpdateNow.Error.NoInstaller", "This release has no installer that RelayDock can check.");
		trouble.detail.clear();
		trouble.action = loc("UpdateNow.Error.NoInstaller.Action", "Open the release page and download the installer there.");
		return ChecksumFetch::Failed;
	}
	if (cancel.load())
		return ChecksumFetch::Cancelled;

	Fetched list = fetch(release.checksumsUrl, kMaxChecksumListBytes, config, cancel, {});
	if (list.cancelled)
		return ChecksumFetch::Cancelled;
	if (!list.ok) {
		trouble = std::move(list.problem);
		return ChecksumFetch::Failed;
	}
	const std::string expected = checksumFor(parseChecksumList(list.body), release.installerName);
	if (expected.empty()) {
		trouble = problem(loc("UpdateNow.Error.NoChecksum", "The checksum list of the release does not name the installer."));
		return ChecksumFetch::Failed;
	}
	if (!release.installerSha256.empty() && release.installerSha256 != expected) {
		trouble = problem(loc("UpdateNow.Error.Conflict",
				      "GitHub and the checksum list of the release name different checksums for the installer."));
		return ChecksumFetch::Failed;
	}
	sha256 = expected;
	return ChecksumFetch::Found;
}

UpdateDownload downloadUpdate(const ReleaseInfo &release, const UpdateDownloadConfig &config,
			      const std::atomic<bool> &cancel, const DownloadProgress &progress)
{
	UpdateDownload result;
	const auto failed = [&result](UserMessage message) -> UpdateDownload & {
		result.status = DownloadStatus::Failed;
		result.file.clear();
		result.problem = std::move(message);
		return result;
	};
	const auto cancelled = [&result]() -> UpdateDownload & {
		result.status = DownloadStatus::Cancelled;
		result.file.clear();
		return result;
	};

	if (!canDownloadUpdate(release, config.rules) || config.folder.empty()) {
		UserMessage message;
		message.what = loc("UpdateNow.Error.NoInstaller", "This release has no installer that RelayDock can check.");
		message.action = loc("UpdateNow.Error.NoInstaller.Action", "Open the release page and download the installer there.");
		return failed(std::move(message));
	}
	if (release.installerSize > config.maxInstallerBytes)
		return failed(problem(loc("UpdateNow.Error.TooLarge", "The installer of this release is larger than RelayDock accepts.")));
	if (cancel.load())
		return cancelled();

	// The checksum list first. It is small, and without it the installer is of no use.
	std::string expected;
	UserMessage trouble;
	switch (fetchInstallerChecksum(release, config, cancel, expected, trouble)) {
	case ChecksumFetch::Cancelled:
		return cancelled();
	case ChecksumFetch::Failed:
		return failed(std::move(trouble));
	case ChecksumFetch::Found:
		break;
	}

	Fetched installer = fetch(release.installerUrl, config.maxInstallerBytes, config, cancel, progress);
	if (installer.cancelled)
		return cancelled();
	if (!installer.ok)
		return failed(std::move(installer.problem));
	if (release.installerSize > 0 && installer.body.size() != release.installerSize)
		return failed(problem(locf("UpdateNow.Error.Size", "The download is incomplete: {0} of {1} bytes arrived.",
					   installer.body.size(), release.installerSize)));
	const std::string actual = sha256Hex(installer.body);
	if (actual.empty() || actual != expected)
		return failed(problem(loc("UpdateNow.Error.Mismatch",
					  "The downloaded installer does not match its checksum, so RelayDock did not keep it.")));
	if (cancel.load())
		return cancelled();

	std::string file;
	std::shared_ptr<void> lock;
	DWORD error = 0;
	if (!saveInstaller(config.folder, release.installerName, installer.body, expected, file, lock, error))
		return failed(problem(locf("UpdateNow.Error.Save", "Windows could not save the installer (error {0}).", error)));

	result.status = DownloadStatus::Ready;
	result.file = std::move(file);
	result.lock = std::move(lock);
	result.problem = {};
	return result;
}

// ---- Tidying up ---------------------------------------------------------------------------------

namespace {

std::string withoutTrailingSlashes(std::string path)
{
	while (!path.empty() && (path.back() == '\\' || path.back() == '/'))
		path.pop_back();
	return path;
}

bool startsWith(const std::wstring &text, const std::wstring &prefix)
{
	return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool endsWithWide(const std::wstring &text, const std::wstring &suffix)
{
	return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

std::string newUpdateFolder(const std::string &parent)
{
	return withoutTrailingSlashes(parent) + "\\" + kUpdateFolderPrefix + generateUuid();
}

void discardUpdateDownload(UpdateDownload &download)
{
	download.lock.reset();
	if (download.file.empty())
		return;
	DeleteFileW(widen(download.file).c_str());
	if (const size_t slash = download.file.find_last_of("\\/"); slash != std::string::npos)
		RemoveDirectoryW(widen(download.file.substr(0, slash)).c_str());
	download.file.clear();
}

int removeUpdateLeftovers(const std::string &parent)
{
	const std::string base = withoutTrailingSlashes(parent);
	if (base.empty())
		return 0;

	std::vector<std::wstring> folders;
	WIN32_FIND_DATAW found{};
	HANDLE search = FindFirstFileW(widen(base + "\\" + kUpdateFolderPrefix + "*").c_str(), &found);
	if (search == INVALID_HANDLE_VALUE)
		return 0;
	do {
		// A real folder only. A link to somewhere else is not followed.
		const bool folder = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		const bool link = (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
		if (folder && !link)
			folders.push_back(widen(base) + L"\\" + found.cFileName);
	} while (FindNextFileW(search, &found));
	FindClose(search);

	int removed = 0;
	for (const std::wstring &folder : folders) {
		std::vector<std::wstring> installers;
		std::vector<std::wstring> requests;
		bool foreign = false;
		search = FindFirstFileW((folder + L"\\*").c_str(), &found);
		if (search == INVALID_HANDLE_VALUE)
			continue;
		do {
			const std::wstring name = found.cFileName;
			if (name == L"." || name == L"..")
				continue;
			const bool plainFile = (found.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
			if (plainFile && startsWith(name, L"RelayDock-") && endsWithWide(name, L"-Setup.exe"))
				installers.push_back(folder + L"\\" + name);
			else if (plainFile && name == widen(kUpdateRequestName))
				requests.push_back(folder + L"\\" + name);
			else
				foreign = true;
		} while (FindNextFileW(search, &found));
		FindClose(search);
		if (foreign)
			continue;

		// The installer first. One that cannot be deleted is running, and its request stays.
		bool running = false;
		for (const std::wstring &installer : installers) {
			if (!DeleteFileW(installer.c_str()))
				running = true;
		}
		if (running)
			continue;
		for (const std::wstring &request : requests)
			DeleteFileW(request.c_str());
		if (RemoveDirectoryW(folder.c_str()))
			removed++;
	}
	return removed;
}

// ---- Starting the installer --------------------------------------------------------------------

UpdatePlan planUpdate(const std::string &installerFile, const std::string &installFolder)
{
	// A path that ends in a backslash would swallow the closing quote.
	std::string folder = installFolder;
	while (!folder.empty() && (folder.back() == '\\' || folder.back() == '/'))
		folder.pop_back();

	UpdatePlan plan;
	plan.program = installerFile;
	plan.arguments = "/SILENT /NORESTART /WAITFOROBS=1";
	if (!folder.empty())
		plan.arguments += " /DIR=\"" + folder + "\"";
	return plan;
}

} // namespace rd
