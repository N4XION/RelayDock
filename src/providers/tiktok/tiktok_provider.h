// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "providers/provider_base.h"

namespace rd {

// TikTok LIVE.
//
// TikTok publishes no ingest server list and no RTMP specification. It shows a server URL
// and a stream key to accounts that have access, each time they set up a LIVE. So the user
// supplies both, and RelayDock applies no hard limits. The defaults follow the settings in
// TikTok's own OBS guides. Sources and dates: docs/research/platform-requirements.md.
class TikTokProvider final : public ProviderBase {
public:
	TikTokProvider();

	static constexpr const char *kId = "tiktok";

	std::vector<std::string> setupNotes() const override;

protected:
	std::string rejectedAdvice() const override;
};

} // namespace rd
