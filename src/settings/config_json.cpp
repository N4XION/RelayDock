// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "settings/config_json.h"

#include "settings/migrations.h"

#include <nlohmann/json.hpp>

#include <format>
#include <limits>

namespace rd {

namespace {

using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

// ---- Tolerant readers ------------------------------------------------------------------------

template <class Json> std::string readString(const Json &object, const char *key, const std::string &fallback = {})
{
	if (!object.is_object())
		return fallback;
	const auto it = object.find(key);
	if (it == object.end() || !it->is_string())
		return fallback;
	return it->template get<std::string>();
}

template <class Json> int readInt(const Json &object, const char *key, int fallback)
{
	if (!object.is_object())
		return fallback;
	const auto it = object.find(key);
	if (it == object.end() || !it->is_number())
		return fallback;
	const double value = it->template get<double>();
	if (value < static_cast<double>(std::numeric_limits<int>::min()) ||
	    value > static_cast<double>(std::numeric_limits<int>::max()))
		return fallback;
	return static_cast<int>(value);
}

template <class Json> bool readBool(const Json &object, const char *key, bool fallback)
{
	if (!object.is_object())
		return fallback;
	const auto it = object.find(key);
	if (it == object.end() || !it->is_boolean())
		return fallback;
	return it->template get<bool>();
}

template <class Json> const Json &readObject(const Json &object, const char *key)
{
	static const Json empty = Json::object();
	if (!object.is_object())
		return empty;
	const auto it = object.find(key);
	if (it == object.end() || !it->is_object())
		return empty;
	return *it;
}

template <class Json> const Json &readArray(const Json &object, const char *key)
{
	static const Json empty = Json::array();
	if (!object.is_object())
		return empty;
	const auto it = object.find(key);
	if (it == object.end() || !it->is_array())
		return empty;
	return *it;
}

// Reads an enum stored by name. Keeps the current value when the name is unknown.
template <class Json, class Enum>
void readEnum(const Json &object, const char *key, Enum &value, bool (*fromName)(const std::string &, Enum &))
{
	const std::string name = readString(object, key);
	if (!name.empty())
		fromName(name, value);
}

// ---- Destination -----------------------------------------------------------------------------

ordered_json destinationToJson(const DestinationConfig &d)
{
	ordered_json j;
	j["id"] = d.id;
	j["provider"] = d.provider;
	j["name"] = d.name;
	j["enabled"] = d.enabled;
	j["server_id"] = d.serverId;
	j["server_url"] = d.serverUrl;
	j["use_auth"] = d.useAuth;
	j["username"] = d.username;

	ordered_json video;
	video["orientation"] = orientationName(d.video.orientation);
	video["width"] = d.video.width;
	video["height"] = d.video.height;
	video["fps"] = d.video.fps;
	video["encoder"] = d.video.encoder;
	video["bitrate_kbps"] = d.video.bitrateKbps;
	video["rate_control"] = d.video.rateControl;
	video["preset"] = d.video.preset;
	video["profile"] = d.video.profile;
	video["keyframe_interval_sec"] = d.video.keyframeIntervalSec;
	video["b_frames"] = d.video.bFrames;
	video["custom_options"] = d.video.customOptions;
	j["video"] = std::move(video);

	ordered_json audio;
	audio["encoder"] = d.audio.encoder;
	audio["bitrate_kbps"] = d.audio.bitrateKbps;
	audio["track"] = d.audio.track;
	j["audio"] = std::move(audio);

	ordered_json connection;
	connection["auto_reconnect"] = d.connection.autoReconnect;
	connection["reconnect_attempts"] = d.connection.reconnectAttempts;
	connection["reconnect_delay_sec"] = d.connection.reconnectDelaySec;
	connection["stream_delay_sec"] = d.connection.streamDelaySec;
	connection["bind_ip"] = d.connection.bindIp;
	j["connection"] = std::move(connection);

	ordered_json locks;
	locks["resolution"] = d.locks.resolution;
	locks["fps"] = d.locks.fps;
	locks["bitrate"] = d.locks.bitrate;
	locks["encoder"] = d.locks.encoder;
	locks["aspect_ratio"] = d.locks.aspectRatio;
	j["locks"] = std::move(locks);

	j["auto_optimize"] = d.autoOptimize;
	j["vertical_layout_id"] = d.verticalLayoutId;
	return j;
}

template <class Json> DestinationConfig destinationFromJson(const Json &j)
{
	DestinationConfig d;
	d.id = readString(j, "id");
	d.provider = readString(j, "provider");
	d.name = readString(j, "name");
	d.enabled = readBool(j, "enabled", d.enabled);
	d.serverId = readString(j, "server_id");
	d.serverUrl = readString(j, "server_url");
	d.useAuth = readBool(j, "use_auth", d.useAuth);
	d.username = readString(j, "username");

	const Json &video = readObject(j, "video");
	readEnum<Json, Orientation>(video, "orientation", d.video.orientation,
				    [](const std::string &name, Orientation &out) {
					    return orientationFromName(name, out);
				    });
	d.video.width = readInt(video, "width", d.video.width);
	d.video.height = readInt(video, "height", d.video.height);
	d.video.fps = readInt(video, "fps", d.video.fps);
	d.video.encoder = readString(video, "encoder", d.video.encoder);
	d.video.bitrateKbps = readInt(video, "bitrate_kbps", d.video.bitrateKbps);
	d.video.rateControl = readString(video, "rate_control", d.video.rateControl);
	d.video.preset = readString(video, "preset");
	d.video.profile = readString(video, "profile");
	d.video.keyframeIntervalSec = readInt(video, "keyframe_interval_sec", d.video.keyframeIntervalSec);
	d.video.bFrames = readInt(video, "b_frames", d.video.bFrames);
	d.video.customOptions = readString(video, "custom_options");

	const Json &audio = readObject(j, "audio");
	d.audio.encoder = readString(audio, "encoder", d.audio.encoder);
	d.audio.bitrateKbps = readInt(audio, "bitrate_kbps", d.audio.bitrateKbps);
	d.audio.track = readInt(audio, "track", d.audio.track);

	const Json &connection = readObject(j, "connection");
	d.connection.autoReconnect = readBool(connection, "auto_reconnect", d.connection.autoReconnect);
	d.connection.reconnectAttempts = readInt(connection, "reconnect_attempts", d.connection.reconnectAttempts);
	d.connection.reconnectDelaySec = readInt(connection, "reconnect_delay_sec", d.connection.reconnectDelaySec);
	d.connection.streamDelaySec = readInt(connection, "stream_delay_sec", d.connection.streamDelaySec);
	d.connection.bindIp = readString(connection, "bind_ip");

	const Json &locks = readObject(j, "locks");
	d.locks.resolution = readBool(locks, "resolution", d.locks.resolution);
	d.locks.fps = readBool(locks, "fps", d.locks.fps);
	d.locks.bitrate = readBool(locks, "bitrate", d.locks.bitrate);
	d.locks.encoder = readBool(locks, "encoder", d.locks.encoder);
	d.locks.aspectRatio = readBool(locks, "aspect_ratio", d.locks.aspectRatio);

	d.autoOptimize = readBool(j, "auto_optimize", d.autoOptimize);
	d.verticalLayoutId = readString(j, "vertical_layout_id");
	return d;
}

} // namespace

// ---- Whole configuration -----------------------------------------------------------------------

std::string serializeConfig(const AppConfig &config)
{
	ordered_json root;
	root["schema_version"] = kConfigSchemaVersion;

	ordered_json destinations = ordered_json::array();
	for (const DestinationConfig &destination : config.destinations)
		destinations.push_back(destinationToJson(destination));
	root["destinations"] = std::move(destinations);

	root["performance_mode"] = performanceModeName(config.performanceMode);

	ordered_json optimizer;
	optimizer["mode"] = optimizationModeName(config.optimizer.mode);
	optimizer["prevent_game_lag"] = config.optimizer.preventGameLag;
	optimizer["allow_reconnecting_changes"] = config.optimizer.allowReconnectingChanges;
	root["optimizer"] = std::move(optimizer);

	ordered_json network;
	network["upload_kbps"] = config.network.uploadKbps;
	network["safety_percent"] = config.network.safetyPercent;
	root["network"] = std::move(network);

	ordered_json theme;
	theme["mode"] = themeModeName(config.theme.mode);
	theme["accent_color"] = config.theme.accentColor;
	theme["background_color"] = config.theme.backgroundColor;
	theme["background_image"] = config.theme.backgroundImage;
	theme["background_fit"] = backgroundFitName(config.theme.backgroundFit);
	theme["background_opacity"] = config.theme.backgroundOpacity;
	theme["card_opacity"] = config.theme.cardOpacity;
	theme["corner_radius"] = config.theme.cornerRadius;
	theme["font_scale"] = config.theme.fontScale;
	theme["density"] = densityName(config.theme.density);
	theme["animations"] = config.theme.animations;
	theme["reduced_motion"] = config.theme.reducedMotion;
	theme["platform_logos"] = config.theme.platformLogos;
	root["theme"] = std::move(theme);

	ordered_json layouts = ordered_json::array();
	for (const DockLayout &layout : config.layouts) {
		ordered_json j;
		j["id"] = layout.id;
		j["name"] = layout.name;
		j["section_order"] = layout.sectionOrder;
		j["show_suggestions"] = layout.showSuggestions;
		j["show_performance"] = layout.showPerformance;
		j["show_network"] = layout.showNetwork;
		j["control_placement"] = controlPlacementName(layout.controlPlacement);
		j["card_view"] = cardViewName(layout.cardView);
		j["list_mode"] = listModeName(layout.listMode);
		layouts.push_back(std::move(j));
	}
	root["layouts"] = std::move(layouts);
	root["active_layout_id"] = config.activeLayoutId;

	ordered_json legal = ordered_json::array();
	for (const LegalAcceptance &record : config.legal) {
		ordered_json j;
		j["document_id"] = record.documentId;
		j["version"] = record.version;
		j["accepted_at_utc"] = record.acceptedAtUtc;
		j["app_version"] = record.appVersion;
		legal.push_back(std::move(j));
	}
	root["legal"] = std::move(legal);

	ordered_json general;
	general["confirm_stop_all"] = config.general.confirmStopAll;
	general["preflight_on_start_all"] = config.general.preflightOnStartAll;
	general["follow_obs_streaming"] = config.general.followObsStreaming;
	general["check_updates_on_start"] = config.general.checkUpdatesOnStart;
	general["skipped_update_version"] = config.general.skippedUpdateVersion;
	root["general"] = std::move(general);

	ordered_json vertical;
	vertical["width"] = config.verticalCanvas.width;
	vertical["height"] = config.verticalCanvas.height;
	root["vertical_canvas"] = std::move(vertical);

	return root.dump(2) + "\n";
}

ConfigParseResult parseConfig(std::string_view jsonText)
{
	ConfigParseResult result;

	json root = json::parse(jsonText, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
	if (root.is_discarded()) {
		result.error = "The configuration file is not valid JSON.";
		return result;
	}
	if (!root.is_object()) {
		result.error = "The configuration file does not contain a JSON object.";
		return result;
	}

	result.fileSchemaVersion = readInt(root, "schema_version", 0);
	if (result.fileSchemaVersion < 1) {
		result.error = "The configuration file has no schema version.";
		return result;
	}

	if (result.fileSchemaVersion > kConfigSchemaVersion) {
		// Written by a newer RelayDock. Read what this version understands and say so.
		result.newerSchema = true;
		result.notes.push_back(std::format(
			"The configuration was written by a newer RelayDock (format {}). This version reads format {}. Settings it does not know are ignored.",
			result.fileSchemaVersion, kConfigSchemaVersion));
	} else if (result.fileSchemaVersion < kConfigSchemaVersion) {
		const MigrationChain::Result migrated =
			configMigrations().run(root, result.fileSchemaVersion, kConfigSchemaVersion);
		if (!migrated.ok) {
			result.error = migrated.error;
			return result;
		}
		for (const std::string &description : migrated.applied)
			result.notes.push_back("Migrated: " + description);
	}

	AppConfig &config = result.config;

	for (const json &entry : readArray(root, "destinations")) {
		if (entry.is_object())
			config.destinations.push_back(destinationFromJson(entry));
	}

	readEnum<json, PerformanceMode>(root, "performance_mode", config.performanceMode, performanceModeFromName);

	const json &optimizer = readObject(root, "optimizer");
	readEnum<json, OptimizationMode>(optimizer, "mode", config.optimizer.mode, optimizationModeFromName);
	config.optimizer.preventGameLag = readBool(optimizer, "prevent_game_lag", config.optimizer.preventGameLag);
	config.optimizer.allowReconnectingChanges =
		readBool(optimizer, "allow_reconnecting_changes", config.optimizer.allowReconnectingChanges);

	const json &network = readObject(root, "network");
	config.network.uploadKbps = readInt(network, "upload_kbps", config.network.uploadKbps);
	config.network.safetyPercent = readInt(network, "safety_percent", config.network.safetyPercent);

	const json &theme = readObject(root, "theme");
	readEnum<json, ThemeMode>(theme, "mode", config.theme.mode, themeModeFromName);
	config.theme.accentColor = readString(theme, "accent_color");
	config.theme.backgroundColor = readString(theme, "background_color");
	config.theme.backgroundImage = readString(theme, "background_image");
	readEnum<json, BackgroundFit>(theme, "background_fit", config.theme.backgroundFit, backgroundFitFromName);
	config.theme.backgroundOpacity = readInt(theme, "background_opacity", config.theme.backgroundOpacity);
	config.theme.cardOpacity = readInt(theme, "card_opacity", config.theme.cardOpacity);
	config.theme.cornerRadius = readInt(theme, "corner_radius", config.theme.cornerRadius);
	config.theme.fontScale = readInt(theme, "font_scale", config.theme.fontScale);
	readEnum<json, Density>(theme, "density", config.theme.density, densityFromName);
	config.theme.animations = readBool(theme, "animations", config.theme.animations);
	config.theme.reducedMotion = readBool(theme, "reduced_motion", config.theme.reducedMotion);
	config.theme.platformLogos = readBool(theme, "platform_logos", config.theme.platformLogos);

	for (const json &entry : readArray(root, "layouts")) {
		if (!entry.is_object())
			continue;
		DockLayout layout;
		layout.id = readString(entry, "id");
		layout.name = readString(entry, "name");
		layout.sectionOrder.clear();
		for (const json &name : readArray(entry, "section_order")) {
			if (name.is_string())
				layout.sectionOrder.push_back(name.get<std::string>());
		}
		layout.showSuggestions = readBool(entry, "show_suggestions", layout.showSuggestions);
		layout.showPerformance = readBool(entry, "show_performance", layout.showPerformance);
		layout.showNetwork = readBool(entry, "show_network", layout.showNetwork);
		readEnum<json, ControlPlacement>(entry, "control_placement", layout.controlPlacement,
						 controlPlacementFromName);
		readEnum<json, CardView>(entry, "card_view", layout.cardView, cardViewFromName);
		readEnum<json, ListMode>(entry, "list_mode", layout.listMode, listModeFromName);
		config.layouts.push_back(std::move(layout));
	}
	config.activeLayoutId = readString(root, "active_layout_id");

	for (const json &entry : readArray(root, "legal")) {
		if (!entry.is_object())
			continue;
		LegalAcceptance record;
		record.documentId = readString(entry, "document_id");
		record.version = readString(entry, "version");
		record.acceptedAtUtc = readString(entry, "accepted_at_utc");
		record.appVersion = readString(entry, "app_version");
		config.legal.push_back(std::move(record));
	}

	const json &general = readObject(root, "general");
	config.general.confirmStopAll = readBool(general, "confirm_stop_all", config.general.confirmStopAll);
	config.general.preflightOnStartAll =
		readBool(general, "preflight_on_start_all", config.general.preflightOnStartAll);
	config.general.followObsStreaming =
		readBool(general, "follow_obs_streaming", config.general.followObsStreaming);
	config.general.checkUpdatesOnStart =
		readBool(general, "check_updates_on_start", config.general.checkUpdatesOnStart);
	config.general.skippedUpdateVersion = readString(general, "skipped_update_version");

	const json &vertical = readObject(root, "vertical_canvas");
	config.verticalCanvas.width = readInt(vertical, "width", config.verticalCanvas.width);
	config.verticalCanvas.height = readInt(vertical, "height", config.verticalCanvas.height);

	for (std::string &note : sanitizeConfig(config))
		result.notes.push_back(std::move(note));

	result.ok = true;
	return result;
}

std::string serializeDestination(const DestinationConfig &destination)
{
	return destinationToJson(destination).dump(2);
}

bool parseDestination(std::string_view jsonText, DestinationConfig &out)
{
	const json root = json::parse(jsonText, nullptr, false, true);
	if (root.is_discarded() || !root.is_object())
		return false;
	out = destinationFromJson(root);
	return true;
}

} // namespace rd
