// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "chat/chat_types.h"
#include "network/http_client.h"

#include <string>
#include <string_view>
#include <vector>

// What RelayDock asks the YouTube Data API and how it reads the answers. Nothing here touches
// the network.
//
// RelayDock reads the chat of a public live stream with an API key and the stream's video id.
// YouTube's developer policies forbid shipping an API key in an open-source project, so the key
// is one the user creates, and RelayDock keeps it in Windows Credential Manager. There is no
// sign-in.
//
// Sources: developers.google.com/youtube/v3/live/docs/liveChatMessages,
// developers.google.com/youtube/v3/live/streaming-live-chat and
// developers.google.com/youtube/terms/developer-policies, read on 2026-10-04.
namespace rd::youtube {

struct Endpoints {
	std::string api = "https://www.googleapis.com";
};

// Takes the video id out of what the user pasted: a link to the stream in any of YouTube's
// forms, or the id itself. Returns false when the text holds no video id.
bool parseVideoId(std::string_view input, std::string &videoId);

// An API key is made of letters, digits, "-" and "_". This catches a pasted link or sentence.
bool plausibleApiKey(const std::string &key);

enum class ApiResult {
	Ok,
	NotFound,      // No such video, or no such chat
	NotLive,       // The video exists and has no live chat right now
	KeyRejected,   // Google does not accept the API key
	ApiNotEnabled, // The key's project has the YouTube Data API switched off
	QuotaExceeded, // The key's project has used its requests for today
	ChatEnded,     // The stream is over
	Forbidden,     // The chat is not public
	Failed,        // The request or its answer could not be used. Worth another try.
};

// "ok", "not_found", ... Used in test output.
const char *apiResultId(ApiResult result);

struct Video {
	std::string title;
	std::string channelTitle;
	std::string liveChatId;
};

// Asks for the stream's chat. Costs one unit of the key's daily quota.
HttpRequest videoRequest(const Endpoints &endpoints, const std::string &apiKey, const std::string &videoId);
ApiResult parseVideo(const HttpResponse &response, Video &out, std::string &error);

struct Page {
	std::vector<ChatEvent> events;
	std::string nextPageToken;
	int pollingIntervalMs = 0; // YouTube says how long to wait before the next request
	bool ended = false;        // The stream went offline or its chat ended
};

// Asks for the messages since `pageToken`. An empty token asks for the newest ones.
HttpRequest messagesRequest(const Endpoints &endpoints, const std::string &apiKey, const std::string &liveChatId,
			    const std::string &pageToken);
ApiResult parseMessages(const HttpResponse &response, Page &out, std::string &error);

// What to tell the user, and what they can do about it.
UserMessage describeProblem(ApiResult result, const std::string &detail);

// Whether RelayDock should try again by itself after this result.
bool worthRetrying(ApiResult result);

} // namespace rd::youtube
