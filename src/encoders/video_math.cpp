// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "encoders/video_math.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace rd {

int roundToEven(double value)
{
	int rounded = static_cast<int>(std::lround(value / 2.0)) * 2;
	return rounded < 2 ? 2 : rounded;
}

Size canvasOutputSize(const CanvasInfo &canvas)
{
	return {canvas.outputWidth, canvas.outputHeight};
}

Size scaleCanvasToLines(const CanvasInfo &canvas, int lines)
{
	const Size base{canvas.baseWidth, canvas.baseHeight};
	if (base.empty())
		return {};

	const int baseLines = base.lines();
	if (lines <= 0 || lines >= baseLines)
		return base;

	const double scale = static_cast<double>(lines) / baseLines;
	Size out;
	if (base.width <= base.height) {
		out.width = roundToEven(lines);
		out.height = roundToEven(base.height * scale);
	} else {
		out.height = roundToEven(lines);
		out.width = roundToEven(base.width * scale);
	}
	return out;
}

bool keepsCanvasAspect(const CanvasInfo &canvas, Size size)
{
	if (size.empty() || canvas.baseWidth <= 0 || canvas.baseHeight <= 0)
		return false;
	// Compare against the exact size for this width and for this height. Rounding each
	// dimension to an even number moves it by at most one pixel, so allow two.
	const double exactHeight = static_cast<double>(size.width) * canvas.baseHeight / canvas.baseWidth;
	const double exactWidth = static_cast<double>(size.height) * canvas.baseWidth / canvas.baseHeight;
	return std::abs(exactHeight - size.height) <= 2.0 || std::abs(exactWidth - size.width) <= 2.0;
}

Size nearestCanvasSize(const CanvasInfo &canvas, Size requested)
{
	if (requested.empty())
		return canvasOutputSize(canvas);
	return scaleCanvasToLines(canvas, requested.lines());
}

std::vector<Size> resolutionChoices(const CanvasInfo &canvas)
{
	static const int kLines[] = {2160, 1440, 1080, 900, 720, 540, 480, 360};

	std::vector<Size> out;
	auto add = [&out](Size size) {
		if (!size.empty() && std::find(out.begin(), out.end(), size) == out.end())
			out.push_back(size);
	};

	const Size base{canvas.baseWidth, canvas.baseHeight};
	add(base);
	add(canvasOutputSize(canvas));
	for (int lines : kLines) {
		if (lines < base.lines())
			add(scaleCanvasToLines(canvas, lines));
	}

	std::sort(out.begin(), out.end(), [](const Size &a, const Size &b) {
		return static_cast<long long>(a.width) * a.height > static_cast<long long>(b.width) * b.height;
	});
	return out;
}

double fpsForDivisor(const CanvasInfo &canvas, int divisor)
{
	if (divisor < 1)
		divisor = 1;
	return canvas.fps() / divisor;
}

std::vector<int> fpsChoices(const CanvasInfo &canvas)
{
	std::vector<int> out;
	for (int divisor = 1; divisor <= kMaxFpsDivisor; ++divisor) {
		const int fps = static_cast<int>(std::lround(fpsForDivisor(canvas, divisor)));
		if (fps < 10)
			break;
		if (std::find(out.begin(), out.end(), fps) == out.end())
			out.push_back(fps);
	}
	if (out.empty())
		out.push_back(static_cast<int>(std::lround(canvas.fps())));
	return out;
}

int divisorForTargetFps(const CanvasInfo &canvas, int targetFps)
{
	if (targetFps <= 0)
		return 1;

	int best = 1;
	double bestDistance = std::abs(fpsForDivisor(canvas, 1) - targetFps);
	for (int divisor = 2; divisor <= kMaxFpsDivisor; ++divisor) {
		const double fps = fpsForDivisor(canvas, divisor);
		if (fps < 9.5)
			break;
		const double distance = std::abs(fps - targetFps);
		if (distance < bestDistance - 1e-9) {
			best = divisor;
			bestDistance = distance;
		}
	}
	return best;
}

int divisorForMaxFps(const CanvasInfo &canvas, int maxFps)
{
	if (maxFps <= 0)
		return 1;
	for (int divisor = 1; divisor <= kMaxFpsDivisor; ++divisor) {
		// 59.94 counts as 60. Allow half a frame of slack.
		if (fpsForDivisor(canvas, divisor) <= maxFps + 0.5)
			return divisor;
	}
	return kMaxFpsDivisor;
}

} // namespace rd
