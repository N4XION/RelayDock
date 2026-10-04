// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "chat/twitch_protocol.h"

#include "utils/i18n.h"
#include "utils/strings.h"

#include <nlohmann/json.hpp>

#include <format>

namespace rd::twitch {

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
	return it != object.end() && it->is_number_integer() ? it->get<long long>() : fallback;
}

// The object under `key`, or a null value when there is none.
const Json &child(const Json &object, const char *key)
{
	static const Json none;
	if (!object.is_object())
		return none;
	const auto it = object.find(key);
	return it != object.end() ? *it : none;
}

// What Twitch said went wrong. Its error answers carry a "message".
std::string problem(const HttpResponse &response)
{
	if (!response.ok)
		return response.error;
	const Json root = parseJson(response.body);
	const std::string message = cleanChatText(text(root, "message"), 200);
	if (message.empty())
		return std::format("Twitch answered with status {}.", response.status);
	return std::format("Twitch answered with status {}: {}", response.status, message);
}

HttpRequest formPost(const std::string &url, const std::string &body)
{
	HttpRequest request;
	request.method = "POST";
	request.url = url;
	request.headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
	request.body = body;
	return request;
}

bool readTokens(const HttpResponse &response, Tokens &out)
{
	const Json root = parseJson(response.body);
	Tokens tokens;
	tokens.accessToken = text(root, "access_token");
	tokens.refreshToken = text(root, "refresh_token");
	tokens.expiresInSec = static_cast<int>(number(root, "expires_in"));
	if (tokens.accessToken.empty() || tokens.refreshToken.empty())
		return false;
	out = std::move(tokens);
	return true;
}

bool isThisPc(const std::string &host)
{
	return host == "127.0.0.1" || host == "localhost";
}

bool endsWith(const std::string &value, std::string_view suffix)
{
	return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

bool validClientId(const std::string &clientId)
{
	if (clientId.empty() || clientId.size() > 64)
		return false;
	for (const char c : clientId) {
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
		if (!ok)
			return false;
	}
	return true;
}

// ---- Sign-in ----------------------------------------------------------------------------------

HttpRequest deviceCodeRequest(const Endpoints &endpoints, const std::string &clientId)
{
	return formPost(endpoints.auth + "/oauth2/device",
			std::format("client_id={}&scopes={}", urlEncode(clientId), urlEncode(kScope)));
}

bool parseDeviceCode(const HttpResponse &response, DeviceCode &out, std::string &error)
{
	if (!response.ok || response.status != 200) {
		error = problem(response);
		return false;
	}
	const Json root = parseJson(response.body);
	DeviceCode code;
	code.deviceCode = text(root, "device_code");
	code.userCode = cleanChatText(text(root, "user_code"), 32);
	code.verificationUri = text(root, "verification_uri");
	code.expiresInSec = static_cast<int>(number(root, "expires_in"));
	code.intervalSec = static_cast<int>(number(root, "interval", 5));
	if (code.deviceCode.empty() || code.userCode.empty() || code.expiresInSec <= 0) {
		error = "Twitch's answer holds no sign-in code.";
		return false;
	}
	// RelayDock opens this page in the browser, so it must be a page of twitch.tv and nothing
	// else.
	HttpUrl page;
	if (!parseHttpUrl(code.verificationUri, page) || !page.secure || page.port != 443 ||
	    (page.host != "twitch.tv" && page.host != "www.twitch.tv")) {
		error = "Twitch's answer names a sign-in page that is not on twitch.tv.";
		return false;
	}
	if (code.intervalSec < 1)
		code.intervalSec = 5;
	out = std::move(code);
	return true;
}

HttpRequest deviceTokenRequest(const Endpoints &endpoints, const std::string &clientId, const std::string &deviceCode)
{
	return formPost(endpoints.auth + "/oauth2/token",
			std::format("client_id={}&scopes={}&device_code={}&grant_type={}", urlEncode(clientId), urlEncode(kScope),
				    urlEncode(deviceCode), urlEncode("urn:ietf:params:oauth:grant-type:device_code")));
}

TokenPoll parseDeviceToken(const HttpResponse &response, Tokens &out, std::string &error)
{
	if (!response.ok) {
		error = response.error;
		return TokenPoll::Failed;
	}
	if (response.status == 200) {
		if (readTokens(response, out))
			return TokenPoll::Granted;
		error = "Twitch's answer holds no token.";
		return TokenPoll::Failed;
	}
	if (response.status == 400) {
		const std::string message = toLower(text(parseJson(response.body), "message"));
		if (message == "authorization_pending")
			return TokenPoll::Pending;
		if (message == "slow_down")
			return TokenPoll::SlowDown;
		if (containsNoCase(message, "invalid device code") || message == "expired_token" || message == "access_denied") {
			error = problem(response);
			return TokenPoll::Ended;
		}
	}
	error = problem(response);
	return TokenPoll::Failed;
}

HttpRequest refreshRequest(const Endpoints &endpoints, const std::string &clientId, const std::string &refreshToken)
{
	return formPost(endpoints.auth + "/oauth2/token",
			std::format("grant_type=refresh_token&refresh_token={}&client_id={}", urlEncode(refreshToken),
				    urlEncode(clientId)));
}

RefreshResult parseRefresh(const HttpResponse &response, Tokens &out, std::string &error)
{
	if (!response.ok) {
		error = response.error;
		return RefreshResult::Failed;
	}
	if (response.status == 200) {
		if (readTokens(response, out))
			return RefreshResult::Ok;
		error = "Twitch's answer holds no token.";
		return RefreshResult::Failed;
	}
	error = problem(response);
	// 400 and 401 mean Twitch does not accept this saved sign-in any more: it was used
	// already, it expired after 30 days without use, or the user disconnected RelayDock.
	if (response.status == 400 || response.status == 401)
		return RefreshResult::SignInAgain;
	return RefreshResult::Failed;
}

HttpRequest validateRequest(const Endpoints &endpoints, const std::string &accessToken)
{
	HttpRequest request;
	request.url = endpoints.auth + "/oauth2/validate";
	request.headers = {{"Authorization", "OAuth " + accessToken}};
	return request;
}

ValidateResult parseValidate(const HttpResponse &response, Identity &out, std::string &error)
{
	if (!response.ok) {
		error = response.error;
		return ValidateResult::Failed;
	}
	if (response.status == 401) {
		error = problem(response);
		return ValidateResult::Invalid;
	}
	if (response.status != 200) {
		error = problem(response);
		return ValidateResult::Failed;
	}
	const Json root = parseJson(response.body);
	Identity identity;
	identity.userId = text(root, "user_id");
	identity.login = cleanChatText(text(root, "login"), 64);
	identity.clientId = text(root, "client_id");
	identity.expiresInSec = static_cast<int>(number(root, "expires_in"));
	if (const Json &scopes = child(root, "scopes"); scopes.is_array()) {
		for (const Json &scope : scopes) {
			if (scope.is_string())
				identity.scopes.push_back(scope.get<std::string>());
		}
	}
	if (identity.userId.empty() || identity.login.empty()) {
		error = "Twitch's answer does not say whose token this is.";
		return ValidateResult::Failed;
	}
	out = std::move(identity);
	return ValidateResult::Ok;
}

HttpRequest revokeRequest(const Endpoints &endpoints, const std::string &clientId, const std::string &token)
{
	return formPost(endpoints.auth + "/oauth2/revoke",
			std::format("client_id={}&token={}", urlEncode(clientId), urlEncode(token)));
}

// ---- EventSub ---------------------------------------------------------------------------------

std::string eventSubUrl(const Endpoints &endpoints)
{
	const char separator = endpoints.eventSub.find('?') == std::string::npos ? '?' : '&';
	return std::format("{}{}keepalive_timeout_seconds={}", endpoints.eventSub, separator, kKeepaliveSeconds);
}

HttpRequest subscribeRequest(const Endpoints &endpoints, const std::string &clientId, const std::string &accessToken,
			     const Subscription &subscription, const std::string &userId, const std::string &sessionId)
{
	// The user's own channel, read as the user.
	const Json body = {
		{"type", subscription.type},
		{"version", subscription.version},
		{"condition", {{"broadcaster_user_id", userId}, {"user_id", userId}}},
		{"transport", {{"method", "websocket"}, {"session_id", sessionId}}},
	};

	HttpRequest request;
	request.method = "POST";
	request.url = endpoints.api + "/helix/eventsub/subscriptions";
	request.headers = {{"Authorization", "Bearer " + accessToken},
			   {"Client-Id", clientId},
			   {"Content-Type", "application/json"}};
	request.body = body.dump();
	return request;
}

SubscribeResult parseSubscribe(const HttpResponse &response, std::string &error)
{
	if (!response.ok) {
		error = response.error;
		return SubscribeResult::Failed;
	}
	// 202: accepted. 409: this session has the subscription already.
	if (response.status == 202 || response.status == 409)
		return SubscribeResult::Ok;
	error = problem(response);
	if (response.status == 401)
		return SubscribeResult::Unauthorized;
	if (response.status == 400 || response.status == 403)
		return SubscribeResult::Refused;
	return SubscribeResult::Failed;
}

namespace {

void readBadges(const Json &event, ChatEvent &out)
{
	const Json &badges = child(event, "badges");
	if (!badges.is_array())
		return;
	for (const Json &badge : badges) {
		const std::string set = text(badge, "set_id");
		if (set == "broadcaster")
			out.fromBroadcaster = true;
		else if (set == "moderator")
			out.fromModerator = true;
		else if (set == "subscriber" || set == "founder")
			out.fromMember = true;
	}
}

bool readChatMessage(const Json &event, ChatEvent &out)
{
	out.kind = ChatEventKind::Message;
	out.id = text(event, "message_id");
	out.author = cleanChatText(text(event, "chatter_user_name"), 100);
	out.authorId = text(event, "chatter_user_id");
	out.text = cleanChatText(text(child(event, "message"), "text"));
	readBadges(event, out);

	// A comment that carries Bits.
	const long long bits = number(child(event, "cheer"), "bits");
	if (bits > 0) {
		out.kind = ChatEventKind::Paid;
		out.headline = locn(bits, "Chat.Twitch.Bit", "{0} Bit", "Chat.Twitch.Bits", "{0} Bits", bits);
	}
	return !out.id.empty();
}

bool readChatNotification(const Json &event, ChatEvent &out)
{
	const std::string notice = text(event, "notice_type");

	// A shared chat shows what happens in the other channels too. Those are not this
	// channel's subscribers or raids.
	if (notice.rfind("shared_chat_", 0) == 0)
		return false;
	// A gift to many viewers arrives as one announcement and then one notice per receiver.
	// The announcement says it all.
	if (notice == "sub_gift" && !text(child(event, "sub_gift"), "community_gift_id").empty())
		return false;

	if (notice == "sub" || notice == "resub" || notice == "sub_gift" || notice == "community_sub_gift" ||
	    notice == "gift_paid_upgrade" || notice == "prime_paid_upgrade" || notice == "pay_it_forward")
		out.kind = ChatEventKind::Membership;
	else if (notice == "raid")
		out.kind = ChatEventKind::Raid;
	else if (notice == "charity_donation")
		out.kind = ChatEventKind::Paid;
	else
		out.kind = ChatEventKind::Notice;

	out.id = text(event, "message_id");
	const Json &anonymous = child(event, "chatter_is_anonymous");
	out.author = anonymous.is_boolean() && anonymous.get<bool>() ? loc("Chat.Anonymous", "Anonymous")
								    : cleanChatText(text(event, "chatter_user_name"), 100);
	out.authorId = text(event, "chatter_user_id");
	out.text = cleanChatText(text(child(event, "message"), "text"));
	// Twitch writes the sentence it shows in chat, such as "Kai subscribed at Tier 1."
	out.headline = cleanChatText(text(event, "system_message"), 500);
	if (out.headline.empty())
		out.headline = notice;
	readBadges(event, out);
	return !out.id.empty();
}

} // namespace

bool parseFrame(const std::string &json, Frame &out, std::string &error)
{
	const Json root = parseJson(json);
	const Json &metadata = child(root, "metadata");
	const std::string type = text(metadata, "message_type");
	if (root.is_discarded() || type.empty()) {
		error = "The message is not part of the EventSub protocol.";
		return false;
	}

	Frame frame;
	frame.messageId = text(metadata, "message_id");
	const Json &payload = child(root, "payload");
	const Json &session = child(payload, "session");

	if (type == "session_welcome") {
		frame.type = Frame::Type::Welcome;
		frame.sessionId = text(session, "id");
		frame.keepaliveSeconds = static_cast<int>(number(session, "keepalive_timeout_seconds"));
		if (frame.sessionId.empty()) {
			error = "Twitch's welcome names no session.";
			return false;
		}
	} else if (type == "session_keepalive") {
		frame.type = Frame::Type::Keepalive;
	} else if (type == "session_reconnect") {
		frame.type = Frame::Type::Reconnect;
		frame.reconnectUrl = text(session, "reconnect_url");
		if (frame.reconnectUrl.empty()) {
			error = "Twitch asked for a move and named no address.";
			return false;
		}
	} else if (type == "revocation") {
		frame.type = Frame::Type::Revocation;
		frame.revokedStatus = text(child(payload, "subscription"), "status");
	} else if (type == "notification") {
		frame.type = Frame::Type::Notification;
		const std::string subscription = text(metadata, "subscription_type");
		const Json &event = child(payload, "event");

		ChatEvent chat;
		chat.platform = ChatPlatform::Twitch;
		bool shown = false;
		if (subscription == "channel.chat.message")
			shown = readChatMessage(event, chat);
		else if (subscription == "channel.chat.notification")
			shown = readChatNotification(event, chat);
		if (shown) {
			parseRfc3339Ms(text(metadata, "message_timestamp"), chat.timeMs);
			frame.hasEvent = true;
			frame.event = std::move(chat);
		}
	} else {
		frame.type = Frame::Type::Other;
	}

	out = std::move(frame);
	return true;
}

bool acceptableReconnectUrl(const Endpoints &endpoints, const std::string &url)
{
	HttpUrl configured;
	HttpUrl target;
	if (!parseHttpUrl(endpoints.eventSub, configured) || !parseHttpUrl(url, target))
		return false;
	// A test runs against this PC, and its sessions move within this PC.
	if (isThisPc(configured.host))
		return isThisPc(target.host);
	return target.secure && (target.host == "twitch.tv" || endsWith(target.host, ".twitch.tv"));
}

} // namespace rd::twitch
