// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "encoders/encoder_caps.h"

#include <vector>

namespace rd {

struct Size {
	int width = 0;
	int height = 0;

	bool operator==(const Size &other) const = default;

	// The shorter side: 1080 for both 1920x1080 and 1080x1920. People call this "1080p".
	int lines() const { return width < height ? width : height; }
	int longEdge() const { return width > height ? width : height; }
	bool empty() const { return width <= 0 || height <= 0; }
};

// Rounds to the nearest even number, never below 2. Video encoders need even dimensions.
int roundToEven(double value);

// The canvas scaled so its shorter side is `lines` pixels. Keeps the aspect ratio, so the
// picture is never stretched. Never larger than the canvas base size.
Size scaleCanvasToLines(const CanvasInfo &canvas, int lines);

// The size OBS itself outputs for this canvas.
Size canvasOutputSize(const CanvasInfo &canvas);

// True when `size` has the canvas aspect ratio, allowing for rounding to even numbers.
bool keepsCanvasAspect(const CanvasInfo &canvas, Size size);

// The valid size closest to a requested one: same number of lines, canvas aspect ratio.
Size nearestCanvasSize(const CanvasInfo &canvas, Size requested);

// Sizes to offer for a canvas, largest first. All keep the canvas aspect ratio.
std::vector<Size> resolutionChoices(const CanvasInfo &canvas);

// OBS encodes every Nth frame, so a destination's frame rate is the OBS rate divided by a
// whole number. These helpers work with that rule.

// Largest divisor RelayDock uses. 60 FPS / 6 = 10 FPS is the floor.
inline constexpr int kMaxFpsDivisor = 6;

// Frame rate after applying a divisor.
double fpsForDivisor(const CanvasInfo &canvas, int divisor);

// Frame rates to offer, highest first, rounded to whole numbers. Rates below 10 are left out.
std::vector<int> fpsChoices(const CanvasInfo &canvas);

// Divisor whose frame rate is closest to `targetFps`. 0 or a rate above the OBS rate gives 1.
int divisorForTargetFps(const CanvasInfo &canvas, int targetFps);

// Smallest divisor whose frame rate does not exceed `maxFps`. 0 means no cap.
int divisorForMaxFps(const CanvasInfo &canvas, int maxFps);

} // namespace rd
