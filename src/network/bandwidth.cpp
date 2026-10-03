// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "network/bandwidth.h"

#include "utils/i18n.h"
#include "utils/strings.h"

#include <cmath>

namespace rd {

const char *bandwidthStatusName(BandwidthStatus status)
{
	switch (status) {
	case BandwidthStatus::Unknown:
		return "unknown";
	case BandwidthStatus::Ok:
		return "ok";
	case BandwidthStatus::Tight:
		return "tight";
	case BandwidthStatus::Exceeded:
		return "exceeded";
	}
	return "unknown";
}

BandwidthBudget computeBandwidth(const std::vector<EffectiveDestination> &destinations,
				 const std::map<std::string, std::string> &names,
				 const std::map<std::string, int> &measuredKbps, const NetworkConfig &network)
{
	BandwidthBudget budget;

	for (const EffectiveDestination &destination : destinations) {
		BandwidthEntry entry;
		entry.destinationId = destination.id;
		const auto name = names.find(destination.id);
		entry.name = name == names.end() ? destination.id : name->second;
		entry.videoKbps = destination.video.bitrateKbps;
		entry.audioKbps = destination.audio.bitrateKbps;
		entry.requiredKbps =
			static_cast<int>(std::lround((entry.videoKbps + entry.audioKbps) * kProtocolOverhead));
		const auto measured = measuredKbps.find(destination.id);
		entry.measuredKbps = measured == measuredKbps.end() ? 0 : measured->second;

		budget.requiredKbps += entry.requiredKbps;
		budget.measuredKbps += entry.measuredKbps;
		budget.entries.push_back(std::move(entry));
	}

	budget.uploadKbps = network.uploadKbps;
	if (network.uploadKbps <= 0) {
		budget.status = BandwidthStatus::Unknown;
		return budget;
	}

	budget.safeLimitKbps = static_cast<int>(static_cast<long long>(network.uploadKbps) * network.safetyPercent / 100);
	if (budget.requiredKbps > budget.uploadKbps)
		budget.status = BandwidthStatus::Exceeded;
	else if (budget.requiredKbps > budget.safeLimitKbps)
		budget.status = BandwidthStatus::Tight;
	else
		budget.status = BandwidthStatus::Ok;
	return budget;
}

UserMessage bandwidthMessage(const BandwidthBudget &budget)
{
	UserMessage message;
	switch (budget.status) {
	case BandwidthStatus::Ok:
		break;
	case BandwidthStatus::Unknown:
		message.what = locf("Bandwidth.Unknown", "Your destinations need {0} of upload in total.",
				    formatBitrate(budget.requiredKbps));
		message.detail = loc("Bandwidth.Unknown.Detail",
				     "RelayDock does not know your upload speed, so it cannot tell whether that fits.");
		message.action = loc("Bandwidth.Unknown.Action", "Enter your upload speed under Settings, Network.");
		break;
	case BandwidthStatus::Tight:
		message.what = locf("Bandwidth.Tight", "Your destinations need {0} of your {1} upload.",
				    formatBitrate(budget.requiredKbps), formatBitrate(budget.uploadKbps));
		message.detail = locf("Bandwidth.Tight.Detail",
				      "That is above the safe limit of {0}. Other traffic on your network can cause dropped frames.",
				      formatBitrate(budget.safeLimitKbps));
		message.action =
			loc("Bandwidth.Tight.Action", "Lower a bitrate or disable a destination to leave more room.");
		break;
	case BandwidthStatus::Exceeded:
		message.what = locf("Bandwidth.Exceeded", "Your destinations need {0}, more than your {1} upload.",
				    formatBitrate(budget.requiredKbps), formatBitrate(budget.uploadKbps));
		message.detail =
			loc("Bandwidth.Exceeded.Detail", "Streams will drop frames or disconnect when you start them all.");
		message.action = loc("Bandwidth.Exceeded.Action", "Lower the bitrates or disable a destination.");
		break;
	}
	return message;
}

} // namespace rd
