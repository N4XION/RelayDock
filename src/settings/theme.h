// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "settings/app_config.h"

#include <string>
#include <string_view>

namespace rd {

// Turns the user's appearance settings into colours, sizes and a Qt style sheet.
//
// Everything here is plain arithmetic on colours, so it runs in unit tests without Qt. The
// plugin feeds it the colours of the current OBS theme and applies the result to RelayDock's
// own windows only. It never restyles OBS.
//
// How the modes work
//   Follow OBS  Background, text and accent come from the OBS theme. RelayDock only styles its
//               own parts (cards, status labels) to match.
//   Light, Dark RelayDock's built-in palettes.
//   Custom      You pick the background colour. RelayDock picks dark or light text to match.
//   The accent colour, when set, replaces the accent in every mode.
//
// Text must stay readable whatever the user picks, so every text colour is checked against its
// background and corrected until it reaches the WCAG AA contrast ratio of 4.5 to 1.

struct Rgb {
	int r = 0;
	int g = 0;
	int b = 0;

	bool operator==(const Rgb &other) const = default;
};

bool parseHexColor(std::string_view text, Rgb &out); // "#RRGGBB"
std::string toHex(Rgb color);

double relativeLuminance(Rgb color);
double contrastRatio(Rgb a, Rgb b);

// Blends from a (t = 0) to b (t = 1).
Rgb mixColors(Rgb a, Rgb b, double t);

// Black or white, whichever reads better on the background.
Rgb readableOn(Rgb background);

// Moves `text` towards black or white until it has at least `ratio` contrast on `background`.
Rgb ensureContrast(Rgb text, Rgb background, double ratio = 4.5);

// Colours of the OBS theme in use, read from Qt by the plugin.
struct BasePalette {
	Rgb window{0x1f, 0x21, 0x25};
	Rgb text{0xe6, 0xe6, 0xe6};
	Rgb highlight{0x28, 0x4c, 0xb8};
	int fontPx = 13;
};

struct ThemeColors {
	bool dark = true;
	bool followsObs = true; // True when OBS's own styling of standard controls is kept
	Rgb background;
	Rgb card;
	Rgb cardBorder;
	Rgb hover;
	Rgb input;
	Rgb text;
	Rgb mutedText;
	Rgb accent;
	Rgb accentText;
	Rgb ok;      // Live, ready
	Rgb warning; // Connecting, reconnecting, warnings
	Rgb error;   // Failed
};

struct ThemeMetrics {
	int fontPx = 13;
	int smallFontPx = 11;
	int radius = 4;
	int padding = 10;      // Inside cards
	int spacing = 8;       // Between controls
	int iconPx = 16;
	int controlHeight = 26;
	int cardOpacity = 100; // Percent
};

ThemeColors resolveThemeColors(const ThemeConfig &theme, const BasePalette &base);
ThemeMetrics resolveThemeMetrics(const ThemeConfig &theme, const BasePalette &base);

// The style sheet for a RelayDock window. Set it on the window's root widget.
std::string buildStyleSheet(const ThemeColors &colors, const ThemeMetrics &metrics);

// Whether the interface may animate. Potato Mode and reduced motion both switch it off.
bool animationsAllowed(const ThemeConfig &theme, PerformanceMode mode);

} // namespace rd
