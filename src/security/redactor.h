// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace rd {

inline constexpr std::string_view kRedactedMarker = "[redacted]";

// Removes secrets from text before it reaches a log, a diagnostic export or the screen.
//
// Two layers work together:
//   1. Exact values. Every stream key and password RelayDock handles is registered here for
//      the lifetime of the session, and every occurrence is replaced.
//   2. Patterns. Text that looks like a credential is replaced even when it was never
//      registered: URL user info, URL query strings, the part of an RTMP URL after the
//      application name, known platform key formats, and "key = value" pairs.
//
// Thread safety: every method may be called from any thread.
class Redactor {
public:
	// Values shorter than kMinSecretLength are ignored. Replacing a two-letter password
	// everywhere would shred ordinary text.
	static constexpr size_t kMinSecretLength = 4;

	void addSecret(std::string_view secret);
	void removeSecret(std::string_view secret);
	void clearSecrets();
	size_t secretCount() const;

	// Applies both layers.
	std::string redact(std::string_view text) const;

	// Applies the pattern layer only.
	static std::string redactPatterns(std::string_view text);

	// Makes a server URL safe to show: drops user info, the query string and everything
	// after the RTMP application name.
	static std::string redactUrl(std::string_view url);

private:
	mutable std::mutex mutex_;
	std::vector<std::string> secrets_; // Longest first, so a key wins over its own prefix.
};

// The process-wide redactor used by rd::logMessage and the diagnostic export.
Redactor &globalRedactor();

} // namespace rd
