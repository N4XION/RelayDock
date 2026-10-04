// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "chat/youtube_protocol.h"

#include "utils/i18n.h"
#include "utils/strings.h"

#include <nlohmann/json.hpp>

#include <format>

namespace rd::youtube {

namespace {

using Json = nlohmann::json;

Json parseJson(const std::string &text)
{
	return Json::parse(text, nullptr, false);
}

std::string text(const Json &object, const char *key)
{
	if (!object.is_object())
		return {};
	const auto it = object.find(key);
	return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

long long number(const Json &object, const char *key, long long fallback = 0)
{
	if (!object.is_object())
		return fallback;
	const auto it = object.find(key);
	if (it == object.end())
		return fallback;
	if (it->is_number_integer())
		return it->get<long long>();
	// The API writes some numbers as text, such as "amountMicros": "5000000".
	if (it->is_string()) {
		const std::string value = it->get<std::string>();
		if (!value.empty() && value.size() < 19 && value.find_first_not_of("0123456789") == std::string::npos)
			return std::stoll(value);
	}
	return fallback;
}

bool flag(const Json &object, const char *key)
{
	if (!object.is_object())
		return false;
	const auto it = object.find(key);
	return it != object.end() && it->is_boolean() && it->get<bool>();
}

const Json &child(const Json &object, const char *key)
{
	static const Json none;
	if (!object.is_object())
		return none;
	const auto it = object.find(key);
	return it != object.end() ? *it : none;
}

bool idCharacter(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

// The eleven characters at `at`, when they are a video id that ends there.
bool idAt(std::string_view input, size_t at, std::string &videoId)
{
	if (at + 11 > input.size())
		return false;
	for (size_t i = 0; i < 11; ++i) {
		if (!idCharacter(input[at + i]))
			return false;
	}
	if (at + 11 < input.size() && idCharacter(input[at + 11]))
		return false;
	videoId = std::string(input.substr(at, 11));
	return true;
}

HttpRequest get(const std::string &url, const std::string &apiKey)
{
	HttpRequest request;
	request.url = url;
	// The key travels in a header, so it is never part of an address that could end up in a log.
	request.headers = {{"X-Goog-Api-Key", apiKey}, {"Accept", "application/json"}};
	request.maxBytes = 2 * 1024 * 1024;
	return request;
}

// Reads an error answer. Returns Ok when the answer is not an error.
ApiResult readError(const HttpResponse &response, std::string &error)
{
	if (!response.ok) {
		error = response.error;
		return ApiResult::Failed;
	}
	if (response.status == 200)
		return ApiResult::Ok;

	const Json root = parseJson(response.body);
	const Json &problem = child(root, "error");
	std::string reason;
	if (const Json &errors = child(problem, "errors"); errors.is_array() && !errors.empty())
		reason = text(errors.front(), "reason");
	std::string detailReason;
	if (const Json &details = child(problem, "details"); details.is_array()) {
		for (const Json &detail : details) {
			if (const std::string value = text(detail, "reason"); !value.empty())
				detailReason = value;
		}
	}
	const std::string message = cleanChatText(text(problem, "message"), 300);
	error = message.empty() ? std::format("YouTube answered with status {}.", response.status)
				: std::format("YouTube answered with status {}: {}", response.status, message);

	if (reason == "quotaExceeded" || reason == "dailyLimitExceeded")
		return ApiResult::QuotaExceeded;
	if (reason == "keyInvalid" || reason == "keyExpired" || detailReason == "API_KEY_INVALID" ||
	    detailReason == "API_KEY_HTTP_REFERRER_BLOCKED" || detailReason == "API_KEY_IP_ADDRESS_BLOCKED" ||
	    detailReason == "API_KEY_SERVICE_BLOCKED" || reason == "ipRefererBlocked")
		return ApiResult::KeyRejected;
	if (reason == "accessNotConfigured" || detailReason == "SERVICE_DISABLED")
		return ApiResult::ApiNotEnabled;
	if (reason == "liveChatEnded")
		return ApiResult::ChatEnded;
	if (reason == "liveChatNotFound" || response.status == 404)
		return ApiResult::NotFound;
	if (reason == "liveChatDisabled" || reason == "forbidden")
		return ApiResult::Forbidden;
	// Anything else, including "too many requests in a short time", may pass.
	return ApiResult::Failed;
}

bool readEvent(const Json &item, ChatEvent &out, bool &chatEnded)
{
	const Json &snippet = child(item, "snippet");
	const Json &author = child(item, "authorDetails");
	const std::string type = text(snippet, "type");

	out.platform = ChatPlatform::YouTube;
	out.id = text(item, "id");
	parseRfc3339Ms(text(snippet, "publishedAt"), out.timeMs);
	out.author = cleanChatText(text(author, "displayName"), 100);
	out.authorId = text(author, "channelId");
	out.fromBroadcaster = flag(author, "isChatOwner");
	out.fromModerator = flag(author, "isChatModerator");
	out.fromMember = flag(author, "isChatSponsor");

	// YouTube's own sentence for the event, such as "NZ$5.00 from Kai: hello".
	const std::string display = cleanChatText(text(snippet, "displayMessage"), 500);

	if (type == "textMessageEvent") {
		out.kind = ChatEventKind::Message;
		out.text = cleanChatText(text(child(snippet, "textMessageDetails"), "messageText"));
		if (out.text.empty())
			out.text = display;
	} else if (type == "superChatEvent") {
		const Json &details = child(snippet, "superChatDetails");
		const std::string amount = cleanChatText(text(details, "amountDisplayString"), 40);
		out.kind = ChatEventKind::Paid;
		out.headline = amount.empty() ? loc("Chat.YouTube.SuperChatPlain", "Super Chat")
					      : locf("Chat.YouTube.SuperChat", "{0} Super Chat", amount);
		out.text = cleanChatText(text(details, "userComment"));
	} else if (type == "superStickerEvent") {
		const Json &details = child(snippet, "superStickerDetails");
		const std::string amount = cleanChatText(text(details, "amountDisplayString"), 40);
		out.kind = ChatEventKind::Paid;
		out.headline = amount.empty() ? loc("Chat.YouTube.SuperStickerPlain", "Super Sticker")
					      : locf("Chat.YouTube.SuperSticker", "{0} Super Sticker", amount);
		out.text = cleanChatText(text(child(details, "superStickerMetadata"), "altText"), 200);
	} else if (type == "giftEvent") {
		const Json &gift = child(child(snippet, "giftEventDetails"), "giftMetadata");
		const std::string name = cleanChatText(text(gift, "giftName"), 80);
		const long long jewels = number(gift, "jewelsAmount");
		out.kind = ChatEventKind::Paid;
		if (!name.empty() && jewels > 0)
			out.headline = locf("Chat.YouTube.Gift", "Gift: {0} ({1} Jewels)", name, jewels);
		else if (!name.empty())
			out.headline = locf("Chat.YouTube.GiftPlain", "Gift: {0}", name);
		else
			out.headline = display.empty() ? loc("Chat.YouTube.GiftUnnamed", "A gift") : display;
	} else if (type == "newSponsorEvent") {
		out.kind = ChatEventKind::Membership;
		out.headline = display.empty() ? loc("Chat.YouTube.NewMember", "New member") : display;
	} else if (type == "memberMilestoneChatEvent") {
		const Json &details = child(snippet, "memberMilestoneChatDetails");
		const long long months = number(details, "memberMonth");
		out.kind = ChatEventKind::Membership;
		out.headline = months > 0 ? locn(months, "Chat.YouTube.MemberMonth", "Member for {0} month", "Chat.YouTube.MemberMonths",
						 "Member for {0} months", months)
					  : (display.empty() ? loc("Chat.YouTube.Milestone", "Member milestone") : display);
		out.text = cleanChatText(text(details, "userComment"));
	} else if (type == "membershipGiftingEvent") {
		const long long count = number(child(snippet, "membershipGiftingDetails"), "giftMembershipsCount");
		out.kind = ChatEventKind::Membership;
		out.headline = count > 0 ? locn(count, "Chat.YouTube.GiftedMembership", "Gifted {0} membership",
						"Chat.YouTube.GiftedMemberships", "Gifted {0} memberships", count)
					 : (display.empty() ? loc("Chat.YouTube.GiftedSome", "Gifted memberships") : display);
	} else if (type == "chatEndedEvent") {
		chatEnded = true;
		return false;
	} else {
		// One notice per receiver of a gifted membership, bans, deletions, polls and modes.
		// None of them is a comment or a gift to the streamer.
		return false;
	}
	return !out.id.empty();
}

} // namespace

const char *apiResultId(ApiResult result)
{
	switch (result) {
	case ApiResult::Ok:
		return "ok";
	case ApiResult::NotFound:
		return "not_found";
	case ApiResult::NotLive:
		return "not_live";
	case ApiResult::KeyRejected:
		return "key_rejected";
	case ApiResult::ApiNotEnabled:
		return "api_not_enabled";
	case ApiResult::QuotaExceeded:
		return "quota_exceeded";
	case ApiResult::ChatEnded:
		return "chat_ended";
	case ApiResult::Forbidden:
		return "forbidden";
	case ApiResult::Failed:
		return "failed";
	}
	return "failed";
}

bool parseVideoId(std::string_view input, std::string &videoId)
{
	const std::string trimmed = trim(input);
	const std::string_view value = trimmed;
	if (value.size() == 11 && idAt(value, 0, videoId))
		return true;

	// Anything longer must be a link to YouTube.
	const std::string lower = toLower(value);
	const bool youtube = lower.find("youtube.com/") != std::string::npos || lower.find("youtu.be/") != std::string::npos;
	if (!youtube)
		return false;

	static constexpr std::string_view markers[] = {"youtu.be/", "?v=", "&v=", "/live/", "/shorts/", "/embed/", "/video/"};
	for (const std::string_view marker : markers) {
		const size_t at = lower.find(marker);
		if (at != std::string::npos && idAt(value, at + marker.size(), videoId))
			return true;
	}
	return false;
}

bool plausibleApiKey(const std::string &key)
{
	if (key.size() < 20 || key.size() > 100)
		return false;
	for (const char c : key) {
		if (!idCharacter(c))
			return false;
	}
	return true;
}

HttpRequest videoRequest(const Endpoints &endpoints, const std::string &apiKey, const std::string &videoId)
{
	return get(std::format("{}/youtube/v3/videos?part=snippet,liveStreamingDetails&id={}", endpoints.api, urlEncode(videoId)),
		   apiKey);
}

ApiResult parseVideo(const HttpResponse &response, Video &out, std::string &error)
{
	if (const ApiResult problem = readError(response, error); problem != ApiResult::Ok)
		return problem;

	const Json root = parseJson(response.body);
	const Json &items = child(root, "items");
	if (!items.is_array()) {
		error = "YouTube's answer is not a list of videos.";
		return ApiResult::Failed;
	}
	if (items.empty())
		return ApiResult::NotFound;

	const Json &item = items.front();
	const Json &snippet = child(item, "snippet");
	const Json &live = child(item, "liveStreamingDetails");
	Video video;
	video.title = cleanChatText(text(snippet, "title"), 200);
	video.channelTitle = cleanChatText(text(snippet, "channelTitle"), 100);
	video.liveChatId = text(live, "activeLiveChatId");
	out = video;
	if (video.liveChatId.empty())
		return text(live, "actualEndTime").empty() ? ApiResult::NotLive : ApiResult::ChatEnded;
	return ApiResult::Ok;
}

HttpRequest messagesRequest(const Endpoints &endpoints, const std::string &apiKey, const std::string &liveChatId,
			    const std::string &pageToken)
{
	std::string url = std::format("{}/youtube/v3/liveChat/messages?liveChatId={}&part=snippet,authorDetails&maxResults=200",
				      endpoints.api, urlEncode(liveChatId));
	if (!pageToken.empty())
		url += "&pageToken=" + urlEncode(pageToken);
	return get(url, apiKey);
}

ApiResult parseMessages(const HttpResponse &response, Page &out, std::string &error)
{
	if (const ApiResult problem = readError(response, error); problem != ApiResult::Ok)
		return problem;

	const Json root = parseJson(response.body);
	if (!root.is_object()) {
		error = "YouTube's answer is not valid JSON.";
		return ApiResult::Failed;
	}

	Page page;
	page.nextPageToken = text(root, "nextPageToken");
	page.pollingIntervalMs = static_cast<int>(number(root, "pollingIntervalMillis"));
	page.ended = !text(root, "offlineAt").empty();
	if (const Json &items = child(root, "items"); items.is_array()) {
		for (const Json &item : items) {
			ChatEvent event;
			if (readEvent(item, event, page.ended))
				page.events.push_back(std::move(event));
		}
	}
	out = std::move(page);
	return ApiResult::Ok;
}

UserMessage describeProblem(ApiResult result, const std::string &detail)
{
	UserMessage message;
	switch (result) {
	case ApiResult::Ok:
		break;
	case ApiResult::NotFound:
		message.what = loc("Chat.YouTube.NotFound", "YouTube found no stream for this link.");
		message.action = loc("Chat.YouTube.NotFound.Action", "Check the link to your stream under Settings, Chat.");
		break;
	case ApiResult::NotLive:
		message.what = loc("Chat.YouTube.NotLive", "This YouTube stream has no live chat right now.");
		message.action = loc("Chat.YouTube.NotLive.Action", "RelayDock looks again every minute. Start the stream, or paste the link of the stream that is live.");
		break;
	case ApiResult::KeyRejected:
		message.what = loc("Chat.YouTube.KeyRejected", "Google did not accept your API key.");
		message.detail = detail;
		message.action = loc("Chat.YouTube.KeyRejected.Action", "Check the key under Settings, Chat. A key with restrictions must allow the YouTube Data API from this PC.");
		break;
	case ApiResult::ApiNotEnabled:
		message.what = loc("Chat.YouTube.ApiOff", "The Google project of your API key has the YouTube Data API switched off.");
		message.action = loc("Chat.YouTube.ApiOff.Action", "Open the project in the Google Cloud console and switch on YouTube Data API v3.");
		break;
	case ApiResult::QuotaExceeded:
		message.what = loc("Chat.YouTube.Quota", "Your API key has used its requests for today.");
		message.action = loc("Chat.YouTube.Quota.Action", "YouTube gives every key a daily amount, which starts again at midnight Pacific Time. A longer pause between requests under Settings, Chat makes it last longer.");
		break;
	case ApiResult::ChatEnded:
		message.what = loc("Chat.YouTube.Ended", "The YouTube stream has ended.");
		message.action = loc("Chat.YouTube.Ended.Action", "Paste the link of your next stream under Settings, Chat.");
		break;
	case ApiResult::Forbidden:
		message.what = loc("Chat.YouTube.Forbidden", "YouTube does not let an API key read this chat.");
		message.detail = detail;
		message.action = loc("Chat.YouTube.Forbidden.Action", "The stream must be public or unlisted, with its chat switched on.");
		break;
	case ApiResult::Failed:
		message.what = loc("Chat.YouTube.Failed", "YouTube's chat could not be read.");
		message.detail = detail;
		message.action = loc("Chat.YouTube.Failed.Action", "RelayDock tries again by itself.");
		break;
	}
	return message;
}

bool worthRetrying(ApiResult result)
{
	return result == ApiResult::Failed || result == ApiResult::NotLive;
}

} // namespace rd::youtube
