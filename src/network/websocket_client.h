// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <atomic>
#include <memory>
#include <string>

namespace rd {

// A WebSocket connection that receives text messages, with Windows' own HTTP stack and its
// certificate checks. RelayDock only listens on such a connection, so there is no send.
//
// Every call blocks and ends within about a tenth of a second after `cancel` becomes true.
// Windows answers the server's pings by itself.
//
// Thread ownership: one worker thread creates, uses and destroys the object.
class WebSocketClient {
public:
	WebSocketClient();
	~WebSocketClient();
	WebSocketClient(const WebSocketClient &) = delete;
	WebSocketClient &operator=(const WebSocketClient &) = delete;

	// A longer message ends the connection. Twitch's largest messages are a few kilobytes.
	static constexpr size_t kMaxMessageBytes = 1024 * 1024;

	// Opens the connection. `url` is a wss address, or a ws address on this PC for tests.
	// Returns false with a reason in `error`, which is "cancelled" after a cancel.
	// `timeoutMs` applies to each step: finding the server, connecting and the server's answer.
	// A server that takes the connection and then says nothing ends the attempt after three
	// times that.
	bool connect(const std::string &url, const std::string &userAgent, int timeoutMs, const std::atomic<bool> &cancel,
		     std::string &error);

	enum class Receive {
		Message,   // `message` holds one complete text message
		Idle,      // Nothing arrived within `idleMs`. The connection is still open.
		Closed,    // The server closed the connection
		Failed,    // The connection broke. `error` says how.
		Cancelled,
	};

	// Waits for the next text message. Binary messages are dropped.
	Receive receive(std::string &message, int idleMs, const std::atomic<bool> &cancel, std::string &error);

	bool connected() const;

	// Ends the connection. Safe to call twice.
	void close();

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace rd
