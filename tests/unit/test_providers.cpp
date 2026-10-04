// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
//
// Every key in this file is made up for the test. None of them belongs to a real account.
#include <doctest/doctest.h>

#include "network/stream_url.h"
#include "providers/custom_rtmp/custom_rtmp_provider.h"
#include "providers/provider_registry.h"
#include "providers/twitch/twitch_provider.h"
#include "security/redactor.h"
#include "utils/uuid.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <string>

using namespace rd;

namespace {

const ValidationContext kWithKey{true, false};
const ValidationContext kNoKey{false, false};

struct Registry {
	ProviderRegistry registry;
	Registry() { registerBuiltInProviders(registry); }
	const IProvider &get(const char *id) const
	{
		const IProvider *provider = registry.find(id);
		REQUIRE(provider != nullptr);
		return *provider;
	}
};

DestinationConfig fresh(const IProvider &provider)
{
	DestinationConfig config = provider.newDestination();
	config.id = generateUuid();
	return config;
}

bool hasIssue(const std::vector<ValidationIssue> &issues, const char *fieldId, Severity severity)
{
	return std::any_of(issues.begin(), issues.end(), [&](const ValidationIssue &issue) {
		return issue.field == fieldId && issue.severity == severity;
	});
}

} // namespace

TEST_SUITE("providers.registry")
{
	TEST_CASE("the six built-in providers load in menu order")
	{
		Registry r;
		const auto all = r.registry.all();
		REQUIRE(all.size() == 6);
		CHECK(all[0]->info().id == "twitch");
		CHECK(all[1]->info().id == "tiktok");
		CHECK(all[2]->info().id == "youtube");
		CHECK(all[3]->info().id == "facebook");
		CHECK(all[4]->info().id == "custom_rtmp");
		CHECK(all[5]->info().id == "custom_rtmps");
	}

	TEST_CASE("every provider has the data the interface needs")
	{
		Registry r;
		std::set<std::string> ids;
		for (const IProvider *provider : r.registry.all()) {
			const ProviderInfo &info = provider->info();
			CAPTURE(info.id);
			CHECK(ids.insert(info.id).second);
			CHECK_FALSE(info.displayName.empty());
			CHECK_FALSE(info.monogram.empty());
			CHECK(info.monogram.size() <= 2);
			CHECK(info.accentColor.size() == 7);
			CHECK(info.accentColor[0] == '#');
			// A logo comes with both of its colours, or there is none.
			if (!info.logo.empty()) {
				CHECK(info.logoColor.size() == 7);
				CHECK(info.logoColor[0] == '#');
				CHECK(info.logoBackground.size() == 7);
				CHECK(info.logoBackground[0] == '#');
				CHECK(info.logoColor != info.logoBackground);
			}
			CHECK_FALSE(info.guide.empty());
			CHECK_FALSE((info.tlsRequired && info.tlsForbidden));
			if (!info.userSuppliesServer)
				CHECK_FALSE(provider->servers().empty());
			else
				CHECK(provider->servers().empty());
		}
	}

	TEST_CASE("the four platforms have a logo that RelayDock can draw, and a custom server has none")
	{
		const auto fileText = [](const std::string &relative) {
			std::ifstream file(std::string(RD_SOURCE_DIR) + "/" + relative, std::ios::binary);
			return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		};
		const std::string resources = fileText("resources/relaydock.qrc");
		REQUIRE_FALSE(resources.empty());

		Registry r;
		int logos = 0;
		for (const IProvider *provider : r.registry.all()) {
			const ProviderInfo &info = provider->info();
			CAPTURE(info.id);
			if (info.userSuppliesServer && info.id.rfind("custom", 0) == 0) {
				CHECK(info.logo.empty());
				continue;
			}
			REQUIRE_FALSE(info.logo.empty());
			++logos;
			const std::string svg = fileText("resources/brands/" + info.logo + ".svg");
			REQUIRE_FALSE(svg.empty());
			// The file is one drawing in one colour, which RelayDock sets when it draws.
			CHECK(svg.find("fill=\"currentColor\"") != std::string::npos);
			CHECK(svg.find("<path") != std::string::npos);
			// Nothing in it can load or run anything.
			for (const char *banned : {"<script", "href", "<image", "<foreignObject", "<style", "url("})
				CHECK(svg.find(banned) == std::string::npos);
			// It is part of the plugin's resources.
			CHECK(resources.find("<file>brands/" + info.logo + ".svg</file>") != std::string::npos);
		}
		CHECK(logos == 4);
		// The notice that says where the logos come from and whose they are.
		CHECK(fileText("resources/brands/NOTICE.txt").find("trademarks of their owners") != std::string::npos);
	}

	TEST_CASE("listed servers are valid addresses with unique ids")
	{
		Registry r;
		for (const IProvider *provider : r.registry.all()) {
			std::set<std::string> ids;
			for (const ServerOption &server : provider->servers()) {
				CAPTURE(provider->info().id);
				CAPTURE(server.id);
				CHECK(ids.insert(server.id).second);
				CHECK(server.id != kCustomServerId);
				const auto parsed = parseStreamUrl(server.url);
				CHECK(parsed.ok());
				if (provider->info().tlsRequired)
					CHECK(parsed.url.tls());
				CHECK_FALSE(parsed.url.hasUserInfo);
			}
		}
	}

	TEST_CASE("a duplicate or empty id is refused")
	{
		ProviderRegistry registry;
		CHECK(registry.add(std::make_unique<TwitchProvider>()));
		CHECK_FALSE(registry.add(std::make_unique<TwitchProvider>()));
		CHECK_FALSE(registry.add(nullptr));
		CHECK(registry.size() == 1);
		CHECK(registry.find("nope") == nullptr);
	}

	TEST_CASE("a third-party provider can be added without touching the built-in ones")
	{
		class ExampleProvider final : public ProviderBase {
		public:
			ExampleProvider()
			{
				info_.id = "example_platform";
				info_.displayName = "Example";
				info_.monogram = "Ex";
				info_.accentColor = "#336699";
				info_.guide = "docs/example.md";
				servers_.push_back({"main", "Main", "rtmps://ingest.example.com/live"});
				limits_.maxVideoBitrateKbps = 4000;
			}
		};

		Registry r;
		REQUIRE(r.registry.add(std::make_unique<ExampleProvider>()));
		CHECK(r.registry.size() == 7);

		const IProvider &example = r.get("example_platform");
		DestinationConfig config = fresh(example);
		CHECK(config.serverId == "main");
		CHECK(example.endpoint(config).serverUrl == "rtmps://ingest.example.com/live");
		CHECK(example.endpoint(config).tls);

		config.video.bitrateKbps = 5000;
		CHECK(hasIssue(example.validate(config, kWithKey), field::kVideoBitrate, Severity::Warning));
	}
}

TEST_SUITE("providers.defaults")
{
	TEST_CASE("a new destination of every provider is valid once it has a server and a key")
	{
		Registry r;
		for (const IProvider *provider : r.registry.all()) {
			CAPTURE(provider->info().id);
			DestinationConfig config = fresh(*provider);
			CHECK(config.provider == provider->info().id);
			CHECK(config.name == provider->info().displayName);
			CHECK(config.enabled);

			if (provider->info().userSuppliesServer) {
				config.serverUrl = provider->info().tlsForbidden ? "rtmp://ingest.example.org/live"
										 : "rtmps://ingest.example.org/live";
			}
			const auto issues = provider->validate(config, kWithKey);
			for (const ValidationIssue &issue : issues)
				CAPTURE(issue.message.text());
			CHECK_FALSE(hasErrors(issues));
		}
	}

	TEST_CASE("platform providers default to an encrypted server where the platform offers one")
	{
		Registry r;
		for (const char *id : {"twitch", "youtube", "facebook"}) {
			const IProvider &provider = r.get(id);
			CAPTURE(id);
			CHECK(provider.endpoint(fresh(provider)).tls);
		}
	}

	TEST_CASE("TikTok starts vertical, the others horizontal")
	{
		Registry r;
		CHECK(fresh(r.get("tiktok")).video.orientation == Orientation::Vertical);
		CHECK(fresh(r.get("twitch")).video.orientation == Orientation::Horizontal);
		CHECK(fresh(r.get("youtube")).video.orientation == Orientation::Horizontal);
		CHECK(fresh(r.get("facebook")).video.orientation == Orientation::Horizontal);
	}

	TEST_CASE("every platform provider explains itself and names an official help page")
	{
		Registry r;
		for (const char *id : {"twitch", "tiktok", "youtube", "facebook"}) {
			const IProvider &provider = r.get(id);
			CAPTURE(id);
			CHECK_FALSE(provider.setupNotes().empty());
			CHECK(provider.info().keyHelpUrl.rfind("https://", 0) == 0);
			CHECK_FALSE(provider.limits().checkedOn.empty());
			CHECK(provider.limits().source.rfind("https://", 0) == 0);
		}
	}
}

TEST_SUITE("providers.validation")
{
	TEST_CASE("a missing stream key blocks platforms and only warns for custom servers")
	{
		Registry r;
		const IProvider &twitch = r.get("twitch");
		CHECK(hasIssue(twitch.validate(fresh(twitch), kNoKey), field::kStreamKey, Severity::Error));

		const IProvider &custom = r.get("custom_rtmp");
		DestinationConfig config = fresh(custom);
		config.serverUrl = "rtmp://media.example.org/live";
		const auto issues = custom.validate(config, kNoKey);
		CHECK(hasIssue(issues, field::kStreamKey, Severity::Warning));
		CHECK_FALSE(hasErrors(issues));
	}

	TEST_CASE("custom RTMP server URL problems are reported on the server field")
	{
		Registry r;
		const IProvider &custom = r.get("custom_rtmp");
		DestinationConfig config = fresh(custom);

		for (const char *bad : {"", "media.example.org/live", "http://media.example.org/live", "rtmp://",
					"rtmp://media.example.org:99999/live", "rtmp://bad host/live"}) {
			CAPTURE(bad);
			config.serverUrl = bad;
			CHECK(hasIssue(custom.validate(config, kWithKey), field::kServer, Severity::Error));
		}
	}

	TEST_CASE("Custom RTMP refuses rtmps:// and Custom RTMPS refuses rtmp://")
	{
		Registry r;
		const IProvider &plain = r.get("custom_rtmp");
		DestinationConfig a = fresh(plain);
		a.serverUrl = "rtmps://media.example.org/live";
		CHECK(hasIssue(plain.validate(a, kWithKey), field::kServer, Severity::Error));
		a.serverUrl = "rtmp://media.example.org/live";
		CHECK_FALSE(hasErrors(plain.validate(a, kWithKey)));

		const IProvider &secure = r.get("custom_rtmps");
		DestinationConfig b = fresh(secure);
		b.serverUrl = "rtmp://media.example.org/live";
		CHECK(hasIssue(secure.validate(b, kWithKey), field::kServer, Severity::Error));
		b.serverUrl = "rtmps://media.example.org/live";
		CHECK_FALSE(hasErrors(secure.validate(b, kWithKey)));
		CHECK(secure.endpoint(b).tls);
	}

	TEST_CASE("credentials inside the server URL are an error, a query string is a warning")
	{
		Registry r;
		const IProvider &custom = r.get("custom_rtmp");
		DestinationConfig config = fresh(custom);

		config.serverUrl = "rtmp://alice:hunter2@media.example.org/live";
		CHECK(hasIssue(custom.validate(config, kWithKey), field::kServer, Severity::Error));

		config.serverUrl = "rtmp://media.example.org/live?token=abc";
		const auto issues = custom.validate(config, kWithKey);
		CHECK(hasIssue(issues, field::kServer, Severity::Warning));
		CHECK_FALSE(hasErrors(issues));
	}

	TEST_CASE("a server URL without an application name is a warning")
	{
		Registry r;
		const IProvider &custom = r.get("custom_rtmp");
		DestinationConfig config = fresh(custom);
		config.serverUrl = "rtmp://media.example.org";
		const auto issues = custom.validate(config, kWithKey);
		CHECK(hasIssue(issues, field::kServer, Severity::Warning));
		CHECK_FALSE(hasErrors(issues));
	}

	TEST_CASE("authentication needs a user name and a saved password")
	{
		Registry r;
		const IProvider &custom = r.get("custom_rtmp");
		DestinationConfig config = fresh(custom);
		config.serverUrl = "rtmp://media.example.org/live";
		config.useAuth = true;

		auto issues = custom.validate(config, {true, false});
		CHECK(hasIssue(issues, field::kUsername, Severity::Error));
		CHECK(hasIssue(issues, field::kPassword, Severity::Error));

		config.username = "alice";
		issues = custom.validate(config, {true, true});
		CHECK_FALSE(hasErrors(issues));

		const IProvider &twitch = r.get("twitch");
		DestinationConfig t = fresh(twitch);
		t.useAuth = true;
		CHECK(hasIssue(twitch.validate(t, kWithKey), field::kUsername, Severity::Error));
	}

	TEST_CASE("Facebook accepts RTMPS only")
	{
		Registry r;
		const IProvider &facebook = r.get("facebook");
		DestinationConfig config = fresh(facebook);
		config.serverId = kCustomServerId;
		config.serverUrl = "rtmp://rtmp-api.facebook.com/rtmp/";
		CHECK(hasIssue(facebook.validate(config, kWithKey), field::kServer, Severity::Error));
		config.serverUrl = "rtmps://rtmp-api.facebook.com:443/rtmp/";
		CHECK_FALSE(hasErrors(facebook.validate(config, kWithKey)));
	}

	TEST_CASE("an unknown server id is an error")
	{
		Registry r;
		const IProvider &youtube = r.get("youtube");
		DestinationConfig config = fresh(youtube);
		config.serverId = "does-not-exist";
		CHECK(hasIssue(youtube.validate(config, kWithKey), field::kServer, Severity::Error));
	}

	TEST_CASE("values above a platform limit warn, values out of range are errors")
	{
		Registry r;
		const IProvider &twitch = r.get("twitch");
		DestinationConfig config = fresh(twitch);

		config.video.bitrateKbps = 8000;
		CHECK(hasIssue(twitch.validate(config, kWithKey), field::kVideoBitrate, Severity::Warning));
		config.video.bitrateKbps = 6000;
		CHECK_FALSE(hasIssue(twitch.validate(config, kWithKey), field::kVideoBitrate, Severity::Warning));
		config.video.bitrateKbps = 50;
		CHECK(hasIssue(twitch.validate(config, kWithKey), field::kVideoBitrate, Severity::Error));
		config.video.bitrateKbps = 6000;

		config.audio.bitrateKbps = 320;
		CHECK(hasIssue(twitch.validate(config, kWithKey), field::kAudioBitrate, Severity::Warning));
		config.audio.bitrateKbps = 8;
		CHECK(hasIssue(twitch.validate(config, kWithKey), field::kAudioBitrate, Severity::Error));
		config.audio.bitrateKbps = 160;

		config.video.fps = 120;
		CHECK(hasIssue(twitch.validate(config, kWithKey), field::kFps, Severity::Warning));
		config.video.fps = 0;

		config.video.width = 2560;
		config.video.height = 1440;
		CHECK(hasIssue(twitch.validate(config, kWithKey), field::kResolution, Severity::Warning));
		config.video.width = 1921;
		config.video.height = 1080;
		CHECK(hasIssue(twitch.validate(config, kWithKey), field::kResolution, Severity::Error));
		config.video.width = 1920;
		config.video.height = 0;
		CHECK(hasIssue(twitch.validate(config, kWithKey), field::kResolution, Severity::Error));
		config.video.height = 1080;
		CHECK_FALSE(hasErrors(twitch.validate(config, kWithKey)));

		config.audio.track = 7;
		CHECK(hasIssue(twitch.validate(config, kWithKey), field::kAudioTrack, Severity::Error));
	}

	TEST_CASE("resolution and orientation must agree")
	{
		Registry r;
		const IProvider &tiktok = r.get("tiktok");
		DestinationConfig config = fresh(tiktok);
		config.serverUrl = "rtmp://push.example.net/live";
		config.video.orientation = Orientation::Vertical;
		config.video.width = 1920;
		config.video.height = 1080;
		CHECK(hasIssue(tiktok.validate(config, kWithKey), field::kOrientation, Severity::Error));

		config.video.width = 1080;
		config.video.height = 1920;
		CHECK_FALSE(hasErrors(tiktok.validate(config, kWithKey)));
	}

	TEST_CASE("keyframe guidance: YouTube and Facebook cap at 4 seconds, Twitch recommends 2")
	{
		Registry r;
		for (const char *id : {"youtube", "facebook"}) {
			const IProvider &provider = r.get(id);
			DestinationConfig config = fresh(provider);
			config.video.keyframeIntervalSec = 6;
			CAPTURE(id);
			CHECK(hasIssue(provider.validate(config, kWithKey), field::kKeyframe, Severity::Warning));
			config.video.keyframeIntervalSec = 4;
			CHECK_FALSE(hasIssue(provider.validate(config, kWithKey), field::kKeyframe, Severity::Warning));
		}

		const IProvider &twitch = r.get("twitch");
		DestinationConfig config = fresh(twitch);
		config.video.keyframeIntervalSec = 5;
		CHECK(hasIssue(twitch.validate(config, kWithKey), field::kKeyframe, Severity::Warning));
		config.video.keyframeIntervalSec = 2;
		CHECK_FALSE(hasIssue(twitch.validate(config, kWithKey), field::kKeyframe, Severity::Warning));
	}

	TEST_CASE("vertical video warns where the platform does not document it")
	{
		Registry r;
		DestinationConfig facebook = fresh(r.get("facebook"));
		facebook.video.orientation = Orientation::Vertical;
		CHECK(hasIssue(r.get("facebook").validate(facebook, kWithKey), field::kOrientation, Severity::Warning));

		DestinationConfig twitch = fresh(r.get("twitch"));
		twitch.video.orientation = Orientation::Vertical;
		CHECK(hasIssue(r.get("twitch").validate(twitch, kWithKey), field::kOrientation, Severity::Warning));

		DestinationConfig youtube = fresh(r.get("youtube"));
		youtube.video.orientation = Orientation::Vertical;
		CHECK_FALSE(hasIssue(r.get("youtube").validate(youtube, kWithKey), field::kOrientation, Severity::Warning));
	}

	TEST_CASE("every validation message says what is wrong and what to do")
	{
		Registry r;
		const IProvider &custom = r.get("custom_rtmp");
		DestinationConfig config = fresh(custom);
		config.name = " ";
		config.serverUrl = "ftp://x";
		config.useAuth = true;
		config.video.bitrateKbps = 1;
		config.video.fps = 999;
		config.video.keyframeIntervalSec = 99;
		config.video.bFrames = 99;
		config.audio.bitrateKbps = 1;
		config.audio.track = 0;
		config.connection.reconnectAttempts = 0;
		config.connection.streamDelaySec = -1;

		const auto issues = custom.validate(config, kNoKey);
		CHECK(issues.size() >= 10);
		for (const ValidationIssue &issue : issues) {
			CAPTURE(issue.field);
			CHECK_FALSE(issue.field.empty());
			CHECK_FALSE(issue.message.what.empty());
			CHECK_FALSE(issue.message.action.empty());
		}
	}
}

TEST_SUITE("providers.endpoint")
{
	TEST_CASE("a listed server resolves to its URL, a custom one to the typed URL")
	{
		Registry r;
		const IProvider &youtube = r.get("youtube");
		DestinationConfig config = fresh(youtube);
		CHECK(youtube.endpoint(config).serverUrl == "rtmps://a.rtmps.youtube.com:443/live2");

		config.serverId = kCustomServerId;
		config.serverUrl = "  rtmp://x.rtmp.youtube.com/live2  ";
		const Endpoint endpoint = youtube.endpoint(config);
		CHECK(endpoint.serverUrl == "rtmp://x.rtmp.youtube.com/live2");
		CHECK_FALSE(endpoint.tls);
	}

	TEST_CASE("Twitch test mode appends the bandwidth test flag to the key")
	{
		Registry r;
		const IProvider &twitch = r.get("twitch");
		CHECK(twitch.info().testSupport == TestSupport::PrivateStream);

		const SecretString key("live_000000000_TESTONLYnotarealkey0001"); // NOT-REAL
		CHECK(twitch.publishKey(key, false).reveal() == key.reveal());
		CHECK(twitch.publishKey(key, true).reveal() == key.reveal() + "?bandwidthtest=true");

		const SecretString withQuery("live_000000000_TESTONLYnotarealkey0001?x=1"); // NOT-REAL
		CHECK(twitch.publishKey(withQuery, true).reveal() == withQuery.reveal() + "&bandwidthtest=true");

		CHECK(twitch.publishKey(SecretString(), true).empty());
	}

	TEST_CASE("other providers publish the key unchanged")
	{
		Registry r;
		const SecretString key("abcd-efgh-ijkl-mnop-TEST");
		for (const char *id : {"tiktok", "youtube", "facebook", "custom_rtmp", "custom_rtmps"}) {
			CAPTURE(id);
			CHECK(r.get(id).publishKey(key, false).reveal() == key.reveal());
			CHECK(r.get(id).info().testSupport == TestSupport::Reachability);
		}
	}
}

TEST_SUITE("providers.errors")
{
	TEST_CASE("a rejected connection names the destination and tells the user what to check")
	{
		Registry r;
		const UserMessage message = r.get("tiktok").describeStop("TikTok", StopReason::InvalidStream, "");
		CHECK(message.what == "TikTok rejected the connection.");
		CHECK_FALSE(message.action.empty());
		CHECK(message.text().find("TikTok rejected the connection.") == 0);
	}

	TEST_CASE("every failure reason produces a message with an action")
	{
		Registry r;
		for (const IProvider *provider : r.registry.all()) {
			for (StopReason reason :
			     {StopReason::BadPath, StopReason::ConnectFailed, StopReason::InvalidStream, StopReason::Error,
			      StopReason::Disconnected, StopReason::Unsupported, StopReason::NoSpace,
			      StopReason::EncodeError, StopReason::HdrDisabled, StopReason::Unknown}) {
				CAPTURE(provider->info().id);
				CAPTURE(stopReasonName(reason));
				const UserMessage message = provider->describeStop("My stream", reason, "");
				CHECK(message.what.find("My stream") != std::string::npos);
				CHECK_FALSE(message.action.empty());
			}
		}
	}

	TEST_CASE("a normal stop produces no error message")
	{
		Registry r;
		CHECK(r.get("twitch").describeStop("Twitch", StopReason::UserStopped, "").empty());
	}

	TEST_CASE("an empty destination name falls back to the provider name")
	{
		Registry r;
		const UserMessage message = r.get("youtube").describeStop("", StopReason::ConnectFailed, "");
		CHECK(message.what.find("YouTube") != std::string::npos);
	}

	TEST_CASE("secrets in the OBS error text never reach the message")
	{
		Registry r;
		const std::string key = "live_000000000_TESTONLYnotarealkey0002"; // NOT-REAL
		globalRedactor().addSecret(key);
		const UserMessage message = r.get("twitch").describeStop(
			"Twitch", StopReason::ConnectFailed,
			"Could not access rtmp://ingest.example.net/app/" + key + " with key=" + key);
		globalRedactor().clearSecrets();

		CHECK(message.text().find(key) == std::string::npos);
		CHECK(message.detail.find("[redacted]") != std::string::npos);
	}

	TEST_CASE("OBS stop codes map to reasons")
	{
		CHECK(stopReasonFromObsCode(0) == StopReason::UserStopped);
		CHECK(stopReasonFromObsCode(-1) == StopReason::BadPath);
		CHECK(stopReasonFromObsCode(-2) == StopReason::ConnectFailed);
		CHECK(stopReasonFromObsCode(-3) == StopReason::InvalidStream);
		CHECK(stopReasonFromObsCode(-4) == StopReason::Error);
		CHECK(stopReasonFromObsCode(-5) == StopReason::Disconnected);
		CHECK(stopReasonFromObsCode(-6) == StopReason::Unsupported);
		CHECK(stopReasonFromObsCode(-7) == StopReason::NoSpace);
		CHECK(stopReasonFromObsCode(-8) == StopReason::EncodeError);
		CHECK(stopReasonFromObsCode(-9) == StopReason::HdrDisabled);
		CHECK(stopReasonFromObsCode(-99) == StopReason::Unknown);
	}
}
