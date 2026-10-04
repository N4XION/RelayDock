// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "chat/twitch_chat.h"

#include "network/websocket_client.h"
#include "security/redactor.h"
#include "utils/i18n.h"
#include "utils/log.h"

#include <chrono>

namespace rd {

namespace {

long long nowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

HttpResponse send(HttpRequest request, const TwitchChatConfig &config, const std::atomic<bool> &cancel)
{
	request.userAgent = config.userAgent;
	request.timeoutMs = config.timeoutMs;
	return httpRequest(request, cancel);
}

// Twitch tokens never reach a log line or a diagnostics report.
void keepOutOfLogs(const twitch::Tokens &tokens)
{
	globalRedactor().addSecret(tokens.accessToken);
	globalRedactor().addSecret(tokens.refreshToken);
}

UserMessage noClientId()
{
	UserMessage message;
	message.what = loc("Chat.Twitch.NoClientId", "This RelayDock has no Twitch application id, so it cannot sign in to Twitch.");
	message.action = loc("Chat.Twitch.NoClientId.Action", "Enter one under Settings, Chat. The chat guide shows how to get one.");
	return message;
}

UserMessage signInNeeded()
{
	UserMessage message;
	message.what = loc("Chat.Twitch.SignInNeeded", "Twitch chat needs a sign-in.");
	message.action = loc("Chat.Twitch.SignInNeeded.Action", "Open Settings, Chat and choose Sign in with Twitch.");
	return message;
}

UserMessage signInAgain(const std::string &what, const std::string &detail)
{
	UserMessage message;
	message.what = what;
	message.detail = detail;
	message.action = loc("Chat.Twitch.SignInAgain.Action", "Sign in again under Settings, Chat.");
	return message;
}

UserMessage notConnected(const std::string &detail, long long waitMs)
{
	const long long seconds = waitMs < 1000 ? 1 : (waitMs + 500) / 1000;
	UserMessage message;
	message.what = loc("Chat.Twitch.NotConnected", "Twitch chat is not connected.");
	message.detail = detail;
	message.action = locn(seconds, "Chat.RetryIn.One", "RelayDock tries again in {0} second.", "Chat.RetryIn.Many",
			      "RelayDock tries again in {0} seconds.", seconds);
	return message;
}

} // namespace

// ---- TwitchSignIn -----------------------------------------------------------------------------

TwitchSignIn::TwitchSignIn(TwitchChatConfig config, TwitchTokenStore store, Callbacks callbacks)
	: config_(std::move(config)),
	  store_(std::move(store)),
	  callbacks_(std::move(callbacks))
{
}

TwitchSignIn::~TwitchSignIn()
{
	cancel();
}

void TwitchSignIn::start()
{
	cancel();
	cancel_ = false;
	running_ = true;
	thread_ = std::thread([this] {
		run();
		running_ = false;
	});
}

void TwitchSignIn::cancel()
{
	cancel_ = true;
	if (thread_.joinable())
		thread_.join();
	running_ = false;
}

void TwitchSignIn::run()
{
	const auto fail = [&](const std::string &what, const std::string &detail, const std::string &action) {
		if (cancel_.load() || !callbacks_.finished)
			return;
		UserMessage message;
		message.what = what;
		message.detail = detail;
		message.action = action;
		callbacks_.finished(false, {}, message);
	};
	const std::string tryAgain = loc("Chat.Twitch.SignIn.TryAgain", "Start the sign-in again in a moment.");

	if (!twitch::validClientId(config_.clientId)) {
		const UserMessage message = noClientId();
		fail(message.what, {}, message.action);
		return;
	}

	twitch::DeviceCode code;
	std::string error;
	if (!twitch::parseDeviceCode(send(twitch::deviceCodeRequest(config_.endpoints, config_.clientId), config_, cancel_), code,
				     error)) {
		fail(loc("Chat.Twitch.SignIn.NoCode", "Twitch gave no sign-in code."), error, tryAgain);
		return;
	}
	if (cancel_.load())
		return;
	globalRedactor().addSecret(code.deviceCode);
	if (callbacks_.code)
		callbacks_.code(code);

	const long long deadline = nowMs() + code.expiresInSec * 1000LL;
	long long intervalMs = config_.signInPollMs > 0 ? config_.signInPollMs : code.intervalSec * 1000LL;
	int failures = 0;

	while (!cancel_.load()) {
		// Waits in small steps, so a cancel ends it at once.
		const long long wakeAt = nowMs() + intervalMs;
		while (nowMs() < wakeAt) {
			if (cancel_.load())
				return;
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		if (nowMs() > deadline) {
			fail(loc("Chat.Twitch.SignIn.Expired", "The sign-in code expired before it was confirmed."), {},
			     loc("Chat.Twitch.SignIn.Expired.Action", "Start the sign-in again and confirm the code on twitch.tv."));
			return;
		}

		twitch::Tokens tokens;
		const HttpResponse response =
			send(twitch::deviceTokenRequest(config_.endpoints, config_.clientId, code.deviceCode), config_, cancel_);
		if (cancel_.load())
			return;

		switch (twitch::parseDeviceToken(response, tokens, error)) {
		case twitch::TokenPoll::Granted: {
			keepOutOfLogs(tokens);
			twitch::Identity identity;
			const HttpResponse who = send(twitch::validateRequest(config_.endpoints, tokens.accessToken), config_, cancel_);
			if (cancel_.load())
				return;
			if (twitch::parseValidate(who, identity, error) != twitch::ValidateResult::Ok) {
				fail(loc("Chat.Twitch.SignIn.NoName", "Twitch accepted the sign-in and did not say whose it is."), error,
				     tryAgain);
				return;
			}
			if (store_.save)
				store_.save(tokens.refreshToken);
			logInfo("Signed in to Twitch as {}.", identity.login);
			if (callbacks_.finished)
				callbacks_.finished(true, identity.login, {});
			return;
		}
		case twitch::TokenPoll::Pending:
			failures = 0;
			break;
		case twitch::TokenPoll::SlowDown:
			intervalMs += config_.signInPollMs > 0 ? config_.signInPollMs : 5000;
			break;
		case twitch::TokenPoll::Ended:
			fail(loc("Chat.Twitch.SignIn.Ended", "The sign-in was not completed."), error,
			     loc("Chat.Twitch.SignIn.Expired.Action", "Start the sign-in again and confirm the code on twitch.tv."));
			return;
		case twitch::TokenPoll::Failed:
			if (++failures >= 5) {
				fail(loc("Chat.Twitch.SignIn.Unreachable", "Twitch could not be reached."), error, tryAgain);
				return;
			}
			break;
		}
	}
}

// ---- TwitchChat -------------------------------------------------------------------------------

TwitchChat::TwitchChat(TwitchChatConfig config, TwitchTokenStore store, Callbacks callbacks)
	: ChatWorker(std::move(callbacks)),
	  config_(std::move(config)),
	  store_(std::move(store))
{
}

TwitchChat::~TwitchChat()
{
	stop();
}

void TwitchChat::start()
{
	launch([this] { run(); });
}

bool TwitchChat::firstTime(const std::string &messageId)
{
	if (messageId.empty())
		return true;
	if (!recent_.insert(messageId).second)
		return false;
	recentOrder_.push_back(messageId);
	while (recentOrder_.size() > 500) {
		recent_.erase(recentOrder_.front());
		recentOrder_.pop_front();
	}
	return true;
}

bool TwitchChat::ensureToken(bool &retry, std::string &problem)
{
	retry = false;
	if (!access_.empty() && nowMs() < accessUntilMs_)
		return true;

	const std::string refresh = store_.load ? store_.load() : std::string();
	if (refresh.empty()) {
		report(ChatState::NotSetUp, signInNeeded());
		return false;
	}
	globalRedactor().addSecret(refresh);

	twitch::Tokens tokens;
	std::string error;
	const HttpResponse renewed = send(twitch::refreshRequest(config_.endpoints, config_.clientId, refresh), config_, cancel_);
	if (cancelled())
		return false;
	switch (twitch::parseRefresh(renewed, tokens, error)) {
	case twitch::RefreshResult::Ok:
		break;
	case twitch::RefreshResult::SignInAgain:
		if (store_.clear)
			store_.clear();
		logWarning("Twitch no longer accepts the saved sign-in. {}", error);
		report(ChatState::Stopped,
		       signInAgain(loc("Chat.Twitch.SignInGone",
				       "Twitch no longer accepts the saved sign-in. Twitch ends a sign-in that goes unused for 30 days."),
				   error),
		       identity_.login);
		return false;
	case twitch::RefreshResult::Failed:
		problem = error;
		retry = true;
		return false;
	}

	// The token that renewed the sign-in is spent now. The new one is saved before anything
	// else can go wrong.
	keepOutOfLogs(tokens);
	if (store_.save)
		store_.save(tokens.refreshToken);
	access_ = tokens.accessToken;
	// Renew five minutes before Twitch's own end.
	const long long lifeSec = tokens.expiresInSec > 600 ? tokens.expiresInSec - 300 : 300;
	accessUntilMs_ = nowMs() + lifeSec * 1000;

	twitch::Identity identity;
	const HttpResponse who = send(twitch::validateRequest(config_.endpoints, access_), config_, cancel_);
	if (cancelled())
		return false;
	if (twitch::parseValidate(who, identity, error) != twitch::ValidateResult::Ok) {
		access_.clear();
		problem = error;
		retry = true;
		return false;
	}
	identity_ = std::move(identity);
	return true;
}

TwitchChat::SessionEnd TwitchChat::readSession(const std::string &url, bool moved, std::string &problem, std::string &nextUrl)
{
	WebSocketClient socket;
	std::string error;
	if (!socket.connect(url, config_.userAgent, config_.timeoutMs, cancel_, error)) {
		if (cancelled())
			return SessionEnd::Cancelled;
		problem = error;
		return SessionEnd::Lost;
	}

	bool welcomed = false;
	// The welcome comes right after the connection, and it must come within the usual time,
	// whatever else arrives. Later, Twitch sends a keepalive when nothing else is due, so a
	// silence longer than that means the connection is dead.
	const long long welcomeBy = nowMs() + config_.timeoutMs;
	int idleMs = config_.timeoutMs;
	std::string message;

	for (;;) {
		if (!welcomed) {
			const long long left = welcomeBy - nowMs();
			if (left <= 0) {
				problem = loc("Chat.Twitch.NoWelcome", "Twitch did not open the session.");
				return SessionEnd::Lost;
			}
			idleMs = static_cast<int>(left);
		}
		switch (socket.receive(message, idleMs, cancel_, error)) {
		case WebSocketClient::Receive::Cancelled:
			return SessionEnd::Cancelled;
		case WebSocketClient::Receive::Idle:
			problem = welcomed ? loc("Chat.Twitch.Quiet", "Twitch stopped sending.")
					   : loc("Chat.Twitch.NoWelcome", "Twitch did not open the session.");
			return SessionEnd::Lost;
		case WebSocketClient::Receive::Closed:
			problem = loc("Chat.Twitch.Closed", "Twitch closed the connection.");
			return SessionEnd::Lost;
		case WebSocketClient::Receive::Failed:
			problem = error;
			return SessionEnd::Lost;
		case WebSocketClient::Receive::Message:
			break;
		}

		twitch::Frame frame;
		if (!twitch::parseFrame(message, frame, error) || !firstTime(frame.messageId))
			continue;

		switch (frame.type) {
		case twitch::Frame::Type::Welcome: {
			welcomed = true;
			const int keepalive = frame.keepaliveSeconds > 0 ? frame.keepaliveSeconds : twitch::kKeepaliveSeconds;
			idleMs = static_cast<int>(keepalive * 1000LL + config_.keepaliveGraceMs);
			// A session that moved keeps its subscriptions.
			if (!moved) {
				for (const twitch::Subscription &subscription : twitch::kSubscriptions) {
					const HttpResponse response =
						send(twitch::subscribeRequest(config_.endpoints, config_.clientId, access_, subscription,
									      identity_.userId, frame.sessionId),
						     config_, cancel_);
					if (cancelled())
						return SessionEnd::Cancelled;
					switch (twitch::parseSubscribe(response, error)) {
					case twitch::SubscribeResult::Ok:
						break;
					case twitch::SubscribeResult::Unauthorized:
						access_.clear();
						problem = error;
						return SessionEnd::Unauthorized;
					case twitch::SubscribeResult::Refused:
						logWarning("Twitch refused the chat subscription {}. {}", subscription.type, error);
						report(ChatState::Stopped,
						       signInAgain(loc("Chat.Twitch.Refused", "Twitch does not let RelayDock read this chat."),
								   error),
						       identity_.login);
						return SessionEnd::Ended;
					case twitch::SubscribeResult::Failed:
						problem = error;
						return SessionEnd::Lost;
					}
				}
			}
			connectedOnce_ = true;
			report(ChatState::Connected, {}, identity_.login);
			logInfo("Reading Twitch chat of {}.", identity_.login);
			break;
		}
		case twitch::Frame::Type::Notification:
			if (frame.hasEvent)
				deliver({frame.event});
			break;
		case twitch::Frame::Type::Reconnect:
			if (!twitch::acceptableReconnectUrl(config_.endpoints, frame.reconnectUrl)) {
				problem = loc("Chat.Twitch.BadMove", "Twitch named an address RelayDock does not accept.");
				return SessionEnd::Lost;
			}
			nextUrl = frame.reconnectUrl;
			return SessionEnd::Moved;
		case twitch::Frame::Type::Revocation:
			logWarning("Twitch ended the chat subscription: {}.", frame.revokedStatus);
			report(ChatState::Stopped,
			       signInAgain(loc("Chat.Twitch.Revoked", "Twitch ended RelayDock's permission to read this chat."),
					   frame.revokedStatus),
			       identity_.login);
			return SessionEnd::Ended;
		case twitch::Frame::Type::Keepalive:
		case twitch::Frame::Type::Other:
			break;
		}
	}
}

void TwitchChat::run()
{
	if (!twitch::validClientId(config_.clientId)) {
		report(ChatState::NotSetUp, noClientId());
		return;
	}

	RetryDelay delay(config_.retryFirstMs, config_.retryMaxMs);
	std::string url; // Empty: the usual address
	bool moved = false;
	int unauthorized = 0;

	while (!cancelled()) {
		report(ChatState::Connecting, {}, identity_.login);

		bool retry = false;
		std::string problem;
		if (!ensureToken(retry, problem)) {
			if (!retry || cancelled())
				return;
			const long long wait = delay.next();
			report(ChatState::Waiting, notConnected(problem, wait), identity_.login);
			if (!sleepFor(wait))
				return;
			continue;
		}

		std::string nextUrl;
		connectedOnce_ = false;
		const SessionEnd end =
			readSession(url.empty() ? twitch::eventSubUrl(config_.endpoints) : url, moved, problem, nextUrl);
		url.clear();
		moved = false;
		if (connectedOnce_) {
			// It worked before it broke, so the next try comes soon.
			delay.reset();
			unauthorized = 0;
		}

		switch (end) {
		case SessionEnd::Cancelled:
		case SessionEnd::Ended:
			return;
		case SessionEnd::Moved:
			url = nextUrl;
			moved = true;
			continue;
		case SessionEnd::Unauthorized:
			// Renew the token and come back at once. A token that is refused right after
			// Twitch issued it gets the usual wait instead of a tight loop.
			if (++unauthorized < 2)
				continue;
			break;
		case SessionEnd::Lost:
			break;
		}

		const long long wait = delay.next();
		logInfo("Twitch chat is not connected. {} Next try in {} ms.", problem, wait);
		report(ChatState::Waiting, notConnected(problem, wait), identity_.login);
		if (!sleepFor(wait))
			return;
	}
}

} // namespace rd
