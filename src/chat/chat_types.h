// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "core/user_message.h"

#include <cstdint>
#include <deque>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rd {

// Live chat: what viewers write on each platform, and what the platform announces next to it,
// such as a gift or a new subscriber. RelayDock reads it through the interfaces the platforms
// publish for that, shows it in one list, and keeps none of it after OBS closes.

enum class ChatPlatform { Twitch, YouTube };

// "twitch". Used in settings and in test output, so do not change the text.
const char *chatPlatformId(ChatPlatform platform);
// "Twitch". A product name, the same in every language.
const char *chatPlatformName(ChatPlatform platform);

enum class ChatEventKind {
	Message,    // A comment
	Paid,       // Money or a paid gift: Bits, a Super Chat, a Super Sticker, a gift
	Membership, // A new subscriber or member, a renewal, subscriptions given to others
	Raid,       // Another channel sent its viewers over
	Notice,     // Anything else the platform announces in chat
};

// "message", "paid", "membership", "raid", "notice". Used in test output.
const char *chatEventKindId(ChatEventKind kind);

struct ChatEvent {
	ChatPlatform platform = ChatPlatform::Twitch;
	ChatEventKind kind = ChatEventKind::Message;
	std::string id;       // The platform's own id. Unique within one platform.
	int64_t timeMs = 0;   // When the platform says it happened. Unix time in milliseconds.
	std::string author;   // The name the platform shows
	std::string authorId;
	std::string text;     // What the viewer wrote. May be empty for an event.
	// For everything but a plain comment: what happened, in the platform's own words or built
	// from its numbers. "100 Bits", "NZ$5.00 Super Chat", "Kai gifted 5 subscriptions".
	std::string headline;
	bool fromBroadcaster = false;
	bool fromModerator = false;
	bool fromMember = false; // A subscriber on Twitch, a channel member on YouTube

	bool operator==(const ChatEvent &other) const = default;
};

// Whether an event is one a streamer wants to notice: everything but a plain comment.
inline bool isHighlight(const ChatEvent &event)
{
	return event.kind != ChatEventKind::Message;
}

// Reads a time such as "2026-10-05T01:02:03.464757833Z" or "2026-10-05T13:02:03+12:00".
// Returns false for anything else.
bool parseRfc3339Ms(std::string_view text, int64_t &outMs);

// Text from a platform, made safe to show: control characters and line breaks become spaces,
// and it is cut after `maxBytes` at a character boundary. Chat is untrusted input.
std::string cleanChatText(std::string_view text, size_t maxBytes = 2000);

// ---- The merged list --------------------------------------------------------------------------

// Every platform's events in the order they arrived, oldest first. It holds the newest
// `capacity` events and drops an event it has already seen, which happens when a platform
// sends something twice after a reconnect.
//
// Thread ownership: one thread. The interface owns one on the OBS UI thread.
class ChatTimeline {
public:
	explicit ChatTimeline(size_t capacity = 500);

	// Adds events. Returns how many were new.
	size_t add(const std::vector<ChatEvent> &events);
	void clear();

	const std::deque<ChatEvent> &events() const { return events_; }
	size_t capacity() const { return capacity_; }
	// Events added since the start or the last clear(), including the ones that dropped out.
	uint64_t total() const { return total_; }

private:
	size_t capacity_;
	std::deque<ChatEvent> events_;
	// Ids of the events still in the list, plus the same number of older ones.
	std::set<std::pair<int, std::string>> seen_;
	std::deque<std::pair<int, std::string>> seenOrder_;
	uint64_t total_ = 0;
};

// ---- Connections ------------------------------------------------------------------------------

enum class ChatState {
	Off,        // Not asked to read
	NotSetUp,   // Something the user must provide is missing
	Connecting,
	Connected,
	Waiting,    // The connection failed or ended. RelayDock tries again by itself.
	Stopped,    // RelayDock gave up. The message says what the user can do.
};

// "off", "not_set_up", "connecting", "connected", "waiting", "stopped". Used in test output.
const char *chatStateId(ChatState state);

struct ChatStatus {
	ChatState state = ChatState::Off;
	UserMessage message; // Empty while everything is fine
	std::string account; // Whose chat is read: a Twitch login or the title of a YouTube stream

	bool operator==(const ChatStatus &other) const = default;
};

} // namespace rd
