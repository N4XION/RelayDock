// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include <doctest/doctest.h>

#include "core/vertical_layout.h"
#include "utils/uuid.h"

#include <cmath>

using namespace rd;

namespace {

LayoutItem box(double x, double y, double w, double h, FitMode fit = FitMode::Fit)
{
	LayoutItem item;
	item.id = generateUuid();
	item.sourceName = "Source";
	item.x = x;
	item.y = y;
	item.width = w;
	item.height = h;
	item.fit = fit;
	return item;
}

double aspect(const RectD &rect)
{
	return rect.width / rect.height;
}

} // namespace

TEST_SUITE("core.vertical_layout.placement")
{
	TEST_CASE("Fill centre-crops a 16:9 source into a 9:16 canvas without stretching")
	{
		const LayoutItem item = box(0, 0, 1080, 1920, FitMode::Fill);
		const Placement placement = computePlacement(item, 1920, 1080);

		// The picture keeps 16:9. It is scaled until its height covers the canvas.
		CHECK(aspect(placement.drawn) == doctest::Approx(16.0 / 9.0));
		CHECK(placement.drawn.height == doctest::Approx(1920.0));
		CHECK(placement.drawn.width == doctest::Approx(3413.33).epsilon(0.001));
		CHECK(placement.scale == doctest::Approx(1920.0 / 1080.0));

		// It is centred, so the same amount is cut from the left and the right.
		CHECK(placement.drawn.x == doctest::Approx(-(3413.33 - 1080.0) / 2.0).epsilon(0.001));
		CHECK(placement.drawn.y == doctest::Approx(0.0));

		// What shows is exactly the canvas.
		CHECK(placement.visible.x == doctest::Approx(0.0));
		CHECK(placement.visible.width == doctest::Approx(1080.0));
		CHECK(placement.visible.height == doctest::Approx(1920.0));
	}

	TEST_CASE("Fit shows the whole 16:9 source inside the box and leaves space above and below")
	{
		const LayoutItem item = box(0, 0, 1080, 1920, FitMode::Fit);
		const Placement placement = computePlacement(item, 1920, 1080);

		CHECK(aspect(placement.drawn) == doctest::Approx(16.0 / 9.0));
		CHECK(placement.drawn.width == doctest::Approx(1080.0));
		CHECK(placement.drawn.height == doctest::Approx(607.5));
		CHECK(placement.drawn.x == doctest::Approx(0.0));
		CHECK(placement.drawn.y == doctest::Approx((1920.0 - 607.5) / 2.0));
		CHECK(placement.visible.width == doctest::Approx(placement.drawn.width));
		CHECK(placement.visible.height == doctest::Approx(placement.drawn.height));
	}

	TEST_CASE("neither mode ever changes the proportions of a source")
	{
		const int sizes[][2] = {{1920, 1080}, {1280, 720}, {640, 480}, {1080, 1920}, {500, 500}, {3440, 1440}, {300, 1200}};
		const double boxes[][2] = {{1080, 1920}, {1080, 608}, {540, 540}, {200, 900}, {1080, 300}};

		for (const auto &size : sizes) {
			for (const auto &b : boxes) {
				for (FitMode fit : {FitMode::Fit, FitMode::Fill}) {
					const LayoutItem item = box(10, 20, b[0], b[1], fit);
					const Placement placement = computePlacement(item, size[0], size[1]);
					CAPTURE(size[0]);
					CAPTURE(size[1]);
					CAPTURE(b[0]);
					CAPTURE(b[1]);
					CHECK(aspect(placement.drawn) ==
					      doctest::Approx(static_cast<double>(size[0]) / size[1]).epsilon(0.0001));

					if (fit == FitMode::Fit) {
						// Entirely inside the box.
						CHECK(placement.drawn.x >= item.x - 0.001);
						CHECK(placement.drawn.y >= item.y - 0.001);
						CHECK(placement.drawn.right() <= item.x + item.width + 0.001);
						CHECK(placement.drawn.bottom() <= item.y + item.height + 0.001);
					} else {
						// Covers the whole box.
						CHECK(placement.visible.width == doctest::Approx(item.width));
						CHECK(placement.visible.height == doctest::Approx(item.height));
					}
				}
			}
		}
	}

	TEST_CASE("crop removes source pixels before the source is placed")
	{
		// Cut a 1920x1080 game down to its 608x1080 centre, which is 9:16.
		LayoutItem item = box(0, 0, 1080, 1920, FitMode::Fit);
		item.cropLeft = 656;
		item.cropRight = 656;
		const Placement placement = computePlacement(item, 1920, 1080);
		CHECK(placement.drawn.width == doctest::Approx(1080.0).epsilon(0.002));
		CHECK(placement.drawn.height == doctest::Approx(1918.4).epsilon(0.002));
		CHECK(aspect(placement.drawn) == doctest::Approx(608.0 / 1080.0));
	}

	TEST_CASE("a crop larger than the source leaves one pixel instead of a negative size")
	{
		LayoutItem item = box(0, 0, 100, 100);
		item.cropLeft = 5000;
		item.cropTop = 5000;
		const Placement placement = computePlacement(item, 1920, 1080);
		CHECK(placement.drawn.width > 0.0);
		CHECK(placement.drawn.height > 0.0);
		CHECK(std::isfinite(placement.scale));
	}

	TEST_CASE("an empty box does not divide by zero")
	{
		const Placement placement = computePlacement(box(0, 0, 0, 0), 1920, 1080);
		CHECK(std::isfinite(placement.scale));
		CHECK(placement.visible.width == 0.0);
	}
}

TEST_SUITE("core.vertical_layout.editing")
{
	TEST_CASE("the default layout shows Program across the whole canvas, cropped, never stretched")
	{
		const VerticalLayout layout = makeDefaultVerticalLayout(1080, 1920);
		CHECK(isUuid(layout.id));
		REQUIRE(layout.items.size() == 1);
		const LayoutItem &item = layout.items.front();
		CHECK(item.kind == LayoutItemKind::Program);
		CHECK(item.fit == FitMode::Fill);
		CHECK(item.x == 0.0);
		CHECK(item.y == 0.0);
		CHECK(item.width == 1080.0);
		CHECK(item.height == 1920.0);
		CHECK(item.visible);
	}

	TEST_CASE("presets place the box and keep fit mode and crop")
	{
		LayoutItem item = box(5, 5, 50, 50, FitMode::Fill);
		item.cropTop = 12;

		applyPreset(item, LayoutPreset::TopHalf, 1080, 1920);
		CHECK(item.y == 0.0);
		CHECK(item.height == 960.0);
		CHECK(item.width == 1080.0);

		applyPreset(item, LayoutPreset::BottomHalf, 1080, 1920);
		CHECK(item.y == 960.0);
		CHECK(item.height == 960.0);

		applyPreset(item, LayoutPreset::MiddleThird, 1080, 1920);
		CHECK(item.y == 640.0);
		CHECK(item.height == 640.0);

		applyPreset(item, LayoutPreset::BottomThird, 1080, 1920);
		CHECK(item.y + item.height == 1920.0);

		// A 16:9 band across the width: 1080 wide is 608 tall.
		applyPreset(item, LayoutPreset::WideCenter, 1080, 1920);
		CHECK(item.width == 1080.0);
		CHECK(item.height == 608.0);
		CHECK(item.y == 656.0);

		applyPreset(item, LayoutPreset::WideBottom, 1080, 1920);
		CHECK(item.y + item.height == 1920.0);

		applyPreset(item, LayoutPreset::FullCanvas, 1080, 1920);
		CHECK(item.width == 1080.0);
		CHECK(item.height == 1920.0);

		CHECK(item.fit == FitMode::Fill);
		CHECK(item.cropTop == 12);
	}

	TEST_CASE("three thirds cover the canvas exactly")
	{
		LayoutItem top = box(0, 0, 1, 1);
		LayoutItem middle = top;
		LayoutItem bottom = top;
		applyPreset(top, LayoutPreset::TopThird, 1080, 1920);
		applyPreset(middle, LayoutPreset::MiddleThird, 1080, 1920);
		applyPreset(bottom, LayoutPreset::BottomThird, 1080, 1920);
		CHECK(top.y == 0.0);
		CHECK(middle.y == top.y + top.height);
		CHECK(bottom.y == middle.y + middle.height);
		CHECK(bottom.y + bottom.height == 1920.0);
	}

	TEST_CASE("clampItem keeps a box grabbable and its values sane")
	{
		LayoutItem tiny = box(100, 100, 1, 1);
		clampItem(tiny, 1080, 1920);
		CHECK(tiny.width == kMinItemSize);
		CHECK(tiny.height == kMinItemSize);

		LayoutItem lost = box(99999, -99999, 200, 200);
		clampItem(lost, 1080, 1920);
		CHECK(lost.x <= 1080 - kMinItemSize);
		CHECK(lost.y >= kMinItemSize - lost.height);

		LayoutItem broken = box(0, 0, 100, 100);
		broken.x = std::nan("");
		broken.width = std::numeric_limits<double>::infinity();
		broken.cropLeft = -40;
		clampItem(broken, 1080, 1920);
		CHECK(std::isfinite(broken.x));
		CHECK(std::isfinite(broken.width));
		CHECK(broken.cropLeft == 0);

		// A box larger than the canvas is allowed. That is how a source is zoomed in.
		LayoutItem zoomed = box(-500, -500, 3000, 4000);
		clampItem(zoomed, 1080, 1920);
		CHECK(zoomed.width == 3000.0);
		CHECK(zoomed.x == -500.0);
	}

	TEST_CASE("hit testing picks the topmost visible, unlocked item")
	{
		VerticalLayout layout;
		layout.items.push_back(box(0, 0, 1080, 1920));     // 0: background
		layout.items.push_back(box(100, 100, 400, 400));   // 1: middle
		layout.items.push_back(box(300, 300, 400, 400));   // 2: top

		CHECK(hitTest(layout, 350, 350) == 2);
		CHECK(hitTest(layout, 150, 150) == 1);
		CHECK(hitTest(layout, 900, 1800) == 0);
		CHECK(hitTest(layout, -5, 10) == -1);
		CHECK(hitTest(layout, 1080, 10) == -1); // The right edge is outside

		layout.items[2].visible = false;
		CHECK(hitTest(layout, 350, 350) == 1);

		layout.items[1].locked = true;
		CHECK(hitTest(layout, 350, 350) == 0);
		CHECK(hitTest(layout, 350, 350, true) == 1);
	}

	TEST_CASE("snapping pulls a coordinate to a nearby guide only")
	{
		const std::vector<double> guides = horizontalGuides(1080);
		CHECK(guides == std::vector<double>{0.0, 540.0, 1080.0});
		CHECK(snapToGuides(6.0, guides, 8.0) == 0.0);
		CHECK(snapToGuides(535.0, guides, 8.0) == 540.0);
		CHECK(snapToGuides(520.0, guides, 8.0) == 520.0);
		CHECK(snapToGuides(1075.0, guides, 8.0) == 1080.0);
		CHECK(snapToGuides(300.0, {}, 8.0) == 300.0);

		const std::vector<double> vertical = verticalGuides(1920);
		CHECK(vertical == std::vector<double>{0.0, 640.0, 960.0, 1280.0, 1920.0});
	}

	TEST_CASE("scaling a layout follows a canvas size change")
	{
		VerticalLayout layout = makeDefaultVerticalLayout(1080, 1920);
		layout.items.push_back(box(90, 1200, 900, 600));
		scaleLayout(layout, 720.0 / 1080.0, 1280.0 / 1920.0);
		CHECK(layout.items[0].width == 720.0);
		CHECK(layout.items[0].height == 1280.0);
		CHECK(layout.items[1].x == 60.0);
		CHECK(layout.items[1].y == 800.0);
		CHECK(layout.items[1].width == 600.0);
		CHECK(layout.items[1].height == 400.0);

		scaleLayout(layout, 0.0, -1.0); // Invalid factors change nothing
		CHECK(layout.items[0].width == 720.0);
	}

	TEST_CASE("items move up and down the stack")
	{
		VerticalLayout layout;
		for (int i = 0; i < 4; ++i)
			layout.items.push_back(box(0, 0, 10, 10));
		const std::string a = layout.items[0].id;
		const std::string d = layout.items[3].id;

		moveItem(layout, a, 2);
		CHECK(layout.indexOf(a) == 2);
		moveItem(layout, d, 0);
		CHECK(layout.indexOf(d) == 0);
		moveItem(layout, a, 99);
		CHECK(layout.indexOf(a) == 3);
		moveItem(layout, "missing", 0);
		CHECK(layout.items.size() == 4);
		CHECK(layout.findItem(a) != nullptr);
		CHECK(layout.findItem("missing") == nullptr);
	}
}

TEST_SUITE("core.vertical_layout.saving")
{
	TEST_CASE("layouts round-trip with every field")
	{
		VerticalLayout layout = makeDefaultVerticalLayout(1080, 1920);
		layout.name = "Gameplay over cam \xE2\x9C\x93";
		LayoutItem camera = box(40.5, 1300.25, 1000, 580, FitMode::Fill);
		camera.sourceName = "Webcam (C920)";
		camera.sourceUuid = generateUuid();
		camera.cropLeft = 10;
		camera.cropTop = 20;
		camera.cropRight = 30;
		camera.cropBottom = 40;
		camera.visible = false;
		camera.locked = true;
		layout.items.push_back(camera);

		VerticalLayout second = makeDefaultVerticalLayout(720, 1280);
		second.name = "Second";

		const std::vector<VerticalLayout> original = {layout, second};
		std::vector<VerticalLayout> parsed;
		REQUIRE(parseVerticalLayouts(serializeVerticalLayouts(original), parsed));
		CHECK(parsed == original);
	}

	TEST_CASE("the canvas size the layouts were made for is saved with them")
	{
		const std::vector<VerticalLayout> layouts = {makeDefaultVerticalLayout(720, 1280)};
		std::vector<VerticalLayout> parsed;
		int width = -1;
		int height = -1;
		REQUIRE(parseVerticalLayouts(serializeVerticalLayouts(layouts, 720, 1280), parsed, &width, &height));
		CHECK(width == 720);
		CHECK(height == 1280);
		CHECK(parsed == layouts);

		// Text without a recorded size reports 0.
		REQUIRE(parseVerticalLayouts(serializeVerticalLayouts(layouts), parsed, &width, &height));
		CHECK(width == 0);
		CHECK(height == 0);
	}

	TEST_CASE("text that is not a layout list is refused")
	{
		std::vector<VerticalLayout> parsed;
		CHECK_FALSE(parseVerticalLayouts("", parsed));
		CHECK_FALSE(parseVerticalLayouts("nope", parsed));
		CHECK_FALSE(parseVerticalLayouts("[]", parsed));
		CHECK_FALSE(parseVerticalLayouts("{\"layouts\": 5}", parsed));
		CHECK(parseVerticalLayouts("{\"layouts\": []}", parsed));
		CHECK(parsed.empty());
		// A number too large for JSON makes the whole text invalid.
		CHECK_FALSE(parseVerticalLayouts("{\"layouts\": [{\"items\": [{\"x\": 1e999}]}]}", parsed));
	}

	TEST_CASE("damaged values fall back to defaults and are repaired")
	{
		const std::string text = R"({"layouts": [
			{"id": "bad-id", "name": "", "items": [
				{"kind": "program", "width": "wide", "height": null},
				{"kind": "source", "source_name": "Cam", "x": 1e300, "y": 5, "width": 2, "height": 2, "fit": "stretch",
				 "crop_left": -9},
				{"kind": "source"},
				42
			]},
			"junk"
		]})";

		std::vector<VerticalLayout> parsed;
		REQUIRE(parseVerticalLayouts(text, parsed));
		REQUIRE(parsed.size() == 1);

		const std::vector<std::string> notes = sanitizeVerticalLayouts(parsed, 1080, 1920);
		CHECK_FALSE(notes.empty());

		const VerticalLayout &layout = parsed.front();
		CHECK(isUuid(layout.id));
		CHECK_FALSE(layout.name.empty());
		REQUIRE(layout.items.size() == 2); // The source item with no source is gone

		// The program item had no size and now fills the canvas.
		CHECK(layout.items[0].kind == LayoutItemKind::Program);
		CHECK(layout.items[0].width == 1080.0);
		CHECK(layout.items[0].height == 1920.0);

		// "stretch" is not a mode RelayDock has. The item falls back to Fit.
		CHECK(layout.items[1].fit == FitMode::Fit);
		CHECK(layout.items[1].width == kMinItemSize);
		CHECK(layout.items[1].cropLeft == 0);
		CHECK(std::isfinite(layout.items[1].x));
		for (const LayoutItem &item : layout.items)
			CHECK(isUuid(item.id));
	}

	TEST_CASE("duplicate ids are replaced so every layout and item is addressable")
	{
		VerticalLayout a = makeDefaultVerticalLayout(1080, 1920);
		VerticalLayout b = a; // Same layout id and same item id
		b.items.push_back(b.items.front());
		std::vector<VerticalLayout> layouts = {a, b};

		sanitizeVerticalLayouts(layouts, 1080, 1920);
		CHECK(layouts[0].id != layouts[1].id);
		CHECK(layouts[1].items[0].id != layouts[1].items[1].id);
	}

	TEST_CASE("fit mode and item kind names round-trip")
	{
		FitMode fit = FitMode::Fit;
		CHECK(fitModeFromName(fitModeName(FitMode::Fill), fit));
		CHECK(fit == FitMode::Fill);
		CHECK_FALSE(fitModeFromName("stretch", fit));

		LayoutItemKind kind = LayoutItemKind::Source;
		CHECK(layoutItemKindFromName(layoutItemKindName(LayoutItemKind::Program), kind));
		CHECK(kind == LayoutItemKind::Program);
		CHECK_FALSE(layoutItemKindFromName("window", kind));
	}
}
