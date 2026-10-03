// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <string>
#include <string_view>

namespace rd {

// Holds a stream key or password in memory.
//
// - The buffer is overwritten when the object is destroyed, moved from or reassigned.
// - There is no implicit conversion to a string and no stream operator, so a secret cannot
//   slip into a log line or a format call by accident. Code that needs the value calls
//   reveal() and that call is easy to find in review.
// - Copies are explicit through clone().
//
// Limits: once a value is handed to OBS (the RTMP service settings) or to the clipboard,
// those copies are outside RelayDock's control and are not wiped.
class SecretString {
public:
	SecretString() = default;
	explicit SecretString(std::string_view value);
	~SecretString();

	SecretString(const SecretString &) = delete;
	SecretString &operator=(const SecretString &) = delete;

	SecretString(SecretString &&other) noexcept;
	SecretString &operator=(SecretString &&other) noexcept;

	SecretString clone() const;

	bool empty() const { return value_.empty(); }
	size_t size() const { return value_.size(); }

	// The secret itself. Never pass the result to a log, an error message or a file.
	const std::string &reveal() const { return value_; }

	void assign(std::string_view value);
	void clear();

	// Constant-time comparison.
	bool equals(const SecretString &other) const;

private:
	std::string value_;
};

// A fixed-length mask for showing that a secret exists without hinting at its length.
// Sixteen bullet characters, UTF-8.
std::string secretMask();

} // namespace rd
