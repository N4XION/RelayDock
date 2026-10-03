// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "core/user_message.h"

#include <atomic>
#include <string>

namespace rd {

// "Test connection": checks that a streaming server answers on its port.
//
// What this proves: the name resolves and the server accepts a TCP connection.
// What it cannot prove: that the stream key is right. RTMP checks the key only when a stream
// starts, and RelayDock never starts a stream that viewers could see as a test. It also does
// not perform the TLS handshake of an RTMPS server.
//
// The call blocks, so run it on a worker thread. It returns quickly when `cancel` becomes true.

enum class ReachStatus {
	Reachable,
	InvalidAddress, // Empty host or a port outside 1 to 65535
	DnsFailed,      // The name did not resolve
	Refused,        // The server answered, but nothing listens on that port
	TimedOut,       // No answer in time. A firewall may be dropping the connection.
	Unreachable,    // No route to the server, or no network
	Cancelled,
};

const char *reachStatusName(ReachStatus status);

struct ReachResult {
	ReachStatus status = ReachStatus::Unreachable;
	std::string host;
	int port = 0;
	std::string address;  // The IP address that was tried last
	int elapsedMs = 0;    // Time until the connection was accepted, for Reachable
	int systemError = 0;  // Winsock error code. 0 when none.
};

ReachResult checkReachable(const std::string &host, int port, int timeoutMs, const std::atomic<bool> &cancel);

// What to tell the user. `tls` is true for an RTMPS address.
UserMessage describeReachability(const std::string &destinationName, const ReachResult &result, bool tls);

} // namespace rd
