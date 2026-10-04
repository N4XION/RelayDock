// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "chat/chat_types.h"

namespace rd {

const char *chatPlatformId(ChatPlatform platform)
{
	switch (platform) {
	case ChatPlatform::Twitch:
		return "twitch";
	case ChatPlatform::YouTube:
		return "youtube";
	}
	return "twitch";
}

const char *chatPlatformName(ChatPlatform platform)
{
	switch (platform) {
	case ChatPlatform::Twitch:
		return "Twitch";
	case ChatPlatform::YouTube:
		return "YouTube";
	}
	return "Twitch";
}

const char *chatEventKindId(ChatEventKind kind)
{
	switch (kind) {
	case ChatEventKind::Message:
		return "message";
	case ChatEventKind::Paid:
		return "paid";
	case ChatEventKind::Membership:
		return "membership";
	case ChatEventKind::Raid:
		return "raid";
	case ChatEventKind::Notice:
		return "notice";
	}
	return "message";
}

const char *chatStateId(ChatState state)
{
	switch (state) {
	case ChatState::Off:
		return "off";
	case ChatState::NotSetUp:
		return "not_set_up";
	case ChatState::Connecting:
		return "connecting";
	case ChatState::Connected:
		return "connected";
	case ChatState::Waiting:
		return "waiting";
	case ChatState::Stopped:
		return "stopped";
	}
	return "off";
}

// ---- Time -------------------------------------------------------------------------------------

namespace {

bool readDigits(std::string_view text, size_t at, size_t count, int &out)
{
	if (at + count > text.size())
		return false;
	int value = 0;
	for (size_t i = 0; i < count; ++i) {
		const char c = text[at + i];
		if (c < '0' || c > '9')
			return false;
		value = value * 10 + (c - '0');
	}
	out = value;
	return true;
}

// Days between 1970-01-01 and a date of the Gregorian calendar.
int64_t daysFromCivil(int year, int month, int day)
{
	year -= month <= 2 ? 1 : 0;
	const int64_t era = (year >= 0 ? year : year - 399) / 400;
	const int64_t yearOfEra = year - era * 400;
	const int64_t dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
	const int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
	return era * 146097 + dayOfEra - 719468;
}

} // namespace

bool parseRfc3339Ms(std::string_view text, int64_t &outMs)
{
	int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
	if (text.size() < 20 || !readDigits(text, 0, 4, year) || text[4] != '-' || !readDigits(text, 5, 2, month) ||
	    text[7] != '-' || !readDigits(text, 8, 2, day) || (text[10] != 'T' && text[10] != 't') ||
	    !readDigits(text, 11, 2, hour) || text[13] != ':' || !readDigits(text, 14, 2, minute) || text[16] != ':' ||
	    !readDigits(text, 17, 2, second))
		return false;
	if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60)
		return false;

	size_t at = 19;
	int millis = 0;
	if (at < text.size() && text[at] == '.') {
		++at;
		int digits = 0;
		while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
			if (digits < 3)
				millis = millis * 10 + (text[at] - '0');
			++digits;
			++at;
		}
		if (digits == 0)
			return false;
		for (; digits < 3; ++digits)
			millis *= 10;
	}

	int64_t offsetMinutes = 0;
	if (at >= text.size())
		return false;
	if (text[at] == 'Z' || text[at] == 'z') {
		++at;
	} else if (text[at] == '+' || text[at] == '-') {
		int offsetHour = 0, offsetMinute = 0;
		if (!readDigits(text, at + 1, 2, offsetHour) || at + 3 >= text.size() || text[at + 3] != ':' ||
		    !readDigits(text, at + 4, 2, offsetMinute) || offsetHour > 23 || offsetMinute > 59)
			return false;
		offsetMinutes = (offsetHour * 60 + offsetMinute) * (text[at] == '-' ? -1 : 1);
		at += 6;
	} else {
		return false;
	}
	if (at != text.size())
		return false;

	const int64_t seconds = daysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second -
				offsetMinutes * 60;
	outMs = seconds * 1000 + millis;
	return true;
}

// ---- Text -------------------------------------------------------------------------------------

std::string cleanChatText(std::string_view text, size_t maxBytes)
{
	std::string out;
	out.reserve(text.size() < maxBytes ? text.size() : maxBytes);

	for (size_t i = 0; i < text.size();) {
		const auto byte = static_cast<unsigned char>(text[i]);

		// How long this character is. A byte that cannot start one becomes a question mark.
		size_t length = 1;
		if (byte >= 0xF0 && byte <= 0xF4)
			length = 4;
		else if (byte >= 0xE0 && byte < 0xF0)
			length = 3;
		else if (byte >= 0xC2 && byte < 0xE0)
			length = 2;
		else if (byte >= 0x80) {
			out += '?';
			++i;
			continue;
		}
		bool whole = i + length <= text.size();
		for (size_t k = 1; whole && k < length; ++k)
			whole = (static_cast<unsigned char>(text[i + k]) & 0xC0) == 0x80;
		if (!whole) {
			out += '?';
			++i;
			continue;
		}

		if (out.size() + length > maxBytes)
			break;

		if (length == 1) {
			// Line breaks and control characters would let a comment draw outside its line.
			out += (byte < 0x20 || byte == 0x7F) ? ' ' : static_cast<char>(byte);
		} else if (length == 3 && byte == 0xE2) {
			// Characters that reverse the reading direction can make a comment look as if
			// someone else wrote it. U+202A to U+202E and U+2066 to U+2069 are dropped.
			const auto second = static_cast<unsigned char>(text[i + 1]);
			const auto third = static_cast<unsigned char>(text[i + 2]);
			const bool direction = (second == 0x80 && third >= 0xAA && third <= 0xAE) ||
					       (second == 0x81 && third >= 0xA6 && third <= 0xA9);
			if (!direction)
				out.append(text.substr(i, length));
		} else {
			out.append(text.substr(i, length));
		}
		i += length;
	}
	return out;
}

// ---- ChatTimeline -----------------------------------------------------------------------------

ChatTimeline::ChatTimeline(size_t capacity) : capacity_(capacity > 0 ? capacity : 1) {}

size_t ChatTimeline::add(const std::vector<ChatEvent> &events, std::vector<ChatEvent> *added)
{
	size_t count = 0;
	for (const ChatEvent &event : events) {
		if (!event.id.empty()) {
			std::pair<int, std::string> key{static_cast<int>(event.platform), event.id};
			if (!seen_.insert(key).second)
				continue;
			seenOrder_.push_back(std::move(key));
			while (seenOrder_.size() > capacity_ * 2) {
				seen_.erase(seenOrder_.front());
				seenOrder_.pop_front();
			}
		}
		events_.push_back(event);
		if (added)
			added->push_back(event);
		++total_;
		++count;
	}
	while (events_.size() > capacity_)
		events_.pop_front();
	return count;
}

void ChatTimeline::clear()
{
	events_.clear();
	seen_.clear();
	seenOrder_.clear();
	total_ = 0;
}

} // namespace rd
