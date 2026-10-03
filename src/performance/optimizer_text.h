// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "performance/optimizer.h"

#include <string>
#include <vector>

namespace rd {

// The words for what the optimiser proposes or did. Kept apart from the rules so the wording
// can be tested and translated without touching them.

struct ChangeText {
	std::string title;  // "Lower the bitrate of Twitch and YouTube to 85 percent"
	std::string reason; // What RelayDock measured
	std::string effect; // What applying it does to the stream
};

// `names` are the display names of change.destinationIds, in the same order.
ChangeText describeChange(const OptimizerChange &change, const std::vector<std::string> &names);

// One line for the log and the "recent changes" list, in the past tense.
std::string describeAppliedChange(const OptimizerChange &change, const std::vector<std::string> &names);

// A problem RelayDock sees but may not act on, because every setting that would help is
// locked or already at its lowest step.
std::string describeNotice(const OptimizerNotice &notice, const std::vector<std::string> &names);

// "Twitch", "Twitch and YouTube", "Twitch, YouTube and Facebook"
std::string joinNames(const std::vector<std::string> &names);

} // namespace rd
