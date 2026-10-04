// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "support/fake_server.h"

#include <nlohmann/json.hpp>

#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <vector>

// Stand-ins for Twitch and YouTube on this PC. They follow the parts of both protocols that
// RelayDock uses, as the platforms document them, and let a test decide what happens next: who
// confirms a sign-in, what viewers write, when a connection breaks.
namespace rdtest {

class FakeTwitch {
public:
	FakeTwitch();
	~FakeTwitch();

	// The addresses to hand to RelayDock.
	std::string authUrl() const { return server_.url(""); }
	std::string apiUrl() const { return server_.url(""); }
	std::string eventSubUrl() const { return server_.wsUrl("/ws"); }
	int port() const { return server_.port(); }

	// ---- What the test sets up ----------------------------------------------------------------
	std::string clientId = "testclientid0123456789";
	std::string userId = "1971641";
	std::string login = "teststreamer";
	std::string userCode = "ABCDEFGH";
	int codeExpiresInSec = 1800;
	int pendingPolls = 2;          // How often the sign-in answers "not yet" before it grants
	bool refuseSignIn = false;     // The code is answered with "invalid device code"
	int deviceCodeStatus = 200;    // What the request for a code is answered with
	int failRefreshes = 0;         // This many renewals are answered with status 503 first
	int unauthorizedSubscribes = 0; // This many subscriptions are answered with status 401 first
	int subscribeStatus = 202;     // What a subscription is answered with after that
	int keepaliveSeconds = 1;      // What the welcome announces
	int keepaliveEveryMs = 200;    // How often a keepalive really goes out. 0: never.
	bool sendWelcome = true;

	// A sign-in that exists already, as if the user had signed in earlier. Returns the token
	// that renews it, which a test puts into RelayDock's store.
	std::string existingSignIn();

	// ---- What the test makes happen ------------------------------------------------------------
	void chat(const std::string &messageId, const std::string &author, const std::string &text, int bits = 0);
	void notice(const std::string &messageId, const std::string &type, const std::string &systemMessage,
		    const nlohmann::json &extra = nlohmann::json::object());
	// The same message again, with the same id, as Twitch may do after a hiccup.
	void repeatLast();
	void sendRaw(const std::string &text);
	// Asks RelayDock to move the session to a new connection.
	void requestReconnect();
	void revoke(const std::string &status = "authorization_revoked");
	// Breaks the connection without a goodbye.
	void dropConnection();
	void closeConnection();

	// ---- What the test looks at ----------------------------------------------------------------
	int connections() const;      // WebSocket connections so far
	int movedConnections() const; // Of those, the ones that came after a move
	int refreshes() const;
	int tokenPolls() const;
	std::vector<nlohmann::json> subscriptions() const; // Bodies of the accepted subscriptions
	std::vector<FakeRequest> requests() const { return server_.requests(); }
	std::string currentRefreshToken() const;
	std::string currentAccessToken() const;
	bool revoked() const;

private:
	struct Command {
		enum class Kind { Text, Drop, Close } kind = Kind::Text;
		std::string text;
	};

	FakeResponse handle(const FakeRequest &request);
	void runSocket(FakeSocket &socket, const FakeRequest &request);
	std::string issueTokens(); // Returns the JSON of a token answer
	std::string frame(const std::string &type, const nlohmann::json &payload, const std::string &subscription = "",
			  const std::string &messageId = "");
	void push(Command command);

	mutable std::mutex mutex_;
	int tokenCounter_ = 0;
	int messageCounter_ = 0;
	int sessionCounter_ = 0;
	int connections_ = 0;
	int movedConnections_ = 0;
	int activeConnection_ = 0;
	int refreshes_ = 0;
	int tokenPolls_ = 0;
	int pollsSeen_ = 0;
	bool revoked_ = false;
	std::string sessionId_;
	std::string accessToken_;
	std::string refreshToken_;
	std::string deviceCode_;
	std::string lastFrame_;
	std::deque<Command> queue_;
	std::vector<nlohmann::json> subscriptions_;
	FakeServer server_; // Last, so it stops before the state above goes
};

class FakeYouTube {
public:
	FakeYouTube();
	~FakeYouTube();

	std::string apiUrl() const { return server_.url(""); }

	// ---- What the test sets up ----------------------------------------------------------------
	std::string apiKey = "test-key-NOT-REAL-fake-youtube-0123456789";
	std::string videoId = "dQw4w9WgXcQ";
	std::string title = "Test stream";
	std::string liveChatId = "chat-id-1";
	std::atomic<bool> live{true};
	std::atomic<bool> ended{false}; // The video is over: no chat, and an end time
	int pollingIntervalMs = 50;  // What every page of messages announces

	// ---- What the test makes happen ------------------------------------------------------------
	void comment(const std::string &id, const std::string &author, const std::string &text);
	void superChat(const std::string &id, const std::string &author, const std::string &amount, const std::string &text);
	void item(const nlohmann::json &item);
	// The next requests for messages are answered with this error instead. `times` of them.
	void failMessages(int status, const std::string &reason, int times = 1);
	void failVideos(int status, const std::string &reason, int times = 1);
	// The stream goes offline: the next page says so.
	void goOffline();

	// ---- What the test looks at ----------------------------------------------------------------
	int videoRequests() const;
	int messageRequests() const;
	std::vector<std::string> pageTokens() const; // The token of every request for messages
	std::vector<FakeRequest> requests() const { return server_.requests(); }

private:
	FakeResponse handle(const FakeRequest &request);

	mutable std::mutex mutex_;
	std::vector<nlohmann::json> pending_;
	int page_ = 0;
	int videoRequests_ = 0;
	int messageRequests_ = 0;
	int failMessagesLeft_ = 0;
	int failMessagesStatus_ = 500;
	std::string failMessagesReason_;
	int failVideosLeft_ = 0;
	int failVideosStatus_ = 500;
	std::string failVideosReason_;
	bool offline_ = false;
	std::vector<std::string> pageTokens_;
	FakeServer server_;
};

// An error answer in the shape Google's APIs use.
std::string googleError(int status, const std::string &reason, const std::string &message);

} // namespace rdtest
