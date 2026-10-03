// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "utils/strings.h"

#include <algorithm>
#include <format>

namespace rd {

namespace {

char lowerAscii(char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool isSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

} // namespace

std::string trim(std::string_view text)
{
	size_t begin = 0;
	size_t end = text.size();
	while (begin < end && isSpace(text[begin]))
		++begin;
	while (end > begin && isSpace(text[end - 1]))
		--end;
	return std::string(text.substr(begin, end - begin));
}

std::string toLower(std::string_view text)
{
	std::string out(text);
	std::transform(out.begin(), out.end(), out.begin(), lowerAscii);
	return out;
}

bool equalsNoCase(std::string_view a, std::string_view b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i) {
		if (lowerAscii(a[i]) != lowerAscii(b[i]))
			return false;
	}
	return true;
}

bool startsWithNoCase(std::string_view text, std::string_view prefix)
{
	return text.size() >= prefix.size() && equalsNoCase(text.substr(0, prefix.size()), prefix);
}

bool containsNoCase(std::string_view text, std::string_view needle)
{
	if (needle.empty())
		return true;
	if (needle.size() > text.size())
		return false;
	for (size_t i = 0; i + needle.size() <= text.size(); ++i) {
		if (equalsNoCase(text.substr(i, needle.size()), needle))
			return true;
	}
	return false;
}

std::vector<std::string> split(std::string_view text, char separator, bool keepEmpty)
{
	std::vector<std::string> parts;
	size_t start = 0;
	while (start <= text.size()) {
		size_t end = text.find(separator, start);
		if (end == std::string_view::npos)
			end = text.size();
		std::string_view part = text.substr(start, end - start);
		if (keepEmpty || !part.empty())
			parts.emplace_back(part);
		start = end + 1;
	}
	return parts;
}

std::string join(const std::vector<std::string> &parts, std::string_view separator)
{
	std::string out;
	for (size_t i = 0; i < parts.size(); ++i) {
		if (i > 0)
			out += separator;
		out += parts[i];
	}
	return out;
}

std::string replaceAll(std::string text, std::string_view from, std::string_view to)
{
	if (from.empty())
		return text;
	size_t pos = 0;
	while ((pos = text.find(from, pos)) != std::string::npos) {
		text.replace(pos, from.size(), to);
		pos += to.size();
	}
	return text;
}

bool isPrintableNoSpace(std::string_view text)
{
	for (unsigned char c : text) {
		if (c <= 0x20 || c >= 0x7F)
			return false;
	}
	return true;
}

std::string formatResolution(int width, int height)
{
	return std::format("{}x{}", width, height);
}

std::string formatBitrate(int kbps)
{
	if (kbps >= 10000) {
		const double mbps = kbps / 1000.0;
		if (kbps % 1000 == 0)
			return std::format("{} Mbps", kbps / 1000);
		return std::format("{:.1f} Mbps", mbps);
	}
	return std::format("{} Kbps", kbps);
}

std::string formatDuration(long long seconds)
{
	if (seconds < 0)
		seconds = 0;
	const long long hours = seconds / 3600;
	const long long minutes = (seconds % 3600) / 60;
	const long long secs = seconds % 60;
	if (hours > 0)
		return std::format("{}:{:02}:{:02}", hours, minutes, secs);
	return std::format("{}:{:02}", minutes, secs);
}

} // namespace rd
