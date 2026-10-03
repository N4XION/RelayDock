// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <string>
#include <string_view>

namespace rd {

// A parsed streaming server address such as "rtmps://live.example.com:443/app".
struct StreamUrl {
	std::string scheme; // Lower case: "rtmp" or "rtmps"
	std::string host;   // Host name, IPv4 address, or IPv6 address without brackets
	int port = 0;       // 0 when the address has no explicit port
	std::string path;   // Starts with "/" when present. The RTMP application name.
	std::string query;  // Without the "?"
	bool hasUserInfo = false;

	bool tls() const { return scheme == "rtmps"; }

	// The explicit port, or the scheme default (1935 for RTMP, 443 for RTMPS).
	int effectivePort() const;

	// "rtmps://host:443/app". Never includes user info or the query.
	std::string displayText() const;
};

enum class StreamUrlProblem {
	None,
	Empty,
	TooLong,
	Whitespace,        // Contains a space, tab, line break or control character
	MissingScheme,     // No "scheme://"
	UnsupportedScheme, // Something other than rtmp or rtmps
	MissingHost,
	InvalidHost,
	InvalidPort,
};

struct StreamUrlResult {
	StreamUrlProblem problem = StreamUrlProblem::None;
	StreamUrl url;

	bool ok() const { return problem == StreamUrlProblem::None; }
};

inline constexpr size_t kMaxStreamUrlLength = 2048;

// Parses and checks a server address. Leading and trailing whitespace is ignored.
// RelayDock streams over RTMP and RTMPS only, so every other scheme is UnsupportedScheme.
StreamUrlResult parseStreamUrl(std::string_view text);

} // namespace rd
