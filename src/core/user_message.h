// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <string>

namespace rd {

// A message for the user. Every error RelayDock shows answers three questions:
//   what    What failed, and for which platform. "TikTok rejected the connection."
//   detail  What RelayDock knows about it. May be empty.
//   action  What to check or do next. "Check your server URL and stream key."
//
// None of the fields may contain a stream key or password.
struct UserMessage {
	std::string what;
	std::string detail;
	std::string action;

	bool empty() const { return what.empty() && detail.empty() && action.empty(); }

	// The parts joined with single spaces, skipping empty ones.
	std::string text() const
	{
		std::string out = what;
		if (!detail.empty()) {
			if (!out.empty())
				out += " ";
			out += detail;
		}
		if (!action.empty()) {
			if (!out.empty())
				out += " ";
			out += action;
		}
		return out;
	}

	bool operator==(const UserMessage &other) const = default;
};

enum class Severity { Info, Warning, Error };

} // namespace rd
