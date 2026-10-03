// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rd {

std::string trim(std::string_view text);
std::string toLower(std::string_view text);
bool equalsNoCase(std::string_view a, std::string_view b);
bool startsWithNoCase(std::string_view text, std::string_view prefix);
bool containsNoCase(std::string_view text, std::string_view needle);

std::vector<std::string> split(std::string_view text, char separator, bool keepEmpty = false);
std::string join(const std::vector<std::string> &parts, std::string_view separator);

// Replaces every occurrence of `from` with `to`.
std::string replaceAll(std::string text, std::string_view from, std::string_view to);

// True when the text holds only printable ASCII with no spaces or control characters.
bool isPrintableNoSpace(std::string_view text);

// "1920x1080"
std::string formatResolution(int width, int height);

// 6000 -> "6000 Kbps", 12500 -> "12.5 Mbps"
std::string formatBitrate(int kbps);

// 754 -> "12:34", 3723 -> "1:02:03"
std::string formatDuration(long long seconds);

} // namespace rd
