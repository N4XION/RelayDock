// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "core/types.h"

#include <string>
#include <vector>

namespace rd {

// Everything RelayDock saves to its configuration file. No field here may hold a secret.
// Stream keys and passwords live in the credential store.

inline constexpr int kConfigSchemaVersion = 2;

enum class PerformanceMode { Potato, Balanced, Quality, Custom };
enum class OptimizationMode { Off, Suggest, Automatic };

const char *performanceModeName(PerformanceMode mode);
bool performanceModeFromName(const std::string &name, PerformanceMode &out);
const char *optimizationModeName(OptimizationMode mode);
bool optimizationModeFromName(const std::string &name, OptimizationMode &out);

struct OptimizerConfig {
	OptimizationMode mode = OptimizationMode::Suggest;

	// Shed load earlier and recover later, so the game keeps its frame rate.
	bool preventGameLag = false;

	// Automatic mode may apply changes that restart a destination's connection
	// (resolution, frame rate, encoder). Off means those changes stay suggestions.
	bool allowReconnectingChanges = false;

	bool operator==(const OptimizerConfig &other) const = default;
};

struct NetworkConfig {
	int uploadKbps = 0;     // The user's known upload speed. 0 means not set.
	int safetyPercent = 75; // Share of the upload speed RelayDock treats as safe to use.

	bool operator==(const NetworkConfig &other) const = default;
};

enum class ThemeMode { FollowObs, Light, Dark, Custom };
enum class Density { Compact, Comfortable };
enum class BackgroundFit { Fill, Fit, Stretch, Tile, Center };

const char *themeModeName(ThemeMode mode);
bool themeModeFromName(const std::string &name, ThemeMode &out);
const char *densityName(Density density);
bool densityFromName(const std::string &name, Density &out);
const char *backgroundFitName(BackgroundFit fit);
bool backgroundFitFromName(const std::string &name, BackgroundFit &out);

struct ThemeConfig {
	ThemeMode mode = ThemeMode::FollowObs;
	std::string accentColor;     // "#RRGGBB". Empty uses the OBS highlight colour.
	std::string backgroundColor; // "#RRGGBB". Empty uses the theme's own background.
	std::string backgroundImage; // File path. Empty means no image.
	BackgroundFit backgroundFit = BackgroundFit::Fill;
	int backgroundOpacity = 100; // Percent, applies to the background image.
	int cardOpacity = 100;       // Percent.
	int cornerRadius = 4;        // Pixels.
	int fontScale = 100;         // Percent of the OBS font size.
	Density density = Density::Comfortable;
	bool animations = true;
	bool reducedMotion = false;
	bool platformLogos = true;   // Badges show the platform's logo. Off: its initials.

	bool operator==(const ThemeConfig &other) const = default;
};

enum class CardView { Compact, Expanded };
enum class ListMode { List, Grid };
enum class ControlPlacement { Top, Bottom };

const char *cardViewName(CardView view);
bool cardViewFromName(const std::string &name, CardView &out);
const char *listModeName(ListMode mode);
bool listModeFromName(const std::string &name, ListMode &out);
const char *controlPlacementName(ControlPlacement placement);
bool controlPlacementFromName(const std::string &name, ControlPlacement &out);

// Ids of the sections the dock can show, in the order stored in DockLayout::sectionOrder.
namespace section {
inline constexpr const char *kDestinations = "destinations";
inline constexpr const char *kSuggestions = "suggestions";
inline constexpr const char *kPerformance = "performance";
inline constexpr const char *kNetwork = "network";
} // namespace section

std::vector<std::string> defaultSectionOrder();

// A saved arrangement of the dock.
struct DockLayout {
	std::string id;   // UUID
	std::string name; // Shown in the layout list
	std::vector<std::string> sectionOrder = defaultSectionOrder();
	bool showSuggestions = true;
	bool showPerformance = true;
	bool showNetwork = true;
	ControlPlacement controlPlacement = ControlPlacement::Top;
	CardView cardView = CardView::Expanded;
	ListMode listMode = ListMode::List;

	bool operator==(const DockLayout &other) const = default;
};

// A record that the user reviewed and agreed to one version of one legal document.
// Holds no credentials and nothing that identifies the user.
struct LegalAcceptance {
	std::string documentId;   // For example "terms-of-use"
	std::string version;      // Version of the document that was shown
	std::string acceptedAtUtc; // ISO 8601, UTC
	std::string appVersion;   // RelayDock version at the time

	bool operator==(const LegalAcceptance &other) const = default;
};

struct GeneralConfig {
	bool confirmStopAll = true;         // Ask before Stop All ends every stream.
	bool preflightOnStartAll = true;    // Run the preflight check before Start All Enabled.
	bool followObsStreaming = false;    // Start and stop with the OBS Start Streaming button.
	bool checkUpdatesOnStart = true;    // Ask GitHub for the latest release when OBS starts.
	std::string skippedUpdateVersion;   // "1.0.1": the user chose not to be told about this one.

	bool operator==(const GeneralConfig &other) const = default;
};

// Live chat. What is saved here is no secret: the Twitch sign-in and the YouTube key are in
// the credential store.
struct ChatConfig {
	bool twitchEnabled = true;   // Read Twitch chat whenever a sign-in is saved
	std::string twitchClientId;  // A Twitch application id of the user's own. Empty: the build's.
	std::string twitchLogin;     // Whose sign-in is saved, to show it
	bool youtubeEnabled = false; // Read YouTube chat. Connect switches it on, Disconnect off.
	std::string youtubeVideo;    // The link to the stream, or its video id, as the user entered it
	int youtubePollSeconds = 5;  // Shortest pause between two requests to YouTube
	bool showTime = true;        // Show the time in front of every line

	bool operator==(const ChatConfig &other) const = default;
};

inline constexpr int kChatPollSecondsMin = 3;
inline constexpr int kChatPollSecondsMax = 60;

// Size of the vertical canvas. Destinations with Vertical orientation scale from it.
struct VerticalCanvasConfig {
	int width = 1080;
	int height = 1920;

	bool operator==(const VerticalCanvasConfig &other) const = default;
};

struct AppConfig {
	int schemaVersion = kConfigSchemaVersion;

	std::vector<DestinationConfig> destinations; // In card order
	PerformanceMode performanceMode = PerformanceMode::Balanced;
	OptimizerConfig optimizer;
	NetworkConfig network;
	ThemeConfig theme;
	std::vector<DockLayout> layouts;
	std::string activeLayoutId;
	std::vector<LegalAcceptance> legal;
	GeneralConfig general;
	VerticalCanvasConfig verticalCanvas;
	ChatConfig chat;

	bool operator==(const AppConfig &other) const = default;

	DestinationConfig *findDestination(const std::string &id);
	const DestinationConfig *findDestination(const std::string &id) const;
	DockLayout *findLayout(const std::string &id);
	const DockLayout *findLayout(const std::string &id) const;

	// The active layout. Creates the default one when the list is empty or the id is stale.
	DockLayout &activeLayout();
};

// A configuration for a first start: no destinations, one default layout.
AppConfig makeDefaultConfig();

// Brings every value into its valid range and repairs references (duplicate ids, a stale
// active layout, unknown section names). Returns notes about what it changed.
std::vector<std::string> sanitizeConfig(AppConfig &config);

} // namespace rd
