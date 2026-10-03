// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "core/user_message.h"
#include "performance/effective_settings.h"
#include "settings/app_config.h"

#include <map>
#include <string>
#include <vector>

namespace rd {

// Works out how much upload the configured destinations need and compares it with the upload
// speed the user entered. RelayDock never runs a speed test. It only adds up what it is told
// to send and what it measures while sending.
//
// Sharing an encoder saves processing, not upload. Every destination sends its own copy of
// the stream, so the upload need is the sum over all destinations.

// Share added for RTMP, FLV, TCP and IP headers on top of the encoded bitrate.
inline constexpr double kProtocolOverhead = 1.04;

struct BandwidthEntry {
	std::string destinationId;
	std::string name;
	int videoKbps = 0;
	int audioKbps = 0;
	int requiredKbps = 0; // Video plus audio plus protocol overhead
	int measuredKbps = 0; // What the destination sends right now. 0 when it is not live.
};

enum class BandwidthStatus {
	Unknown,  // The user has not entered an upload speed
	Ok,       // Within the safe share of the upload speed
	Tight,    // Above the safe share but below the full upload speed
	Exceeded, // More than the upload speed
};

const char *bandwidthStatusName(BandwidthStatus status);

struct BandwidthBudget {
	std::vector<BandwidthEntry> entries;
	int requiredKbps = 0;  // Sum of what the listed destinations need
	int measuredKbps = 0;  // Sum of what the live destinations send right now
	int uploadKbps = 0;    // The upload speed the user entered. 0 means not set.
	int safeLimitKbps = 0; // The share of the upload speed RelayDock treats as safe
	BandwidthStatus status = BandwidthStatus::Unknown;

	// Required upload as a share of the upload speed, in percent. 0 when the speed is not set.
	double usagePercent() const
	{
		return uploadKbps > 0 ? 100.0 * requiredKbps / uploadKbps : 0.0;
	}
};

// `names` maps destination ids to display names. `measuredKbps` maps destination ids to the
// bitrate they send right now. Both may leave ids out.
BandwidthBudget computeBandwidth(const std::vector<EffectiveDestination> &destinations,
				 const std::map<std::string, std::string> &names,
				 const std::map<std::string, int> &measuredKbps, const NetworkConfig &network);

// What to tell the user about the budget. Empty when the status is Ok.
UserMessage bandwidthMessage(const BandwidthBudget &budget);

} // namespace rd
