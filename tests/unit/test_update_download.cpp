// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include <doctest/doctest.h>

#include "support/fake_server.h"
#include "update/update_check.h"
#include "update/update_download.h"
#include "utils/paths.h"
#include "utils/uuid.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

using namespace rd;
using rdtest::FakeRequest;
using rdtest::FakeResponse;
using rdtest::FakeServer;

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

const char *kRepository = "https://github.com/N4XION/RelayDock";
const char *kInstaller = "RelayDock-9.9.9-windows-x64-Setup.exe";
const char *kReleasePath = "/N4XION/RelayDock/releases/download/v9.9.9/";

bool contains(const std::string &text, const std::string &part)
{
	return text.find(part) != std::string::npos;
}

long long msSince(Clock::time_point start)
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

// Made-up bytes that stand for an installer. Not a program.
std::string installerBytes(size_t size = 300 * 1024)
{
	std::string bytes(size, '\0');
	unsigned value = 12345;
	for (char &c : bytes) {
		value = value * 1103515245u + 12345u;
		c = static_cast<char>(value >> 16);
	}
	return bytes;
}

std::string readFile(const std::string &file)
{
	std::ifstream in(pathFromUtf8(file), std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// A folder name under the temp folder that does not exist yet. Removed again at the end.
struct ScratchFolder {
	std::string path = pathToUtf8(fs::temp_directory_path() / ("rd-test-update-" + generateUuid()));
	~ScratchFolder()
	{
		std::error_code ignored;
		fs::remove_all(pathFromUtf8(path), ignored);
	}
	bool exists() const { return fs::exists(pathFromUtf8(path)); }
};

DownloadRules githubOnly()
{
	return {kRepository, false};
}

DownloadRules withThisPc()
{
	return {kRepository, true};
}

// A stand-in for the release pages of the project. It serves the checksum list and hands the
// installer on to another address, the way GitHub hands a download to its file servers.
struct FakeRelease {
	std::string installer = installerBytes();
	std::string listed;                  // What SHA256SUMS.txt says. Filled by the constructor.
	std::string served;                  // What the file server sends. Filled by the constructor.
	std::string redirectTo;              // Empty: to this server's /files/installer
	int installerStatus = 302;
	int delayMs = 0;
	bool hang = false;
	std::atomic<int> hops{0};
	FakeServer server;

	FakeRelease()
		: listed(sha256Hex(installer) + "  " + kInstaller + "\n" + std::string(64, 'a') + "  RelayDock-9.9.9-windows-x64.zip\n"),
		  served(installer),
		  server([this](const FakeRequest &request) { return answer(request); })
	{
	}

	FakeResponse answer(const FakeRequest &request)
	{
		FakeResponse response;
		response.contentType = "application/octet-stream";
		if (request.path == std::string(kReleasePath) + "SHA256SUMS.txt") {
			response.body = listed;
			return response;
		}
		if (request.path == std::string(kReleasePath) + kInstaller) {
			response.status = installerStatus;
			if (installerStatus == 302)
				response.headers = {{"Location", redirectTo.empty() ? server.url("/files/installer") : redirectTo}};
			return response;
		}
		if (request.path == "/files/installer") {
			response.body = served;
			response.delayMs = delayMs;
			response.hang = hang;
			return response;
		}
		if (request.path == "/loop") {
			response.status = 302;
			response.headers = {{"Location", server.url("/loop")}};
			hops++;
			return response;
		}
		response.status = 404;
		return response;
	}

	ReleaseInfo release() const
	{
		ReleaseInfo info;
		info.tag = "v9.9.9";
		info.url = "https://github.com/N4XION/RelayDock/releases/tag/v9.9.9";
		info.installerName = kInstaller;
		info.installerUrl = server.url(std::string(kReleasePath) + kInstaller);
		info.installerSize = installer.size();
		info.checksumsUrl = server.url(std::string(kReleasePath) + "SHA256SUMS.txt");
		return info;
	}
};

UpdateDownloadConfig configFor(const ScratchFolder &folder)
{
	UpdateDownloadConfig config;
	config.rules = withThisPc();
	config.folder = folder.path;
	config.timeoutMs = 5000;
	return config;
}

const std::atomic<bool> kNever{false};

} // namespace

TEST_SUITE("update.download")
{
	TEST_CASE("the checksum list of a release is read the way sha256sum writes it")
	{
		const std::string a(64, 'a');
		const std::string b = "0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef";
		const std::string text = a + "  RelayDock-1.0.0-windows-x64-Setup.exe\r\n" + b + " *RelayDock-1.0.0-windows-x64.zip\n" +
					 "not a line\n" + std::string(63, 'a') + "  short.exe\n" + std::string(64, 'g') + "  nothex.exe\n" + a +
					 "  folder/file.exe\n" + a + "  \n";
		const std::vector<ChecksumEntry> list = parseChecksumList(text);
		REQUIRE(list.size() == 2);
		CHECK(list[0].sha256 == a);
		CHECK(list[0].file == "RelayDock-1.0.0-windows-x64-Setup.exe");
		// Upper case digits are folded, so two spellings of one checksum compare equal.
		CHECK(list[1].sha256 == "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
		CHECK(list[1].file == "RelayDock-1.0.0-windows-x64.zip");

		CHECK(checksumFor(list, "RelayDock-1.0.0-windows-x64-Setup.exe") == a);
		CHECK(checksumFor(list, "something-else.exe").empty());
		CHECK(parseChecksumList("").empty());
	}

	TEST_CASE("a list that names one file with two checksums gives none")
	{
		const std::string a(64, 'a');
		const std::string b(64, 'b');
		CHECK(checksumFor(parseChecksumList(a + "  x.exe\n" + b + "  x.exe\n"), "x.exe").empty());
		CHECK(checksumFor(parseChecksumList(a + "  x.exe\n" + a + "  x.exe\n"), "x.exe") == a);
	}

	TEST_CASE("SHA-256 matches the published test values")
	{
		CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
		CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	}

	TEST_CASE("a download address must be a file of a release of the project on github.com")
	{
		const DownloadRules rules = githubOnly();
		CHECK(acceptableDownloadUrl("https://github.com/N4XION/RelayDock/releases/download/v1.2.3/RelayDock-1.2.3-windows-x64-Setup.exe", rules));
		// GitHub does not tell upper from lower case in the names of a project.
		CHECK(acceptableDownloadUrl("https://github.com/n4xion/relaydock/releases/download/v1.2.3/SHA256SUMS.txt", rules));

		// Another project, another server, a look-alike server.
		CHECK_FALSE(acceptableDownloadUrl("https://github.com/someone/RelayDock/releases/download/v1.2.3/Setup.exe", rules));
		CHECK_FALSE(acceptableDownloadUrl("https://example.com/N4XION/RelayDock/releases/download/v1.2.3/Setup.exe", rules));
		CHECK_FALSE(acceptableDownloadUrl("https://github.com.example.com/N4XION/RelayDock/releases/download/v1.2.3/Setup.exe", rules));
		CHECK_FALSE(acceptableDownloadUrl("https://github.com@example.com/N4XION/RelayDock/releases/download/v1.2.3/Setup.exe", rules));
		// Not encrypted, or another port.
		CHECK_FALSE(acceptableDownloadUrl("http://github.com/N4XION/RelayDock/releases/download/v1.2.3/Setup.exe", rules));
		CHECK_FALSE(acceptableDownloadUrl("https://github.com:8443/N4XION/RelayDock/releases/download/v1.2.3/Setup.exe", rules));
		// Not a release file, or a path that steps out of the releases.
		CHECK_FALSE(acceptableDownloadUrl("https://github.com/N4XION/RelayDock/archive/main.zip", rules));
		CHECK_FALSE(acceptableDownloadUrl("https://github.com/N4XION/RelayDock/releases/download/", rules));
		CHECK_FALSE(acceptableDownloadUrl("https://github.com/N4XION/RelayDock/releases/download/v1/../../../../evil/x.exe", rules));
		CHECK_FALSE(acceptableDownloadUrl("https://github.com/N4XION/RelayDock/releases/download/v1/%2e%2e/x.exe", rules));
		CHECK_FALSE(acceptableDownloadUrl("https://github.com/N4XION/RelayDock/releases/download/v1/x.exe?to=elsewhere", rules));
		// This PC only stands in for GitHub in a test.
		const std::string local = "http://127.0.0.1:8080/N4XION/RelayDock/releases/download/v1.2.3/Setup.exe";
		CHECK_FALSE(acceptableDownloadUrl(local, rules));
		CHECK(acceptableDownloadUrl(local, withThisPc()));
		// Without a project page there is nothing to download from.
		CHECK_FALSE(acceptableDownloadUrl("https://github.com/N4XION/RelayDock/releases/download/v1.2.3/Setup.exe", DownloadRules{}));
	}

	TEST_CASE("a download is only handed on to GitHub's own file servers")
	{
		const DownloadRules rules = githubOnly();
		CHECK(acceptableRedirect("https://objects.githubusercontent.com/github-production-release-asset/1/2?sig=abc%2Fdef", rules));
		CHECK(acceptableRedirect("https://release-assets.githubusercontent.com/github-production-release-asset/1/2?sp=r&sig=x", rules));
		CHECK(acceptableRedirect("https://github.com/N4XION/RelayDock/releases/download/v1.2.3/Setup.exe", rules));

		CHECK_FALSE(acceptableRedirect("https://example.com/Setup.exe", rules));
		CHECK_FALSE(acceptableRedirect("https://githubusercontent.com.example.com/Setup.exe", rules));
		CHECK_FALSE(acceptableRedirect("https://evilgithubusercontent.com/Setup.exe", rules));
		CHECK_FALSE(acceptableRedirect("http://objects.githubusercontent.com/Setup.exe", rules));
		CHECK_FALSE(acceptableRedirect("https://objects.githubusercontent.com:8443/Setup.exe", rules));
		CHECK_FALSE(acceptableRedirect("/a/path/without/a/server", rules));
		CHECK_FALSE(acceptableRedirect("http://127.0.0.1:8080/files/installer", rules));
		CHECK(acceptableRedirect("http://127.0.0.1:8080/files/installer", withThisPc()));
	}

	TEST_CASE("the installer of a version has one name, and no other file passes for it")
	{
		CHECK(isInstallerName("RelayDock-1.2.3-windows-x64-Setup.exe", "1.2.3"));
		CHECK(isInstallerName("RelayDock-1.0.0-rc.3-windows-x64-Setup.exe", "1.0.0-rc.3"));
		CHECK_FALSE(isInstallerName("RelayDock-1.2.4-windows-x64-Setup.exe", "1.2.3"));
		CHECK_FALSE(isInstallerName("Other-1.2.3-windows-x64-Setup.exe", "1.2.3"));
		CHECK_FALSE(isInstallerName("RelayDock-1.2.3-windows-x64-Setup.exe.bat", "1.2.3"));
		CHECK_FALSE(isInstallerName("RelayDock-1.2.3-windows-x64-Setup.exe", ""));
		CHECK_FALSE(isInstallerName("RelayDock-x-windows-x64-Setup.exe", "x"));
	}

	TEST_CASE("GitHub's list of release files gives the installer's name, size and checksum, and the checksum list")
	{
		const std::string digest(64, 'c');
		const std::string json = R"json([{"tag_name":"v1.2.3","html_url":"https://github.com/N4XION/RelayDock/releases/tag/v1.2.3","assets":[
			{"name":"RelayDock-1.2.3-windows-x64.zip","size":10,"browser_download_url":"https://github.com/N4XION/RelayDock/releases/download/v1.2.3/RelayDock-1.2.3-windows-x64.zip"},
			{"name":"SHA256SUMS.txt","size":200,"browser_download_url":"https://github.com/N4XION/RelayDock/releases/download/v1.2.3/SHA256SUMS.txt"},
			{"name":"RelayDock-1.2.3-windows-x64-Setup.exe","size":4567890,"digest":"sha256:)json" +
					 digest + R"json(","browser_download_url":"https://github.com/N4XION/RelayDock/releases/download/v1.2.3/RelayDock-1.2.3-windows-x64-Setup.exe"}
		]}])json";
		std::vector<ReleaseInfo> releases;
		std::string error;
		REQUIRE(parseReleaseList(json, releases, error));
		REQUIRE(releases.size() == 1);
		const ReleaseInfo &release = releases.front();
		CHECK(release.installerName == "RelayDock-1.2.3-windows-x64-Setup.exe");
		CHECK(release.installerSize == 4567890);
		CHECK(release.installerSha256 == digest);
		CHECK(release.checksumsUrl == "https://github.com/N4XION/RelayDock/releases/download/v1.2.3/SHA256SUMS.txt");
		CHECK(canDownloadUpdate(release, githubOnly()));
	}

	TEST_CASE("a checksum GitHub lists in another form is left out, and a checksum list elsewhere is not used")
	{
		const std::string json = R"json({"tag_name":"v1.2.3","assets":[
			{"name":"SHA256SUMS.txt","browser_download_url":"https://example.com/SHA256SUMS.txt"},
			{"name":"RelayDock-1.2.3-windows-x64-Setup.exe","size":-5,"digest":"md5:abcdef","browser_download_url":"https://github.com/N4XION/RelayDock/releases/download/v1.2.3/RelayDock-1.2.3-windows-x64-Setup.exe"}
		]})json";
		ReleaseInfo release;
		std::string error;
		REQUIRE(parseLatestRelease(json, release, error));
		CHECK(release.installerSha256.empty());
		CHECK(release.installerSize == 0);
		CHECK(release.checksumsUrl.empty());
		// Without a checksum list there is nothing to check the installer against.
		CHECK_FALSE(canDownloadUpdate(release, githubOnly()));
	}

	TEST_CASE("Update now needs the installer and the checksum list of that very release")
	{
		ReleaseInfo release;
		release.tag = "v1.2.3";
		release.installerName = "RelayDock-1.2.3-windows-x64-Setup.exe";
		release.installerUrl = "https://github.com/N4XION/RelayDock/releases/download/v1.2.3/RelayDock-1.2.3-windows-x64-Setup.exe";
		release.checksumsUrl = "https://github.com/N4XION/RelayDock/releases/download/v1.2.3/SHA256SUMS.txt";
		CHECK(canDownloadUpdate(release, githubOnly()));

		ReleaseInfo other = release;
		other.checksumsUrl = "https://github.com/N4XION/RelayDock/releases/download/v1.2.2/SHA256SUMS.txt";
		CHECK_FALSE(canDownloadUpdate(other, githubOnly()));

		other = release;
		other.installerUrl = "https://github.com/N4XION/RelayDock/releases/download/v1.2.3/Other-Setup.exe";
		CHECK_FALSE(canDownloadUpdate(other, githubOnly()));

		other = release;
		other.installerName = "RelayDock-1.2.4-windows-x64-Setup.exe";
		CHECK_FALSE(canDownloadUpdate(other, githubOnly()));

		other = release;
		other.checksumsUrl.clear();
		CHECK_FALSE(canDownloadUpdate(other, githubOnly()));

		// A build without a project page.
		CHECK_FALSE(canDownloadUpdate(release, DownloadRules{}));
	}

	TEST_CASE("the installer is started silently, waits for OBS and goes into the folder RelayDock is in")
	{
		const UpdatePlan plan = planUpdate(R"(C:\Temp\RelayDock-update-1\RelayDock-1.2.3-windows-x64-Setup.exe)",
						   R"(C:\ProgramData\obs-studio\plugins\relaydock\)");
		CHECK(plan.program == R"(C:\Temp\RelayDock-update-1\RelayDock-1.2.3-windows-x64-Setup.exe)");
		// No backslash before the closing quote, which Windows would read as part of the path.
		CHECK(plan.arguments == R"(/SILENT /NORESTART /WAITFOROBS=1 /DIR="C:\ProgramData\obs-studio\plugins\relaydock")");
		CHECK(planUpdate("x.exe", "").arguments == "/SILENT /NORESTART /WAITFOROBS=1");
	}
}

TEST_SUITE("update.download.network")
{
	TEST_CASE("a download that matches its checksum is saved, and nobody can change the file while it is held")
	{
		FakeRelease fake;
		ScratchFolder folder;
		uint64_t lastReceived = 0;
		uint64_t lastTotal = 0;
		int reports = 0;
		UpdateDownload result = downloadUpdate(fake.release(), configFor(folder), kNever, [&](uint64_t received, uint64_t total) {
			lastReceived = received;
			lastTotal = total;
			reports++;
		});

		REQUIRE(result.status == DownloadStatus::Ready);
		CHECK(result.problem.empty());
		CHECK(result.file == folder.path + "\\" + kInstaller);
		CHECK(readFile(result.file) == fake.installer);
		// Progress was reported, up to the whole file.
		CHECK(reports > 0);
		CHECK(lastReceived == fake.installer.size());
		CHECK(lastTotal == fake.installer.size());
		// The checksum list was asked first, and the installer came by way of the redirect.
		const std::vector<FakeRequest> requests = fake.server.requests();
		REQUIRE(requests.size() == 3);
		CHECK(requests[0].path == std::string(kReleasePath) + "SHA256SUMS.txt");
		CHECK(requests[1].path == std::string(kReleasePath) + kInstaller);
		CHECK(requests[2].path == "/files/installer");

		// While the result is held, another program can neither write to the file nor delete it.
		const std::wstring wide = pathFromUtf8(result.file).wstring();
		const HANDLE writer = CreateFileW(wide.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
						  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		CHECK(writer == INVALID_HANDLE_VALUE);
		if (writer != INVALID_HANDLE_VALUE)
			CloseHandle(writer);
		else
			CHECK(GetLastError() == ERROR_SHARING_VIOLATION);
		CHECK_FALSE(DeleteFileW(wide.c_str()));

		// Reading stays possible, which is what starting the program needs.
		CHECK(readFile(result.file) == fake.installer);

		result.lock.reset();
		CHECK(DeleteFileW(wide.c_str()));
	}

	TEST_CASE("an installer that does not match the checksum list is not kept")
	{
		FakeRelease fake;
		fake.served[1000] = static_cast<char>(fake.served[1000] ^ 0x01); // One bit of the file differs.
		ScratchFolder folder;
		const UpdateDownload result = downloadUpdate(fake.release(), configFor(folder), kNever);
		CHECK(result.status == DownloadStatus::Failed);
		CHECK(result.file.empty());
		CHECK(result.lock == nullptr);
		CHECK(contains(result.problem.what, "does not match its checksum"));
		CHECK(contains(result.problem.action, "Nothing was installed"));
		CHECK_FALSE(folder.exists());
	}

	TEST_CASE("an installer that is shorter than GitHub listed is not kept")
	{
		FakeRelease fake;
		ReleaseInfo release = fake.release();
		release.installerSize = fake.installer.size() + 1;
		ScratchFolder folder;
		const UpdateDownload result = downloadUpdate(release, configFor(folder), kNever);
		CHECK(result.status == DownloadStatus::Failed);
		CHECK(contains(result.problem.what, "incomplete"));
		CHECK_FALSE(folder.exists());
	}

	TEST_CASE("when GitHub and the checksum list disagree, the installer is not even downloaded")
	{
		FakeRelease fake;
		ReleaseInfo release = fake.release();
		release.installerSha256 = std::string(64, 'f');
		ScratchFolder folder;
		const UpdateDownload result = downloadUpdate(release, configFor(folder), kNever);
		CHECK(result.status == DownloadStatus::Failed);
		CHECK(contains(result.problem.what, "different checksums"));
		CHECK(fake.server.count(std::string(kReleasePath) + kInstaller) == 0);
		CHECK_FALSE(folder.exists());
	}

	TEST_CASE("when GitHub's checksum and the list agree, the download goes ahead")
	{
		FakeRelease fake;
		ReleaseInfo release = fake.release();
		release.installerSha256 = sha256Hex(fake.installer);
		ScratchFolder folder;
		const UpdateDownload result = downloadUpdate(release, configFor(folder), kNever);
		CHECK(result.status == DownloadStatus::Ready);
	}

	TEST_CASE("a checksum list that does not name the installer ends the update")
	{
		FakeRelease fake;
		fake.listed = std::string(64, 'a') + "  RelayDock-9.9.9-windows-x64.zip\n";
		ScratchFolder folder;
		const UpdateDownload result = downloadUpdate(fake.release(), configFor(folder), kNever);
		CHECK(result.status == DownloadStatus::Failed);
		CHECK(contains(result.problem.what, "does not name the installer"));
		CHECK(fake.server.count(std::string(kReleasePath) + kInstaller) == 0);
		CHECK_FALSE(folder.exists());
	}

	TEST_CASE("a download that is sent to a server outside GitHub stops there")
	{
		FakeRelease fake;
		fake.redirectTo = "https://example.com/RelayDock-9.9.9-windows-x64-Setup.exe";
		ScratchFolder folder;
		const auto start = Clock::now();
		const UpdateDownload result = downloadUpdate(fake.release(), configFor(folder), kNever);
		CHECK(result.status == DownloadStatus::Failed);
		CHECK(contains(result.problem.what, "does not belong to GitHub"));
		// It was refused, not tried.
		CHECK(msSince(start) < 3000);
		CHECK_FALSE(folder.exists());
	}

	TEST_CASE("a download that is sent in circles ends")
	{
		FakeRelease fake;
		fake.redirectTo = fake.server.url("/loop");
		ScratchFolder folder;
		const UpdateDownload result = downloadUpdate(fake.release(), configFor(folder), kNever);
		CHECK(result.status == DownloadStatus::Failed);
		CHECK(contains(result.problem.detail, "too many times"));
		CHECK(fake.hops.load() <= 6);
		CHECK_FALSE(folder.exists());
	}

	TEST_CASE("a release whose installer is missing on the server says so")
	{
		FakeRelease fake;
		fake.installerStatus = 404;
		ScratchFolder folder;
		const UpdateDownload result = downloadUpdate(fake.release(), configFor(folder), kNever);
		CHECK(result.status == DownloadStatus::Failed);
		CHECK(contains(result.problem.what, "status 404"));
		CHECK_FALSE(folder.exists());
	}

	TEST_CASE("cancelling ends a download at once and leaves nothing behind")
	{
		FakeRelease fake;
		fake.hang = true;
		ScratchFolder folder;
		std::atomic<bool> cancel{false};
		std::thread canceller([&cancel] {
			std::this_thread::sleep_for(std::chrono::milliseconds(300));
			cancel = true;
		});
		const auto start = Clock::now();
		const UpdateDownload result = downloadUpdate(fake.release(), configFor(folder), cancel);
		canceller.join();
		CHECK(result.status == DownloadStatus::Cancelled);
		CHECK(result.file.empty());
		CHECK(msSince(start) < 2000);
		CHECK_FALSE(folder.exists());
	}

	TEST_CASE("an installer larger than RelayDock accepts is refused before anything is asked")
	{
		FakeRelease fake;
		ScratchFolder folder;
		UpdateDownloadConfig config = configFor(folder);
		config.maxInstallerBytes = 1024;
		const UpdateDownload result = downloadUpdate(fake.release(), config, kNever);
		CHECK(result.status == DownloadStatus::Failed);
		CHECK(contains(result.problem.what, "larger than RelayDock accepts"));
		CHECK(fake.server.requests().empty());
	}

	TEST_CASE("an installer that grows past the limit while it arrives is cut off")
	{
		FakeRelease fake;
		ReleaseInfo release = fake.release();
		release.installerSize = 0; // GitHub listed no size, so only the limit protects.
		ScratchFolder folder;
		UpdateDownloadConfig config = configFor(folder);
		config.maxInstallerBytes = 64 * 1024;
		const UpdateDownload result = downloadUpdate(release, config, kNever);
		CHECK(result.status == DownloadStatus::Failed);
		CHECK(contains(result.problem.detail, "larger than expected"));
		CHECK_FALSE(folder.exists());
	}

	TEST_CASE("the folder for the download must be new, so an older file is never mistaken for it")
	{
		FakeRelease fake;
		ScratchFolder folder;
		fs::create_directories(pathFromUtf8(folder.path));
		const UpdateDownload result = downloadUpdate(fake.release(), configFor(folder), kNever);
		CHECK(result.status == DownloadStatus::Failed);
		CHECK(contains(result.problem.what, "could not save the installer"));
		// The folder that was there before is left alone.
		CHECK(folder.exists());
	}

	TEST_CASE("a release from outside the project's release pages is refused without a request")
	{
		FakeRelease fake;
		ReleaseInfo release = fake.release();
		ScratchFolder folder;
		UpdateDownloadConfig config = configFor(folder);
		config.rules = githubOnly(); // The real rules: this PC does not stand in for GitHub.
		const UpdateDownload result = downloadUpdate(release, config, kNever);
		CHECK(result.status == DownloadStatus::Failed);
		CHECK(contains(result.problem.what, "no installer that RelayDock can check"));
		CHECK(fake.server.requests().empty());
	}
}

TEST_SUITE("update.download.live")
{
	// Skipped unless asked for by name. It asks the real GitHub: one question about the newest
	// release of RelayDock, and one request for its checksum list. Nothing is saved, and no
	// installer is downloaded. It shows that the addresses GitHub hands out for a real release
	// pass the checks, the redirect to its file servers included.
	//   relaydock-tests --no-skip -tc="live: the checksum list of the newest RelayDock release arrives from GitHub"
	TEST_CASE("live: the checksum list of the newest RelayDock release arrives from GitHub" * doctest::skip())
	{
		const std::atomic<bool> cancel{false};
		// An old version number, so that whatever is published counts as newer.
		const UpdateResult found = checkForUpdate(kRepository, "0.0.1", cancel);
		CAPTURE(found.error);
		REQUIRE(found.status == UpdateStatus::UpdateAvailable);
		const ReleaseInfo &release = found.release;
		CAPTURE(release.tag);
		CAPTURE(release.installerUrl);
		CAPTURE(release.checksumsUrl);
		CHECK(canDownloadUpdate(release, githubOnly()));
		CHECK(release.installerSize > 0);

		UpdateDownloadConfig config;
		config.rules = githubOnly();
		config.userAgent = "RelayDock-tests";
		std::string sha256;
		UserMessage trouble;
		const ChecksumFetch result = fetchInstallerChecksum(release, config, cancel, sha256, trouble);
		CAPTURE(trouble.text());
		REQUIRE(result == ChecksumFetch::Found);
		CHECK(sha256.size() == 64);
		// GitHub computes a checksum of its own for every file. Both name the same file.
		if (!release.installerSha256.empty())
			CHECK(sha256 == release.installerSha256);
		MESSAGE("release " << release.tag << ", " << release.installerName << ", " << release.installerSize << " bytes, sha256 " << sha256
				   << std::string(release.installerSha256.empty() ? ", GitHub listed no checksum" : ", the same as GitHub lists"));
	}
}

TEST_SUITE("update.download.files")
{
	TEST_CASE("each download gets a folder of its own")
	{
		const std::string a = newUpdateFolder(R"(C:\Temp\)");
		const std::string b = newUpdateFolder(R"(C:\Temp)");
		CHECK(a.rfind(R"(C:\Temp\RelayDock-update-)", 0) == 0);
		CHECK(b.rfind(R"(C:\Temp\RelayDock-update-)", 0) == 0);
		CHECK(a != b);
	}

	TEST_CASE("a download that nobody wants is deleted with its folder")
	{
		FakeRelease fake;
		ScratchFolder folder;
		UpdateDownload result = downloadUpdate(fake.release(), configFor(folder), kNever);
		REQUIRE(result.status == DownloadStatus::Ready);
		REQUIRE(folder.exists());

		discardUpdateDownload(result);
		CHECK(result.file.empty());
		CHECK(result.lock == nullptr);
		CHECK_FALSE(folder.exists());
	}

	TEST_CASE("what an earlier update left behind is removed, and nothing else")
	{
		ScratchFolder temp;
		const fs::path parent = pathFromUtf8(temp.path);
		const auto write = [](const fs::path &file) {
			fs::create_directories(file.parent_path());
			std::ofstream(file, std::ios::binary) << "made by a test";
		};

		// An update that ended: its installer and its request file.
		write(parent / "RelayDock-update-ended" / "RelayDock-1.2.3-windows-x64-Setup.exe");
		write(parent / "RelayDock-update-ended" / "update.request");
		// A folder that is already empty.
		fs::create_directories(parent / "RelayDock-update-empty");
		// An installer that runs right now, and waits for OBS with its request file.
		write(parent / "RelayDock-update-running" / "RelayDock-1.2.4-windows-x64-Setup.exe");
		write(parent / "RelayDock-update-running" / "update.request");
		// Something RelayDock did not put there.
		write(parent / "RelayDock-update-foreign" / "RelayDock-1.2.3-windows-x64-Setup.exe");
		write(parent / "RelayDock-update-foreign" / "notes.txt");
		// A folder with another name, and a file whose name looks like a download folder.
		write(parent / "Something-else" / "RelayDock-1.2.3-windows-x64-Setup.exe");
		write(parent / "RelayDock-update-a-file");

		// A running program's file cannot be deleted. Holding it open without sharing deletion
		// stands in for that.
		const fs::path running = parent / "RelayDock-update-running" / "RelayDock-1.2.4-windows-x64-Setup.exe";
		const HANDLE held = CreateFileW(running.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
						FILE_ATTRIBUTE_NORMAL, nullptr);
		REQUIRE(held != INVALID_HANDLE_VALUE);

		CHECK(removeUpdateLeftovers(temp.path) == 2);
		CHECK_FALSE(fs::exists(parent / "RelayDock-update-ended"));
		CHECK_FALSE(fs::exists(parent / "RelayDock-update-empty"));
		// The waiting installer keeps its request, or it would give up.
		CHECK(fs::exists(running));
		CHECK(fs::exists(parent / "RelayDock-update-running" / "update.request"));
		CHECK(fs::exists(parent / "RelayDock-update-foreign" / "RelayDock-1.2.3-windows-x64-Setup.exe"));
		CHECK(fs::exists(parent / "RelayDock-update-foreign" / "notes.txt"));
		CHECK(fs::exists(parent / "Something-else" / "RelayDock-1.2.3-windows-x64-Setup.exe"));
		CHECK(fs::exists(parent / "RelayDock-update-a-file"));

		// Once that installer has ended, the next start removes its folder too.
		CloseHandle(held);
		CHECK(removeUpdateLeftovers(temp.path) == 1);
		CHECK_FALSE(fs::exists(parent / "RelayDock-update-running"));

		CHECK(removeUpdateLeftovers(temp.path) == 0);
		CHECK(removeUpdateLeftovers("") == 0);
		CHECK(removeUpdateLeftovers(temp.path + R"(\does-not-exist)") == 0);
	}
}
