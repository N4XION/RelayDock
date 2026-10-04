// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <atomic>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rd {

// Web requests with Windows' own HTTP stack (WinHTTP) and its certificate checks.
//
// A call blocks, so make it from a worker thread. It ends within about a tenth of a second
// after `cancel` becomes true, whatever the network is doing at that moment. That is what lets
// OBS close without waiting for a server that does not answer.
//
// Only encrypted addresses are accepted (https, wss), plus plain ones on this PC itself
// (127.0.0.1, localhost), which the tests use.

struct HttpUrl {
	bool secure = true;
	std::string host;
	int port = 443;
	std::string path; // Path and query. Starts with "/".
};

// Splits an http, https, ws or wss address. Returns false for any other scheme, for an address
// that carries a user name or a password, and for a plain address on another computer.
bool parseHttpUrl(std::string_view url, HttpUrl &out);

// Percent-encodes a value for a query string or a form body. Letters, digits and "-_.~" stay.
std::string urlEncode(std::string_view value);

struct HttpRequest {
	std::string method = "GET";
	std::string url;
	// Name and value. A value with a line break is refused.
	std::vector<std::pair<std::string, std::string>> headers;
	std::string body;
	std::string userAgent = "RelayDock";
	int timeoutMs = 10000;        // For each step: finding the server, connecting, sending, receiving.
	size_t maxBytes = 512 * 1024; // A longer answer is discarded.
};

struct HttpResponse {
	bool ok = false;   // The request completed and a status code arrived
	int status = 0;    // HTTP status code
	std::string body;
	std::string error; // Why it did not complete. "cancelled" after a cancel. Never holds a secret.

	bool cancelled() const { return !ok && error == "cancelled"; }
};

HttpResponse httpRequest(const HttpRequest &request, const std::atomic<bool> &cancel);

} // namespace rd
