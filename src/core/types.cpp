// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "core/types.h"

#include <cstdlib>
#include <format>
#include <numeric>

namespace rd {

const char *orientationName(Orientation orientation)
{
	return orientation == Orientation::Vertical ? "vertical" : "horizontal";
}

bool orientationFromName(const std::string &name, Orientation &out)
{
	if (name == "horizontal") {
		out = Orientation::Horizontal;
		return true;
	}
	if (name == "vertical") {
		out = Orientation::Vertical;
		return true;
	}
	return false;
}

const char *lockableSettingName(LockableSetting setting)
{
	switch (setting) {
	case LockableSetting::Resolution:
		return "resolution";
	case LockableSetting::Fps:
		return "fps";
	case LockableSetting::Bitrate:
		return "bitrate";
	case LockableSetting::Encoder:
		return "encoder";
	case LockableSetting::AspectRatio:
		return "aspect_ratio";
	}
	return "";
}

bool SettingLocks::isLocked(LockableSetting setting) const
{
	switch (setting) {
	case LockableSetting::Resolution:
		return resolution;
	case LockableSetting::Fps:
		return fps;
	case LockableSetting::Bitrate:
		return bitrate;
	case LockableSetting::Encoder:
		return encoder;
	case LockableSetting::AspectRatio:
		return aspectRatio;
	}
	return false;
}

void SettingLocks::setLocked(LockableSetting setting, bool locked)
{
	switch (setting) {
	case LockableSetting::Resolution:
		resolution = locked;
		break;
	case LockableSetting::Fps:
		fps = locked;
		break;
	case LockableSetting::Bitrate:
		bitrate = locked;
		break;
	case LockableSetting::Encoder:
		encoder = locked;
		break;
	case LockableSetting::AspectRatio:
		aspectRatio = locked;
		break;
	}
}

std::string aspectRatioText(int width, int height)
{
	if (width <= 0 || height <= 0)
		return {};

	// Common sizes are a pixel or two off the exact ratio (1366x768, 854x480). Name them
	// by the ratio people expect.
	struct Known {
		int w;
		int h;
	};
	static const Known known[] = {{16, 9}, {9, 16}, {4, 3}, {3, 4}, {21, 9}, {1, 1}, {16, 10}, {4, 5}, {5, 4}};
	for (const Known &ratio : known) {
		if (sameAspectRatio(width, height, ratio.w * 1000, ratio.h * 1000))
			return std::format("{}:{}", ratio.w, ratio.h);
	}

	const int divisor = std::gcd(width, height);
	return std::format("{}:{}", width / divisor, height / divisor);
}

bool sameAspectRatio(int widthA, int heightA, int widthB, int heightB)
{
	if (widthA <= 0 || heightA <= 0 || widthB <= 0 || heightB <= 0)
		return false;
	// Compare widthA/heightA with widthB/heightB by cross-multiplying, allowing one pixel
	// of rounding on the height of the first size.
	const long long expectedHeight = static_cast<long long>(widthA) * heightB;
	const long long actual = static_cast<long long>(heightA) * widthB;
	return std::llabs(expectedHeight - actual) <= widthB;
}

} // namespace rd
