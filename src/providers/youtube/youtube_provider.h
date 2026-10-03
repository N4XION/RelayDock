// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "providers/provider_base.h"

namespace rd {

// YouTube Live. Limits come from YouTube's encoder settings help page. YouTube's public
// documentation does not print ingest host names, so the server list is the one OBS Studio
// ships. Sources and dates: docs/research/platform-requirements.md.
class YouTubeProvider final : public ProviderBase {
public:
	YouTubeProvider();

	static constexpr const char *kId = "youtube";

	std::vector<std::string> setupNotes() const override;

protected:
	std::string rejectedAdvice() const override;
};

} // namespace rd
