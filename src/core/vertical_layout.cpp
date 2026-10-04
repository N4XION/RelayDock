// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "core/vertical_layout.h"

#include "utils/strings.h"
#include "utils/uuid.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <set>

namespace rd {

namespace {

using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

std::string readString(const json &object, const char *key, const std::string &fallback = {})
{
	const auto it = object.find(key);
	return it != object.end() && it->is_string() ? it->get<std::string>() : fallback;
}

double readNumber(const json &object, const char *key, double fallback)
{
	const auto it = object.find(key);
	if (it == object.end() || !it->is_number())
		return fallback;
	const double value = it->get<double>();
	return std::isfinite(value) ? value : fallback;
}

bool readBool(const json &object, const char *key, bool fallback)
{
	const auto it = object.find(key);
	return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

} // namespace

const char *fitModeName(FitMode mode)
{
	return mode == FitMode::Fill ? "fill" : "fit";
}

bool fitModeFromName(const std::string &name, FitMode &out)
{
	if (name == "fill") {
		out = FitMode::Fill;
		return true;
	}
	if (name == "fit") {
		out = FitMode::Fit;
		return true;
	}
	return false;
}

const char *layoutItemKindName(LayoutItemKind kind)
{
	return kind == LayoutItemKind::Program ? "program" : "source";
}

bool layoutItemKindFromName(const std::string &name, LayoutItemKind &out)
{
	if (name == "program") {
		out = LayoutItemKind::Program;
		return true;
	}
	if (name == "source") {
		out = LayoutItemKind::Source;
		return true;
	}
	return false;
}

LayoutItem *VerticalLayout::findItem(const std::string &itemId)
{
	for (LayoutItem &item : items) {
		if (item.id == itemId)
			return &item;
	}
	return nullptr;
}

const LayoutItem *VerticalLayout::findItem(const std::string &itemId) const
{
	return const_cast<VerticalLayout *>(this)->findItem(itemId);
}

int VerticalLayout::indexOf(const std::string &itemId) const
{
	for (size_t i = 0; i < items.size(); ++i) {
		if (items[i].id == itemId)
			return static_cast<int>(i);
	}
	return -1;
}

Placement computePlacement(const LayoutItem &item, int sourceWidth, int sourceHeight)
{
	Placement placement;
	const RectD box{item.x, item.y, item.width, item.height};

	const double croppedWidth = std::max(1, sourceWidth - std::max(0, item.cropLeft) - std::max(0, item.cropRight));
	const double croppedHeight = std::max(1, sourceHeight - std::max(0, item.cropTop) - std::max(0, item.cropBottom));
	if (box.width <= 0.0 || box.height <= 0.0) {
		placement.drawn = box;
		placement.visible = box;
		return placement;
	}

	const double scaleX = box.width / croppedWidth;
	const double scaleY = box.height / croppedHeight;
	placement.scale = item.fit == FitMode::Fill ? std::max(scaleX, scaleY) : std::min(scaleX, scaleY);

	placement.drawn.width = croppedWidth * placement.scale;
	placement.drawn.height = croppedHeight * placement.scale;
	placement.drawn.x = box.x + (box.width - placement.drawn.width) / 2.0;
	placement.drawn.y = box.y + (box.height - placement.drawn.height) / 2.0;

	// Fill overflows the box and is clipped to it. Fit already lies inside.
	const double left = std::max(placement.drawn.x, box.x);
	const double top = std::max(placement.drawn.y, box.y);
	const double right = std::min(placement.drawn.right(), box.right());
	const double bottom = std::min(placement.drawn.bottom(), box.bottom());
	placement.visible = {left, top, std::max(0.0, right - left), std::max(0.0, bottom - top)};
	return placement;
}

VerticalLayout makeDefaultVerticalLayout(int canvasWidth, int canvasHeight)
{
	VerticalLayout layout;
	layout.id = generateUuid();
	layout.name = "Default";

	LayoutItem program;
	program.id = generateUuid();
	program.kind = LayoutItemKind::Program;
	program.x = 0.0;
	program.y = 0.0;
	program.width = canvasWidth;
	program.height = canvasHeight;
	program.fit = FitMode::Fit;
	layout.items.push_back(std::move(program));
	return layout;
}

void applyPreset(LayoutItem &item, LayoutPreset preset, int canvasWidth, int canvasHeight)
{
	const double w = canvasWidth;
	const double h = canvasHeight;
	// A 16:9 band across the full width.
	const double bandHeight = std::min(h, std::round(w * 9.0 / 16.0));

	switch (preset) {
	case LayoutPreset::FullCanvas:
		item.x = 0.0;
		item.y = 0.0;
		item.width = w;
		item.height = h;
		break;
	case LayoutPreset::TopHalf:
		item.x = 0.0;
		item.y = 0.0;
		item.width = w;
		item.height = std::round(h / 2.0);
		break;
	case LayoutPreset::BottomHalf:
		item.x = 0.0;
		item.y = std::round(h / 2.0);
		item.width = w;
		item.height = h - std::round(h / 2.0);
		break;
	case LayoutPreset::TopThird:
		item.x = 0.0;
		item.y = 0.0;
		item.width = w;
		item.height = std::round(h / 3.0);
		break;
	case LayoutPreset::MiddleThird:
		item.x = 0.0;
		item.y = std::round(h / 3.0);
		item.width = w;
		item.height = std::round(h / 3.0);
		break;
	case LayoutPreset::BottomThird:
		item.x = 0.0;
		item.y = h - std::round(h / 3.0);
		item.width = w;
		item.height = std::round(h / 3.0);
		break;
	case LayoutPreset::WideTop:
		item.x = 0.0;
		item.y = 0.0;
		item.width = w;
		item.height = bandHeight;
		break;
	case LayoutPreset::WideCenter:
		item.x = 0.0;
		item.y = std::round((h - bandHeight) / 2.0);
		item.width = w;
		item.height = bandHeight;
		break;
	case LayoutPreset::WideBottom:
		item.x = 0.0;
		item.y = h - bandHeight;
		item.width = w;
		item.height = bandHeight;
		break;
	}
}

void clampItem(LayoutItem &item, int canvasWidth, int canvasHeight)
{
	if (!std::isfinite(item.x))
		item.x = 0.0;
	if (!std::isfinite(item.y))
		item.y = 0.0;
	if (!std::isfinite(item.width))
		item.width = kMinItemSize;
	if (!std::isfinite(item.height))
		item.height = kMinItemSize;

	// A box may extend past the canvas, which is how a source is zoomed in. It may not be
	// absurdly large and it must keep a grabbable part on the canvas.
	item.width = std::clamp(item.width, kMinItemSize, canvasWidth * 8.0);
	item.height = std::clamp(item.height, kMinItemSize, canvasHeight * 8.0);
	item.x = std::clamp(item.x, kMinItemSize - item.width, canvasWidth - kMinItemSize);
	item.y = std::clamp(item.y, kMinItemSize - item.height, canvasHeight - kMinItemSize);

	item.cropLeft = std::max(0, item.cropLeft);
	item.cropTop = std::max(0, item.cropTop);
	item.cropRight = std::max(0, item.cropRight);
	item.cropBottom = std::max(0, item.cropBottom);
}

int hitTest(const VerticalLayout &layout, double x, double y, bool includeLocked)
{
	for (int i = static_cast<int>(layout.items.size()) - 1; i >= 0; --i) {
		const LayoutItem &item = layout.items[static_cast<size_t>(i)];
		if (!item.visible || (item.locked && !includeLocked))
			continue;
		if (RectD{item.x, item.y, item.width, item.height}.contains(x, y))
			return i;
	}
	return -1;
}

double snapToGuides(double value, const std::vector<double> &guides, double threshold)
{
	double best = value;
	double bestDistance = threshold;
	for (double guide : guides) {
		const double distance = std::abs(guide - value);
		if (distance <= bestDistance) {
			best = guide;
			bestDistance = distance;
		}
	}
	return best;
}

std::vector<double> horizontalGuides(int canvasWidth)
{
	return {0.0, canvasWidth / 2.0, static_cast<double>(canvasWidth)};
}

std::vector<double> verticalGuides(int canvasHeight)
{
	const double h = canvasHeight;
	return {0.0, std::round(h / 3.0), h / 2.0, h - std::round(h / 3.0), h};
}

void scaleLayout(VerticalLayout &layout, double factorX, double factorY)
{
	if (factorX <= 0.0 || factorY <= 0.0)
		return;
	for (LayoutItem &item : layout.items) {
		item.x = std::round(item.x * factorX);
		item.y = std::round(item.y * factorY);
		item.width = std::round(item.width * factorX);
		item.height = std::round(item.height * factorY);
	}
}

void moveItem(VerticalLayout &layout, const std::string &itemId, int newIndex)
{
	const int current = layout.indexOf(itemId);
	if (current < 0)
		return;
	const int target = std::clamp(newIndex, 0, static_cast<int>(layout.items.size()) - 1);
	if (target == current)
		return;
	LayoutItem moving = std::move(layout.items[static_cast<size_t>(current)]);
	layout.items.erase(layout.items.begin() + current);
	layout.items.insert(layout.items.begin() + target, std::move(moving));
}

// ---- Saving ------------------------------------------------------------------------------------

std::string serializeVerticalLayouts(const std::vector<VerticalLayout> &layouts, int canvasWidth, int canvasHeight)
{
	ordered_json root;
	root["version"] = kVerticalLayoutFormat;
	if (canvasWidth > 0 && canvasHeight > 0) {
		root["canvas_width"] = canvasWidth;
		root["canvas_height"] = canvasHeight;
	}
	ordered_json list = ordered_json::array();
	for (const VerticalLayout &layout : layouts) {
		ordered_json l;
		l["id"] = layout.id;
		l["name"] = layout.name;
		ordered_json items = ordered_json::array();
		for (const LayoutItem &item : layout.items) {
			ordered_json i;
			i["id"] = item.id;
			i["kind"] = layoutItemKindName(item.kind);
			i["source_name"] = item.sourceName;
			i["source_uuid"] = item.sourceUuid;
			i["x"] = item.x;
			i["y"] = item.y;
			i["width"] = item.width;
			i["height"] = item.height;
			i["fit"] = fitModeName(item.fit);
			i["crop_left"] = item.cropLeft;
			i["crop_top"] = item.cropTop;
			i["crop_right"] = item.cropRight;
			i["crop_bottom"] = item.cropBottom;
			i["visible"] = item.visible;
			i["locked"] = item.locked;
			items.push_back(std::move(i));
		}
		l["items"] = std::move(items);
		list.push_back(std::move(l));
	}
	root["layouts"] = std::move(list);
	return root.dump(2);
}

bool parseVerticalLayouts(std::string_view jsonText, std::vector<VerticalLayout> &out, int *canvasWidth,
			  int *canvasHeight, int *format)
{
	out.clear();
	if (canvasWidth)
		*canvasWidth = 0;
	if (canvasHeight)
		*canvasHeight = 0;
	if (format)
		*format = 0;

	const json root = json::parse(jsonText, nullptr, false, true);
	if (root.is_discarded() || !root.is_object())
		return false;
	const auto layouts = root.find("layouts");
	if (layouts == root.end() || !layouts->is_array())
		return false;

	if (format)
		*format = static_cast<int>(readNumber(root, "version", 0));

	const int savedWidth = static_cast<int>(readNumber(root, "canvas_width", 0));
	const int savedHeight = static_cast<int>(readNumber(root, "canvas_height", 0));
	if (savedWidth > 0 && savedHeight > 0) {
		if (canvasWidth)
			*canvasWidth = savedWidth;
		if (canvasHeight)
			*canvasHeight = savedHeight;
	}

	for (const json &l : *layouts) {
		if (!l.is_object())
			continue;
		VerticalLayout layout;
		layout.id = readString(l, "id");
		layout.name = readString(l, "name");

		const auto items = l.find("items");
		if (items != l.end() && items->is_array()) {
			for (const json &i : *items) {
				if (!i.is_object())
					continue;
				LayoutItem item;
				item.id = readString(i, "id");
				layoutItemKindFromName(readString(i, "kind", "source"), item.kind);
				item.sourceName = readString(i, "source_name");
				item.sourceUuid = readString(i, "source_uuid");
				item.x = readNumber(i, "x", 0.0);
				item.y = readNumber(i, "y", 0.0);
				item.width = readNumber(i, "width", 0.0);
				item.height = readNumber(i, "height", 0.0);
				fitModeFromName(readString(i, "fit", "fit"), item.fit);
				item.cropLeft = static_cast<int>(readNumber(i, "crop_left", 0));
				item.cropTop = static_cast<int>(readNumber(i, "crop_top", 0));
				item.cropRight = static_cast<int>(readNumber(i, "crop_right", 0));
				item.cropBottom = static_cast<int>(readNumber(i, "crop_bottom", 0));
				item.visible = readBool(i, "visible", true);
				item.locked = readBool(i, "locked", false);
				layout.items.push_back(std::move(item));
			}
		}
		out.push_back(std::move(layout));
	}
	return true;
}

std::vector<std::string> upgradeVerticalLayouts(std::vector<VerticalLayout> &layouts, int savedFormat, int canvasWidth,
						int canvasHeight)
{
	std::vector<std::string> notes;
	if (savedFormat >= 2)
		return notes;

	for (VerticalLayout &layout : layouts) {
		if (layout.items.size() != 1)
			continue;
		LayoutItem &item = layout.items.front();
		const bool untouched = item.kind == LayoutItemKind::Program && item.fit == FitMode::Fill && item.visible &&
				       item.x == 0.0 && item.y == 0.0 && item.width == canvasWidth && item.height == canvasHeight &&
				       item.cropLeft == 0 && item.cropTop == 0 && item.cropRight == 0 && item.cropBottom == 0;
		if (!untouched)
			continue;
		item.fit = FitMode::Fit;
		notes.push_back(std::format(
			"The layout \"{}\" was the standard layout of an earlier version, which cropped the sides of your picture. It now shows the whole picture. To crop again, open the layout editor and set Scaling to Fill.",
			layout.name));
	}
	return notes;
}

std::vector<std::string> sanitizeVerticalLayouts(std::vector<VerticalLayout> &layouts, int canvasWidth, int canvasHeight)
{
	std::vector<std::string> notes;
	std::set<std::string> layoutIds;

	for (VerticalLayout &layout : layouts) {
		if (!isUuid(layout.id) || !layoutIds.insert(toLower(layout.id)).second) {
			layout.id = generateUuid();
			layoutIds.insert(layout.id);
			notes.push_back("A vertical layout had a missing or duplicate id and received a new one.");
		}
		if (trim(layout.name).empty())
			layout.name = "Layout";

		std::set<std::string> itemIds;
		for (auto it = layout.items.begin(); it != layout.items.end();) {
			LayoutItem &item = *it;
			// A source item with nothing to point at cannot be shown or repaired.
			if (item.kind == LayoutItemKind::Source && item.sourceName.empty() && item.sourceUuid.empty()) {
				notes.push_back("A vertical layout item without a source was removed.");
				it = layout.items.erase(it);
				continue;
			}
			if (!isUuid(item.id) || !itemIds.insert(toLower(item.id)).second) {
				item.id = generateUuid();
				itemIds.insert(item.id);
			}
			if (item.width <= 0.0 || item.height <= 0.0) {
				item.x = 0.0;
				item.y = 0.0;
				item.width = canvasWidth;
				item.height = canvasHeight;
				notes.push_back("A vertical layout item had no size and now fills the canvas.");
			}
			clampItem(item, canvasWidth, canvasHeight);
			++it;
		}
	}
	return notes;
}

} // namespace rd
