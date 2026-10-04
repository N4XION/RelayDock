// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "settings/app_config.h"

#include "providers/provider_base.h"
#include "utils/strings.h"
#include "utils/uuid.h"

#include <algorithm>
#include <format>
#include <set>

namespace rd {

// ---- Enum names ------------------------------------------------------------------------------
// These strings are written to the configuration file. Changing one needs a migration.

const char *performanceModeName(PerformanceMode mode)
{
	switch (mode) {
	case PerformanceMode::Potato:
		return "potato";
	case PerformanceMode::Balanced:
		return "balanced";
	case PerformanceMode::Quality:
		return "quality";
	case PerformanceMode::Custom:
		return "custom";
	}
	return "balanced";
}

bool performanceModeFromName(const std::string &name, PerformanceMode &out)
{
	for (PerformanceMode mode : {PerformanceMode::Potato, PerformanceMode::Balanced, PerformanceMode::Quality,
				     PerformanceMode::Custom}) {
		if (name == performanceModeName(mode)) {
			out = mode;
			return true;
		}
	}
	return false;
}

const char *optimizationModeName(OptimizationMode mode)
{
	switch (mode) {
	case OptimizationMode::Off:
		return "off";
	case OptimizationMode::Suggest:
		return "suggest";
	case OptimizationMode::Automatic:
		return "automatic";
	}
	return "suggest";
}

bool optimizationModeFromName(const std::string &name, OptimizationMode &out)
{
	for (OptimizationMode mode : {OptimizationMode::Off, OptimizationMode::Suggest, OptimizationMode::Automatic}) {
		if (name == optimizationModeName(mode)) {
			out = mode;
			return true;
		}
	}
	return false;
}

const char *themeModeName(ThemeMode mode)
{
	switch (mode) {
	case ThemeMode::FollowObs:
		return "follow_obs";
	case ThemeMode::Light:
		return "light";
	case ThemeMode::Dark:
		return "dark";
	case ThemeMode::Custom:
		return "custom";
	}
	return "follow_obs";
}

bool themeModeFromName(const std::string &name, ThemeMode &out)
{
	for (ThemeMode mode : {ThemeMode::FollowObs, ThemeMode::Light, ThemeMode::Dark, ThemeMode::Custom}) {
		if (name == themeModeName(mode)) {
			out = mode;
			return true;
		}
	}
	return false;
}

const char *densityName(Density density)
{
	return density == Density::Compact ? "compact" : "comfortable";
}

bool densityFromName(const std::string &name, Density &out)
{
	if (name == "compact") {
		out = Density::Compact;
		return true;
	}
	if (name == "comfortable") {
		out = Density::Comfortable;
		return true;
	}
	return false;
}

const char *backgroundFitName(BackgroundFit fit)
{
	switch (fit) {
	case BackgroundFit::Fill:
		return "fill";
	case BackgroundFit::Fit:
		return "fit";
	case BackgroundFit::Stretch:
		return "stretch";
	case BackgroundFit::Tile:
		return "tile";
	case BackgroundFit::Center:
		return "center";
	}
	return "fill";
}

bool backgroundFitFromName(const std::string &name, BackgroundFit &out)
{
	for (BackgroundFit fit : {BackgroundFit::Fill, BackgroundFit::Fit, BackgroundFit::Stretch, BackgroundFit::Tile,
				  BackgroundFit::Center}) {
		if (name == backgroundFitName(fit)) {
			out = fit;
			return true;
		}
	}
	return false;
}

const char *cardViewName(CardView view)
{
	return view == CardView::Compact ? "compact" : "expanded";
}

bool cardViewFromName(const std::string &name, CardView &out)
{
	if (name == "compact") {
		out = CardView::Compact;
		return true;
	}
	if (name == "expanded") {
		out = CardView::Expanded;
		return true;
	}
	return false;
}

const char *listModeName(ListMode mode)
{
	return mode == ListMode::Grid ? "grid" : "list";
}

bool listModeFromName(const std::string &name, ListMode &out)
{
	if (name == "grid") {
		out = ListMode::Grid;
		return true;
	}
	if (name == "list") {
		out = ListMode::List;
		return true;
	}
	return false;
}

const char *controlPlacementName(ControlPlacement placement)
{
	return placement == ControlPlacement::Bottom ? "bottom" : "top";
}

bool controlPlacementFromName(const std::string &name, ControlPlacement &out)
{
	if (name == "bottom") {
		out = ControlPlacement::Bottom;
		return true;
	}
	if (name == "top") {
		out = ControlPlacement::Top;
		return true;
	}
	return false;
}

// ---- Defaults --------------------------------------------------------------------------------

std::vector<std::string> defaultSectionOrder()
{
	return {section::kDestinations, section::kSuggestions, section::kPerformance, section::kNetwork};
}

namespace {

DockLayout makeDefaultLayout()
{
	DockLayout layout;
	layout.id = generateUuid();
	layout.name = "Default";
	return layout;
}

bool isHexColor(const std::string &text)
{
	if (text.size() != 7 || text[0] != '#')
		return false;
	for (size_t i = 1; i < text.size(); ++i) {
		const char c = text[i];
		const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
		if (!hex)
			return false;
	}
	return true;
}

// Clamps `value` and records a note when it had to change.
void clampField(int &value, int low, int high, const std::string &label, std::vector<std::string> &notes)
{
	const int clamped = std::clamp(value, low, high);
	if (clamped != value) {
		notes.push_back(std::format("{} was {} and is now {}.", label, value, clamped));
		value = clamped;
	}
}

} // namespace

AppConfig makeDefaultConfig()
{
	AppConfig config;
	config.layouts.push_back(makeDefaultLayout());
	config.activeLayoutId = config.layouts.front().id;
	return config;
}

// ---- Lookups ---------------------------------------------------------------------------------

DestinationConfig *AppConfig::findDestination(const std::string &id)
{
	for (DestinationConfig &destination : destinations) {
		if (destination.id == id)
			return &destination;
	}
	return nullptr;
}

const DestinationConfig *AppConfig::findDestination(const std::string &id) const
{
	return const_cast<AppConfig *>(this)->findDestination(id);
}

DockLayout *AppConfig::findLayout(const std::string &id)
{
	for (DockLayout &layout : layouts) {
		if (layout.id == id)
			return &layout;
	}
	return nullptr;
}

const DockLayout *AppConfig::findLayout(const std::string &id) const
{
	return const_cast<AppConfig *>(this)->findLayout(id);
}

DockLayout &AppConfig::activeLayout()
{
	if (DockLayout *layout = findLayout(activeLayoutId))
		return *layout;
	if (layouts.empty())
		layouts.push_back(makeDefaultLayout());
	activeLayoutId = layouts.front().id;
	return layouts.front();
}

// ---- Sanitising ------------------------------------------------------------------------------

std::vector<std::string> sanitizeConfig(AppConfig &config)
{
	std::vector<std::string> notes;

	// Destinations
	std::set<std::string> seenIds;
	for (DestinationConfig &destination : config.destinations) {
		const std::string label = destination.name.empty() ? std::string("A destination") : destination.name;

		if (!isUuid(destination.id) || !seenIds.insert(toLower(destination.id)).second) {
			destination.id = generateUuid();
			seenIds.insert(destination.id);
			notes.push_back(std::format("{} had a missing or duplicate id and received a new one.", label));
		}
		if (trim(destination.name).empty()) {
			destination.name = destination.provider.empty() ? std::string("Destination") : destination.provider;
			notes.push_back("A destination had no name and received one.");
		}

		VideoSettings &video = destination.video;
		clampField(video.bitrateKbps, range::kMinVideoBitrateKbps, range::kMaxVideoBitrateKbps,
			   label + ": video bitrate", notes);
		if ((video.width == 0) != (video.height == 0)) {
			video.width = 0;
			video.height = 0;
			notes.push_back(std::format("{}: an incomplete resolution was reset to the canvas size.", label));
		}
		if (video.width != 0) {
			clampField(video.width, range::kMinDimension, range::kMaxWidth, label + ": width", notes);
			clampField(video.height, range::kMinDimension, range::kMaxHeight, label + ": height", notes);
			if (video.width % 2 != 0)
				++video.width;
			if (video.height % 2 != 0)
				++video.height;
		}
		clampField(video.fps, 0, range::kMaxFps, label + ": frame rate", notes);
		clampField(video.keyframeIntervalSec, 0, range::kMaxKeyframeIntervalSec, label + ": keyframe interval",
			   notes);
		clampField(video.bFrames, -1, range::kMaxBFrames, label + ": B-frames", notes);
		if (video.encoder.empty())
			video.encoder = kAutoEncoder;
		if (video.rateControl.empty())
			video.rateControl = "CBR";

		AudioSettings &audio = destination.audio;
		clampField(audio.bitrateKbps, range::kMinAudioBitrateKbps, range::kMaxAudioBitrateKbps,
			   label + ": audio bitrate", notes);
		clampField(audio.track, 1, range::kMaxAudioTrack, label + ": audio track", notes);
		if (audio.encoder.empty())
			audio.encoder = kAutoEncoder;

		ConnectionSettings &connection = destination.connection;
		clampField(connection.reconnectAttempts, 1, range::kMaxReconnectAttempts, label + ": reconnect attempts",
			   notes);
		clampField(connection.reconnectDelaySec, 1, range::kMaxReconnectDelaySec, label + ": reconnect delay",
			   notes);
		clampField(connection.streamDelaySec, 0, range::kMaxStreamDelaySec, label + ": stream delay", notes);
	}

	// Network
	clampField(config.network.uploadKbps, 0, 10000000, "Upload speed", notes);
	clampField(config.network.safetyPercent, 10, 100, "Safe share of upload speed", notes);

	// Theme
	ThemeConfig &theme = config.theme;
	if (!theme.accentColor.empty() && !isHexColor(theme.accentColor)) {
		theme.accentColor.clear();
		notes.push_back("The accent colour was not a #RRGGBB value and was reset.");
	}
	if (!theme.backgroundColor.empty() && !isHexColor(theme.backgroundColor)) {
		theme.backgroundColor.clear();
		notes.push_back("The background colour was not a #RRGGBB value and was reset.");
	}
	clampField(theme.backgroundOpacity, 0, 100, "Background opacity", notes);
	clampField(theme.cardOpacity, 20, 100, "Card opacity", notes);
	clampField(theme.cornerRadius, 0, 16, "Corner radius", notes);
	clampField(theme.fontScale, 80, 150, "Font scale", notes);

	// Layouts
	static const std::vector<std::string> knownSections = defaultSectionOrder();
	std::set<std::string> seenLayouts;
	for (DockLayout &layout : config.layouts) {
		if (!isUuid(layout.id) || !seenLayouts.insert(toLower(layout.id)).second) {
			const bool wasActive = layout.id == config.activeLayoutId;
			layout.id = generateUuid();
			seenLayouts.insert(layout.id);
			if (wasActive)
				config.activeLayoutId = layout.id;
			notes.push_back("A layout had a missing or duplicate id and received a new one.");
		}
		if (trim(layout.name).empty())
			layout.name = "Layout";

		std::vector<std::string> order;
		for (const std::string &name : layout.sectionOrder) {
			const bool known = std::find(knownSections.begin(), knownSections.end(), name) != knownSections.end();
			const bool duplicate = std::find(order.begin(), order.end(), name) != order.end();
			if (known && !duplicate)
				order.push_back(name);
		}
		for (const std::string &name : knownSections) {
			if (std::find(order.begin(), order.end(), name) == order.end())
				order.push_back(name);
		}
		layout.sectionOrder = std::move(order);
	}
	if (config.layouts.empty()) {
		config.layouts.push_back(makeDefaultLayout());
		notes.push_back("No layout was saved, so the default layout was created.");
	}
	if (!config.findLayout(config.activeLayoutId))
		config.activeLayoutId = config.layouts.front().id;

	// Legal records
	const size_t legalBefore = config.legal.size();
	std::erase_if(config.legal, [](const LegalAcceptance &record) {
		return record.documentId.empty() || record.version.empty();
	});
	if (config.legal.size() != legalBefore)
		notes.push_back("Incomplete legal acceptance records were removed.");

	// Vertical canvas
	VerticalCanvasConfig &vertical = config.verticalCanvas;
	const bool verticalValid = vertical.width >= range::kMinDimension && vertical.height >= range::kMinDimension &&
				   vertical.width <= 4320 && vertical.height <= range::kMaxHeight &&
				   vertical.width <= vertical.height && vertical.width % 2 == 0 &&
				   vertical.height % 2 == 0;
	if (!verticalValid) {
		notes.push_back(std::format("The vertical canvas size {}x{} is not valid and was reset to 1080x1920.",
					    vertical.width, vertical.height));
		vertical = VerticalCanvasConfig{};
	}

	// Chat
	ChatConfig &chat = config.chat;
	clampField(chat.youtubePollSeconds, kChatPollSecondsMin, kChatPollSecondsMax, "The pause between YouTube chat requests", notes);
	// An application id is letters and digits. Anything else is not sent anywhere.
	const bool clientIdValid = chat.twitchClientId.size() <= 64 &&
				   std::all_of(chat.twitchClientId.begin(), chat.twitchClientId.end(), [](char c) {
					   return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
				   });
	if (!clientIdValid) {
		chat.twitchClientId.clear();
		notes.push_back("The Twitch application id was not valid and was removed.");
	}
	if (chat.youtubeVideo.size() > 300)
		chat.youtubeVideo.clear();
	if (chat.twitchLogin.size() > 64)
		chat.twitchLogin.clear();

	config.schemaVersion = kConfigSchemaVersion;
	return notes;
}

} // namespace rd
