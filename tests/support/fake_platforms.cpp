// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "support/fake_platforms.h"

#include <chrono>
#include <format>

namespace rdtest {

using Json = nlohmann::json;

namespace {

FakeResponse json(int status, const Json &body)
{
	return FakeResponse{status, "application/json", body.dump()};
}

FakeResponse twitchError(int status, const std::string &message)
{
	return json(status, {{"status", status}, {"message", message}});
}

long long steadyMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

std::string googleError(int status, const std::string &reason, const std::string &message)
{
	return Json{{"error",
		     {{"code", status},
		      {"message", message},
		      {"errors", Json::array({{{"message", message}, {"domain", "youtube.test"}, {"reason", reason}}})}}}}
		.dump();
}

// ---- FakeTwitch -------------------------------------------------------------------------------

FakeTwitch::FakeTwitch()
	: server_([this](const FakeRequest &request) { return handle(request); },
		  [this](FakeSocket &socket, const FakeRequest &request) { runSocket(socket, request); })
{
}

FakeTwitch::~FakeTwitch()
{
	server_.stop();
}

std::string FakeTwitch::issueTokens()
{
	// The caller holds the mutex.
	++tokenCounter_;
	accessToken_ = std::format("access-token-{}-abcdefghij", tokenCounter_);
	refreshToken_ = std::format("refresh-token-{}-abcdefghij", tokenCounter_);
	return Json{{"access_token", accessToken_},
		    {"refresh_token", refreshToken_},
		    {"expires_in", 14400},
		    {"scope", Json::array({"user:read:chat"})},
		    {"token_type", "bearer"}}
		.dump();
}

std::string FakeTwitch::existingSignIn()
{
	const std::lock_guard lock(mutex_);
	issueTokens();
	return refreshToken_;
}

std::string FakeTwitch::currentRefreshToken() const
{
	const std::lock_guard lock(mutex_);
	return refreshToken_;
}

std::string FakeTwitch::currentAccessToken() const
{
	const std::lock_guard lock(mutex_);
	return accessToken_;
}

int FakeTwitch::connections() const
{
	const std::lock_guard lock(mutex_);
	return connections_;
}

int FakeTwitch::movedConnections() const
{
	const std::lock_guard lock(mutex_);
	return movedConnections_;
}

int FakeTwitch::refreshes() const
{
	const std::lock_guard lock(mutex_);
	return refreshes_;
}

int FakeTwitch::tokenPolls() const
{
	const std::lock_guard lock(mutex_);
	return tokenPolls_;
}

bool FakeTwitch::revoked() const
{
	const std::lock_guard lock(mutex_);
	return revoked_;
}

std::vector<Json> FakeTwitch::subscriptions() const
{
	const std::lock_guard lock(mutex_);
	return subscriptions_;
}

FakeResponse FakeTwitch::handle(const FakeRequest &request)
{
	const std::lock_guard lock(mutex_);

	if (request.path == "/oauth2/device" && request.method == "POST") {
		const auto form = request.form();
		if (deviceCodeStatus != 200)
			return twitchError(deviceCodeStatus, "invalid client");
		if (form.count("client_id") == 0 || form.at("client_id") != clientId)
			return twitchError(400, "invalid client");
		deviceCode_ = "device-code-0123456789abcdef";
		pollsSeen_ = 0;
		return json(200, {{"device_code", deviceCode_},
				  {"expires_in", codeExpiresInSec},
				  {"interval", 1},
				  {"user_code", userCode},
				  {"verification_uri", "https://www.twitch.tv/activate?public=true&device-code=" + userCode}});
	}

	if (request.path == "/oauth2/token" && request.method == "POST") {
		const auto form = request.form();
		const std::string grant = form.count("grant_type") ? form.at("grant_type") : "";
		if (grant == "urn:ietf:params:oauth:grant-type:device_code") {
			++tokenPolls_;
			if (form.count("device_code") == 0 || form.at("device_code") != deviceCode_ || refuseSignIn)
				return twitchError(400, "invalid device code");
			if (pollsSeen_++ < pendingPolls)
				return twitchError(400, "authorization_pending");
			deviceCode_.clear(); // A code works once
			return FakeResponse{200, "application/json", issueTokens()};
		}
		if (grant == "refresh_token") {
			++refreshes_;
			if (failRefreshes > 0) {
				--failRefreshes;
				return twitchError(503, "service unavailable");
			}
			// A token that renews a sign-in works once. The answer carries the next one.
			if (form.count("refresh_token") == 0 || refreshToken_.empty() || form.at("refresh_token") != refreshToken_ ||
			    form.count("client_id") == 0 || form.at("client_id") != clientId)
				return json(400, {{"error", "Bad Request"}, {"status", 400}, {"message", "Invalid refresh token"}});
			return FakeResponse{200, "application/json", issueTokens()};
		}
		return twitchError(400, "unsupported grant type");
	}

	if (request.path == "/oauth2/validate") {
		if (accessToken_.empty() || request.header("authorization") != "OAuth " + accessToken_)
			return twitchError(401, "invalid access token");
		return json(200, {{"client_id", clientId},
				  {"login", login},
				  {"scopes", Json::array({"user:read:chat"})},
				  {"user_id", userId},
				  {"expires_in", 14400}});
	}

	if (request.path == "/oauth2/revoke" && request.method == "POST") {
		revoked_ = true;
		accessToken_.clear();
		refreshToken_.clear();
		return FakeResponse{200, "text/plain", ""};
	}

	if (request.path == "/helix/eventsub/subscriptions" && request.method == "POST") {
		if (unauthorizedSubscribes > 0) {
			--unauthorizedSubscribes;
			// What happens when a token runs out between the renewal and its use.
			return json(401, {{"error", "Unauthorized"}, {"status", 401}, {"message", "Invalid OAuth token"}});
		}
		if (accessToken_.empty() || request.header("authorization") != "Bearer " + accessToken_ ||
		    request.header("client-id") != clientId)
			return json(401, {{"error", "Unauthorized"}, {"status", 401}, {"message", "Invalid OAuth token"}});
		if (subscribeStatus != 202)
			return twitchError(subscribeStatus, "subscription missing proper authorization");
		const Json body = Json::parse(request.body, nullptr, false);
		std::string session;
		if (body.is_object() && body.contains("transport") && body["transport"].is_object())
			session = body["transport"].value("session_id", std::string());
		if (session.empty() || session != sessionId_)
			return twitchError(400, "session does not exist");
		subscriptions_.push_back(body);
		return json(202, {{"data", Json::array({body})}, {"total", 1}});
	}

	if (request.path == "/ws") {
		FakeResponse response;
		response.webSocket = true;
		return response;
	}

	return twitchError(404, "not found");
}

std::string FakeTwitch::frame(const std::string &type, const Json &payload, const std::string &subscription,
			      const std::string &messageId)
{
	// The caller holds the mutex.
	Json metadata = {{"message_id", messageId.empty() ? std::format("frame-{}", ++messageCounter_) : messageId},
			 {"message_type", type},
			 {"message_timestamp", "2026-10-05T01:02:03.464757833Z"}};
	if (!subscription.empty()) {
		metadata["subscription_type"] = subscription;
		metadata["subscription_version"] = "1";
	}
	return Json{{"metadata", metadata}, {"payload", payload}}.dump();
}

void FakeTwitch::push(Command command)
{
	// The caller holds the mutex.
	queue_.push_back(std::move(command));
}

void FakeTwitch::chat(const std::string &messageId, const std::string &author, const std::string &text, int bits)
{
	const std::lock_guard lock(mutex_);
	Json event = {{"broadcaster_user_id", userId},
		      {"broadcaster_user_login", login},
		      {"broadcaster_user_name", login},
		      {"chatter_user_id", "user-" + author},
		      {"chatter_user_login", author},
		      {"chatter_user_name", author},
		      {"message_id", messageId},
		      {"message", {{"text", text}, {"fragments", Json::array()}}},
		      {"color", "#00FF7F"},
		      {"badges", Json::array()},
		      {"message_type", "text"},
		      {"cheer", nullptr},
		      {"reply", nullptr}};
	if (bits > 0)
		event["cheer"] = {{"bits", bits}};
	lastFrame_ = frame("notification", {{"subscription", {{"type", "channel.chat.message"}}}, {"event", event}},
			   "channel.chat.message");
	push({Command::Kind::Text, lastFrame_});
}

void FakeTwitch::notice(const std::string &messageId, const std::string &type, const std::string &systemMessage,
			const Json &extra)
{
	const std::lock_guard lock(mutex_);
	Json event = {{"broadcaster_user_id", userId},
		      {"chatter_user_id", "user-viewer"},
		      {"chatter_user_name", "viewer"},
		      {"chatter_is_anonymous", false},
		      {"badges", Json::array()},
		      {"system_message", systemMessage},
		      {"message_id", messageId},
		      {"message", {{"text", ""}, {"fragments", Json::array()}}},
		      {"notice_type", type}};
	event.update(extra);
	lastFrame_ = frame("notification", {{"subscription", {{"type", "channel.chat.notification"}}}, {"event", event}},
			   "channel.chat.notification");
	push({Command::Kind::Text, lastFrame_});
}

void FakeTwitch::repeatLast()
{
	const std::lock_guard lock(mutex_);
	push({Command::Kind::Text, lastFrame_});
}

void FakeTwitch::sendRaw(const std::string &text)
{
	const std::lock_guard lock(mutex_);
	push({Command::Kind::Text, text});
}

void FakeTwitch::requestReconnect()
{
	const std::lock_guard lock(mutex_);
	push({Command::Kind::Text,
	      frame("session_reconnect",
		    {{"session",
		      {{"id", sessionId_}, {"status", "reconnecting"}, {"reconnect_url", server_.wsUrl("/ws?moved=1")}}}})});
}

void FakeTwitch::revoke(const std::string &status)
{
	const std::lock_guard lock(mutex_);
	push({Command::Kind::Text,
	      frame("revocation", {{"subscription", {{"type", "channel.chat.message"}, {"status", status}}}})});
}

void FakeTwitch::dropConnection()
{
	const std::lock_guard lock(mutex_);
	push({Command::Kind::Drop, {}});
}

void FakeTwitch::closeConnection()
{
	const std::lock_guard lock(mutex_);
	push({Command::Kind::Close, {}});
}

void FakeTwitch::runSocket(FakeSocket &socket, const FakeRequest &request)
{
	const bool moved = request.param("moved") == "1";
	int connection = 0;
	std::string welcome;
	{
		const std::lock_guard lock(mutex_);
		connection = ++connections_;
		if (moved)
			++movedConnections_;
		else
			sessionId_ = std::format("session-{}", ++sessionCounter_);
		activeConnection_ = connection;
		if (sendWelcome)
			welcome = frame("session_welcome", {{"session",
							     {{"id", sessionId_},
							      {"status", "connected"},
							      {"connected_at", "2026-10-05T01:02:03.464757833Z"},
							      {"keepalive_timeout_seconds", keepaliveSeconds},
							      {"reconnect_url", nullptr}}}});
	}
	if (!welcome.empty())
		socket.sendText(welcome);

	long long lastKeepalive = steadyMs();
	while (socket.wait(10)) {
		std::deque<Command> commands;
		std::string keepalive;
		{
			const std::lock_guard lock(mutex_);
			// A newer connection took the session over. Twitch closes the old one then.
			if (activeConnection_ != connection) {
				socket.sendClose(4004);
				return;
			}
			commands.swap(queue_);
			if (keepaliveEveryMs > 0 && steadyMs() - lastKeepalive >= keepaliveEveryMs) {
				keepalive = frame("session_keepalive", Json::object());
				lastKeepalive = steadyMs();
			}
		}
		for (const Command &command : commands) {
			if (command.kind == Command::Kind::Drop)
				return;
			if (command.kind == Command::Kind::Close) {
				socket.sendClose(1000);
				socket.waitForClose(1000);
				return;
			}
			socket.sendText(command.text);
		}
		if (!keepalive.empty())
			socket.sendText(keepalive);
	}
}

// ---- FakeYouTube ------------------------------------------------------------------------------

FakeYouTube::FakeYouTube() : server_([this](const FakeRequest &request) { return handle(request); }) {}

FakeYouTube::~FakeYouTube()
{
	server_.stop();
}

void FakeYouTube::item(const Json &item)
{
	const std::lock_guard lock(mutex_);
	pending_.push_back(item);
}

void FakeYouTube::comment(const std::string &id, const std::string &author, const std::string &text)
{
	item({{"kind", "youtube#liveChatMessage"},
	      {"id", id},
	      {"snippet",
	       {{"type", "textMessageEvent"},
		{"liveChatId", liveChatId},
		{"publishedAt", "2026-10-05T01:02:03.500+00:00"},
		{"hasDisplayContent", true},
		{"displayMessage", text},
		{"textMessageDetails", {{"messageText", text}}}}},
	      {"authorDetails",
	       {{"channelId", "UC-" + author},
		{"displayName", author},
		{"isChatOwner", false},
		{"isChatModerator", false},
		{"isChatSponsor", false}}}});
}

void FakeYouTube::superChat(const std::string &id, const std::string &author, const std::string &amount,
			    const std::string &text)
{
	item({{"kind", "youtube#liveChatMessage"},
	      {"id", id},
	      {"snippet",
	       {{"type", "superChatEvent"},
		{"liveChatId", liveChatId},
		{"publishedAt", "2026-10-05T01:02:04.000+00:00"},
		{"hasDisplayContent", true},
		{"displayMessage", amount + " from " + author + ": " + text},
		{"superChatDetails",
		 {{"amountMicros", "5000000"}, {"currency", "NZD"}, {"amountDisplayString", amount}, {"userComment", text}, {"tier", 2}}}}},
	      {"authorDetails",
	       {{"channelId", "UC-" + author},
		{"displayName", author},
		{"isChatOwner", false},
		{"isChatModerator", false},
		{"isChatSponsor", true}}}});
}

void FakeYouTube::failMessages(int status, const std::string &reason, int times)
{
	const std::lock_guard lock(mutex_);
	failMessagesStatus_ = status;
	failMessagesReason_ = reason;
	failMessagesLeft_ = times;
}

void FakeYouTube::failVideos(int status, const std::string &reason, int times)
{
	const std::lock_guard lock(mutex_);
	failVideosStatus_ = status;
	failVideosReason_ = reason;
	failVideosLeft_ = times;
}

void FakeYouTube::goOffline()
{
	const std::lock_guard lock(mutex_);
	offline_ = true;
}

int FakeYouTube::videoRequests() const
{
	const std::lock_guard lock(mutex_);
	return videoRequests_;
}

int FakeYouTube::messageRequests() const
{
	const std::lock_guard lock(mutex_);
	return messageRequests_;
}

std::vector<std::string> FakeYouTube::pageTokens() const
{
	const std::lock_guard lock(mutex_);
	return pageTokens_;
}

FakeResponse FakeYouTube::handle(const FakeRequest &request)
{
	const std::lock_guard lock(mutex_);

	if (request.header("x-goog-api-key") != apiKey) {
		return FakeResponse{400, "application/json",
				    Json{{"error",
					  {{"code", 400},
					   {"message", "API key not valid. Please pass a valid API key."},
					   {"errors", Json::array({{{"reason", "badRequest"}}})},
					   {"details", Json::array({{{"reason", "API_KEY_INVALID"}}})}}}}
					    .dump()};
	}

	if (request.path == "/youtube/v3/videos") {
		++videoRequests_;
		if (failVideosLeft_ > 0) {
			--failVideosLeft_;
			return FakeResponse{failVideosStatus_, "application/json",
					    googleError(failVideosStatus_, failVideosReason_, "videos failed for the test")};
		}
		if (request.param("id") != videoId)
			return json(200, {{"items", Json::array()}});
		Json details = Json::object();
		if (ended.load())
			details["actualEndTime"] = "2026-10-05T00:30:00Z";
		else if (live.load())
			details["activeLiveChatId"] = liveChatId;
		return json(200, {{"items",
				   Json::array({{{"id", videoId},
						 {"snippet", {{"title", title}, {"channelTitle", "Test channel"}}},
						 {"liveStreamingDetails", details}}})}});
	}

	if (request.path == "/youtube/v3/liveChat/messages") {
		++messageRequests_;
		pageTokens_.push_back(request.param("pageToken"));
		if (failMessagesLeft_ > 0) {
			--failMessagesLeft_;
			return FakeResponse{failMessagesStatus_, "application/json",
					    googleError(failMessagesStatus_, failMessagesReason_, "messages failed for the test")};
		}
		if (request.param("liveChatId") != liveChatId)
			return FakeResponse{404, "application/json", googleError(404, "liveChatNotFound", "no such chat")};

		Json body = {{"nextPageToken", std::format("page-{}", ++page_)},
			     {"pollingIntervalMillis", pollingIntervalMs},
			     {"items", pending_}};
		pending_.clear();
		if (offline_)
			body["offlineAt"] = "2026-10-05T03:00:00Z";
		return json(200, body);
	}

	return FakeResponse{404, "application/json", googleError(404, "notFound", "no such method")};
}

} // namespace rdtest
