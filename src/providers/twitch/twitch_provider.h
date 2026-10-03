// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "providers/provider_base.h"

namespace rd {

// Twitch. Limits and servers come from Twitch's own pages and its public ingest list.
// Sources and dates: docs/research/platform-requirements.md.
class TwitchProvider final : public ProviderBase {
public:
	TwitchProvider();

	static constexpr const char *kId = "twitch";

	// Twitch publishes a private test mode: a key ending in "?bandwidthtest=true" connects
	// and streams, but the channel does not go live and nobody is notified.
	SecretString publishKey(const SecretString &key, bool privateTest) const override;
	std::vector<std::string> setupNotes() const override;

protected:
	void validateExtra(const DestinationConfig &config, const ValidationContext &context,
			   std::vector<ValidationIssue> &issues) const override;
	std::string rejectedAdvice() const override;
};

} // namespace rd
