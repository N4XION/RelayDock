// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "chat/chat_worker.h"
#include "chat/youtube_protocol.h"

#include <atomic>
#include <string>

namespace rd {

struct YouTubeChatConfig {
	youtube::Endpoints endpoints;
	std::string apiKey;  // The user's own key. It stays in memory and in Windows Credential Manager.
	std::string videoId; // The stream whose chat is read
	std::string userAgent = "RelayDock";
	int timeoutMs = 10000;
	// The shortest pause between two requests for messages. YouTube names a pause with every
	// answer, and RelayDock waits for the longer of the two. Each request costs quota of the
	// user's key, so a longer pause makes the key last longer.
	long long minPollMs = 5000;
	// How often to look again while the stream has not started.
	long long notLiveRetryMs = 60000;
	long long retryFirstMs = 5000;
	long long retryMaxMs = 60000;
};

// Reads the chat of one YouTube live stream by asking for new messages again and again:
// comments, Super Chats, Super Stickers, new members and gifts.
class YouTubeChat : public ChatWorker {
public:
	explicit YouTubeChat(Callbacks callbacks);
	~YouTubeChat() override;

	void start(YouTubeChatConfig config);

	// Requests sent to YouTube since the object was made. Each costs quota of the user's key.
	unsigned long long requests() const { return requests_.load(); }

private:
	void run();

	YouTubeChatConfig config_;
	std::atomic<unsigned long long> requests_{0};
};

} // namespace rd
