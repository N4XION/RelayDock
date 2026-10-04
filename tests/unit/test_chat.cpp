// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include <doctest/doctest.h>

#include "chat/chat_types.h"
#include "chat/twitch_protocol.h"
#include "chat/youtube_protocol.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

using namespace rd;
using Json = nlohmann::json;

namespace {

HttpResponse answer(int status, const std::string &body)
{
	HttpResponse response;
	response.ok = true;
	response.status = status;
	response.body = body;
	return response;
}

HttpResponse networkFailure(const std::string &error)
{
	HttpResponse response;
	response.error = error;
	return response;
}

bool contains(const std::string &text, const std::string &part)
{
	return text.find(part) != std::string::npos;
}

std::string header(const HttpRequest &request, const std::string &name)
{
	for (const auto &[key, value] : request.headers) {
		if (key == name)
			return value;
	}
	return {};
}

ChatEvent comment(ChatPlatform platform, const std::string &id, const std::string &text = "hi")
{
	ChatEvent event;
	event.platform = platform;
	event.id = id;
	event.author = "viewer";
	event.text = text;
	return event;
}

// A Twitch EventSub message, built the way Twitch documents it.
std::string twitchFrame(const std::string &type, const Json &payload, const std::string &subscription = "")
{
	Json metadata = {{"message_id", "msg-" + type},
			 {"message_type", type},
			 {"message_timestamp", "2026-10-05T01:02:03.464757833Z"}};
	if (!subscription.empty()) {
		metadata["subscription_type"] = subscription;
		metadata["subscription_version"] = "1";
	}
	return Json{{"metadata", metadata}, {"payload", payload}}.dump();
}

Json twitchChatEvent()
{
	return {
		{"broadcaster_user_id", "1971641"},
		{"broadcaster_user_login", "streamer"},
		{"broadcaster_user_name", "streamer"},
		{"chatter_user_id", "4145994"},
		{"chatter_user_login", "viewer32"},
		{"chatter_user_name", "viewer32"},
		{"message_id", "cc106a89-1814-919d-454c-f4f2f970aae7"},
		{"message", {{"text", "Hi chat"}, {"fragments", Json::array()}}},
		{"color", "#00FF7F"},
		{"badges", Json::array({{{"set_id", "moderator"}, {"id", "1"}, {"info", ""}},
					{{"set_id", "subscriber"}, {"id", "12"}, {"info", "16"}}})},
		{"message_type", "text"},
		{"cheer", nullptr},
		{"reply", nullptr},
		{"channel_points_custom_reward_id", nullptr},
	};
}

Json youtubeItem(const std::string &id, const std::string &type, const Json &details, const std::string &detailsKey,
		 const std::string &display = "")
{
	Json snippet = {{"type", type},
			{"liveChatId", "chat-1"},
			{"authorChannelId", "UCviewer"},
			{"publishedAt", "2026-10-05T01:02:03.500+00:00"},
			{"hasDisplayContent", true},
			{"displayMessage", display}};
	if (!detailsKey.empty())
		snippet[detailsKey] = details;
	return {{"kind", "youtube#liveChatMessage"},
		{"id", id},
		{"snippet", snippet},
		{"authorDetails",
		 {{"channelId", "UCviewer"},
		  {"displayName", "Viewer One"},
		  {"isChatOwner", false},
		  {"isChatModerator", false},
		  {"isChatSponsor", true}}}};
}

} // namespace

TEST_SUITE("chat.types")
{
	TEST_CASE("a platform's time is read to the millisecond")
	{
		int64_t ms = 0;
		REQUIRE(parseRfc3339Ms("1970-01-01T00:00:00Z", ms));
		CHECK(ms == 0);
		REQUIRE(parseRfc3339Ms("2026-10-05T01:02:03Z", ms));
		CHECK(ms == 1791162123000LL);
		// Twitch writes nanoseconds.
		REQUIRE(parseRfc3339Ms("2026-10-05T01:02:03.464757833Z", ms));
		CHECK(ms == 1791162123464LL);
		REQUIRE(parseRfc3339Ms("2026-10-05T01:02:03.5Z", ms));
		CHECK(ms == 1791162123500LL);
		// YouTube writes an offset.
		REQUIRE(parseRfc3339Ms("2026-10-05T01:02:03.500+00:00", ms));
		CHECK(ms == 1791162123500LL);
		REQUIRE(parseRfc3339Ms("2026-10-05T14:02:03+13:00", ms));
		CHECK(ms == 1791162123000LL);
		REQUIRE(parseRfc3339Ms("2026-10-04T20:02:03-05:00", ms));
		CHECK(ms == 1791162123000LL);
		// A leap day.
		REQUIRE(parseRfc3339Ms("2024-02-29T12:00:00Z", ms));
		CHECK(ms == 1709208000000LL);
	}

	TEST_CASE("text that is not a time is refused")
	{
		int64_t ms = 7;
		CHECK_FALSE(parseRfc3339Ms("", ms));
		CHECK_FALSE(parseRfc3339Ms("yesterday", ms));
		CHECK_FALSE(parseRfc3339Ms("2026-10-05", ms));
		CHECK_FALSE(parseRfc3339Ms("2026-10-05T01:02:03", ms));
		CHECK_FALSE(parseRfc3339Ms("2026-13-05T01:02:03Z", ms));
		CHECK_FALSE(parseRfc3339Ms("2026-10-05T25:02:03Z", ms));
		CHECK_FALSE(parseRfc3339Ms("2026-10-05T01:02:03.Z", ms));
		CHECK_FALSE(parseRfc3339Ms("2026-10-05T01:02:03Zjunk", ms));
		CHECK_FALSE(parseRfc3339Ms("2026-10-05T01:02:03+1300", ms));
		CHECK(ms == 7);
	}

	TEST_CASE("chat text cannot break out of its line")
	{
		CHECK(cleanChatText("hello") == "hello");
		CHECK(cleanChatText("two\nlines\r\nand\ttabs") == "two lines  and tabs");
		CHECK(cleanChatText(std::string("nul\0byte", 8)) == "nul byte");
		CHECK(cleanChatText("del\x7f.") == "del .");
		// Markup stays text. The interface escapes it when it draws.
		CHECK(cleanChatText("<b>bold</b> & <img src=x>") == "<b>bold</b> & <img src=x>");
	}

	TEST_CASE("characters that reverse the reading direction are dropped")
	{
		// U+202E makes what follows read right to left, which can fake who wrote a line.
		CHECK(cleanChatText("abc\xE2\x80\xAE" "def") == "abcdef");
		CHECK(cleanChatText("\xE2\x81\xA6x\xE2\x81\xA9") == "x");
		// Other characters of the same block stay: a dash and an ellipsis.
		CHECK(cleanChatText("a\xE2\x80\x93" "b\xE2\x80\xA6") == "a\xE2\x80\x93" "b\xE2\x80\xA6");
	}

	TEST_CASE("text in other scripts and emoji stay whole, broken bytes do not get through")
	{
		const std::string japanese = "\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF";
		const std::string emoji = "\xF0\x9F\x8E\x89";
		CHECK(cleanChatText(japanese) == japanese);
		CHECK(cleanChatText(emoji) == emoji);
		// A lone continuation byte, a cut-off character, and bytes UTF-8 never uses.
		CHECK(cleanChatText("a\x80z") == "a?z");
		CHECK(cleanChatText("a\xE3\x81") == "a??");
		CHECK(cleanChatText("a\xFF\xC0z") == "a??z");
	}

	TEST_CASE("long text is cut at a character, not inside one")
	{
		CHECK(cleanChatText("abcdefgh", 5) == "abcde");
		const std::string emoji = "\xF0\x9F\x8E\x89";
		CHECK(cleanChatText(emoji + emoji + emoji, 9) == emoji + emoji);
		CHECK(cleanChatText(emoji, 3).empty());
		CHECK(cleanChatText(std::string(5000, 'x')).size() == 2000);
	}

	TEST_CASE("names used in settings and test output")
	{
		CHECK(std::string(chatPlatformId(ChatPlatform::Twitch)) == "twitch");
		CHECK(std::string(chatPlatformId(ChatPlatform::YouTube)) == "youtube");
		CHECK(std::string(chatPlatformName(ChatPlatform::YouTube)) == "YouTube");
		CHECK(std::string(chatEventKindId(ChatEventKind::Paid)) == "paid");
		CHECK(std::string(chatStateId(ChatState::NotSetUp)) == "not_set_up");
	}
}

TEST_SUITE("chat.timeline")
{
	TEST_CASE("events from every platform share one list, in the order they arrived")
	{
		ChatTimeline timeline;
		CHECK(timeline.add({comment(ChatPlatform::Twitch, "t1"), comment(ChatPlatform::YouTube, "y1")}) == 2);
		CHECK(timeline.add({comment(ChatPlatform::Twitch, "t2")}) == 1);
		REQUIRE(timeline.events().size() == 3);
		CHECK(timeline.events()[0].id == "t1");
		CHECK(timeline.events()[1].id == "y1");
		CHECK(timeline.events()[2].id == "t2");
		CHECK(timeline.total() == 3);
	}

	TEST_CASE("an event that arrives twice is shown once")
	{
		ChatTimeline timeline;
		timeline.add({comment(ChatPlatform::Twitch, "same")});
		CHECK(timeline.add({comment(ChatPlatform::Twitch, "same"), comment(ChatPlatform::Twitch, "new")}) == 1);
		CHECK(timeline.events().size() == 2);
		// The same id on another platform is another event.
		CHECK(timeline.add({comment(ChatPlatform::YouTube, "same")}) == 1);
		CHECK(timeline.events().size() == 3);
	}

	TEST_CASE("the list keeps the newest events and still knows what it has seen")
	{
		ChatTimeline timeline(3);
		for (int i = 1; i <= 5; ++i)
			timeline.add({comment(ChatPlatform::Twitch, "id" + std::to_string(i))});
		REQUIRE(timeline.events().size() == 3);
		CHECK(timeline.events().front().id == "id3");
		CHECK(timeline.events().back().id == "id5");
		CHECK(timeline.total() == 5);
		// An event that dropped out a moment ago does not come back as new.
		CHECK(timeline.add({comment(ChatPlatform::Twitch, "id2")}) == 0);
		// One from long ago would. The memory for that is bounded on purpose.
		for (int i = 6; i <= 20; ++i)
			timeline.add({comment(ChatPlatform::Twitch, "id" + std::to_string(i))});
		CHECK(timeline.add({comment(ChatPlatform::Twitch, "id1")}) == 1);
	}

	TEST_CASE("events without an id are all kept")
	{
		ChatTimeline timeline;
		CHECK(timeline.add({comment(ChatPlatform::Twitch, ""), comment(ChatPlatform::Twitch, "")}) == 2);
	}

	TEST_CASE("clear empties the list and forgets what it has seen")
	{
		ChatTimeline timeline;
		timeline.add({comment(ChatPlatform::Twitch, "a")});
		timeline.clear();
		CHECK(timeline.events().empty());
		CHECK(timeline.total() == 0);
		CHECK(timeline.add({comment(ChatPlatform::Twitch, "a")}) == 1);
	}

	TEST_CASE("only a plain comment is not a highlight")
	{
		ChatEvent event;
		CHECK_FALSE(isHighlight(event));
		for (const ChatEventKind kind :
		     {ChatEventKind::Paid, ChatEventKind::Membership, ChatEventKind::Raid, ChatEventKind::Notice}) {
			event.kind = kind;
			CHECK(isHighlight(event));
		}
	}
}

TEST_SUITE("chat.twitch")
{
	using namespace rd::twitch;

	TEST_CASE("the sign-in asks for one permission and sends no secret")
	{
		const Endpoints endpoints;
		const HttpRequest request = deviceCodeRequest(endpoints, "abc123clientid");
		CHECK(request.method == "POST");
		CHECK(request.url == "https://id.twitch.tv/oauth2/device");
		CHECK(header(request, "Content-Type") == "application/x-www-form-urlencoded");
		CHECK(request.body == "client_id=abc123clientid&scopes=user%3Aread%3Achat");
		CHECK_FALSE(contains(request.body, "secret"));
	}

	TEST_CASE("Twitch's sign-in code is read")
	{
		DeviceCode code;
		std::string error;
		REQUIRE(parseDeviceCode(answer(200, R"({"device_code":"dev-0123456789","expires_in":1800,"interval":5,
			"user_code":"ABCDEFGH","verification_uri":"https://www.twitch.tv/activate?public=true&device-code=ABCDEFGH"})"),
					code, error));
		CHECK(code.deviceCode == "dev-0123456789");
		CHECK(code.userCode == "ABCDEFGH");
		CHECK(code.verificationUri == "https://www.twitch.tv/activate?public=true&device-code=ABCDEFGH");
		CHECK(code.expiresInSec == 1800);
		CHECK(code.intervalSec == 5);
	}

	TEST_CASE("a sign-in page that is not on twitch.tv is refused")
	{
		DeviceCode code;
		std::string error;
		const auto withPage = [](const std::string &page) {
			return answer(200, Json{{"device_code", "d"}, {"expires_in", 1800}, {"interval", 5}, {"user_code", "ABCD"},
						{"verification_uri", page}}
						   .dump());
		};
		CHECK_FALSE(parseDeviceCode(withPage("https://www.twitch.tv.evil.example/activate"), code, error));
		CHECK(contains(error, "not on twitch.tv"));
		CHECK_FALSE(parseDeviceCode(withPage("http://www.twitch.tv/activate"), code, error));
		CHECK_FALSE(parseDeviceCode(withPage("https://evil.example/?https://www.twitch.tv/"), code, error));
		CHECK_FALSE(parseDeviceCode(withPage("https://user@www.twitch.tv/activate"), code, error));
		CHECK_FALSE(parseDeviceCode(withPage("javascript:alert(1)"), code, error));
		CHECK_FALSE(parseDeviceCode(withPage(""), code, error));
		CHECK(parseDeviceCode(withPage("https://twitch.tv/activate"), code, error));
	}

	TEST_CASE("an answer without a code, or an error, gives a reason")
	{
		DeviceCode code;
		std::string error;
		CHECK_FALSE(parseDeviceCode(answer(200, "{}"), code, error));
		CHECK_FALSE(error.empty());
		CHECK_FALSE(parseDeviceCode(answer(400, R"({"status":400,"message":"invalid client"})"), code, error));
		CHECK(contains(error, "400"));
		CHECK(contains(error, "invalid client"));
		CHECK_FALSE(parseDeviceCode(networkFailure("The server did not answer in time (Windows error 12002)."), code, error));
		CHECK(contains(error, "did not answer"));
	}

	TEST_CASE("waiting for the user: every answer Twitch gives while RelayDock asks")
	{
		const Endpoints endpoints;
		const HttpRequest request = deviceTokenRequest(endpoints, "abc", "dev code");
		CHECK(request.url == "https://id.twitch.tv/oauth2/token");
		CHECK(contains(request.body, "client_id=abc"));
		CHECK(contains(request.body, "device_code=dev%20code"));
		CHECK(contains(request.body, "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code"));

		Tokens tokens;
		std::string error;
		CHECK(parseDeviceToken(answer(400, R"({"status":400,"message":"authorization_pending"})"), tokens, error) ==
		      TokenPoll::Pending);
		CHECK(parseDeviceToken(answer(400, R"({"status":400,"message":"slow_down"})"), tokens, error) == TokenPoll::SlowDown);
		CHECK(parseDeviceToken(answer(400, R"({"status":400,"message":"invalid device code"})"), tokens, error) ==
		      TokenPoll::Ended);
		CHECK(parseDeviceToken(answer(400, R"({"status":400,"message":"something new"})"), tokens, error) == TokenPoll::Failed);
		CHECK(parseDeviceToken(answer(500, ""), tokens, error) == TokenPoll::Failed);
		CHECK(parseDeviceToken(networkFailure("offline"), tokens, error) == TokenPoll::Failed);
		CHECK(parseDeviceToken(answer(200, R"({"access_token":"only"})"), tokens, error) == TokenPoll::Failed);

		REQUIRE(parseDeviceToken(answer(200, R"({"access_token":"access-1","expires_in":14124,"refresh_token":"refresh-1",
			"scope":["user:read:chat"],"token_type":"bearer"})"),
					 tokens, error) == TokenPoll::Granted);
		CHECK(tokens.accessToken == "access-1");
		CHECK(tokens.refreshToken == "refresh-1");
		CHECK(tokens.expiresInSec == 14124);
	}

	TEST_CASE("renewing the sign-in: the saved token is sent, and a refused one asks for a new sign-in")
	{
		const Endpoints endpoints;
		const HttpRequest request = refreshRequest(endpoints, "abc", "refresh+token/1");
		CHECK(request.url == "https://id.twitch.tv/oauth2/token");
		CHECK(request.body == "grant_type=refresh_token&refresh_token=refresh%2Btoken%2F1&client_id=abc");
		CHECK_FALSE(contains(request.body, "client_secret"));

		Tokens tokens;
		std::string error;
		REQUIRE(parseRefresh(answer(200, R"({"access_token":"a2","refresh_token":"r2","expires_in":14400,"scope":["user:read:chat"],"token_type":"bearer"})"),
				     tokens, error) == RefreshResult::Ok);
		CHECK(tokens.accessToken == "a2");
		CHECK(tokens.refreshToken == "r2");
		CHECK(parseRefresh(answer(400, R"({"error":"Bad Request","status":400,"message":"Invalid refresh token"})"), tokens,
				   error) == RefreshResult::SignInAgain);
		CHECK(parseRefresh(answer(401, R"({"status":401,"message":"invalid client"})"), tokens, error) ==
		      RefreshResult::SignInAgain);
		// A broken connection or a server error says nothing about the sign-in.
		CHECK(parseRefresh(answer(503, ""), tokens, error) == RefreshResult::Failed);
		CHECK(parseRefresh(networkFailure("offline"), tokens, error) == RefreshResult::Failed);
	}

	TEST_CASE("whose token it is")
	{
		const Endpoints endpoints;
		const HttpRequest request = validateRequest(endpoints, "access-1");
		CHECK(request.method == "GET");
		CHECK(request.url == "https://id.twitch.tv/oauth2/validate");
		CHECK(header(request, "Authorization") == "OAuth access-1");

		Identity identity;
		std::string error;
		REQUIRE(parseValidate(answer(200, R"({"client_id":"abc","login":"streamer","scopes":["user:read:chat"],"user_id":"1971641","expires_in":5520838})"),
				      identity, error) == ValidateResult::Ok);
		CHECK(identity.userId == "1971641");
		CHECK(identity.login == "streamer");
		CHECK(identity.clientId == "abc");
		CHECK(identity.scopes == std::vector<std::string>{"user:read:chat"});
		CHECK(parseValidate(answer(401, R"({"status":401,"message":"invalid access token"})"), identity, error) ==
		      ValidateResult::Invalid);
		CHECK(parseValidate(answer(200, R"({"login":"x"})"), identity, error) == ValidateResult::Failed);
		CHECK(parseValidate(answer(500, ""), identity, error) == ValidateResult::Failed);
	}

	TEST_CASE("signing out tells Twitch to forget the token")
	{
		const HttpRequest request = revokeRequest(Endpoints{}, "abc", "tok/1");
		CHECK(request.method == "POST");
		CHECK(request.url == "https://id.twitch.tv/oauth2/revoke");
		CHECK(request.body == "client_id=abc&token=tok%2F1");
	}

	TEST_CASE("a client id is letters and digits")
	{
		CHECK(validClientId("hof5gwx0su6owfnys0yan9c87zr6t"));
		CHECK_FALSE(validClientId(""));
		CHECK_FALSE(validClientId("abc def"));
		CHECK_FALSE(validClientId("abc&scopes=all"));
		CHECK_FALSE(validClientId(std::string(65, 'a')));
	}

	TEST_CASE("the WebSocket address asks for a keepalive")
	{
		CHECK(eventSubUrl(Endpoints{}) == "wss://eventsub.wss.twitch.tv/ws?keepalive_timeout_seconds=30");
		Endpoints custom;
		custom.eventSub = "ws://127.0.0.1:9000/ws?x=1";
		CHECK(eventSubUrl(custom) == "ws://127.0.0.1:9000/ws?x=1&keepalive_timeout_seconds=30");
	}

	TEST_CASE("a subscription names the user's own channel and this session")
	{
		const HttpRequest request =
			subscribeRequest(Endpoints{}, "abc", "access-1", kSubscriptions[0], "1971641", "session-9");
		CHECK(request.method == "POST");
		CHECK(request.url == "https://api.twitch.tv/helix/eventsub/subscriptions");
		CHECK(header(request, "Authorization") == "Bearer access-1");
		CHECK(header(request, "Client-Id") == "abc");
		CHECK(header(request, "Content-Type") == "application/json");

		const Json body = Json::parse(request.body);
		CHECK(body["type"] == "channel.chat.message");
		CHECK(body["version"] == "1");
		CHECK(body["condition"]["broadcaster_user_id"] == "1971641");
		CHECK(body["condition"]["user_id"] == "1971641");
		CHECK(body["transport"]["method"] == "websocket");
		CHECK(body["transport"]["session_id"] == "session-9");

		CHECK(std::string(kSubscriptions[1].type) == "channel.chat.notification");
	}

	TEST_CASE("Twitch's answers to a subscription")
	{
		std::string error;
		CHECK(parseSubscribe(answer(202, "{}"), error) == SubscribeResult::Ok);
		CHECK(parseSubscribe(answer(409, R"({"message":"subscription already exists"})"), error) == SubscribeResult::Ok);
		CHECK(parseSubscribe(answer(401, R"({"status":401,"message":"Invalid OAuth token"})"), error) ==
		      SubscribeResult::Unauthorized);
		CHECK(contains(error, "Invalid OAuth token"));
		CHECK(parseSubscribe(answer(403, R"({"status":403,"message":"subscription missing proper authorization"})"), error) ==
		      SubscribeResult::Refused);
		CHECK(parseSubscribe(answer(400, R"({"status":400,"message":"bad"})"), error) == SubscribeResult::Refused);
		CHECK(parseSubscribe(answer(429, ""), error) == SubscribeResult::Failed);
		CHECK(parseSubscribe(networkFailure("offline"), error) == SubscribeResult::Failed);
	}

	TEST_CASE("the welcome names the session and the keepalive time")
	{
		Frame frame;
		std::string error;
		REQUIRE(parseFrame(twitchFrame("session_welcome",
					       {{"session",
						 {{"id", "AQoQexAWVYKSTIu4ec_2VAxyuhAB"},
						  {"status", "connected"},
						  {"connected_at", "2026-10-05T01:02:03.464757833Z"},
						  {"keepalive_timeout_seconds", 30},
						  {"reconnect_url", nullptr}}}}),
				   frame, error));
		CHECK(frame.type == Frame::Type::Welcome);
		CHECK(frame.sessionId == "AQoQexAWVYKSTIu4ec_2VAxyuhAB");
		CHECK(frame.keepaliveSeconds == 30);
		CHECK(frame.messageId == "msg-session_welcome");
		CHECK_FALSE(frame.hasEvent);

		CHECK_FALSE(parseFrame(twitchFrame("session_welcome", {{"session", Json::object()}}), frame, error));
	}

	TEST_CASE("keepalive, reconnect, revocation and unknown messages")
	{
		Frame frame;
		std::string error;
		REQUIRE(parseFrame(twitchFrame("session_keepalive", Json::object()), frame, error));
		CHECK(frame.type == Frame::Type::Keepalive);

		REQUIRE(parseFrame(twitchFrame("session_reconnect",
					       {{"session",
						 {{"id", "s"}, {"status", "reconnecting"},
						  {"reconnect_url", "wss://eventsub.wss.twitch.tv?challenge=1"}}}}),
				   frame, error));
		CHECK(frame.type == Frame::Type::Reconnect);
		CHECK(frame.reconnectUrl == "wss://eventsub.wss.twitch.tv?challenge=1");
		CHECK_FALSE(parseFrame(twitchFrame("session_reconnect", {{"session", {{"reconnect_url", nullptr}}}}), frame, error));

		REQUIRE(parseFrame(twitchFrame("revocation",
					       {{"subscription", {{"type", "channel.chat.message"}, {"status", "authorization_revoked"}}}}),
				   frame, error));
		CHECK(frame.type == Frame::Type::Revocation);
		CHECK(frame.revokedStatus == "authorization_revoked");

		REQUIRE(parseFrame(twitchFrame("something_new", Json::object()), frame, error));
		CHECK(frame.type == Frame::Type::Other);

		CHECK_FALSE(parseFrame("not json", frame, error));
		CHECK_FALSE(parseFrame("{}", frame, error));
		CHECK_FALSE(parseFrame("[1,2]", frame, error));
	}

	TEST_CASE("a comment becomes a chat event with its author, badges and time")
	{
		Frame frame;
		std::string error;
		REQUIRE(parseFrame(twitchFrame("notification", {{"subscription", {{"type", "channel.chat.message"}}},
								  {"event", twitchChatEvent()}},
					       "channel.chat.message"),
				   frame, error));
		CHECK(frame.type == Frame::Type::Notification);
		REQUIRE(frame.hasEvent);
		const ChatEvent &event = frame.event;
		CHECK(event.platform == ChatPlatform::Twitch);
		CHECK(event.kind == ChatEventKind::Message);
		CHECK(event.id == "cc106a89-1814-919d-454c-f4f2f970aae7");
		CHECK(event.author == "viewer32");
		CHECK(event.authorId == "4145994");
		CHECK(event.text == "Hi chat");
		CHECK(event.headline.empty());
		CHECK(event.fromModerator);
		CHECK(event.fromMember);
		CHECK_FALSE(event.fromBroadcaster);
		CHECK(event.timeMs == 1791162123464LL);
	}

	TEST_CASE("a comment with Bits is a paid event")
	{
		Json event = twitchChatEvent();
		event["cheer"] = {{"bits", 100}};
		event["message"]["text"] = "Cheer100 nice";
		Frame frame;
		std::string error;
		REQUIRE(parseFrame(twitchFrame("notification", {{"event", event}}, "channel.chat.message"), frame, error));
		REQUIRE(frame.hasEvent);
		CHECK(frame.event.kind == ChatEventKind::Paid);
		CHECK(frame.event.headline == "100 Bits");
		CHECK(frame.event.text == "Cheer100 nice");

		event["cheer"] = {{"bits", 1}};
		REQUIRE(parseFrame(twitchFrame("notification", {{"event", event}}, "channel.chat.message"), frame, error));
		CHECK(frame.event.headline == "1 Bit");
	}

	TEST_CASE("what Twitch announces in chat: subscriptions, gifts and raids")
	{
		const auto notice = [](const std::string &type, const std::string &system, const Json &extra = Json::object()) {
			Json event = {{"broadcaster_user_id", "1971641"},
				      {"chatter_user_id", "49912639"},
				      {"chatter_user_name", "viewer23"},
				      {"chatter_is_anonymous", false},
				      {"badges", Json::array()},
				      {"system_message", system},
				      {"message_id", "id-" + type},
				      {"message", {{"text", ""}, {"fragments", Json::array()}}},
				      {"notice_type", type}};
			event.update(extra);
			Frame frame;
			std::string error;
			REQUIRE(parseFrame(twitchFrame("notification", {{"event", event}}, "channel.chat.notification"), frame, error));
			return frame;
		};

		Frame frame = notice("sub", "viewer23 subscribed at Tier 1.");
		REQUIRE(frame.hasEvent);
		CHECK(frame.event.kind == ChatEventKind::Membership);
		CHECK(frame.event.headline == "viewer23 subscribed at Tier 1.");
		CHECK(frame.event.author == "viewer23");

		frame = notice("resub", "viewer23 subscribed at Tier 1. They've subscribed for 10 months!",
			       {{"message", {{"text", "Love the stream"}}}});
		CHECK(frame.event.kind == ChatEventKind::Membership);
		CHECK(frame.event.text == "Love the stream");

		frame = notice("community_sub_gift", "viewer23 is gifting 5 Tier 1 Subs to streamer's community!",
			       {{"community_sub_gift", {{"id", "gift-1"}, {"total", 5}, {"sub_tier", "1000"}}}});
		REQUIRE(frame.hasEvent);
		CHECK(frame.event.kind == ChatEventKind::Membership);
		CHECK(contains(frame.event.headline, "gifting 5"));

		// The five notices that follow, one per receiver, are left out.
		frame = notice("sub_gift", "viewer23 gifted a Tier 1 sub to viewer9!",
			       {{"sub_gift", {{"recipient_user_name", "viewer9"}, {"community_gift_id", "gift-1"}}}});
		CHECK_FALSE(frame.hasEvent);
		// A gift to one viewer stands by itself and is shown.
		frame = notice("sub_gift", "viewer23 gifted a Tier 1 sub to viewer9!",
			       {{"sub_gift", {{"recipient_user_name", "viewer9"}, {"community_gift_id", nullptr}}}});
		REQUIRE(frame.hasEvent);
		CHECK(frame.event.kind == ChatEventKind::Membership);

		frame = notice("raid", "15 raiders from otherchannel have joined!",
			       {{"raid", {{"user_name", "otherchannel"}, {"viewer_count", 15}}}});
		CHECK(frame.event.kind == ChatEventKind::Raid);

		frame = notice("charity_donation", "viewer23: Donated NZ$5 to support a charity");
		CHECK(frame.event.kind == ChatEventKind::Paid);

		frame = notice("announcement", "");
		CHECK(frame.event.kind == ChatEventKind::Notice);
		CHECK(frame.event.headline == "announcement");

		// What happens in another channel of a shared chat is not this channel's event.
		frame = notice("shared_chat_sub", "someone subscribed over there");
		CHECK_FALSE(frame.hasEvent);

		frame = notice("sub", "An anonymous user subscribed.", {{"chatter_is_anonymous", true}});
		CHECK(frame.event.author == "Anonymous");
	}

	TEST_CASE("a notification RelayDock does not show carries no event")
	{
		Frame frame;
		std::string error;
		REQUIRE(parseFrame(twitchFrame("notification", {{"event", {{"user_name", "x"}}}}, "channel.follow"), frame, error));
		CHECK(frame.type == Frame::Type::Notification);
		CHECK_FALSE(frame.hasEvent);
		// A chat message without an id cannot be told apart from a repeat, so it is left out.
		Json event = twitchChatEvent();
		event.erase("message_id");
		REQUIRE(parseFrame(twitchFrame("notification", {{"event", event}}, "channel.chat.message"), frame, error));
		CHECK_FALSE(frame.hasEvent);
	}

	TEST_CASE("hostile text in a comment arrives as plain text on one line")
	{
		Json event = twitchChatEvent();
		event["chatter_user_name"] = "evil\nname";
		event["message"]["text"] = "line1\nline2 <script>alert(1)</script> \xE2\x80\xAEtxet";
		Frame frame;
		std::string error;
		REQUIRE(parseFrame(twitchFrame("notification", {{"event", event}}, "channel.chat.message"), frame, error));
		CHECK(frame.event.author == "evil name");
		CHECK(frame.event.text == "line1 line2 <script>alert(1)</script> txet");
	}

	TEST_CASE("a session may only move within Twitch")
	{
		const Endpoints real;
		CHECK(acceptableReconnectUrl(real, "wss://eventsub.wss.twitch.tv/ws?challenge=abc"));
		CHECK(acceptableReconnectUrl(real, "wss://cell-a.eventsub.wss.twitch.tv/ws"));
		CHECK_FALSE(acceptableReconnectUrl(real, "ws://eventsub.wss.twitch.tv/ws"));
		CHECK_FALSE(acceptableReconnectUrl(real, "wss://twitch.tv.evil.example/ws"));
		CHECK_FALSE(acceptableReconnectUrl(real, "wss://eviltwitch.tv/ws"));
		CHECK_FALSE(acceptableReconnectUrl(real, "ws://127.0.0.1:9000/ws"));
		CHECK_FALSE(acceptableReconnectUrl(real, "https://example.com/"));
		CHECK_FALSE(acceptableReconnectUrl(real, ""));

		// A test runs against this PC, and stays on it.
		Endpoints local;
		local.eventSub = "ws://127.0.0.1:9000/ws";
		CHECK(acceptableReconnectUrl(local, "ws://127.0.0.1:9001/ws?moved=1"));
		CHECK_FALSE(acceptableReconnectUrl(local, "wss://eventsub.wss.twitch.tv/ws"));
	}
}

TEST_SUITE("chat.youtube")
{
	using namespace rd::youtube;

	TEST_CASE("the video id is found in every kind of link")
	{
		std::string id;
		REQUIRE(parseVideoId("dQw4w9WgXcQ", id));
		CHECK(id == "dQw4w9WgXcQ");
		REQUIRE(parseVideoId("  https://www.youtube.com/watch?v=dQw4w9WgXcQ  ", id));
		CHECK(id == "dQw4w9WgXcQ");
		REQUIRE(parseVideoId("https://www.youtube.com/watch?app=desktop&v=a-b_c1D2e3F&t=10s", id));
		CHECK(id == "a-b_c1D2e3F");
		REQUIRE(parseVideoId("https://youtu.be/dQw4w9WgXcQ?si=abc", id));
		CHECK(id == "dQw4w9WgXcQ");
		REQUIRE(parseVideoId("https://www.youtube.com/live/dQw4w9WgXcQ", id));
		CHECK(id == "dQw4w9WgXcQ");
		REQUIRE(parseVideoId("https://studio.youtube.com/video/dQw4w9WgXcQ/livestreaming", id));
		CHECK(id == "dQw4w9WgXcQ");
		REQUIRE(parseVideoId("https://www.youtube.com/live_chat?is_popout=1&v=dQw4w9WgXcQ", id));
		CHECK(id == "dQw4w9WgXcQ");
		REQUIRE(parseVideoId("youtube.com/watch?v=dQw4w9WgXcQ", id));
		CHECK(id == "dQw4w9WgXcQ");
	}

	TEST_CASE("text without a video id is refused")
	{
		std::string id = "unchanged";
		CHECK_FALSE(parseVideoId("", id));
		CHECK_FALSE(parseVideoId("my stream", id));
		CHECK_FALSE(parseVideoId("dQw4w9WgXc", id));       // Ten characters
		CHECK_FALSE(parseVideoId("dQw4w9WgXcQQ", id));     // Twelve
		CHECK_FALSE(parseVideoId("https://www.youtube.com/@channel", id));
		CHECK_FALSE(parseVideoId("https://www.youtube.com/watch?v=short", id));
		CHECK_FALSE(parseVideoId("https://example.com/watch?v=dQw4w9WgXcQ", id));
		CHECK_FALSE(parseVideoId("https://www.youtube.com/watch?v=dQw4w9WgXcQtoolong", id));
		CHECK(id == "unchanged");
	}

	TEST_CASE("an API key looks like one")
	{
		CHECK(plausibleApiKey("test-key-NOT-REAL-0123456789-abcdefghijk"));
		CHECK_FALSE(plausibleApiKey(""));
		CHECK_FALSE(plausibleApiKey("short"));
		CHECK_FALSE(plausibleApiKey("https://console.cloud.google.com/apis/credentials"));
		CHECK_FALSE(plausibleApiKey("test-key HAS-A-SPACE-0123456789-abcdef"));
	}

	TEST_CASE("the key travels in a header, never in the address")
	{
		const std::string key = "test-key-NOT-REAL-0123456789-abcdefghijk";
		const HttpRequest video = videoRequest(Endpoints{}, key, "dQw4w9WgXcQ");
		CHECK(video.method == "GET");
		CHECK(video.url == "https://www.googleapis.com/youtube/v3/videos?part=snippet,liveStreamingDetails&id=dQw4w9WgXcQ");
		CHECK(header(video, "X-Goog-Api-Key") == key);
		CHECK_FALSE(contains(video.url, key));

		const HttpRequest first = messagesRequest(Endpoints{}, key, "chat/id=1", "");
		CHECK(first.url == "https://www.googleapis.com/youtube/v3/liveChat/messages?liveChatId=chat%2Fid%3D1&part=snippet,authorDetails&maxResults=200");
		CHECK_FALSE(contains(first.url, key));
		const HttpRequest next = messagesRequest(Endpoints{}, key, "chat1", "token+2");
		CHECK(contains(next.url, "&pageToken=token%2B2"));
		CHECK(header(next, "X-Goog-Api-Key") == key);
	}

	TEST_CASE("a live stream gives its chat")
	{
		Video video;
		std::string error;
		REQUIRE(parseVideo(answer(200, R"({"items":[{"id":"dQw4w9WgXcQ",
			"snippet":{"title":"Friday stream","channelTitle":"My Channel","liveBroadcastContent":"live"},
			"liveStreamingDetails":{"actualStartTime":"2026-10-05T01:00:00Z","activeLiveChatId":"Cg0KC2RRdzR3OVdnWGNR"}}]})"),
				   video, error) == ApiResult::Ok);
		CHECK(video.title == "Friday stream");
		CHECK(video.channelTitle == "My Channel");
		CHECK(video.liveChatId == "Cg0KC2RRdzR3OVdnWGNR");
	}

	TEST_CASE("a video without a live chat says why")
	{
		Video video;
		std::string error;
		CHECK(parseVideo(answer(200, R"({"items":[]})"), video, error) == ApiResult::NotFound);
		CHECK(parseVideo(answer(200, R"({"items":[{"snippet":{"title":"Upcoming"},"liveStreamingDetails":{"scheduledStartTime":"2026-10-06T01:00:00Z"}}]})"),
				 video, error) == ApiResult::NotLive);
		CHECK(video.title == "Upcoming");
		CHECK(parseVideo(answer(200, R"({"items":[{"snippet":{"title":"An upload"}}]})"), video, error) == ApiResult::NotLive);
		CHECK(parseVideo(answer(200, R"({"items":[{"snippet":{"title":"Over"},"liveStreamingDetails":{"actualEndTime":"2026-10-04T03:00:00Z"}}]})"),
				 video, error) == ApiResult::ChatEnded);
		CHECK(parseVideo(answer(200, R"({"kind":"x"})"), video, error) == ApiResult::Failed);
	}

	TEST_CASE("Google's error answers are told apart")
	{
		Video video;
		std::string error;
		CHECK(parseVideo(answer(400, R"({"error":{"code":400,"message":"API key not valid. Please pass a valid API key.",
			"errors":[{"message":"API key not valid.","domain":"global","reason":"badRequest"}],"status":"INVALID_ARGUMENT",
			"details":[{"@type":"type.googleapis.com/google.rpc.ErrorInfo","reason":"API_KEY_INVALID","domain":"googleapis.com"}]}})"),
				 video, error) == ApiResult::KeyRejected);
		CHECK(contains(error, "API key not valid"));

		CHECK(parseVideo(answer(403, R"({"error":{"code":403,"message":"YouTube Data API v3 has not been used in project 1 before or it is disabled.",
			"errors":[{"reason":"accessNotConfigured"}],"status":"PERMISSION_DENIED",
			"details":[{"@type":"type.googleapis.com/google.rpc.ErrorInfo","reason":"SERVICE_DISABLED"}]}})"),
				 video, error) == ApiResult::ApiNotEnabled);

		CHECK(parseVideo(answer(403, R"({"error":{"code":403,"message":"The request cannot be completed because you have exceeded your quota.",
			"errors":[{"domain":"youtube.quota","reason":"quotaExceeded"}]}})"),
				 video, error) == ApiResult::QuotaExceeded);

		CHECK(parseVideo(answer(403, R"({"error":{"code":403,"message":"Requests from this IP are blocked.",
			"errors":[{"reason":"forbidden"}],"details":[{"reason":"API_KEY_IP_ADDRESS_BLOCKED"}]}})"),
				 video, error) == ApiResult::KeyRejected);

		Page page;
		CHECK(parseMessages(answer(403, R"({"error":{"code":403,"message":"The live chat is no longer live.","errors":[{"reason":"liveChatEnded"}]}})"),
				    page, error) == ApiResult::ChatEnded);
		CHECK(parseMessages(answer(404, R"({"error":{"code":404,"message":"The live chat that you are trying to retrieve cannot be found.","errors":[{"reason":"liveChatNotFound"}]}})"),
				    page, error) == ApiResult::NotFound);
		CHECK(parseMessages(answer(403, R"({"error":{"code":403,"message":"The live chat is disabled.","errors":[{"reason":"liveChatDisabled"}]}})"),
				    page, error) == ApiResult::Forbidden);
		CHECK(parseMessages(answer(403, R"({"error":{"code":403,"message":"Too many requests.","errors":[{"reason":"rateLimitExceeded"}]}})"),
				    page, error) == ApiResult::Failed);
		CHECK(parseMessages(answer(500, "oops"), page, error) == ApiResult::Failed);
		CHECK(contains(error, "500"));
		CHECK(parseMessages(networkFailure("The server did not answer in time (Windows error 12002)."), page, error) ==
		      ApiResult::Failed);
		CHECK(contains(error, "did not answer"));
	}

	TEST_CASE("a page of chat: comments, paid messages, members and gifts")
	{
		const Json items = Json::array({
			youtubeItem("m1", "textMessageEvent", {{"messageText", "hello from YouTube"}}, "textMessageDetails",
				    "hello from YouTube"),
			youtubeItem("m2", "superChatEvent",
				    {{"amountMicros", "5000000"}, {"currency", "NZD"}, {"amountDisplayString", "NZ$5.00"},
				     {"userComment", "great stream"}, {"tier", 2}},
				    "superChatDetails", "NZ$5.00 from Viewer One: great stream"),
			youtubeItem("m3", "superStickerEvent",
				    {{"amountDisplayString", "$2.00"}, {"superStickerMetadata", {{"altText", "A waving hand"}}}},
				    "superStickerDetails"),
			youtubeItem("m4", "newSponsorEvent", {{"memberLevelName", "Fan"}}, "newSponsorDetails",
				    "Welcome to Fan, Viewer One!"),
			youtubeItem("m5", "memberMilestoneChatEvent",
				    {{"userComment", "six months!"}, {"memberMonth", 6}, {"memberLevelName", "Fan"}},
				    "memberMilestoneChatDetails"),
			youtubeItem("m6", "membershipGiftingEvent", {{"giftMembershipsCount", 5}, {"giftMembershipsLevelName", "Fan"}},
				    "membershipGiftingDetails"),
			youtubeItem("m7", "giftMembershipReceivedEvent", {{"memberLevelName", "Fan"}}, "giftMembershipReceivedDetails"),
			youtubeItem("m8", "giftEvent", {{"giftMetadata", {{"giftName", "Rose"}, {"jewelsAmount", 30}, {"comboCount", 1}}}},
				    "giftEventDetails"),
			youtubeItem("m9", "userBannedEvent", Json::object(), "userBannedDetails"),
		});
		const Json body = {{"nextPageToken", "token-2"}, {"pollingIntervalMillis", 5000}, {"items", items}};

		Page page;
		std::string error;
		REQUIRE(parseMessages(answer(200, body.dump()), page, error) == ApiResult::Ok);
		CHECK(page.nextPageToken == "token-2");
		CHECK(page.pollingIntervalMs == 5000);
		CHECK_FALSE(page.ended);
		REQUIRE(page.events.size() == 7);

		CHECK(page.events[0].platform == ChatPlatform::YouTube);
		CHECK(page.events[0].kind == ChatEventKind::Message);
		CHECK(page.events[0].id == "m1");
		CHECK(page.events[0].author == "Viewer One");
		CHECK(page.events[0].authorId == "UCviewer");
		CHECK(page.events[0].text == "hello from YouTube");
		CHECK(page.events[0].fromMember);
		CHECK(page.events[0].timeMs == 1791162123500LL);

		CHECK(page.events[1].kind == ChatEventKind::Paid);
		CHECK(page.events[1].headline == "NZ$5.00 Super Chat");
		CHECK(page.events[1].text == "great stream");

		CHECK(page.events[2].kind == ChatEventKind::Paid);
		CHECK(page.events[2].headline == "$2.00 Super Sticker");
		CHECK(page.events[2].text == "A waving hand");

		CHECK(page.events[3].kind == ChatEventKind::Membership);
		CHECK(page.events[3].headline == "Welcome to Fan, Viewer One!");

		CHECK(page.events[4].kind == ChatEventKind::Membership);
		CHECK(page.events[4].headline == "Member for 6 months");
		CHECK(page.events[4].text == "six months!");

		CHECK(page.events[5].kind == ChatEventKind::Membership);
		CHECK(page.events[5].headline == "Gifted 5 memberships");

		// m7, one notice per receiver of a gifted membership, and m9, a ban, are left out.
		CHECK(page.events[6].id == "m8");
		CHECK(page.events[6].kind == ChatEventKind::Paid);
		CHECK(page.events[6].headline == "Gift: Rose (30 Jewels)");
	}

	TEST_CASE("the end of a stream is noticed")
	{
		Page page;
		std::string error;
		REQUIRE(parseMessages(answer(200, R"({"nextPageToken":"t","pollingIntervalMillis":5000,"offlineAt":"2026-10-05T03:00:00Z","items":[]})"),
				      page, error) == ApiResult::Ok);
		CHECK(page.ended);

		const Json body = {{"nextPageToken", "t"}, {"items", Json::array({youtubeItem("e1", "chatEndedEvent", Json::object(), "")})}};
		REQUIRE(parseMessages(answer(200, body.dump()), page, error) == ApiResult::Ok);
		CHECK(page.ended);
		CHECK(page.events.empty());
	}

	TEST_CASE("an answer with missing parts does not crash and gives what it can")
	{
		Page page;
		std::string error;
		REQUIRE(parseMessages(answer(200, "{}"), page, error) == ApiResult::Ok);
		CHECK(page.events.empty());
		CHECK(page.nextPageToken.empty());
		CHECK(parseMessages(answer(200, "not json"), page, error) == ApiResult::Failed);
		CHECK(parseMessages(answer(200, "[]"), page, error) == ApiResult::Failed);

		// An item without an id, one that is not an object, and one without details.
		const Json body = {{"items", Json::array({Json{{"snippet", {{"type", "textMessageEvent"}}}}, 5, "text",
							  youtubeItem("ok", "superChatEvent", Json::object(), "")})}};
		REQUIRE(parseMessages(answer(200, body.dump()), page, error) == ApiResult::Ok);
		REQUIRE(page.events.size() == 1);
		CHECK(page.events[0].id == "ok");
		CHECK(page.events[0].headline == "Super Chat");
	}

	TEST_CASE("every problem has words for the user, and only some are worth another try")
	{
		for (const ApiResult result :
		     {ApiResult::NotFound, ApiResult::NotLive, ApiResult::KeyRejected, ApiResult::ApiNotEnabled,
		      ApiResult::QuotaExceeded, ApiResult::ChatEnded, ApiResult::Forbidden, ApiResult::Failed}) {
			const UserMessage message = describeProblem(result, "detail text");
			CAPTURE(apiResultId(result));
			CHECK_FALSE(message.what.empty());
			CHECK_FALSE(message.action.empty());
		}
		CHECK(describeProblem(ApiResult::Ok, "").empty());
		CHECK(describeProblem(ApiResult::Failed, "detail text").detail == "detail text");

		CHECK(worthRetrying(ApiResult::Failed));
		CHECK(worthRetrying(ApiResult::NotLive));
		CHECK_FALSE(worthRetrying(ApiResult::KeyRejected));
		CHECK_FALSE(worthRetrying(ApiResult::QuotaExceeded));
		CHECK_FALSE(worthRetrying(ApiResult::ChatEnded));
		CHECK_FALSE(worthRetrying(ApiResult::NotFound));
	}
}
