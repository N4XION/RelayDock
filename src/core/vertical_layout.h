// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rd {

// A vertical layout says which OBS sources appear on the vertical canvas and where.
//
// Each item is a box on the canvas plus a rule for placing the source inside that box. Both
// rules keep the source's proportions, so nothing is ever stretched:
//   Fit   The whole source is visible inside the box. Empty space stays beside or above it.
//   Fill  The source covers the whole box. Whatever sticks out is cut off.
//
// Layouts are saved inside the OBS scene collection, because they refer to its sources.

enum class FitMode { Fit, Fill };

const char *fitModeName(FitMode mode);
bool fitModeFromName(const std::string &name, FitMode &out);

enum class LayoutItemKind {
	Program, // Whatever OBS shows in Program. Follows your scene switches.
	Source,  // One OBS source or scene, by name
};

const char *layoutItemKindName(LayoutItemKind kind);
bool layoutItemKindFromName(const std::string &name, LayoutItemKind &out);

struct LayoutItem {
	std::string id; // UUID
	LayoutItemKind kind = LayoutItemKind::Source;
	std::string sourceName; // OBS source name. Empty for Program.
	std::string sourceUuid; // OBS source UUID. Still matches after a rename.

	// The box, in canvas pixels. The origin is the top left corner of the canvas.
	double x = 0.0;
	double y = 0.0;
	double width = 0.0;
	double height = 0.0;

	FitMode fit = FitMode::Fit;

	// Cut from each edge of the source before it is placed, in source pixels.
	int cropLeft = 0;
	int cropTop = 0;
	int cropRight = 0;
	int cropBottom = 0;

	bool visible = true;
	bool locked = false; // Protects the item from the mouse in the editor

	bool operator==(const LayoutItem &other) const = default;
};

struct VerticalLayout {
	std::string id; // UUID
	std::string name;
	std::vector<LayoutItem> items; // Bottom first. Later items draw on top.

	bool operator==(const VerticalLayout &other) const = default;

	LayoutItem *findItem(const std::string &itemId);
	const LayoutItem *findItem(const std::string &itemId) const;
	int indexOf(const std::string &itemId) const;
};

struct RectD {
	double x = 0.0;
	double y = 0.0;
	double width = 0.0;
	double height = 0.0;

	bool contains(double px, double py) const
	{
		return px >= x && py >= y && px < x + width && py < y + height;
	}
	double right() const { return x + width; }
	double bottom() const { return y + height; }
};

// Where a source lands on the canvas.
struct Placement {
	RectD drawn;   // The scaled source. For Fill it is larger than the box.
	RectD visible; // The part that shows: drawn clipped to the box.
	double scale = 1.0; // Same factor in both directions, which is what "not stretched" means
};

Placement computePlacement(const LayoutItem &item, int sourceWidth, int sourceHeight);

// One item showing Program, filling the whole canvas. A 16:9 scene is centre-cropped to the
// canvas, so a vertical stream works before the user arranges anything.
VerticalLayout makeDefaultVerticalLayout(int canvasWidth, int canvasHeight);

enum class LayoutPreset {
	FullCanvas,
	TopHalf,
	BottomHalf,
	TopThird,
	MiddleThird,
	BottomThird,
	WideTop,    // A 16:9 band across the full width, at the top
	WideCenter, // The same band, centred
	WideBottom, // The same band, at the bottom
};

// Moves and sizes the item's box. Leaves fit mode and crop alone.
void applyPreset(LayoutItem &item, LayoutPreset preset, int canvasWidth, int canvasHeight);

// Smallest box the editor allows, in canvas pixels.
inline constexpr double kMinItemSize = 16.0;

// Keeps a box usable: at least kMinItemSize in each direction and at least partly on the
// canvas. Negative crop values become 0.
void clampItem(LayoutItem &item, int canvasWidth, int canvasHeight);

// Index of the topmost visible item under a point, or -1. Locked items are skipped unless
// `includeLocked` is set.
int hitTest(const VerticalLayout &layout, double x, double y, bool includeLocked = false);

// Returns the guide closest to `value` when it is within `threshold`, otherwise `value`.
double snapToGuides(double value, const std::vector<double> &guides, double threshold);

// Canvas edges and centre lines, for snapping.
std::vector<double> horizontalGuides(int canvasWidth);
std::vector<double> verticalGuides(int canvasHeight);

// Scales every box when the canvas size changes, for example from 1080x1920 to 720x1280.
void scaleLayout(VerticalLayout &layout, double factorX, double factorY);

void moveItem(VerticalLayout &layout, const std::string &itemId, int newIndex);

// ---- Saving ------------------------------------------------------------------------------------

std::string serializeVerticalLayouts(const std::vector<VerticalLayout> &layouts);

// Reads layouts. Tolerates missing and wrong-typed values. Returns false when the text is not
// a layout list at all.
bool parseVerticalLayouts(std::string_view jsonText, std::vector<VerticalLayout> &out);

// Repairs ids and boxes. Returns notes about what changed.
std::vector<std::string> sanitizeVerticalLayouts(std::vector<VerticalLayout> &layouts, int canvasWidth, int canvasHeight);

} // namespace rd
