// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include <doctest/doctest.h>

#include "build_info.h"
#include "network/reachability.h"
#include "performance/optimizer_text.h"
#include "performance/system_sampler.h"
#include "settings/theme.h"
#include "update/update_check.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace rd;

namespace {

// A TCP listener on a free local port.
struct LocalListener {
	SOCKET handle = INVALID_SOCKET;
	int port = 0;

	LocalListener()
	{
		WSADATA data{};
		WSAStartup(MAKEWORD(2, 2), &data);
		handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = 0;
		bind(handle, reinterpret_cast<sockaddr *>(&address), sizeof(address));
		int length = sizeof(address);
		getsockname(handle, reinterpret_cast<sockaddr *>(&address), &length);
		port = ntohs(address.sin_port);
	}

	void listenNow() { listen(handle, 4); }

	void close()
	{
		if (handle != INVALID_SOCKET)
			closesocket(handle);
		handle = INVALID_SOCKET;
	}

	~LocalListener()
	{
		close();
		WSACleanup();
	}
};

bool contains(const std::string &text, const std::string &needle)
{
	return text.find(needle) != std::string::npos;
}

int countOf(const std::string &text, char c)
{
	return static_cast<int>(std::count(text.begin(), text.end(), c));
}

} // namespace

TEST_SUITE("network.reachability")
{
	TEST_CASE("a listening server is reachable")
	{
		LocalListener listener;
		listener.listenNow();
		const std::atomic<bool> cancel{false};
		const ReachResult result = checkReachable("127.0.0.1", listener.port, 3000, cancel);
		CHECK(result.status == ReachStatus::Reachable);
		CHECK(result.address == "127.0.0.1");
		CHECK(result.port == listener.port);
		CHECK(result.elapsedMs >= 0);
		CHECK(result.systemError == 0);
	}

	TEST_CASE("a closed port is refused")
	{
		int port = 0;
		{
			LocalListener listener; // Reserve a port, then free it without ever listening
			port = listener.port;
		}
		const std::atomic<bool> cancel{false};
		const ReachResult result = checkReachable("127.0.0.1", port, 6000, cancel);
		CHECK(result.status == ReachStatus::Refused);
	}

	TEST_CASE("an invalid address is rejected without touching the network")
	{
		const std::atomic<bool> cancel{false};
		CHECK(checkReachable("", 1935, 1000, cancel).status == ReachStatus::InvalidAddress);
		CHECK(checkReachable("example.com", 0, 1000, cancel).status == ReachStatus::InvalidAddress);
		CHECK(checkReachable("example.com", 70000, 1000, cancel).status == ReachStatus::InvalidAddress);
	}

	TEST_CASE("a cancelled test returns at once")
	{
		const std::atomic<bool> cancel{true};
		const auto started = std::chrono::steady_clock::now();
		CHECK(checkReachable("example.com", 1935, 10000, cancel).status == ReachStatus::Cancelled);
		CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(1));
	}

	TEST_CASE("cancelling stops a test that is waiting for an answer")
	{
		// A listener that never accepts still completes the handshake, so use an address that
		// does not answer at all: a reserved documentation network.
		std::atomic<bool> cancel{false};
		std::thread canceller([&] {
			std::this_thread::sleep_for(std::chrono::milliseconds(300));
			cancel = true;
		});
		const auto started = std::chrono::steady_clock::now();
		const ReachResult result = checkReachable("192.0.2.1", 1935, 20000, cancel);
		canceller.join();
		// Without a network the attempt fails at once, which is also a valid outcome.
		CHECK((result.status == ReachStatus::Cancelled || result.status == ReachStatus::Unreachable));
		CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(3));
	}

	TEST_CASE("a name that cannot exist does not resolve")
	{
		const std::atomic<bool> cancel{false};
		const ReachResult result = checkReachable("relaydock-test.invalid", 1935, 4000, cancel);
		CHECK((result.status == ReachStatus::DnsFailed || result.status == ReachStatus::TimedOut));
	}

	TEST_CASE("every outcome has a message that names the destination and says what to do")
	{
		for (ReachStatus status : {ReachStatus::Reachable, ReachStatus::InvalidAddress, ReachStatus::DnsFailed,
					   ReachStatus::Refused, ReachStatus::TimedOut, ReachStatus::Unreachable}) {
			ReachResult result;
			result.status = status;
			result.host = "live.example.net";
			result.port = 443;
			const UserMessage message = describeReachability("My server", result, true);
			CAPTURE(reachStatusName(status));
			CHECK(contains(message.what, "My server"));
			CHECK_FALSE(message.action.empty());
		}
		ReachResult ok;
		ok.status = ReachStatus::Reachable;
		// A reachable server says nothing about the key. The message must say so.
		CHECK(contains(describeReachability("Twitch", ok, false).action, "does not check your stream key"));
	}
}

TEST_SUITE("update.check")
{
	TEST_CASE("versions parse")
	{
		SemVer v;
		REQUIRE(parseSemVer("1.2.3", v));
		CHECK(v == SemVer{1, 2, 3, ""});
		REQUIRE(parseSemVer("v10.0.7", v));
		CHECK(v == SemVer{10, 0, 7, ""});
		REQUIRE(parseSemVer("1.0.0-rc.1", v));
		CHECK(v == SemVer{1, 0, 0, "rc.1"});
		REQUIRE(parseSemVer(" 1.0.0-rc.1+42.abc1234.dirty ", v));
		CHECK(v == SemVer{1, 0, 0, "rc.1"});

		for (const char *bad : {"", "1", "1.0", "1.0.0.0", "a.b.c", "1.0.0-", "1..0", "1.0.x", "1.0.0-rc..1", "1.0.0-rc_1",
					"-1.0.0", "1.0.0 beta"}) {
			CAPTURE(bad);
			CHECK_FALSE(parseSemVer(bad, v));
		}
	}

	TEST_CASE("the running version is a valid version")
	{
		SemVer v;
		CHECK(parseSemVer(buildVersionString(), v));
	}

	TEST_CASE("versions order by the semantic versioning rules")
	{
		auto less = [](const char *a, const char *b) {
			SemVer left;
			SemVer right;
			REQUIRE(parseSemVer(a, left));
			REQUIRE(parseSemVer(b, right));
			return compareSemVer(left, right) < 0 && compareSemVer(right, left) > 0;
		};
		// The ordering example from the specification.
		CHECK(less("1.0.0-alpha", "1.0.0-alpha.1"));
		CHECK(less("1.0.0-alpha.1", "1.0.0-alpha.beta"));
		CHECK(less("1.0.0-alpha.beta", "1.0.0-beta"));
		CHECK(less("1.0.0-beta", "1.0.0-beta.2"));
		CHECK(less("1.0.0-beta.2", "1.0.0-beta.11"));
		CHECK(less("1.0.0-beta.11", "1.0.0-rc.1"));
		CHECK(less("1.0.0-rc.1", "1.0.0"));

		CHECK(less("1.0.0", "1.0.1"));
		CHECK(less("1.0.9", "1.1.0"));
		CHECK(less("1.9.9", "2.0.0"));
		CHECK(less("1.0.0-rc.2", "1.0.0-rc.10"));

		SemVer a;
		SemVer b;
		parseSemVer("1.0.0+1", a);
		parseSemVer("1.0.0+2", b);
		CHECK(compareSemVer(a, b) == 0); // Build metadata does not order
	}

	TEST_CASE("only github.com project pages are accepted")
	{
		std::string owner;
		std::string name;
		REQUIRE(parseGitHubRepository("https://github.com/example-owner/relaydock", owner, name));
		CHECK(owner == "example-owner");
		CHECK(name == "relaydock");
		CHECK(parseGitHubRepository("https://github.com/o/r/", owner, name));
		CHECK(parseGitHubRepository("https://github.com/o/r.git", owner, name));
		CHECK(name == "r");

		for (const char *bad : {"", "https://github.com/", "https://github.com/owner", "https://github.com/a/b/c",
					"http://github.com/a/b", "https://gitlab.com/a/b", "https://github.com.evil.example/a/b",
					"https://github.com/a/b?x=1", "https://github.com/a//b", "https://github.com/a b/c"}) {
			CAPTURE(bad);
			CHECK_FALSE(parseGitHubRepository(bad, owner, name));
		}
	}

	TEST_CASE("a release answer is read, and odd answers fail cleanly")
	{
		ReleaseInfo release;
		std::string error;
		REQUIRE(parseLatestRelease(
			R"({"tag_name":"v1.0.1","name":"RelayDock 1.0.1","html_url":"https://github.com/o/r/releases/tag/v1.0.1","prerelease":false})",
			release, error));
		CHECK(release.tag == "v1.0.1");
		CHECK(release.name == "RelayDock 1.0.1");
		CHECK(release.url == "https://github.com/o/r/releases/tag/v1.0.1");
		CHECK_FALSE(release.prerelease);

		// A link that does not point at github.com is dropped.
		REQUIRE(parseLatestRelease(R"({"tag_name":"v1.0.1","html_url":"https://evil.example/x"})", release, error));
		CHECK(release.url.empty());

		CHECK_FALSE(parseLatestRelease(R"({"message":"Not Found"})", release, error));
		CHECK(contains(error, "Not Found"));
		CHECK_FALSE(parseLatestRelease("<html>", release, error));
		CHECK_FALSE(parseLatestRelease("[]", release, error));
		CHECK_FALSE(parseLatestRelease(R"({"tag_name":42})", release, error));
		CHECK_FALSE(parseLatestRelease("", release, error));
	}

	TEST_CASE("a list of releases is read, drafts are left out, and odd answers fail cleanly")
	{
		std::vector<ReleaseInfo> releases;
		std::string error;
		REQUIRE(parseReleaseList(
			R"([{"tag_name":"v1.0.0-rc.2","name":"Second candidate","html_url":"https://github.com/o/n/releases/tag/v1.0.0-rc.2","prerelease":true},
			    {"tag_name":"v1.0.0-rc.1","html_url":"https://evil.example/x","prerelease":true},
			    {"tag_name":"v9.9.9","draft":true},
			    {"name":"no tag"}, 7, "text"])",
			releases, error));
		REQUIRE(releases.size() == 2);
		CHECK(releases[0].tag == "v1.0.0-rc.2");
		CHECK(releases[0].name == "Second candidate");
		CHECK(releases[0].prerelease);
		CHECK(releases[0].url == "https://github.com/o/n/releases/tag/v1.0.0-rc.2");
		CHECK(releases[1].tag == "v1.0.0-rc.1");
		CHECK(releases[1].url.empty()); // Not a github.com link, so it is dropped

		// A project without releases answers with an empty list. That is not an error.
		REQUIRE(parseReleaseList("[]", releases, error));
		CHECK(releases.empty());

		CHECK_FALSE(parseReleaseList(R"({"message":"Not Found"})", releases, error));
		CHECK(error.find("Not Found") != std::string::npos);
		CHECK_FALSE(parseReleaseList("<html>", releases, error));
		CHECK_FALSE(parseReleaseList("", releases, error));
		CHECK_FALSE(parseReleaseList("42", releases, error));
	}

	TEST_CASE("the newest release is the one with the highest version")
	{
		const std::vector<ReleaseInfo> releases = {
			{"v1.0.0-rc.1", "", "", true}, {"v1.0.0-rc.3", "", "", true}, {"v1.0.0-rc.2", "", "", true},
			{"v0.9.0", "", "", false},     {"nightly", "", "", false},
		};
		ReleaseInfo newest;
		REQUIRE(newestRelease(releases, true, newest));
		CHECK(newest.tag == "v1.0.0-rc.3");

		// Without candidates, only finished releases count.
		REQUIRE(newestRelease(releases, false, newest));
		CHECK(newest.tag == "v0.9.0");

		// A finished release outranks its own candidates.
		std::vector<ReleaseInfo> withFinal = releases;
		withFinal.push_back({"v1.0.0", "", "", false});
		REQUIRE(newestRelease(withFinal, true, newest));
		CHECK(newest.tag == "v1.0.0");

		// A candidate by its tag counts as one, whatever the flag says.
		CHECK_FALSE(newestRelease({{"v2.0.0-rc.1", "", "", false}}, false, newest));
		CHECK_FALSE(newestRelease({}, true, newest));
		CHECK_FALSE(newestRelease({{"nightly", "", "", false}}, true, newest));
	}

	TEST_CASE("a release candidate hears about the next candidate and about the finished release")
	{
		const ReleaseInfo next{"v1.0.0-rc.2", "", "", true};
		CHECK(evaluateRelease("1.0.0-rc.1", next).status == UpdateStatus::UpdateAvailable);
		CHECK(evaluateRelease("1.0.0-rc.2", next).status == UpdateStatus::UpToDate);
		CHECK(evaluateRelease("1.0.0-rc.1", ReleaseInfo{"v1.0.0", "", "", false}).status == UpdateStatus::UpdateAvailable);
		// A finished release is newer than every candidate with its number.
		CHECK(evaluateRelease("1.0.0", next).status == UpdateStatus::UpToDate);
	}

	TEST_CASE("the installer of a release is found among its files, and only on github.com")
	{
		ReleaseInfo release;
		std::string error;
		REQUIRE(parseLatestRelease(
			R"({"tag_name":"v1.0.1","html_url":"https://github.com/o/n/releases/tag/v1.0.1","assets":[
			     {"name":"SHA256SUMS.txt","browser_download_url":"https://github.com/o/n/releases/download/v1.0.1/SHA256SUMS.txt"},
			     {"name":"RelayDock-1.0.1-windows-x64.zip","browser_download_url":"https://github.com/o/n/releases/download/v1.0.1/RelayDock-1.0.1-windows-x64.zip"},
			     {"name":"RelayDock-1.0.1-windows-x64-Setup.exe","browser_download_url":"https://github.com/o/n/releases/download/v1.0.1/RelayDock-1.0.1-windows-x64-Setup.exe"}]})",
			release, error));
		CHECK(release.installerUrl == "https://github.com/o/n/releases/download/v1.0.1/RelayDock-1.0.1-windows-x64-Setup.exe");

		// An installer that is hosted anywhere else is not offered.
		REQUIRE(parseLatestRelease(
			R"({"tag_name":"v1.0.1","assets":[{"name":"X-Setup.exe","browser_download_url":"https://evil.example/X-Setup.exe"},
			     {"name":"Y-Setup.exe","browser_download_url":"https://github.com/o/n/blob/main/Y-Setup.exe"}]})",
			release, error));
		CHECK(release.installerUrl.empty());

		// No files, odd files, or no installer: no link, and no failure.
		REQUIRE(parseLatestRelease(R"({"tag_name":"v1.0.1"})", release, error));
		CHECK(release.installerUrl.empty());
		REQUIRE(parseLatestRelease(R"({"tag_name":"v1.0.1","assets":[7,{"name":5},{"name":"a.zip"}]})", release, error));
		CHECK(release.installerUrl.empty());

		// The list of releases carries the files too.
		std::vector<ReleaseInfo> releases;
		REQUIRE(parseReleaseList(
			R"([{"tag_name":"v1.0.0-rc.2","assets":[{"name":"RelayDock-1.0.0-rc.2-windows-x64-Setup.exe",
			      "browser_download_url":"https://github.com/o/n/releases/download/v1.0.0-rc.2/RelayDock-1.0.0-rc.2-windows-x64-Setup.exe"}]}])",
			releases, error));
		REQUIRE(releases.size() == 1);
		CHECK(releases[0].installerUrl.find("/releases/download/v1.0.0-rc.2/") != std::string::npos);
	}

	TEST_CASE("the check at start-up speaks up for a newer version only, and not for a skipped one")
	{
		UpdateResult result;
		result.status = UpdateStatus::UpdateAvailable;
		result.release.tag = "v1.0.1";
		CHECK(shouldAnnounceUpdate(result, ""));
		CHECK(shouldAnnounceUpdate(result, "1.0.0"));
		CHECK_FALSE(shouldAnnounceUpdate(result, "1.0.1"));
		CHECK_FALSE(shouldAnnounceUpdate(result, "v1.0.1"));
		// Skipping one version does not silence the next one.
		result.release.tag = "v1.0.2";
		CHECK(shouldAnnounceUpdate(result, "1.0.1"));
		// Text that is no version skips nothing.
		CHECK(shouldAnnounceUpdate(result, "never"));

		for (UpdateStatus quiet : {UpdateStatus::UpToDate, UpdateStatus::Failed, UpdateStatus::Cancelled, UpdateStatus::NotConfigured}) {
			result.status = quiet;
			CHECK_FALSE(shouldAnnounceUpdate(result, ""));
		}

		CHECK(versionOfTag("v1.0.1") == "1.0.1");
		CHECK(versionOfTag("1.0.0-rc.2") == "1.0.0-rc.2");
		CHECK(versionOfTag("nightly") == "nightly");
	}

	TEST_CASE("a release is compared with the running version")
	{
		ReleaseInfo release;
		release.tag = "v1.1.0";
		CHECK(evaluateRelease("1.0.0", release).status == UpdateStatus::UpdateAvailable);
		CHECK(evaluateRelease("1.1.0", release).status == UpdateStatus::UpToDate);
		CHECK(evaluateRelease("1.2.0", release).status == UpdateStatus::UpToDate);
		CHECK(evaluateRelease("1.1.0-rc.1+5.abc", release).status == UpdateStatus::UpdateAvailable);

		release.tag = "nightly";
		const UpdateResult bad = evaluateRelease("1.0.0", release);
		CHECK(bad.status == UpdateStatus::Failed);
		CHECK_FALSE(bad.error.empty());
		CHECK(evaluateRelease("not-a-version", ReleaseInfo{"v1.0.0", "", "", false}).status == UpdateStatus::Failed);
	}

	TEST_CASE("a build without a project page never contacts the network")
	{
		const std::atomic<bool> cancel{false};
		CHECK(checkForUpdate("", "1.0.0", cancel).status == UpdateStatus::NotConfigured);
		CHECK(checkForUpdate("https://example.com/a/b", "1.0.0", cancel).status == UpdateStatus::NotConfigured);
	}

	TEST_CASE("a cancelled check reports that it was cancelled")
	{
		const std::atomic<bool> cancel{true};
		CHECK(checkForUpdate("https://github.com/obsproject/obs-studio", "1.0.0", cancel).status == UpdateStatus::Cancelled);
	}

	TEST_CASE("every outcome has a message")
	{
		for (UpdateStatus status : {UpdateStatus::NotConfigured, UpdateStatus::UpToDate, UpdateStatus::UpdateAvailable,
					    UpdateStatus::Failed, UpdateStatus::Cancelled}) {
			UpdateResult result;
			result.status = status;
			result.release.tag = "v1.0.1";
			CHECK_FALSE(describeUpdate(result, "1.0.0").what.empty());
		}
	}

	// Talks to GitHub, so it is skipped in the normal run. Run it with:
	//   relaydock-tests --no-skip -tc="live: GitHub answers a release request"
	TEST_CASE("live: GitHub answers a release request" * doctest::skip())
	{
		const std::atomic<bool> cancel{false};
		const UpdateResult result = checkForUpdate("https://github.com/obsproject/obs-studio", "1.0.0", cancel);
		CAPTURE(result.error);
		CHECK(result.status == UpdateStatus::UpdateAvailable);
		CHECK_FALSE(result.release.tag.empty());
		CHECK(contains(result.release.url, "https://github.com/obsproject/obs-studio/releases/"));
	}

	// The same for a release candidate, which asks for the list of releases instead.
	//   relaydock-tests --no-skip -tc="live: a release candidate asks GitHub for the list"
	TEST_CASE("live: a release candidate asks GitHub for the list" * doctest::skip())
	{
		const std::atomic<bool> cancel{false};
		const UpdateResult result = checkForUpdate("https://github.com/obsproject/obs-studio", "1.0.0-rc.1", cancel);
		CAPTURE(result.error);
		CHECK(result.status == UpdateStatus::UpdateAvailable);
		CHECK(contains(result.release.url, "https://github.com/obsproject/obs-studio/releases/"));

		// A project page that does not exist is an error, not "up to date".
		const UpdateResult missing =
			checkForUpdate("https://github.com/obsproject/this-project-does-not-exist-relaydock-test", "1.0.0-rc.1", cancel);
		CHECK(missing.status == UpdateStatus::Failed);
	}
}

TEST_SUITE("performance.optimizer_text")
{
	TEST_CASE("names read naturally")
	{
		CHECK(joinNames({"Twitch"}) == "Twitch");
		CHECK(joinNames({"Twitch", "YouTube"}) == "Twitch and YouTube");
		CHECK(joinNames({"Twitch", "YouTube", "Facebook"}) == "Twitch, YouTube and Facebook");
		CHECK_FALSE(joinNames({}).empty());
	}

	TEST_CASE("a change says what, why and what applying it does")
	{
		OptimizerChange change;
		change.cause = OptimizerCause::NetworkDrops;
		change.kind = OptimizerChangeKind::Bitrate;
		change.to.bitratePercent = 85;
		ChangeText text = describeChange(change, {"Twitch", "YouTube"});
		CHECK(text.title == "Lower the bitrate of Twitch and YouTube to 85 percent");
		CHECK(contains(text.reason, "dropped frames"));
		CHECK(contains(text.effect, "Nothing reconnects"));

		change.cause = OptimizerCause::EncoderOverload;
		change.kind = OptimizerChangeKind::Fps;
		change.to = {};
		change.to.maxFps = 30;
		change.needsReconnect = true;
		text = describeChange(change, {"Twitch"});
		CHECK(text.title == "Limit Twitch to 30 FPS");
		CHECK(contains(text.effect, "reconnects Twitch"));

		change.cause = OptimizerCause::RenderingLag;
		change.kind = OptimizerChangeKind::Resolution;
		change.to = {};
		change.to.maxLines = 720;
		CHECK(describeChange(change, {"Twitch"}).title == "Limit Twitch to 720p");

		change.cause = OptimizerCause::Recovery;
		change.to = {};
		CHECK(describeChange(change, {"Twitch"}).title == "Restore the resolution of Twitch");
		change.kind = OptimizerChangeKind::Bitrate;
		change.to.bitratePercent = 100;
		CHECK(describeChange(change, {"Twitch"}).title == "Restore the full bitrate of Twitch");
		change.to.bitratePercent = 85;
		CHECK(describeChange(change, {"Twitch"}).title == "Raise the bitrate of Twitch to 85 percent");
	}

	TEST_CASE("every cause and kind produces text")
	{
		for (OptimizerCause cause : {OptimizerCause::NetworkDrops, OptimizerCause::EncoderOverload, OptimizerCause::RenderingLag,
					     OptimizerCause::HighCpu, OptimizerCause::Recovery}) {
			for (OptimizerChangeKind kind :
			     {OptimizerChangeKind::Bitrate, OptimizerChangeKind::Fps, OptimizerChangeKind::Resolution}) {
				OptimizerChange change;
				change.cause = cause;
				change.kind = kind;
				change.to.bitratePercent = 70;
				change.to.maxFps = 30;
				change.to.maxLines = 720;
				const ChangeText text = describeChange(change, {"A"});
				CHECK_FALSE(text.title.empty());
				CHECK_FALSE(text.reason.empty());
				CHECK_FALSE(text.effect.empty());
				CHECK_FALSE(describeAppliedChange(change, {"A"}).empty());
			}
			if (cause != OptimizerCause::Recovery) {
				OptimizerNotice notice;
				notice.cause = cause;
				CHECK_FALSE(describeNotice(notice, {"A"}).empty());
			}
		}
	}
}

TEST_SUITE("settings.theme")
{
	TEST_CASE("colours parse and print")
	{
		Rgb color;
		REQUIRE(parseHexColor("#1e90ff", color));
		CHECK(color == Rgb{0x1e, 0x90, 0xff});
		CHECK(toHex(color) == "#1e90ff");
		REQUIRE(parseHexColor("#FFFFFF", color));
		CHECK(color == Rgb{255, 255, 255});
		for (const char *bad : {"", "#fff", "1e90ff", "#1e90fg", "#1e90ff00", "red"})
			CHECK_FALSE(parseHexColor(bad, color));
	}

	TEST_CASE("contrast follows the WCAG formula")
	{
		CHECK(contrastRatio({0, 0, 0}, {255, 255, 255}) == doctest::Approx(21.0));
		CHECK(contrastRatio({255, 255, 255}, {255, 255, 255}) == doctest::Approx(1.0));
		CHECK(contrastRatio({0x77, 0x77, 0x77}, {255, 255, 255}) == doctest::Approx(4.48).epsilon(0.01));
		CHECK(readableOn({0, 0, 0}) == Rgb{255, 255, 255});
		CHECK(readableOn({255, 255, 0}) == Rgb{0, 0, 0});
		CHECK(mixColors({0, 0, 0}, {255, 255, 255}, 0.0) == Rgb{0, 0, 0});
		CHECK(mixColors({0, 0, 0}, {255, 255, 255}, 1.0) == Rgb{255, 255, 255});
		CHECK(mixColors({0, 0, 0}, {200, 100, 50}, 0.5) == Rgb{100, 50, 25});
	}

	TEST_CASE("text that is too faint is corrected")
	{
		const Rgb background{0x20, 0x20, 0x20};
		const Rgb faint{0x30, 0x30, 0x30};
		CHECK(contrastRatio(faint, background) < 4.5);
		CHECK(contrastRatio(ensureContrast(faint, background), background) >= 4.5);
		const Rgb fine{0xee, 0xee, 0xee};
		CHECK(ensureContrast(fine, background) == fine); // Left alone when it already passes
	}

	TEST_CASE("every mode and every user colour gives readable text")
	{
		const std::vector<BasePalette> bases = {
			{}, // Dark OBS theme
			{{0xf0, 0xf0, 0xf0}, {0x10, 0x10, 0x10}, {0x28, 0x4c, 0xb8}, 13}, // Light OBS theme
			{{0x80, 0x80, 0x80}, {0x80, 0x80, 0x80}, {0x80, 0x80, 0x80}, 13}, // A broken theme: text equals background
		};
		const std::vector<std::string> picks = {"",        "#000000", "#ffffff", "#ff0000", "#00ff00", "#0000ff", "#ffff00",
							"#808080", "#1e1f22", "#f4f4f2", "#ff00ff", "#123456", "#fedcba"};

		for (const BasePalette &base : bases) {
			for (ThemeMode mode : {ThemeMode::FollowObs, ThemeMode::Light, ThemeMode::Dark, ThemeMode::Custom}) {
				for (const std::string &accent : picks) {
					for (const std::string &background : picks) {
						ThemeConfig theme;
						theme.mode = mode;
						theme.accentColor = accent;
						theme.backgroundColor = background;
						const ThemeColors c = resolveThemeColors(theme, base);
						CAPTURE(themeModeName(mode));
						CAPTURE(accent);
						CAPTURE(background);
						CHECK(contrastRatio(c.text, c.background) >= 4.5);
						CHECK(contrastRatio(c.text, c.card) >= 4.5);
						CHECK(contrastRatio(c.mutedText, c.card) >= 4.5);
						CHECK(contrastRatio(c.accentText, c.accent) >= 4.5);
						CHECK(contrastRatio(c.ok, c.card) >= 4.5);
						CHECK(contrastRatio(c.warning, c.card) >= 4.5);
						CHECK(contrastRatio(c.error, c.card) >= 4.5);
						// A filled button must be visible on a card.
						CHECK(contrastRatio(c.accent, c.card) >= 1.5);
					}
				}
			}
		}
	}

	TEST_CASE("modes pick their base, and custom colours apply where they should")
	{
		const BasePalette base;
		ThemeConfig theme;

		ThemeColors colors = resolveThemeColors(theme, base);
		CHECK(colors.followsObs);
		CHECK(colors.background == base.window);
		CHECK(colors.accent == base.highlight);

		theme.mode = ThemeMode::Light;
		colors = resolveThemeColors(theme, base);
		CHECK_FALSE(colors.followsObs);
		CHECK_FALSE(colors.dark);

		theme.mode = ThemeMode::Dark;
		theme.backgroundColor = "#ffffff"; // Ignored outside Custom
		colors = resolveThemeColors(theme, base);
		CHECK(colors.dark);

		theme.mode = ThemeMode::Custom;
		colors = resolveThemeColors(theme, base);
		CHECK(colors.background == Rgb{255, 255, 255});
		CHECK_FALSE(colors.dark);

		theme.accentColor = "#d9480f";
		colors = resolveThemeColors(theme, base);
		CHECK(colors.accent == Rgb{0xd9, 0x48, 0x0f});
		theme.mode = ThemeMode::FollowObs;
		CHECK(resolveThemeColors(theme, base).accent == Rgb{0xd9, 0x48, 0x0f});
	}

	TEST_CASE("sizes follow font scale and density and stay in range")
	{
		const BasePalette base;
		ThemeConfig theme;
		ThemeMetrics metrics = resolveThemeMetrics(theme, base);
		CHECK(metrics.fontPx == 13);
		CHECK(metrics.padding == 10);

		theme.fontScale = 150;
		theme.density = Density::Compact;
		theme.cornerRadius = 99;
		theme.cardOpacity = 0;
		metrics = resolveThemeMetrics(theme, base);
		CHECK(metrics.fontPx == 20);
		CHECK(metrics.padding == 6);
		CHECK(metrics.radius == 16);
		CHECK(metrics.cardOpacity == 20);
		CHECK(metrics.smallFontPx < metrics.fontPx);
		CHECK(metrics.iconPx >= 12);
	}

	TEST_CASE("the style sheet is well formed")
	{
		const BasePalette base;
		for (ThemeMode mode : {ThemeMode::FollowObs, ThemeMode::Light, ThemeMode::Dark, ThemeMode::Custom}) {
			ThemeConfig theme;
			theme.mode = mode;
			const std::string css = buildStyleSheet(resolveThemeColors(theme, base), resolveThemeMetrics(theme, base));
			CAPTURE(themeModeName(mode));
			CHECK(countOf(css, '{') == countOf(css, '}'));
			CHECK(countOf(css, '{') > 20);
			CHECK(contains(css, "QFrame#rdCard"));
			CHECK(contains(css, "QPushButton#rdPrimary"));
			CHECK(contains(css, "rdPhase=\"live\""));
			CHECK_FALSE(contains(css, "nan"));
			CHECK_FALSE(contains(css, "{}"));
			// Follow OBS keeps OBS's own buttons and inputs. The other modes restyle them.
			CHECK(contains(css, "\nQPushButton {") == (mode != ThemeMode::FollowObs));
		}
	}

	TEST_CASE("animations are off in Potato Mode and with reduced motion")
	{
		ThemeConfig theme;
		CHECK(animationsAllowed(theme, PerformanceMode::Balanced));
		CHECK_FALSE(animationsAllowed(theme, PerformanceMode::Potato));
		theme.reducedMotion = true;
		CHECK_FALSE(animationsAllowed(theme, PerformanceMode::Quality));
		theme.reducedMotion = false;
		theme.animations = false;
		CHECK_FALSE(animationsAllowed(theme, PerformanceMode::Quality));
	}
}

TEST_SUITE("performance.system_sampler")
{
	TEST_CASE("processor load is measured on a background thread and stays in range")
	{
		SystemSampler sampler;
		CHECK(sampler.cpuPercent() == -1.0);
		sampler.start(250, true);
		CHECK(sampler.running());

		double cpu = -1.0;
		for (int i = 0; i < 40 && cpu < 0.0; ++i) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			cpu = sampler.cpuPercent();
		}
		CHECK(cpu >= 0.0);
		CHECK(cpu <= 100.0);

		// Graphics load exists on Windows versions that have the counters. Either way it is in range.
		const double gpu = sampler.gpuPercent();
		CHECK(((gpu == -1.0) || (gpu >= 0.0 && gpu <= 100.0)));

		// Switching graphics sampling off reports it as unknown.
		sampler.configure(250, false);
		std::this_thread::sleep_for(std::chrono::milliseconds(700));
		CHECK(sampler.gpuPercent() == -1.0);

		const auto stopStarted = std::chrono::steady_clock::now();
		sampler.stop();
		CHECK_FALSE(sampler.running());
		CHECK(std::chrono::steady_clock::now() - stopStarted < std::chrono::seconds(2));
		CHECK(sampler.cpuPercent() == -1.0);
	}

	TEST_CASE("stopping twice and destroying a running sampler are safe")
	{
		{
			SystemSampler sampler;
			sampler.stop();
			sampler.start(250, false);
			sampler.stop();
			sampler.stop();
		}
		{
			SystemSampler sampler;
			sampler.start(250, false);
		} // Destructor joins the thread
		CHECK(true);
	}
}
