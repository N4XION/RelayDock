// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "chat/chat_types.h"
#include "network/http_client.h"

#include <string>
#include <vector>

// What RelayDock says to Twitch and how it reads the answers. Nothing here touches the network:
// each function builds a request or reads a response, so the tests cover every answer Twitch
// documents without Twitch.
//
// Sign-in is the device code flow for public clients: RelayDock shows a code, the user enters
// it on twitch.tv, and Twitch hands RelayDock a token. There is no client secret, so nothing
// secret ships with RelayDock. Chat arrives over EventSub on a WebSocket.
//
// Sources: dev.twitch.tv/docs/authentication/getting-tokens-oauth (device code grant flow),
// dev.twitch.tv/docs/eventsub/handling-websocket-events and
// dev.twitch.tv/docs/eventsub/eventsub-subscription-types, read on 2026-10-04.
namespace rd::twitch {

// Reading chat, and the events Twitch shows in chat. RelayDock asks for nothing else.
inline constexpr const char *kScope = "user:read:chat";

// How often Twitch says "still here" on a quiet connection. Twitch allows 10 to 600 seconds.
inline constexpr int kKeepaliveSeconds = 30;

struct Endpoints {
	std::string auth = "https://id.twitch.tv";
	std::string api = "https://api.twitch.tv";
	std::string eventSub = "wss://eventsub.wss.twitch.tv/ws";
};

// A client id is a public name for the application, made of letters and digits.
bool validClientId(const std::string &clientId);

// ---- Sign-in ----------------------------------------------------------------------------------

struct DeviceCode {
	std::string deviceCode;      // RelayDock keeps this to itself
	std::string userCode;        // The user enters this on twitch.tv
	std::string verificationUri; // The page to open. Twitch puts the code into the link.
	int expiresInSec = 0;
	int intervalSec = 5;         // How often RelayDock may ask whether the user is done
};

HttpRequest deviceCodeRequest(const Endpoints &endpoints, const std::string &clientId);
bool parseDeviceCode(const HttpResponse &response, DeviceCode &out, std::string &error);

struct Tokens {
	std::string accessToken;
	std::string refreshToken;
	int expiresInSec = 0;
};

HttpRequest deviceTokenRequest(const Endpoints &endpoints, const std::string &clientId, const std::string &deviceCode);

enum class TokenPoll {
	Granted,
	Pending,  // The user has not finished yet
	SlowDown, // Twitch wants longer pauses between questions
	Ended,    // The code expired or the user said no
	Failed,   // The request or its answer could not be used
};
TokenPoll parseDeviceToken(const HttpResponse &response, Tokens &out, std::string &error);

HttpRequest refreshRequest(const Endpoints &endpoints, const std::string &clientId, const std::string &refreshToken);

enum class RefreshResult {
	Ok,
	SignInAgain, // Twitch no longer accepts the saved sign-in
	Failed,      // Worth another try later
};
RefreshResult parseRefresh(const HttpResponse &response, Tokens &out, std::string &error);

struct Identity {
	std::string userId;
	std::string login;
	std::string clientId;
	std::vector<std::string> scopes;
	int expiresInSec = 0;
};

HttpRequest validateRequest(const Endpoints &endpoints, const std::string &accessToken);

enum class ValidateResult { Ok, Invalid, Failed };
ValidateResult parseValidate(const HttpResponse &response, Identity &out, std::string &error);

// Tells Twitch to forget a token. Used on sign-out.
HttpRequest revokeRequest(const Endpoints &endpoints, const std::string &clientId, const std::string &token);

// ---- EventSub ---------------------------------------------------------------------------------

// The address of the WebSocket, with the keepalive time RelayDock asks for.
std::string eventSubUrl(const Endpoints &endpoints);

// The two things RelayDock subscribes to. Both need only kScope.
struct Subscription {
	const char *type;
	const char *version;
};
inline constexpr Subscription kSubscriptions[] = {
	{"channel.chat.message", "1"},
	{"channel.chat.notification", "1"},
};

// Asks Twitch to send one kind of event for the user's own channel to this WebSocket session.
HttpRequest subscribeRequest(const Endpoints &endpoints, const std::string &clientId, const std::string &accessToken,
			     const Subscription &subscription, const std::string &userId, const std::string &sessionId);

enum class SubscribeResult {
	Ok,
	Unauthorized, // The token is no longer valid. Refresh it and try again.
	Refused,      // Twitch will not allow it with this sign-in. Trying again does not help.
	Failed,
};
SubscribeResult parseSubscribe(const HttpResponse &response, std::string &error);

struct Frame {
	enum class Type {
		Welcome,      // The session is open. Subscribe now.
		Keepalive,
		Notification, // An event. `event` holds it when it is one RelayDock shows.
		Reconnect,    // Twitch moves the session. Connect to `reconnectUrl`.
		Revocation,   // Twitch ended a subscription
		Other,
	};

	Type type = Type::Other;
	std::string messageId; // Twitch may send a message twice. This id tells.
	std::string sessionId;
	int keepaliveSeconds = 0;
	std::string reconnectUrl;
	std::string revokedStatus; // "authorization_revoked", "user_removed", "version_removed"
	bool hasEvent = false;
	ChatEvent event;
};

// Reads one WebSocket message. Returns false when it is not a message of the EventSub protocol.
bool parseFrame(const std::string &json, Frame &out, std::string &error);

// Whether a session may move to `url`: an encrypted address at twitch.tv, or, when the
// configured address is on this PC (a test), an address on this PC.
bool acceptableReconnectUrl(const Endpoints &endpoints, const std::string &url);

} // namespace rd::twitch
