// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include <doctest/doctest.h>

#include "diagnostics/preflight.h"
#include "legal/legal_documents.h"
#include "network/bandwidth.h"
#include "test_helpers.h"
#include "utils/paths.h"
#include "utils/strings.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <regex>
#include <set>

using namespace rd;
using rdtest::Providers;

namespace {

bool hasItem(const PreflightReport &report, const std::string &id, PreflightStatus status)
{
	return std::any_of(report.items.begin(), report.items.end(),
			   [&](const PreflightItem &item) { return item.id == id && item.status == status; });
}

// A ready-to-go input: healthy OBS, the given providers with keys, Balanced mode.
PreflightInput makeInput(const Providers &providers, std::initializer_list<const char *> providerIds,
			 const StreamContext &context, NetworkConfig network = {20000, 75})
{
	PreflightInput input;
	input.obs.videoEncoderCount = static_cast<int>(context.videoEncoders.size());

	std::vector<ResolveInput> resolveInputs;
	for (const char *id : providerIds)
		resolveInputs.push_back(providers.input(id));
	const auto effective = resolveEffectiveSettings(resolveInputs, context, PerformanceMode::Balanced);

	std::map<std::string, std::string> names;
	for (size_t i = 0; i < resolveInputs.size(); ++i) {
		PreflightDestination destination;
		destination.config = resolveInputs[i].config;
		destination.provider = resolveInputs[i].provider;
		destination.issues = destination.provider->validate(destination.config, {true, false});
		destination.effective = effective[i];
		names[destination.config.id] = destination.config.name;
		input.destinations.push_back(std::move(destination));
	}
	input.plan = planEncoders(effective);
	input.bandwidth = computeBandwidth(effective, names, {}, network);
	return input;
}

} // namespace

TEST_SUITE("network.bandwidth")
{
	TEST_CASE("the upload need is the sum over destinations, even when they share an encoder")
	{
		Providers p;
		const StreamContext context = rdtest::softwareContext(60);
		const std::vector<ResolveInput> inputs = {p.input("twitch"), p.input("youtube"), p.input("facebook")};
		const auto effective = resolveEffectiveSettings(inputs, context, PerformanceMode::Balanced);
		REQUIRE(planEncoders(effective).videoEncoderCount() == 1); // One encode

		const BandwidthBudget budget = computeBandwidth(effective, {}, {}, {30000, 75});
		REQUIRE(budget.entries.size() == 3);
		// Three uploads of 6000 Kbps video plus audio plus overhead.
		int expected = 0;
		for (const EffectiveDestination &destination : effective)
			expected += static_cast<int>(std::lround((6000 + destination.audio.bitrateKbps) * kProtocolOverhead));
		CHECK(budget.requiredKbps == expected);
		CHECK(budget.requiredKbps > 18000);
		CHECK(budget.status == BandwidthStatus::Ok);
		CHECK(budget.safeLimitKbps == 22500);
		CHECK(bandwidthMessage(budget).empty());
	}

	TEST_CASE("status follows the upload speed the user entered")
	{
		EffectiveDestination destination;
		destination.id = "a";
		destination.video.bitrateKbps = 6000;
		destination.audio.bitrateKbps = 160;
		const std::vector<EffectiveDestination> one = {destination};

		const BandwidthBudget unknown = computeBandwidth(one, {}, {}, {0, 75});
		CHECK(unknown.status == BandwidthStatus::Unknown);
		CHECK(unknown.requiredKbps == 6406);
		CHECK(unknown.usagePercent() == 0.0);
		CHECK_FALSE(bandwidthMessage(unknown).empty());

		CHECK(computeBandwidth(one, {}, {}, {20000, 75}).status == BandwidthStatus::Ok);
		CHECK(computeBandwidth(one, {}, {}, {8000, 75}).status == BandwidthStatus::Tight);   // safe limit 6000
		CHECK(computeBandwidth(one, {}, {}, {6000, 75}).status == BandwidthStatus::Exceeded);

		const BandwidthBudget tight = computeBandwidth(one, {}, {}, {8000, 75});
		CHECK(tight.usagePercent() == doctest::Approx(80.075));
		const UserMessage message = bandwidthMessage(tight);
		CHECK_FALSE(message.what.empty());
		CHECK_FALSE(message.action.empty());
	}

	TEST_CASE("names and measured bitrates are attached to their destinations")
	{
		EffectiveDestination a;
		a.id = "a";
		a.video.bitrateKbps = 3000;
		a.audio.bitrateKbps = 128;
		EffectiveDestination b = a;
		b.id = "b";

		const BandwidthBudget budget = computeBandwidth({a, b}, {{"a", "Twitch"}}, {{"a", 3100}, {"b", 2900}}, {10000, 75});
		CHECK(budget.entries[0].name == "Twitch");
		CHECK(budget.entries[1].name == "b"); // No name given: the id stands in
		CHECK(budget.entries[0].measuredKbps == 3100);
		CHECK(budget.measuredKbps == 6000);
	}

	TEST_CASE("no destinations need no upload")
	{
		const BandwidthBudget budget = computeBandwidth({}, {}, {}, {10000, 75});
		CHECK(budget.requiredKbps == 0);
		CHECK(budget.status == BandwidthStatus::Ok);
	}
}

TEST_SUITE("diagnostics.preflight")
{
	TEST_CASE("a healthy setup is READY and says why")
	{
		Providers p;
		const PreflightInput input = makeInput(p, {"twitch", "youtube"}, rdtest::softwareContext(60));
		const PreflightReport report = runPreflight(input);
		for (const PreflightItem &item : report.items)
			CAPTURE(item.id + ": " + item.message.text());
		CHECK(report.status == PreflightStatus::Ready);
		CHECK(report.count(PreflightStatus::Failed) == 0);
		CHECK(report.count(PreflightStatus::Warning) == 0);
		CHECK(hasItem(report, "video.ok", PreflightStatus::Ready));
		CHECK(hasItem(report, "audio.ok", PreflightStatus::Ready));
		CHECK(hasItem(report, "destination.ok", PreflightStatus::Ready));
		CHECK(hasItem(report, "network.ok", PreflightStatus::Ready));
		CHECK(hasItem(report, "performance.encoders", PreflightStatus::Ready));
	}

	TEST_CASE("every finding explains itself")
	{
		Providers p;
		PreflightInput input = makeInput(p, {"twitch", "facebook"}, rdtest::softwareContext(60), {4000, 75});
		input.obs.sceneHasVideo = false;
		input.obs.hasAudioSource = false;
		input.obs.renderLagPercent = 6.0;
		input.destinations[0].issues = input.destinations[0].provider->validate(input.destinations[0].config, {false, false});
		input.destinations[1].keySessionOnly = true;
		input.destinations[1].duplicateKeyOf = "Twitch";
		input.destinations[1].verticalMissingItems = 2;

		const PreflightReport report = runPreflight(input);
		CHECK(report.status == PreflightStatus::Failed);
		for (const PreflightItem &item : report.items) {
			CAPTURE(item.id);
			CHECK_FALSE(item.title.empty());
			CHECK_FALSE(item.message.what.empty());
			if (item.status != PreflightStatus::Ready)
				CHECK_FALSE((item.message.action.empty() && item.message.detail.empty() && item.id != "destination.adjusted"));
		}
	}

	TEST_CASE("OBS problems")
	{
		Providers p;
		PreflightInput input = makeInput(p, {"twitch"}, rdtest::softwareContext(60));

		input.obs.videoRunning = false;
		PreflightReport report = runPreflight(input);
		CHECK(report.status == PreflightStatus::Failed);
		CHECK(hasItem(report, "video.not_running", PreflightStatus::Failed));

		input.obs.videoRunning = true;
		input.obs.sceneHasVideo = false;
		report = runPreflight(input);
		CHECK(report.status == PreflightStatus::Warning);
		CHECK(hasItem(report, "video.no_source", PreflightStatus::Warning));

		input.obs.sceneHasVideo = true;
		input.obs.hasAudioSource = false;
		CHECK(hasItem(runPreflight(input), "audio.no_source", PreflightStatus::Warning));

		input.obs.hasAudioSource = true;
		input.obs.allAudioMuted = true;
		CHECK(hasItem(runPreflight(input), "audio.muted", PreflightStatus::Warning));

		input.obs.allAudioMuted = false;
		input.obs.renderLagPercent = 4.0;
		CHECK(hasItem(runPreflight(input), "performance.render_lag", PreflightStatus::Warning));
		input.obs.renderLagPercent = 0.5;
		CHECK(runPreflight(input).status == PreflightStatus::Ready);

		input.obs.videoEncoderCount = 0;
		CHECK(hasItem(runPreflight(input), "encoder.none", PreflightStatus::Failed));
	}

	TEST_CASE("no enabled destination fails")
	{
		PreflightInput input;
		input.obs.videoEncoderCount = 1;
		const PreflightReport report = runPreflight(input);
		CHECK(report.status == PreflightStatus::Failed);
		CHECK(hasItem(report, "destinations.none", PreflightStatus::Failed));
	}

	TEST_CASE("a missing stream key fails that destination and names it")
	{
		Providers p;
		PreflightInput input = makeInput(p, {"twitch", "youtube"}, rdtest::softwareContext(60));
		input.destinations[1].issues = input.destinations[1].provider->validate(input.destinations[1].config, {false, false});

		const PreflightReport report = runPreflight(input);
		CHECK(report.status == PreflightStatus::Failed);
		const auto item = std::find_if(report.items.begin(), report.items.end(),
					       [](const PreflightItem &i) { return i.id == "destination.stream_key"; });
		REQUIRE(item != report.items.end());
		CHECK(item->title == "YouTube");
		CHECK(item->destinationId == input.destinations[1].config.id);
		CHECK(item->message.what.find("YouTube") != std::string::npos);
		// The healthy destination still reports ready.
		CHECK(hasItem(report, "destination.ok", PreflightStatus::Ready));
		// Failures are listed first.
		CHECK(report.items.front().status == PreflightStatus::Failed);
	}

	TEST_CASE("an invalid server URL fails with the provider's message")
	{
		Providers p;
		PreflightInput input = makeInput(p, {"custom_rtmp"}, rdtest::softwareContext(60));
		input.destinations[0].config.serverUrl = "http://not-rtmp.example";
		input.destinations[0].issues = input.destinations[0].provider->validate(input.destinations[0].config, {true, false});
		const PreflightReport report = runPreflight(input);
		CHECK(hasItem(report, "destination.server", PreflightStatus::Failed));
	}

	TEST_CASE("an unknown platform and a missing encoder fail")
	{
		Providers p;
		PreflightInput input = makeInput(p, {"twitch"}, rdtest::softwareContext(60));
		input.destinations[0].provider = nullptr;
		CHECK(hasItem(runPreflight(input), "destination.unknown_provider", PreflightStatus::Failed));

		PreflightInput noEncoder = makeInput(p, {"twitch"}, rdtest::softwareContext(60));
		noEncoder.destinations[0].effective.video.encoderId.clear();
		CHECK(hasItem(runPreflight(noEncoder), "destination.no_encoder", PreflightStatus::Failed));
	}

	TEST_CASE("credential and layout findings are warnings")
	{
		Providers p;
		PreflightInput input = makeInput(p, {"twitch", "tiktok"}, rdtest::softwareContext(60));
		input.destinations[0].keySessionOnly = true;
		input.destinations[1].duplicateKeyOf = "Twitch";
		input.destinations[1].verticalMissingItems = 1;

		const PreflightReport report = runPreflight(input);
		CHECK(report.status == PreflightStatus::Warning);
		CHECK(hasItem(report, "destination.key_session_only", PreflightStatus::Warning));
		CHECK(hasItem(report, "destination.duplicate_key", PreflightStatus::Warning));
		CHECK(hasItem(report, "destination.vertical_missing", PreflightStatus::Warning));
	}

	TEST_CASE("values above a platform limit and adjusted settings are warnings")
	{
		Providers p;
		PreflightInput input = makeInput(p, {"twitch"}, rdtest::softwareContext(30));
		PreflightDestination &twitch = input.destinations[0];
		twitch.config.video.bitrateKbps = 9000;
		twitch.config.locks.bitrate = true;
		twitch.issues = twitch.provider->validate(twitch.config, {true, false});
		twitch.effective.notes.push_back("OBS runs at 30 FPS, so 60 FPS is not available.");

		const PreflightReport report = runPreflight(input);
		CHECK(report.status == PreflightStatus::Warning);
		CHECK(hasItem(report, "destination.video.bitrate", PreflightStatus::Warning));
		CHECK(hasItem(report, "destination.adjusted", PreflightStatus::Warning));
	}

	TEST_CASE("upload: tight is a warning, exceeded fails, unknown passes with advice")
	{
		Providers p;
		const StreamContext context = rdtest::softwareContext(60);

		PreflightReport report = runPreflight(makeInput(p, {"twitch"}, context, {8000, 75}));
		CHECK(hasItem(report, "network.tight", PreflightStatus::Warning));

		report = runPreflight(makeInput(p, {"twitch", "youtube"}, context, {8000, 75}));
		CHECK(hasItem(report, "network.exceeded", PreflightStatus::Failed));
		CHECK(report.status == PreflightStatus::Failed);

		report = runPreflight(makeInput(p, {"twitch"}, context, {0, 75}));
		CHECK(hasItem(report, "network.unknown", PreflightStatus::Ready));
		CHECK(report.status == PreflightStatus::Ready);
	}

	TEST_CASE("Twitch simulcast rule: a better picture elsewhere is flagged")
	{
		Providers p;
		const StreamContext context = rdtest::softwareContext(60);
		PreflightInput input = makeInput(p, {"twitch", "youtube"}, context);
		CHECK_FALSE(hasItem(runPreflight(input), "conflict.twitch_simulcast", PreflightStatus::Warning)); // Equal settings

		input.destinations[0].effective.video.width = 1280;
		input.destinations[0].effective.video.height = 720;
		CHECK(hasItem(runPreflight(input), "conflict.twitch_simulcast", PreflightStatus::Warning));

		// A vertical stream on another platform is a different picture, not a better one.
		PreflightInput vertical = makeInput(p, {"twitch", "tiktok"}, context);
		CHECK_FALSE(hasItem(runPreflight(vertical), "conflict.twitch_simulcast", PreflightStatus::Warning));
	}

	TEST_CASE("many software encoders and split encoders in Potato Mode are flagged")
	{
		Providers p;
		const StreamContext context = rdtest::softwareContext(60);
		PreflightInput input = makeInput(p, {"twitch", "youtube", "facebook"}, context, {60000, 75});
		// Pretend three different encodes run.
		input.plan.groups.clear();
		for (size_t i = 0; i < input.destinations.size(); ++i) {
			EncoderGroup group;
			group.settings = input.destinations[i].effective.video;
			group.settings.bitrateKbps += static_cast<int>(i) * 50;
			group.destinationIds = {input.destinations[i].config.id};
			input.plan.groups.push_back(group);
		}
		CHECK(hasItem(runPreflight(input), "performance.software_encoders", PreflightStatus::Warning));

		input.mode = PerformanceMode::Potato;
		CHECK(hasItem(runPreflight(input), "performance.potato_encoders", PreflightStatus::Warning));
	}
}

TEST_SUITE("legal.documents")
{
	TEST_CASE("the six required documents exist with unique ids and versions")
	{
		const auto &documents = legalDocuments();
		REQUIRE(documents.size() == 6);
		std::set<std::string> ids;
		for (const LegalDocument &document : documents) {
			CAPTURE(document.id);
			CHECK(ids.insert(document.id).second);
			CHECK_FALSE(document.title.empty());
			CHECK_FALSE(document.version.empty());
			CHECK(findLegalDocument(document.id) == &document);
		}
		for (const char *id : {"terms-of-use", "privacy-policy", "security-notice", "third-party-services",
				       "streaming-disclaimer", "open-source-licenses"})
			CHECK(findLegalDocument(id) != nullptr);
		CHECK(findLegalDocument("nope") == nullptr);
	}

	TEST_CASE("each document file exists and carries the registered version and title")
	{
		for (const LegalDocument &document : legalDocuments()) {
			CAPTURE(document.id);
			std::string text;
			const std::filesystem::path path =
				pathFromUtf8(std::string(RD_SOURCE_DIR) + "/resources/legal/" + document.file);
			REQUIRE(readFileToString(path, text, 1024 * 1024));

			CHECK(text.rfind("# " + document.title, 0) == 0);
			std::smatch match;
			REQUIRE(std::regex_search(text, match, std::regex(R"(Version (\d+\.\d+)\. Last updated \d{1,2} \w+ \d{4}\.)")));
			CHECK(match[1].str() == document.version);
		}
	}

	TEST_CASE("legal text makes no absolute promises and follows the writing rules")
	{
		for (const LegalDocument &document : legalDocuments()) {
			std::string text;
			REQUIRE(readFileToString(pathFromUtf8(std::string(RD_SOURCE_DIR) + "/resources/legal/" + document.file), text,
						 1024 * 1024));
			const std::string lower = toLower(text);
			CAPTURE(document.id);
			for (const char *banned : {"unhackable", "zero liability", "100%", "bug free", "bug-free", "guarantee zero",
						   "always work", "never fail"})
				CHECK(lower.find(banned) == std::string::npos);
			CHECK(text.find("\xE2\x80\x94") == std::string::npos); // em dash
			CHECK(text.find(';') == std::string::npos);
		}
	}

	TEST_CASE("documents that create obligations are marked for legal review")
	{
		for (const char *id : {"terms-of-use", "privacy-policy", "security-notice", "third-party-services", "streaming-disclaimer"}) {
			std::string text;
			REQUIRE(readFileToString(
				pathFromUtf8(std::string(RD_SOURCE_DIR) + "/resources/legal/" + findLegalDocument(id)->file), text,
				1024 * 1024));
			CAPTURE(id);
			CHECK(text.find("reviewed by a qualified lawyer") != std::string::npos);
		}
	}

	TEST_CASE("the privacy policy in the repository root is the one RelayDock shows")
	{
		std::string shown;
		std::string published;
		REQUIRE(readFileToString(pathFromUtf8(std::string(RD_SOURCE_DIR) + "/resources/legal/privacy-policy.md"), shown, 1024 * 1024));
		REQUIRE(readFileToString(pathFromUtf8(std::string(RD_SOURCE_DIR) + "/PRIVACY.md"), published, 1024 * 1024));
		CHECK(replaceAll(shown, "\r\n", "\n") == replaceAll(published, "\r\n", "\n"));
	}

	TEST_CASE("every document is compiled into the plugin")
	{
		std::string resources;
		REQUIRE(readFileToString(pathFromUtf8(std::string(RD_SOURCE_DIR) + "/resources/relaydock.qrc"), resources, 1024 * 1024));
		for (const LegalDocument &document : legalDocuments()) {
			CAPTURE(document.file);
			CHECK(resources.find("<file>legal/" + document.file + "</file>") != std::string::npos);
		}
	}

	TEST_CASE("nothing is accepted on a first start")
	{
		const std::vector<LegalAcceptance> none;
		CHECK(pendingLegalDocuments(none).size() == 6);
		CHECK_FALSE(legalComplete(none));
	}

	TEST_CASE("accepting every document completes onboarding")
	{
		std::vector<LegalAcceptance> records;
		for (const LegalDocument &document : legalDocuments()) {
			CHECK_FALSE(legalComplete(records));
			REQUIRE(recordLegalAcceptance(records, document.id, "2026-10-04T10:00:00Z", "1.0.0"));
		}
		CHECK(legalComplete(records));
		CHECK(records.size() == 6);
		CHECK(records.front().acceptedAtUtc == "2026-10-04T10:00:00Z");
		CHECK(records.front().appVersion == "1.0.0");
		CHECK(records.front().version == legalDocuments().front().version);
	}

	TEST_CASE("a changed document version needs a new review of that document only")
	{
		std::vector<LegalAcceptance> records;
		for (const LegalDocument &document : legalDocuments())
			recordLegalAcceptance(records, document.id, "2026-10-04T10:00:00Z", "1.0.0");

		// The user agreed to an older version of the privacy policy.
		for (LegalAcceptance &record : records) {
			if (record.documentId == "privacy-policy")
				record.version = "0.9";
		}
		const std::vector<std::string> pending = pendingLegalDocuments(records);
		REQUIRE(pending.size() == 1);
		CHECK(pending.front() == "privacy-policy");

		// Accepting again replaces the old record instead of adding a second one.
		REQUIRE(recordLegalAcceptance(records, "privacy-policy", "2026-11-01T08:00:00Z", "1.0.1"));
		CHECK(legalComplete(records));
		CHECK(records.size() == 6);
	}

	TEST_CASE("unknown documents cannot be accepted and stale records are pruned")
	{
		std::vector<LegalAcceptance> records;
		CHECK_FALSE(recordLegalAcceptance(records, "made-up-document", "t", "1.0.0"));
		CHECK(records.empty());

		records.push_back({"retired-document", "1.0", "t", "1.0.0"});
		recordLegalAcceptance(records, "terms-of-use", "t", "1.0.0");
		pruneLegalRecords(records);
		REQUIRE(records.size() == 1);
		CHECK(records.front().documentId == "terms-of-use");
	}

	TEST_CASE("acknowledgement wording says reviewed, and agree only where there is something to agree to")
	{
		CHECK(legalAcknowledgement(*findLegalDocument("terms-of-use")) == "I have reviewed and agree to the Terms of Use.");
		CHECK(legalAcknowledgement(*findLegalDocument("open-source-licenses")) == "I have reviewed the Open Source Licenses.");
		// The checkbox never claims the user read the whole text.
		for (const LegalDocument &document : legalDocuments())
			CHECK(legalAcknowledgement(document).find("have read") == std::string::npos);
	}

	TEST_CASE("an acceptance record holds nothing but the document, version, time and app version")
	{
		// The struct has exactly these four fields. Adding a field must be a deliberate choice.
		const LegalAcceptance record{"terms-of-use", "1.0", "2026-10-04T10:00:00Z", "1.0.0"};
		CHECK(sizeof(LegalAcceptance) == 4 * sizeof(std::string));
		CHECK(record.documentId == "terms-of-use");
	}
}
