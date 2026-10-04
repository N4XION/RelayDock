// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "settings/theme.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace rd {

namespace {

constexpr Rgb kBlack{0, 0, 0};
constexpr Rgb kWhite{255, 255, 255};

int hexDigit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

double channelLuminance(int value)
{
	const double c = value / 255.0;
	return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

std::string rgba(Rgb color, int opacityPercent)
{
	return std::format("rgba({}, {}, {}, {})", color.r, color.g, color.b, std::clamp(opacityPercent, 0, 100) * 255 / 100);
}

} // namespace

bool parseHexColor(std::string_view text, Rgb &out)
{
	if (text.size() != 7 || text[0] != '#')
		return false;
	int values[6];
	for (int i = 0; i < 6; ++i) {
		values[i] = hexDigit(text[static_cast<size_t>(i) + 1]);
		if (values[i] < 0)
			return false;
	}
	out = {values[0] * 16 + values[1], values[2] * 16 + values[3], values[4] * 16 + values[5]};
	return true;
}

std::string toHex(Rgb color)
{
	return std::format("#{:02x}{:02x}{:02x}", std::clamp(color.r, 0, 255), std::clamp(color.g, 0, 255),
			   std::clamp(color.b, 0, 255));
}

double relativeLuminance(Rgb color)
{
	return 0.2126 * channelLuminance(color.r) + 0.7152 * channelLuminance(color.g) + 0.0722 * channelLuminance(color.b);
}

double contrastRatio(Rgb a, Rgb b)
{
	const double la = relativeLuminance(a);
	const double lb = relativeLuminance(b);
	return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

Rgb mixColors(Rgb a, Rgb b, double t)
{
	t = std::clamp(t, 0.0, 1.0);
	auto blend = [t](int from, int to) { return static_cast<int>(std::lround(from + (to - from) * t)); };
	return {blend(a.r, b.r), blend(a.g, b.g), blend(a.b, b.b)};
}

Rgb readableOn(Rgb background)
{
	return contrastRatio(kWhite, background) >= contrastRatio(kBlack, background) ? kWhite : kBlack;
}

Rgb ensureContrast(Rgb text, Rgb background, double ratio)
{
	if (contrastRatio(text, background) >= ratio)
		return text;
	const Rgb target = readableOn(background);
	for (int step = 1; step <= 20; ++step) {
		const Rgb candidate = mixColors(text, target, step / 20.0);
		if (contrastRatio(candidate, background) >= ratio)
			return candidate;
	}
	return target;
}

ThemeColors resolveThemeColors(const ThemeConfig &theme, const BasePalette &base)
{
	ThemeColors colors;

	Rgb background = base.window;
	Rgb text = base.text;
	Rgb accent = base.highlight;
	colors.followsObs = theme.mode == ThemeMode::FollowObs;

	switch (theme.mode) {
	case ThemeMode::FollowObs:
		break;
	case ThemeMode::Dark:
		background = {0x1e, 0x1f, 0x22};
		text = {0xe8, 0xe8, 0xe8};
		accent = {0xe8, 0xe8, 0xe8}; // A neutral accent. Status colours carry the meaning.
		break;
	case ThemeMode::Light:
		background = {0xf4, 0xf4, 0xf2};
		text = {0x1c, 0x1d, 0x1f};
		accent = {0x26, 0x28, 0x2c};
		break;
	case ThemeMode::Custom: {
		Rgb picked;
		background = parseHexColor(theme.backgroundColor, picked) ? picked : Rgb{0x1e, 0x1f, 0x22};
		text = readableOn(background) == kWhite ? Rgb{0xe8, 0xe8, 0xe8} : Rgb{0x1c, 0x1d, 0x1f};
		accent = text;
		break;
	}
	}

	Rgb pickedAccent;
	if (parseHexColor(theme.accentColor, pickedAccent))
		accent = pickedAccent;

	colors.dark = relativeLuminance(background) < 0.4;
	colors.background = background;
	colors.text = ensureContrast(text, background, 7.0);

	// Cards sit slightly off the background: lighter on dark themes, darker on light ones.
	colors.card = mixColors(background, colors.text, colors.dark ? 0.06 : 0.05);
	colors.cardBorder = mixColors(background, colors.text, 0.17);
	colors.hover = mixColors(background, colors.text, 0.11);
	colors.input = colors.dark ? mixColors(background, kBlack, 0.28) : mixColors(background, kWhite, 0.75);

	colors.mutedText = ensureContrast(mixColors(colors.text, background, 0.38), colors.card, 4.5);

	// The accent fills buttons, so it needs to stand out from the card behind it.
	accent = ensureContrast(accent, colors.card, 1.6);
	colors.accent = accent;
	colors.accentText = readableOn(accent);

	// Status colours double as text on the card, so they must be readable there.
	const Rgb ok = colors.dark ? Rgb{0x4c, 0xc3, 0x7a} : Rgb{0x1a, 0x7f, 0x43};
	const Rgb warning = colors.dark ? Rgb{0xe3, 0xa8, 0x3b} : Rgb{0x8a, 0x5d, 0x00};
	const Rgb error = colors.dark ? Rgb{0xf0, 0x6a, 0x62} : Rgb{0xc2, 0x2f, 0x2a};
	colors.ok = ensureContrast(ok, colors.card, 4.5);
	colors.warning = ensureContrast(warning, colors.card, 4.5);
	colors.error = ensureContrast(error, colors.card, 4.5);
	return colors;
}

ThemeMetrics resolveThemeMetrics(const ThemeConfig &theme, const BasePalette &base)
{
	ThemeMetrics metrics;
	const int basePx = std::clamp(base.fontPx, 9, 24);
	metrics.fontPx = std::clamp(static_cast<int>(std::lround(basePx * theme.fontScale / 100.0)), 9, 32);
	metrics.smallFontPx = std::max(9, static_cast<int>(std::lround(metrics.fontPx * 0.88)));
	metrics.radius = std::clamp(theme.cornerRadius, 0, 16);
	metrics.iconPx = std::max(12, static_cast<int>(std::lround(metrics.fontPx * 1.25)));
	metrics.cardOpacity = std::clamp(theme.cardOpacity, 20, 100);

	const bool compact = theme.density == Density::Compact;
	metrics.padding = compact ? 6 : 10;
	metrics.spacing = compact ? 4 : 8;
	metrics.controlHeight = metrics.fontPx + (compact ? 10 : 14);
	return metrics;
}

std::string buildStyleSheet(const ThemeColors &c, const ThemeMetrics &m)
{
	std::string css;
	auto add = [&css](const std::string &rule) {
		css += rule;
		css += "\n";
	};

	const std::string text = toHex(c.text);
	const std::string muted = toHex(c.mutedText);
	const std::string border = toHex(c.cardBorder);
	const std::string hover = toHex(c.hover);
	const std::string accent = toHex(c.accent);
	const std::string accentText = toHex(c.accentText);
	const std::string accentHover = toHex(mixColors(c.accent, c.accentText, 0.14));
	const std::string accentOff = toHex(mixColors(c.accent, c.card, 0.6));
	const std::string accentOffText = toHex(ensureContrast(mixColors(c.accentText, c.card, 0.4),
							       mixColors(c.accent, c.card, 0.6), 3.0));
	const int pillRadius = std::min(m.radius, 8);

	// The root. With "Follow OBS" the OBS theme already paints the background and the standard
	// controls, so RelayDock leaves them alone.
	add(std::format("#rdRoot, #rdDialog {{ font-size: {}px; }}", m.fontPx));
	// Room between a box or radio button and its text, whatever the theme sets.
	add("QRadioButton, QCheckBox { spacing: 7px; }");
	if (!c.followsObs) {
		const std::string background = toHex(c.background);
		const std::string input = toHex(c.input);
		add(std::format("#rdRoot, #rdDialog, #rdPage {{ background-color: {}; color: {}; }}", background, text));
		add(std::format("QLabel, QCheckBox, QRadioButton, QGroupBox {{ color: {}; background: transparent; }}", text));
		add(std::format("QGroupBox {{ border: 1px solid {}; border-radius: {}px; margin-top: {}px; padding-top: {}px; }}",
				border, m.radius, m.fontPx, m.spacing));
		add("QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }");
		add(std::format(
			"QLineEdit, QPlainTextEdit, QTextBrowser, QSpinBox, QDoubleSpinBox, QComboBox, QListWidget {{ background-color: {}; color: {}; border: 1px solid {}; border-radius: {}px; padding: 3px 6px; selection-background-color: {}; selection-color: {}; }}",
			input, text, border, m.radius, accent, accentText));
		add(std::format("QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus {{ border-color: {}; }}",
				accent));
		add(std::format("QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled {{ color: {}; }}",
				muted));
		add(std::format(
			"QComboBox QAbstractItemView {{ background-color: {}; color: {}; border: 1px solid {}; selection-background-color: {}; selection-color: {}; }}",
			input, text, border, accent, accentText));
		add(std::format(
			"QPushButton {{ background-color: {}; color: {}; border: 1px solid {}; border-radius: {}px; padding: 4px 12px; min-height: {}px; }}",
			toHex(c.card), text, border, m.radius, m.controlHeight - 10));
		add(std::format("QPushButton:hover {{ background-color: {}; }}", hover));
		add(std::format("QPushButton:focus {{ border-color: {}; }}", accent));
		add(std::format("QPushButton:disabled {{ color: {}; }}", muted));
		add(std::format("QListWidget::item:selected {{ background-color: {}; color: {}; }}", accent, accentText));
		add(std::format("QListWidget::item:hover:!selected {{ background-color: {}; }}", hover));
		add(std::format("QScrollArea, QScrollArea > QWidget > QWidget {{ background: transparent; }}"));
		add(std::format("QToolTip {{ background-color: {}; color: {}; border: 1px solid {}; }}", input, text, border));
		add(std::format("QMenu {{ background-color: {}; color: {}; border: 1px solid {}; }}", input, text, border));
		add(std::format("QMenu::item:selected {{ background-color: {}; color: {}; }}", accent, accentText));
		add(std::format("QMenu::item:disabled {{ color: {}; }}", muted));
		// OBS draws its arrows and check marks for its own theme. These themes bring their own,
		// light on dark surfaces and dark on light ones.
		const char *shade = c.dark ? "light" : "dark";
		const char *onAccent = relativeLuminance(c.accentText) > 0.5 ? "light" : "dark";
		add("QComboBox::drop-down { border: none; width: 24px; }");
		add(std::format(
			"QPushButton::menu-indicator {{ image: url(:/relaydock/icons/ui-chevron-down-{}.svg); subcontrol-origin: padding; subcontrol-position: right center; width: 12px; height: 12px; right: 8px; }}",
			shade));
		add(std::format("QComboBox::down-arrow {{ image: url(:/relaydock/icons/ui-chevron-down-{}.svg); width: 14px; height: 14px; }}",
				shade));
		add("QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; border: none; width: 20px; }");
		add("QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; border: none; width: 20px; }");
		add(std::format("QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {{ image: url(:/relaydock/icons/ui-chevron-up-{}.svg); width: 11px; height: 11px; }}",
				shade));
		add(std::format("QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {{ image: url(:/relaydock/icons/ui-chevron-down-{}.svg); width: 11px; height: 11px; }}",
				shade));
		add(std::format(
			"QCheckBox::indicator, QRadioButton::indicator {{ width: 14px; height: 14px; border: 1px solid {}; background-color: {}; }}",
			toHex(mixColors(c.cardBorder, c.text, 0.25)), input));
		add(std::format("QCheckBox::indicator {{ border-radius: {}px; }}", std::min(m.radius, 3)));
		add("QRadioButton::indicator { border-radius: 8px; }");
		add(std::format(
			"QCheckBox::indicator:checked {{ background-color: {}; border-color: {}; image: url(:/relaydock/icons/ui-check-{}.svg); }}",
			accent, accent, onAccent));
		add(std::format("QRadioButton::indicator:checked {{ background-color: {}; border: 4px solid {}; }}", accentText, accent));
		add(std::format("QCheckBox::indicator:focus, QRadioButton::indicator:focus {{ border-color: {}; }}", accent));
		add(std::format("QCheckBox::indicator:disabled, QRadioButton::indicator:disabled {{ border-color: {}; }}", border));
		add(std::format("QCheckBox:disabled, QRadioButton:disabled {{ color: {}; }}", muted));
		add(std::format("QTabWidget::pane {{ border: 1px solid {}; border-radius: {}px; top: -1px; }}", border, m.radius));
		add(std::format(
			"QTabBar::tab {{ background: transparent; color: {}; border: 1px solid transparent; border-bottom: none; padding: 5px 12px; border-top-left-radius: {}px; border-top-right-radius: {}px; }}",
			muted, m.radius, m.radius));
		add(std::format("QTabBar::tab:selected {{ color: {}; background-color: {}; border-color: {}; }}", text, toHex(c.card), border));
		add(std::format("QTabBar::tab:hover:!selected {{ color: {}; }}", text));
		add(std::format("QScrollBar:vertical {{ background: transparent; width: 10px; margin: 0; }}"));
		add(std::format("QScrollBar::handle:vertical {{ background: {}; border-radius: 4px; min-height: 24px; margin: 1px; }}", border));
		add("QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");
		add("QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }");
		add(std::format("QSlider::groove:horizontal {{ height: 4px; background: {}; border-radius: 2px; }}", border));
		add(std::format(
			"QSlider::handle:horizontal {{ width: 14px; margin: -6px 0; border-radius: 7px; background: {}; }}",
			accent));
	} else {
		add("QScrollArea#rdScroll, QScrollArea#rdScroll > QWidget > QWidget { background: transparent; }");
	}

	// Cards
	add(std::format("QFrame#rdCard, QFrame#rdPanel {{ background-color: {}; border: 1px solid {}; border-radius: {}px; }}",
			rgba(c.card, m.cardOpacity), border, m.radius));
	add(std::format("QFrame#rdCard[rdPhase=\"live\"] {{ border-color: {}; }}", toHex(c.ok)));
	add(std::format("QFrame#rdCard[rdPhase=\"failed\"] {{ border-color: {}; }}", toHex(c.error)));
	add(std::format("QFrame#rdCard[rdPhase=\"starting\"], QFrame#rdCard[rdPhase=\"reconnecting\"] {{ border-color: {}; }}",
			toHex(c.warning)));
	add(std::format("QFrame#rdCard:focus {{ border-color: {}; }}", accent));
	add(std::format("QFrame#rdSeparator {{ background-color: {}; border: none; max-height: 1px; min-height: 1px; }}", border));

	// Text roles
	add(std::format("QLabel#rdTitle {{ font-weight: 600; color: {}; }}", text));
	add(std::format("QLabel#rdHeading {{ font-size: {}px; font-weight: 600; color: {}; }}", m.fontPx + 3, text));
	add(std::format("QLabel#rdSection {{ font-size: {}px; font-weight: 600; color: {}; }}", m.smallFontPx, muted));
	add(std::format("QLabel#rdMuted {{ color: {}; }}", muted));
	add(std::format("QLabel#rdSmall {{ color: {}; font-size: {}px; }}", muted, m.smallFontPx));
	add(std::format("QLabel#rdValue {{ color: {}; font-weight: 600; }}", text));
	// The object name is part of each selector, so these win over the plain role colours above.
	auto tone = [&](const char *name, Rgb color) {
		add(std::format(
			"QLabel[rdTone=\"{0}\"], QLabel#rdValue[rdTone=\"{0}\"], QLabel#rdSmall[rdTone=\"{0}\"], QLabel#rdMuted[rdTone=\"{0}\"] {{ color: {1}; }}",
			name, toHex(color)));
	};
	tone("ok", c.ok);
	tone("warning", c.warning);
	tone("error", c.error);

	// Status pills: a tinted background with readable text in the status colour.
	auto pill = [&](const char *tone, Rgb color) {
		const Rgb fill = mixColors(c.card, color, 0.2);
		add(std::format(
			"QLabel#rdPill[rdTone=\"{}\"] {{ background-color: {}; color: {}; border: 1px solid {}; border-radius: {}px; padding: 1px 7px; font-size: {}px; font-weight: 600; }}",
			tone, toHex(fill), toHex(ensureContrast(color, fill, 4.5)), toHex(mixColors(c.card, color, 0.45)), pillRadius,
			m.smallFontPx));
	};
	pill("ok", c.ok);
	pill("warning", c.warning);
	pill("error", c.error);
	add(std::format(
		"QLabel#rdPill[rdTone=\"neutral\"] {{ background-color: {}; color: {}; border: 1px solid {}; border-radius: {}px; padding: 1px 7px; font-size: {}px; font-weight: 600; }}",
		hover, muted, border, pillRadius, m.smallFontPx));

	// Message banners
	auto banner = [&](const char *tone, Rgb color) {
		add(std::format(
			"QFrame#rdBanner[rdTone=\"{}\"] {{ background-color: {}; border: 1px solid {}; border-radius: {}px; }}", tone,
			toHex(mixColors(c.card, color, 0.14)), toHex(mixColors(c.card, color, 0.5)), m.radius));
	};
	banner("ok", c.ok);
	banner("warning", c.warning);
	banner("error", c.error);
	add(std::format("QFrame#rdBanner[rdTone=\"neutral\"] {{ background-color: {}; border: 1px solid {}; border-radius: {}px; }}",
			hover, border, m.radius));
	add("QFrame#rdBanner QLabel { background: transparent; }");

	// Buttons
	add(std::format(
		"QPushButton#rdPrimary {{ background-color: {}; color: {}; border: 1px solid {}; border-radius: {}px; padding: 4px 12px; font-weight: 600; }}",
		accent, accentText, accent, m.radius));
	add(std::format("QPushButton#rdPrimary:hover {{ background-color: {}; border-color: {}; }}", accentHover, accentHover));
	add(std::format("QPushButton#rdPrimary:focus {{ border-color: {}; }}", text));
	add(std::format("QPushButton#rdPrimary:disabled {{ background-color: {}; border-color: {}; color: {}; }}", accentOff,
			accentOff, accentOffText));
	add(std::format(
		"QToolButton#rdTool {{ background: transparent; border: 1px solid transparent; border-radius: {}px; padding: 3px; color: {}; }}",
		m.radius, text));
	add(std::format("QToolButton#rdTool:hover {{ background-color: {}; }}", hover));
	add(std::format("QToolButton#rdTool:focus {{ border-color: {}; }}", accent));
	add(std::format("QToolButton#rdTool:checked {{ background-color: {}; border-color: {}; }}", hover, border));
	add("QToolButton#rdTool::menu-indicator { image: none; }");

	// Meters
	add(std::format(
		"QProgressBar#rdMeter {{ background-color: {}; border: none; border-radius: 3px; max-height: 6px; min-height: 6px; }}",
		toHex(c.cardBorder)));
	add(std::format("QProgressBar#rdMeter::chunk {{ background-color: {}; border-radius: 3px; }}", toHex(c.ok)));
	add(std::format("QProgressBar#rdMeter[rdTone=\"warning\"]::chunk {{ background-color: {}; }}", toHex(c.warning)));
	add(std::format("QProgressBar#rdMeter[rdTone=\"error\"]::chunk {{ background-color: {}; }}", toHex(c.error)));

	// Settings navigation
	add(std::format("QListWidget#rdNav {{ border: none; background: transparent; outline: none; }}"));
	add(std::format("QListWidget#rdNav::item {{ padding: {}px 10px; border-radius: {}px; color: {}; }}", m.spacing / 2 + 2,
			m.radius, text));
	add(std::format("QListWidget#rdNav::item:selected {{ background-color: {}; color: {}; }}", accent, accentText));
	add(std::format("QListWidget#rdNav::item:hover:!selected {{ background-color: {}; }}", hover));

	return css;
}

bool animationsAllowed(const ThemeConfig &theme, PerformanceMode mode)
{
	return theme.animations && !theme.reducedMotion && mode != PerformanceMode::Potato;
}

} // namespace rd
