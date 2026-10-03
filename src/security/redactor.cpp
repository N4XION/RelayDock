// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "security/redactor.h"

#include "security/secure_zero.h"

#include <algorithm>
#include <regex>

namespace rd {

namespace {

std::string percentEncode(std::string_view in)
{
	static const char *hex = "0123456789ABCDEF";
	std::string out;
	out.reserve(in.size() * 3);
	for (unsigned char c : in) {
		const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
					c == '-' || c == '_' || c == '.' || c == '~';
		if (unreserved) {
			out.push_back(static_cast<char>(c));
		} else {
			out.push_back('%');
			out.push_back(hex[c >> 4]);
			out.push_back(hex[c & 0x0F]);
		}
	}
	return out;
}

void replaceAll(std::string &text, std::string_view needle, std::string_view replacement)
{
	if (needle.empty())
		return;
	size_t pos = 0;
	while ((pos = text.find(needle, pos)) != std::string::npos) {
		text.replace(pos, needle.size(), replacement);
		pos += replacement.size();
	}
}

struct PatternRule {
	std::regex pattern;
	const char *replacement;
};

const std::vector<PatternRule> &patternRules()
{
	using std::regex;
	const auto flags = regex::ECMAScript | regex::icase | regex::optimize;

	// Order matters. The RTMP path rule runs before the query rule so a key in the path and a
	// token in the query are both removed.
	static const std::vector<PatternRule> rules = {
		// scheme://user:password@host  ->  scheme://[redacted]@host
		{regex(R"(([a-z][a-z0-9+.\-]*://)[^/\s@"']+@)", flags), "$1[redacted]@"},
		// rtmp://host/app/anything  ->  rtmp://host/app/[redacted]
		{regex(R"((rtmps?://[^/\s"'?]+/[^/\s"'?]+)/[^\s"'?]+)", flags), "$1/[redacted]"},
		// any streaming or web URL: ?query  ->  ?[redacted]
		{regex(R"(((?:rtmps?|srt|rist|https?)://[^\s?"']+)\?[^\s"']*)", flags), "$1?[redacted]"},
		// Twitch stream keys: live_<digits>_<token>
		{regex(R"(\blive_\d{3,}_[a-z0-9]{10,})", flags), "[redacted]"},
		// YouTube stream keys: four to six groups of four characters
		{regex(R"(\b[a-z0-9]{4}(?:-[a-z0-9]{4}){3,5}\b)", flags), "[redacted]"},
		// Facebook stream keys: FB-<digits>-<digit>-<token>
		{regex(R"(\bFB-\d{5,}-\d+-[a-z0-9_\-]{6,})", flags), "[redacted]"},
		// name = value and name: value pairs for credential-like names
		{regex(R"(((?:\bstream[ _\-]?key|\bkey|\bpassword|\bpasswd|\bpassphrase|\bsecret|\btoken|\bbearer|\bauthorization)\b\s*["']?\s*[:=]\s*["']?)(?!\[redacted\])[^\s"',;&]+)",
		       flags),
		 "$1[redacted]"},
	};
	return rules;
}

} // namespace

void Redactor::addSecret(std::string_view secret)
{
	if (secret.size() < kMinSecretLength)
		return;

	std::lock_guard<std::mutex> lock(mutex_);

	auto insert = [this](std::string value) {
		if (std::find(secrets_.begin(), secrets_.end(), value) != secrets_.end()) {
			secureZero(value);
			return;
		}
		secrets_.push_back(std::move(value));
	};

	insert(std::string(secret));
	std::string encoded = percentEncode(secret);
	if (encoded != secret)
		insert(std::move(encoded));
	else
		secureZero(encoded);

	std::stable_sort(secrets_.begin(), secrets_.end(),
			 [](const std::string &a, const std::string &b) { return a.size() > b.size(); });
}

void Redactor::removeSecret(std::string_view secret)
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::string encoded = percentEncode(secret);
	for (auto it = secrets_.begin(); it != secrets_.end();) {
		if (*it == secret || *it == encoded) {
			secureZero(*it);
			it = secrets_.erase(it);
		} else {
			++it;
		}
	}
	secureZero(encoded);
}

void Redactor::clearSecrets()
{
	std::lock_guard<std::mutex> lock(mutex_);
	for (std::string &secret : secrets_)
		secureZero(secret);
	secrets_.clear();
}

size_t Redactor::secretCount() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return secrets_.size();
}

std::string Redactor::redact(std::string_view text) const
{
	std::string out(text);
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for (const std::string &secret : secrets_)
			replaceAll(out, secret, kRedactedMarker);
	}
	return redactPatterns(out);
}

std::string Redactor::redactPatterns(std::string_view text)
{
	std::string out(text);
	for (const PatternRule &rule : patternRules())
		out = std::regex_replace(out, rule.pattern, rule.replacement);
	return out;
}

std::string Redactor::redactUrl(std::string_view url)
{
	return redactPatterns(url);
}

Redactor &globalRedactor()
{
	static Redactor instance;
	return instance;
}

} // namespace rd
