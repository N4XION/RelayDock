// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "chat/youtube_chat.h"

#include "security/redactor.h"
#include "utils/i18n.h"
#include "utils/log.h"

namespace rd {

namespace {

UserMessage notSetUp(const std::string &what)
{
	UserMessage message;
	message.what = what;
	message.action = loc("Chat.YouTube.SetUp.Action", "Open Settings, Chat. The chat guide shows how to get a key.");
	return message;
}

// The words for a problem RelayDock retries, with the time of the next try.
UserMessage retrying(youtube::ApiResult result, const std::string &detail, long long waitMs)
{
	UserMessage message = youtube::describeProblem(result, detail);
	if (result == youtube::ApiResult::Failed) {
		const long long seconds = waitMs < 1000 ? 1 : (waitMs + 500) / 1000;
		message.action = locn(seconds, "Chat.RetryIn.One", "RelayDock tries again in {0} second.", "Chat.RetryIn.Many",
				      "RelayDock tries again in {0} seconds.", seconds);
	}
	return message;
}

} // namespace

UserMessage youtubeNoKeyMessage()
{
	return notSetUp(loc("Chat.YouTube.NoKey", "YouTube chat needs an API key of your own."));
}

UserMessage youtubeNoVideoMessage()
{
	return notSetUp(loc("Chat.YouTube.NoVideo", "YouTube chat needs the link to your stream."));
}

YouTubeChat::YouTubeChat(Callbacks callbacks) : ChatWorker(std::move(callbacks)) {}

YouTubeChat::~YouTubeChat()
{
	stop();
}

void YouTubeChat::start(YouTubeChatConfig config)
{
	stop();
	config_ = std::move(config);
	launch([this] { run(); });
}

void YouTubeChat::run()
{
	if (!youtube::plausibleApiKey(config_.apiKey)) {
		report(ChatState::NotSetUp, youtubeNoKeyMessage());
		return;
	}
	if (config_.videoId.empty()) {
		report(ChatState::NotSetUp, youtubeNoVideoMessage());
		return;
	}
	// The key never reaches a log line or a diagnostics report.
	globalRedactor().addSecret(config_.apiKey);

	const auto send = [&](HttpRequest request) {
		request.userAgent = config_.userAgent;
		request.timeoutMs = config_.timeoutMs;
		++requests_;
		return httpRequest(request, cancel_);
	};

	RetryDelay delay(config_.retryFirstMs, config_.retryMaxMs);
	std::string error;

	// Find the chat of the stream.
	youtube::Video video;
	report(ChatState::Connecting);
	for (;;) {
		const HttpResponse response = send(youtube::videoRequest(config_.endpoints, config_.apiKey, config_.videoId));
		if (cancelled())
			return;
		const youtube::ApiResult result = youtube::parseVideo(response, video, error);
		if (result == youtube::ApiResult::Ok)
			break;
		if (!youtube::worthRetrying(result)) {
			logWarning("YouTube chat stopped: {}. {}", youtube::apiResultId(result), error);
			report(ChatState::Stopped, youtube::describeProblem(result, error), video.title);
			return;
		}
		const long long wait = result == youtube::ApiResult::NotLive ? config_.notLiveRetryMs : delay.next();
		report(ChatState::Waiting, retrying(result, error, wait), video.title);
		if (!sleepFor(wait))
			return;
	}

	delay.reset();
	report(ChatState::Connected, {}, video.title);
	logInfo("Reading the YouTube chat of \"{}\".", video.title);

	// Ask for messages, wait as long as YouTube says, ask again.
	std::string pageToken;
	for (;;) {
		const HttpResponse response =
			send(youtube::messagesRequest(config_.endpoints, config_.apiKey, video.liveChatId, pageToken));
		if (cancelled())
			return;

		youtube::Page page;
		const youtube::ApiResult result = youtube::parseMessages(response, page, error);
		if (result == youtube::ApiResult::Ok) {
			delay.reset();
			report(ChatState::Connected, {}, video.title);
			deliver(std::move(page.events));
			if (page.ended) {
				logInfo("The YouTube stream \"{}\" has ended.", video.title);
				report(ChatState::Stopped, youtube::describeProblem(youtube::ApiResult::ChatEnded, {}), video.title);
				return;
			}
			if (!page.nextPageToken.empty())
				pageToken = page.nextPageToken;
			const long long wait = page.pollingIntervalMs > config_.minPollMs ? page.pollingIntervalMs : config_.minPollMs;
			if (!sleepFor(wait))
				return;
			continue;
		}

		if (result != youtube::ApiResult::Failed) {
			logWarning("YouTube chat stopped: {}. {}", youtube::apiResultId(result), error);
			report(ChatState::Stopped, youtube::describeProblem(result, error), video.title);
			return;
		}
		const long long wait = delay.next();
		report(ChatState::Waiting, retrying(result, error, wait), video.title);
		if (!sleepFor(wait))
			return;
	}
}

} // namespace rd
