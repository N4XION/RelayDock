// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include <doctest/doctest.h>

#include "settings/app_config.h"
#include "settings/config_json.h"
#include "settings/config_store.h"
#include "settings/migrations.h"
#include "utils/paths.h"
#include "utils/uuid.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace rd;

namespace {

// A folder under the system temp directory that is removed when the test ends.
struct TempDir {
	std::filesystem::path path;

	TempDir()
	{
		path = std::filesystem::temp_directory_path() / ("relaydock-test-" + generateUuid());
		std::filesystem::create_directories(path);
	}

	~TempDir()
	{
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
	}
};

void writeText(const std::filesystem::path &path, const std::string &text)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out << text;
}

std::string readText(const std::filesystem::path &path)
{
	std::string text;
	readFileToString(path, text, ConfigStore::kMaxFileBytes);
	return text;
}

// A configuration with every field moved off its default, so a field that fails to
// round-trip shows up.
AppConfig makeFullConfig()
{
	AppConfig config = makeDefaultConfig();

	DestinationConfig a;
	a.id = generateUuid();
	a.provider = "twitch";
	a.name = "Twitch main";
	a.enabled = false;
	a.serverId = "euc10";
	a.serverUrl = "";
	a.video.orientation = Orientation::Horizontal;
	a.video.width = 1280;
	a.video.height = 720;
	a.video.fps = 30;
	a.video.encoder = "obs_x264";
	a.video.bitrateKbps = 4500;
	a.video.rateControl = "VBR";
	a.video.preset = "faster";
	a.video.profile = "high";
	a.video.keyframeIntervalSec = 2;
	a.video.bFrames = 2;
	a.video.customOptions = "scenecut=0";
	a.audio.encoder = "ffmpeg_aac";
	a.audio.bitrateKbps = 128;
	a.audio.track = 3;
	a.connection.autoReconnect = false;
	a.connection.reconnectAttempts = 7;
	a.connection.reconnectDelaySec = 9;
	a.connection.streamDelaySec = 20;
	a.connection.bindIp = "192.168.1.50";
	a.locks.resolution = true;
	a.locks.fps = true;
	a.locks.bitrate = true;
	a.locks.encoder = true;
	a.locks.aspectRatio = false;
	a.autoOptimize = false;
	config.destinations.push_back(a);

	DestinationConfig b;
	b.id = generateUuid();
	b.provider = "custom_rtmps";
	b.name = "Caf\xC3\xA9 server \xE2\x9C\x93";
	b.serverId = kCustomServerId;
	b.serverUrl = "rtmps://media.example.org:443/live";
	b.useAuth = true;
	b.username = "alice";
	b.video.orientation = Orientation::Vertical;
	b.verticalLayoutId = generateUuid();
	config.destinations.push_back(b);

	config.performanceMode = PerformanceMode::Potato;
	config.optimizer.mode = OptimizationMode::Automatic;
	config.optimizer.preventGameLag = true;
	config.optimizer.allowReconnectingChanges = true;
	config.network.uploadKbps = 20000;
	config.network.safetyPercent = 60;

	config.theme.mode = ThemeMode::Custom;
	config.theme.accentColor = "#C2410C";
	config.theme.backgroundColor = "#101418";
	config.theme.backgroundImage = "C:/Users/Example/Pictures/bg \xC3\xA9.png";
	config.theme.backgroundFit = BackgroundFit::Tile;
	config.theme.backgroundOpacity = 40;
	config.theme.cardOpacity = 85;
	config.theme.cornerRadius = 8;
	config.theme.fontScale = 110;
	config.theme.density = Density::Compact;
	config.theme.animations = false;
	config.theme.reducedMotion = true;

	DockLayout second;
	second.id = generateUuid();
	second.name = "Compact grid";
	second.sectionOrder = {section::kNetwork, section::kDestinations, section::kPerformance, section::kSuggestions};
	second.showSuggestions = false;
	second.showPerformance = false;
	second.showNetwork = true;
	second.controlPlacement = ControlPlacement::Bottom;
	second.cardView = CardView::Compact;
	second.listMode = ListMode::Grid;
	config.layouts.push_back(second);
	config.activeLayoutId = second.id;

	config.legal.push_back({"terms-of-use", "1.0", "2026-10-04T10:00:00Z", "1.0.0"});
	config.legal.push_back({"privacy-policy", "1.0", "2026-10-04T10:00:05Z", "1.0.0"});

	config.general.confirmStopAll = false;
	config.general.preflightOnStartAll = false;
	config.general.followObsStreaming = true;
	config.general.checkUpdatesOnStart = false;
	config.general.skippedUpdateVersion = "1.0.1";

	config.verticalCanvas.width = 720;
	config.verticalCanvas.height = 1280;
	return config;
}

} // namespace

TEST_SUITE("settings.json")
{
	TEST_CASE("the default configuration round-trips")
	{
		const AppConfig config = makeDefaultConfig();
		const ConfigParseResult parsed = parseConfig(serializeConfig(config));
		REQUIRE(parsed.ok);
		CHECK(parsed.config == config);
		CHECK(parsed.notes.empty());
		CHECK(parsed.fileSchemaVersion == kConfigSchemaVersion);
		CHECK_FALSE(parsed.newerSchema);
	}

	TEST_CASE("every field round-trips, including non-English text")
	{
		const AppConfig config = makeFullConfig();
		const std::string text = serializeConfig(config);
		const ConfigParseResult parsed = parseConfig(text);
		REQUIRE(parsed.ok);
		CHECK(parsed.notes.empty());
		CHECK(parsed.config == config);

		// Saving what was loaded gives byte-identical text.
		CHECK(serializeConfig(parsed.config) == text);
	}

	TEST_CASE("a new configuration has one layout, no destinations and safe defaults")
	{
		const AppConfig config = makeDefaultConfig();
		CHECK(config.destinations.empty());
		REQUIRE(config.layouts.size() == 1);
		CHECK(config.activeLayoutId == config.layouts.front().id);
		CHECK(config.performanceMode == PerformanceMode::Balanced);
		CHECK(config.optimizer.mode == OptimizationMode::Suggest);
		// Looking for a newer version at start-up is on. The Privacy Policy says so.
		CHECK(config.general.checkUpdatesOnStart);
		CHECK(config.general.skippedUpdateVersion.empty());
		CHECK(config.legal.empty());
	}

	TEST_CASE("text that is not JSON, or not a configuration, is an error")
	{
		CHECK_FALSE(parseConfig("").ok);
		CHECK_FALSE(parseConfig("not json at all").ok);
		CHECK_FALSE(parseConfig("{\"schema_version\": 1,").ok);
		CHECK_FALSE(parseConfig("[1, 2, 3]").ok);
		CHECK_FALSE(parseConfig("{}").ok);
		CHECK_FALSE(parseConfig("{\"schema_version\": 0}").ok);
		CHECK_FALSE(parseConfig("{\"schema_version\": \"one\"}").ok);
	}

	TEST_CASE("missing keys fall back to defaults")
	{
		const ConfigParseResult parsed = parseConfig("{\"schema_version\": 2}");
		REQUIRE(parsed.ok);
		CHECK(parsed.config.destinations.empty());
		CHECK(parsed.config.performanceMode == PerformanceMode::Balanced);
		CHECK(parsed.config.layouts.size() == 1);
		CHECK(parsed.config.findLayout(parsed.config.activeLayoutId) != nullptr);
	}

	TEST_CASE("values of the wrong type fall back to defaults instead of failing")
	{
		const std::string text = R"({
			"schema_version": 2,
			"destinations": "nope",
			"performance_mode": 12,
			"optimizer": [],
			"network": {"upload_kbps": "fast", "safety_percent": true},
			"theme": {"mode": "neon", "corner_radius": "round", "animations": "yes"},
			"layouts": [42, {"name": 5}],
			"legal": {"a": 1},
			"general": null,
			"vertical_canvas": {"width": "wide"}
		})";
		const ConfigParseResult parsed = parseConfig(text);
		REQUIRE(parsed.ok);
		CHECK(parsed.config.destinations.empty());
		CHECK(parsed.config.performanceMode == PerformanceMode::Balanced);
		CHECK(parsed.config.network.uploadKbps == 0);
		CHECK(parsed.config.network.safetyPercent == 75);
		CHECK(parsed.config.theme.mode == ThemeMode::FollowObs);
		CHECK(parsed.config.theme.cornerRadius == 4);
		CHECK(parsed.config.theme.animations);
		CHECK(parsed.config.verticalCanvas.width == 1080);
		CHECK(parsed.config.legal.empty());
		CHECK_FALSE(parsed.config.layouts.empty());
	}

	TEST_CASE("out-of-range values are repaired and reported")
	{
		const std::string id = generateUuid();
		const std::string text = R"({
			"schema_version": 2,
			"destinations": [
				{"id": ")" + id + R"(", "provider": "twitch", "name": "A",
				 "video": {"bitrate_kbps": 99999999, "width": 1921, "height": 1081, "fps": -5,
				           "keyframe_interval_sec": 500, "b_frames": 50},
				 "audio": {"bitrate_kbps": 1, "track": 9},
				 "connection": {"reconnect_attempts": 0, "reconnect_delay_sec": 99999, "stream_delay_sec": -3}},
				{"id": ")" + id + R"(", "provider": "youtube", "name": ""},
				{"id": "not-a-uuid", "provider": "facebook", "name": "C", "video": {"width": 1280}}
			],
			"network": {"upload_kbps": -10, "safety_percent": 500},
			"theme": {"accent_color": "red", "background_color": "#12345", "card_opacity": 0,
			          "background_opacity": 400, "corner_radius": 99, "font_scale": 10},
			"vertical_canvas": {"width": 1920, "height": 1080}
		})";

		const ConfigParseResult parsed = parseConfig(text);
		REQUIRE(parsed.ok);
		CHECK_FALSE(parsed.notes.empty());

		const AppConfig &config = parsed.config;
		REQUIRE(config.destinations.size() == 3);

		const DestinationConfig &a = config.destinations[0];
		CHECK(a.id == id);
		CHECK(a.video.bitrateKbps == 100000);
		CHECK(a.video.width == 1922);
		CHECK(a.video.height == 1082);
		CHECK(a.video.fps == 0);
		CHECK(a.video.keyframeIntervalSec == 20);
		CHECK(a.video.bFrames == 16);
		CHECK(a.audio.bitrateKbps == 32);
		CHECK(a.audio.track == 6);
		CHECK(a.connection.reconnectAttempts == 1);
		CHECK(a.connection.reconnectDelaySec == 300);
		CHECK(a.connection.streamDelaySec == 0);

		// Duplicate and invalid ids are replaced, and every id ends up unique.
		CHECK(isUuid(config.destinations[1].id));
		CHECK(isUuid(config.destinations[2].id));
		CHECK(config.destinations[1].id != id);
		CHECK(config.destinations[1].id != config.destinations[2].id);
		CHECK_FALSE(config.destinations[1].name.empty());

		// Half a resolution becomes "canvas size".
		CHECK(config.destinations[2].video.width == 0);
		CHECK(config.destinations[2].video.height == 0);

		CHECK(config.network.uploadKbps == 0);
		CHECK(config.network.safetyPercent == 100);
		CHECK(config.theme.accentColor.empty());
		CHECK(config.theme.backgroundColor.empty());
		CHECK(config.theme.cardOpacity == 20);
		CHECK(config.theme.backgroundOpacity == 100);
		CHECK(config.theme.cornerRadius == 16);
		CHECK(config.theme.fontScale == 80);

		// A canvas that is wider than tall is not a vertical canvas.
		CHECK(config.verticalCanvas.width == 1080);
		CHECK(config.verticalCanvas.height == 1920);
	}

	TEST_CASE("layout section lists are completed and cleaned")
	{
		const std::string layoutId = generateUuid();
		const std::string text = R"({
			"schema_version": 2,
			"layouts": [{"id": ")" + layoutId + R"(", "name": "Mine",
			             "section_order": ["network", "bogus", "network", "destinations"]}],
			"active_layout_id": "stale-id"
		})";
		const ConfigParseResult parsed = parseConfig(text);
		REQUIRE(parsed.ok);
		REQUIRE(parsed.config.layouts.size() == 1);
		const DockLayout &layout = parsed.config.layouts.front();
		CHECK(layout.sectionOrder ==
		      std::vector<std::string>{"network", "destinations", "suggestions", "performance"});
		CHECK(parsed.config.activeLayoutId == layoutId);
	}

	TEST_CASE("incomplete legal records are dropped")
	{
		const std::string text = R"({
			"schema_version": 2,
			"legal": [
				{"document_id": "terms-of-use", "version": "1.0", "accepted_at_utc": "t", "app_version": "1.0.0"},
				{"document_id": "", "version": "1.0"},
				{"document_id": "privacy-policy"}
			]
		})";
		const ConfigParseResult parsed = parseConfig(text);
		REQUIRE(parsed.ok);
		REQUIRE(parsed.config.legal.size() == 1);
		CHECK(parsed.config.legal.front().documentId == "terms-of-use");
	}

	TEST_CASE("a file from a newer RelayDock loads what this version understands and says so")
	{
		const std::string text = R"({"schema_version": 999, "performance_mode": "quality", "future_setting": {"x": 1}})";
		const ConfigParseResult parsed = parseConfig(text);
		REQUIRE(parsed.ok);
		CHECK(parsed.newerSchema);
		CHECK(parsed.fileSchemaVersion == 999);
		CHECK(parsed.config.performanceMode == PerformanceMode::Quality);
		CHECK(parsed.config.schemaVersion == kConfigSchemaVersion);
		CHECK_FALSE(parsed.notes.empty());
	}

	TEST_CASE("a destination round-trips by itself")
	{
		const DestinationConfig original = makeFullConfig().destinations.front();
		DestinationConfig copy;
		REQUIRE(parseDestination(serializeDestination(original), copy));
		CHECK(copy == original);
		CHECK_FALSE(parseDestination("[]", copy));
		CHECK_FALSE(parseDestination("nope", copy));
	}

	TEST_CASE("the saved file has no field for a stream key or a password")
	{
		const std::string text = serializeConfig(makeFullConfig());
		const nlohmann::json root = nlohmann::json::parse(text);
		for (const auto &destination : root["destinations"]) {
			for (const char *forbidden : {"key", "stream_key", "streamkey", "password", "secret", "token"}) {
				CAPTURE(forbidden);
				CHECK_FALSE(destination.contains(forbidden));
			}
		}
	}
}

TEST_SUITE("settings.migrations")
{
	TEST_CASE("steps run in order from the file's version to the target")
	{
		MigrationChain chain;
		chain.add({1, "rename mode to performance_mode", [](nlohmann::json &root) {
				   root["performance_mode"] = root["mode"];
				   root.erase("mode");
			   }});
		chain.add({2, "move upload speed into network", [](nlohmann::json &root) {
				   root["network"]["upload_kbps"] = root["upload"];
				   root.erase("upload");
			   }});

		nlohmann::json root = {{"schema_version", 1}, {"mode", "potato"}, {"upload", 9000}};
		const MigrationChain::Result result = chain.run(root, 1, 3);

		REQUIRE(result.ok);
		CHECK(result.finalVersion == 3);
		CHECK(result.applied.size() == 2);
		CHECK(root["schema_version"] == 3);
		CHECK(root["performance_mode"] == "potato");
		CHECK(root["network"]["upload_kbps"] == 9000);
		CHECK_FALSE(root.contains("mode"));
		CHECK_FALSE(root.contains("upload"));
	}

	TEST_CASE("a file that is already current needs no step")
	{
		MigrationChain chain;
		nlohmann::json root = {{"schema_version", 3}};
		const MigrationChain::Result result = chain.run(root, 3, 3);
		CHECK(result.ok);
		CHECK(result.applied.empty());
		CHECK(result.finalVersion == 3);
	}

	TEST_CASE("a missing step stops the migration and leaves later data untouched")
	{
		MigrationChain chain;
		chain.add({1, "one to two", [](nlohmann::json &root) { root["a"] = 1; }});
		// No step from 2 to 3.
		nlohmann::json root = {{"schema_version", 1}};
		const MigrationChain::Result result = chain.run(root, 1, 3);
		CHECK_FALSE(result.ok);
		CHECK(result.finalVersion == 2);
		CHECK_FALSE(result.error.empty());
	}

	TEST_CASE("a step that throws is reported instead of crashing")
	{
		MigrationChain chain;
		chain.add({1, "bad step", [](nlohmann::json &root) { root.at("missing").get<int>(); }});
		nlohmann::json root = {{"schema_version", 1}};
		const MigrationChain::Result result = chain.run(root, 1, 2);
		CHECK_FALSE(result.ok);
		CHECK(result.finalVersion == 1);
		CHECK_FALSE(result.error.empty());
	}

	TEST_CASE("a version 1 file gets the update check at start-up switched on, once, and says so")
	{
		// Version 1 had the check off by default and could not tell a choice from that default.
		const ConfigParseResult parsed = parseConfig(
			R"({"schema_version": 1, "general": {"check_updates_on_start": false, "confirm_stop_all": false}})");
		REQUIRE(parsed.ok);
		CHECK(parsed.fileSchemaVersion == 1);
		CHECK(parsed.config.general.checkUpdatesOnStart);
		CHECK_FALSE(parsed.config.general.confirmStopAll); // Everything else is left alone
		bool told = false;
		for (const std::string &note : parsed.notes)
			told = told || note.find("update check") != std::string::npos;
		CHECK(told);

		// A version 1 file without a "general" section migrates too.
		const ConfigParseResult bare = parseConfig(R"({"schema_version": 1})");
		REQUIRE(bare.ok);
		CHECK(bare.config.general.checkUpdatesOnStart);

		// Written again, it is a current file, and a choice made from now on stays.
		AppConfig changed = parsed.config;
		changed.general.checkUpdatesOnStart = false;
		const ConfigParseResult again = parseConfig(serializeConfig(changed));
		REQUIRE(again.ok);
		CHECK(again.fileSchemaVersion == kConfigSchemaVersion);
		CHECK_FALSE(again.config.general.checkUpdatesOnStart);
	}

	TEST_CASE("the real chain covers every version up to the current one")
	{
		// Each released schema version from 1 to current - 1 must have a step.
		for (int version = 1; version < kConfigSchemaVersion; ++version) {
			nlohmann::json root = {{"schema_version", version}};
			CAPTURE(version);
			CHECK(configMigrations().run(root, version, version + 1).ok);
		}
	}
}

TEST_SUITE("settings.store")
{
	TEST_CASE("a first start has no file and gives the default configuration")
	{
		TempDir dir;
		ConfigStore store(dir.path / "relaydock");
		const ConfigLoadResult loaded = store.load();
		CHECK(loaded.status == ConfigLoadStatus::CreatedDefault);
		CHECK(loaded.config.destinations.empty());
		CHECK_FALSE(std::filesystem::exists(store.configPath()));
	}

	TEST_CASE("save then load returns the same configuration")
	{
		TempDir dir;
		ConfigStore store(dir.path / "nested" / "relaydock");
		const AppConfig config = makeFullConfig();

		const ConfigSaveResult saved = store.save(config);
		REQUIRE(saved.ok);
		CHECK(std::filesystem::exists(store.configPath()));

		ConfigStore second(store.directory());
		const ConfigLoadResult loaded = second.load();
		CHECK(loaded.status == ConfigLoadStatus::Loaded);
		CHECK(loaded.config == config);
	}

	TEST_CASE("saving leaves no temporary file behind")
	{
		TempDir dir;
		ConfigStore store(dir.path);
		REQUIRE(store.save(makeDefaultConfig()).ok);
		REQUIRE(store.save(makeFullConfig()).ok);

		int files = 0;
		for (const auto &entry : std::filesystem::directory_iterator(dir.path)) {
			const std::string name = pathToUtf8(entry.path().filename());
			CAPTURE(name);
			CHECK(name.find(".tmp") == std::string::npos);
			++files;
		}
		CHECK(files == 2); // config.json and config.json.bak
	}

	TEST_CASE("the backup holds the configuration from before the last save")
	{
		TempDir dir;
		ConfigStore store(dir.path);
		AppConfig first = makeDefaultConfig();
		first.performanceMode = PerformanceMode::Potato;
		AppConfig second = first;
		second.performanceMode = PerformanceMode::Quality;

		REQUIRE(store.save(first).ok);
		REQUIRE(store.save(second).ok);

		const ConfigParseResult backup = parseConfig(readText(store.backupPath()));
		REQUIRE(backup.ok);
		CHECK(backup.config.performanceMode == PerformanceMode::Potato);
	}

	TEST_CASE("a damaged file is kept aside and the backup is used")
	{
		TempDir dir;
		ConfigStore store(dir.path);
		AppConfig first = makeDefaultConfig();
		first.network.uploadKbps = 12345;
		AppConfig second = first;
		second.network.uploadKbps = 54321;
		REQUIRE(store.save(first).ok);
		REQUIRE(store.save(second).ok);

		writeText(store.configPath(), "{ this is not json");

		ConfigStore reopened(dir.path);
		const ConfigLoadResult loaded = reopened.load();
		CHECK(loaded.status == ConfigLoadStatus::RecoveredFromBackup);
		CHECK(loaded.config.network.uploadKbps == 12345);
		REQUIRE_FALSE(loaded.preservedFile.empty());
		CHECK(std::filesystem::exists(loaded.preservedFile));
		CHECK(readText(loaded.preservedFile) == "{ this is not json");
		CHECK_FALSE(loaded.notes.empty());
	}

	TEST_CASE("a damaged file with no backup resets to defaults and still keeps the damaged file")
	{
		TempDir dir;
		ConfigStore store(dir.path);
		writeText(store.configPath(), "\x01\x02garbage");

		const ConfigLoadResult loaded = store.load();
		CHECK(loaded.status == ConfigLoadStatus::ResetAfterCorruption);
		CHECK(loaded.config.destinations.empty());
		CHECK(std::filesystem::exists(loaded.preservedFile));
		CHECK_FALSE(std::filesystem::exists(store.configPath()));
	}

	TEST_CASE("a missing file with a backup restores from the backup")
	{
		TempDir dir;
		ConfigStore store(dir.path);
		AppConfig config = makeDefaultConfig();
		config.network.uploadKbps = 7777;
		REQUIRE(store.save(config).ok);
		REQUIRE(store.save(config).ok);
		std::filesystem::remove(store.configPath());

		const ConfigLoadResult loaded = store.load();
		CHECK(loaded.status == ConfigLoadStatus::RecoveredFromBackup);
		CHECK(loaded.config.network.uploadKbps == 7777);
	}

	TEST_CASE("a damaged current file never replaces a good backup")
	{
		TempDir dir;
		ConfigStore store(dir.path);
		AppConfig good = makeDefaultConfig();
		good.network.uploadKbps = 4242;
		REQUIRE(store.save(good).ok);
		REQUIRE(store.save(good).ok);

		// The file is damaged while OBS runs, then RelayDock saves again.
		writeText(store.configPath(), "damaged");
		REQUIRE(store.save(good).ok);

		const ConfigParseResult backup = parseConfig(readText(store.backupPath()));
		REQUIRE(backup.ok);
		CHECK(backup.config.network.uploadKbps == 4242);
	}

	TEST_CASE("a file from a newer RelayDock is copied aside before this version overwrites it")
	{
		TempDir dir;
		ConfigStore store(dir.path);
		const std::string newer = R"({"schema_version": 999, "future_setting": {"keep": "me"}})";
		writeText(store.configPath(), newer);

		const ConfigLoadResult loaded = store.load();
		CHECK(loaded.status == ConfigLoadStatus::Loaded);
		CHECK(loaded.newerSchema);

		REQUIRE(store.save(loaded.config).ok);
		const std::filesystem::path preserved = dir.path / "config.newer-v999.json";
		REQUIRE(std::filesystem::exists(preserved));
		CHECK(readText(preserved) == newer);

		const ConfigParseResult current = parseConfig(readText(store.configPath()));
		REQUIRE(current.ok);
		CHECK(current.fileSchemaVersion == kConfigSchemaVersion);
	}

	TEST_CASE("an oversized file is treated as damaged")
	{
		TempDir dir;
		ConfigStore store(dir.path);
		writeText(store.configPath(), std::string(ConfigStore::kMaxFileBytes + 1, ' '));
		const ConfigLoadResult loaded = store.load();
		CHECK(loaded.status == ConfigLoadStatus::ResetAfterCorruption);
	}

	TEST_CASE("folders with non-English names work")
	{
		TempDir dir;
		const std::filesystem::path folder = dir.path / pathFromUtf8("Caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC");
		ConfigStore store(folder);
		AppConfig config = makeDefaultConfig();
		config.network.uploadKbps = 1234;
		REQUIRE(store.save(config).ok);
		const ConfigLoadResult loaded = ConfigStore(folder).load();
		CHECK(loaded.status == ConfigLoadStatus::Loaded);
		CHECK(loaded.config.network.uploadKbps == 1234);
		CHECK(pathToUtf8(pathFromUtf8("Caf\xC3\xA9")) == "Caf\xC3\xA9");
	}

	TEST_CASE("saving into a path that cannot be a folder reports an error")
	{
		TempDir dir;
		const std::filesystem::path blocker = dir.path / "not-a-folder";
		writeText(blocker, "file");
		ConfigStore store(blocker / "relaydock");
		const ConfigSaveResult saved = store.save(makeDefaultConfig());
		CHECK_FALSE(saved.ok);
		CHECK_FALSE(saved.error.empty());
	}
}
