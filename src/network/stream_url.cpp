// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "network/stream_url.h"

#include "utils/strings.h"

#include <format>

namespace rd {

namespace {

bool isHostChar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
	       c == '_';
}

bool isValidHostName(std::string_view host)
{
	if (host.empty() || host.size() > 253)
		return false;
	if (host.front() == '.' || host.back() == '.' || host.front() == '-')
		return false;
	bool previousDot = false;
	for (char c : host) {
		if (!isHostChar(c))
			return false;
		if (c == '.' && previousDot)
			return false;
		previousDot = (c == '.');
	}
	return true;
}

bool isValidIpv6(std::string_view host)
{
	if (host.size() < 2 || host.find(':') == std::string_view::npos)
		return false;
	for (char c : host) {
		const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
		if (!hex && c != ':' && c != '.')
			return false;
	}
	return true;
}

bool parsePort(std::string_view text, int &port)
{
	if (text.empty() || text.size() > 5)
		return false;
	int value = 0;
	for (char c : text) {
		if (c < '0' || c > '9')
			return false;
		value = value * 10 + (c - '0');
	}
	if (value < 1 || value > 65535)
		return false;
	port = value;
	return true;
}

} // namespace

int StreamUrl::effectivePort() const
{
	if (port != 0)
		return port;
	return tls() ? 443 : 1935;
}

std::string StreamUrl::displayText() const
{
	std::string out = scheme + "://";
	if (host.find(':') != std::string::npos)
		out += "[" + host + "]";
	else
		out += host;
	if (port != 0)
		out += std::format(":{}", port);
	out += path;
	return out;
}

StreamUrlResult parseStreamUrl(std::string_view text)
{
	StreamUrlResult result;
	const std::string url = trim(text);

	if (url.empty()) {
		result.problem = StreamUrlProblem::Empty;
		return result;
	}
	if (url.size() > kMaxStreamUrlLength) {
		result.problem = StreamUrlProblem::TooLong;
		return result;
	}
	for (unsigned char c : url) {
		if (c <= 0x20 || c == 0x7F) {
			result.problem = StreamUrlProblem::Whitespace;
			return result;
		}
	}

	const size_t schemeEnd = url.find("://");
	if (schemeEnd == std::string::npos || schemeEnd == 0) {
		result.problem = StreamUrlProblem::MissingScheme;
		return result;
	}

	result.url.scheme = toLower(std::string_view(url).substr(0, schemeEnd));
	if (result.url.scheme != "rtmp" && result.url.scheme != "rtmps") {
		result.problem = StreamUrlProblem::UnsupportedScheme;
		return result;
	}

	std::string_view rest = std::string_view(url).substr(schemeEnd + 3);

	// Split off the query, then the path.
	const size_t queryPos = rest.find('?');
	if (queryPos != std::string_view::npos) {
		result.url.query = std::string(rest.substr(queryPos + 1));
		rest = rest.substr(0, queryPos);
	}
	const size_t pathPos = rest.find('/');
	std::string_view authority = rest;
	if (pathPos != std::string_view::npos) {
		result.url.path = std::string(rest.substr(pathPos));
		authority = rest.substr(0, pathPos);
	}

	// user:password@host
	const size_t atPos = authority.rfind('@');
	if (atPos != std::string_view::npos) {
		result.url.hasUserInfo = true;
		authority = authority.substr(atPos + 1);
	}

	if (authority.empty()) {
		result.problem = StreamUrlProblem::MissingHost;
		return result;
	}

	std::string_view host = authority;
	std::string_view portText;
	bool hasPort = false;

	if (authority.front() == '[') {
		// [IPv6]:port
		const size_t close = authority.find(']');
		if (close == std::string_view::npos) {
			result.problem = StreamUrlProblem::InvalidHost;
			return result;
		}
		host = authority.substr(1, close - 1);
		const std::string_view after = authority.substr(close + 1);
		if (!after.empty()) {
			if (after.front() != ':') {
				result.problem = StreamUrlProblem::InvalidHost;
				return result;
			}
			portText = after.substr(1);
			hasPort = true;
		}
		if (!isValidIpv6(host)) {
			result.problem = StreamUrlProblem::InvalidHost;
			return result;
		}
	} else {
		const size_t colon = authority.rfind(':');
		if (colon != std::string_view::npos) {
			host = authority.substr(0, colon);
			portText = authority.substr(colon + 1);
			hasPort = true;
		}
		if (host.empty()) {
			result.problem = StreamUrlProblem::MissingHost;
			return result;
		}
		if (!isValidHostName(host)) {
			result.problem = StreamUrlProblem::InvalidHost;
			return result;
		}
	}

	if (hasPort && !parsePort(portText, result.url.port)) {
		result.problem = StreamUrlProblem::InvalidPort;
		return result;
	}

	result.url.host = toLower(host);
	return result;
}

} // namespace rd
