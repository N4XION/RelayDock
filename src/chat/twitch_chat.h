// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "chat/chat_worker.h"
#include "chat/twitch_protocol.h"

#include <deque>
#include <functional>
#include <set>
#include <string>

namespace rd {

// Where the saved Twitch sign-in lives. The plugin keeps it in Windows Credential Manager.
// What is saved is the token that renews the sign-in. Twitch replaces it at every renewal, so
// `save` is called each time RelayDock connects. All three run on a worker thread.
struct TwitchTokenStore {
	std::function<std::string()> load;             // Empty when nobody is signed in
	std::function<void(const std::string &)> save;
	std::function<void()> clear;                   // Twitch no longer accepts the sign-in
};

struct TwitchChatConfig {
	twitch::Endpoints endpoints;
	std::string clientId;
	std::string userAgent = "RelayDock";
	int timeoutMs = 10000;
	// The wait before the first retry. It doubles after every failure, up to the limit.
	long long retryFirstMs = 2000;
	long long retryMaxMs = 60000;
	// How often the sign-in asks Twitch whether the user is done. 0: as often as Twitch says.
	long long signInPollMs = 0;
	// Twitch announces how often it sends a sign of life. A silence that long, plus this much,
	// means the connection is dead.
	long long keepaliveGraceMs = 10000;
};

// What to tell the user while Twitch chat cannot be read for lack of an application id or of a
// sign-in.
UserMessage twitchNoClientIdMessage();
UserMessage twitchSignInNeededMessage();

// Signs the user in to Twitch with a code: RelayDock shows the code, the user confirms it on
// twitch.tv in a browser, and Twitch hands RelayDock the sign-in. RelayDock never sees the
// user's Twitch password.
//
// Both callbacks run on the worker thread.
class TwitchSignIn {
public:
	struct Callbacks {
		// The code is ready. Show it and open the page.
		std::function<void(const twitch::DeviceCode &)> code;
		// The end. `login` is the Twitch name on success. `problem` says what went wrong.
		std::function<void(bool ok, const std::string &login, const UserMessage &problem)> finished;
	};

	TwitchSignIn(TwitchChatConfig config, TwitchTokenStore store, Callbacks callbacks);
	~TwitchSignIn();
	TwitchSignIn(const TwitchSignIn &) = delete;
	TwitchSignIn &operator=(const TwitchSignIn &) = delete;

	void start();
	// Ends the attempt and waits for the thread. Nothing is reported afterwards.
	void cancel();
	bool running() const { return running_.load(); }

private:
	void run();

	TwitchChatConfig config_;
	TwitchTokenStore store_;
	Callbacks callbacks_;
	std::thread thread_;
	std::atomic<bool> cancel_{false};
	std::atomic<bool> running_{false};
};

// Reads the chat of the signed-in user's own channel: comments, and what Twitch announces in
// chat, such as subscriptions, gift subscriptions, Bits and raids.
class TwitchChat : public ChatWorker {
public:
	TwitchChat(TwitchChatConfig config, TwitchTokenStore store, Callbacks callbacks);
	~TwitchChat() override;

	void start();

private:
	enum class SessionEnd {
		Cancelled,
		Lost,         // The connection ended. Try again after a wait.
		Unauthorized, // The token ran out. Renew it and come back at once.
		Moved,        // Twitch named a new address for the session
		Ended,        // Twitch will not let RelayDock read. The status says why.
	};

	void run();
	// Makes sure `access_` holds a token that works. Returns false when the loop must not go on
	// to the socket: `retry` then says whether to wait and try again, or to give up.
	bool ensureToken(bool &retry, std::string &problem);
	SessionEnd readSession(const std::string &url, bool moved, std::string &problem, std::string &nextUrl);
	bool firstTime(const std::string &messageId);

	TwitchChatConfig config_;
	TwitchTokenStore store_;
	std::string access_;       // In memory only
	long long accessUntilMs_ = 0;
	twitch::Identity identity_;
	bool connectedOnce_ = false;
	// Ids of the last messages, because Twitch may send one twice.
	std::set<std::string> recent_;
	std::deque<std::string> recentOrder_;
};

} // namespace rd
