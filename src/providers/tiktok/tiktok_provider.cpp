// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "providers/tiktok/tiktok_provider.h"

#include "utils/i18n.h"

namespace rd {

TikTokProvider::TikTokProvider()
{
	info_.id = kId;
	info_.displayName = "TikTok";
	info_.monogram = "TT";
	info_.accentColor = "#FE2C55";
	info_.userSuppliesServer = true;
	info_.testSupport = TestSupport::Reachability;
	info_.keyHelpUrl = "https://www.tiktok.com/creator-academy/article/live-best-practices-publishers";
	info_.guide = "docs/tiktok.md";

	// No published limits. The recommended values come from TikTok's own OBS guides:
	// 1080x1920, 30 FPS, CBR, 5400 Kbps, keyframe every 2 seconds.
	limits_.recommendedVideoBitrateKbps = 5400;
	limits_.recommendedFps = 30;
	limits_.keyframeIntervalSec = 2;
	limits_.videoCodecs = {"h264"};
	limits_.verticalSupported = true;
	limits_.preferredOrientation = Orientation::Vertical;
	limits_.checkedOn = "2026-10-04";
	limits_.source = "https://seller-us.tiktok.com/university/essay?knowledge_id=3360165320984366&lang=en";

	defaults_.video.bitrateKbps = 5400;
	defaults_.video.fps = 30;
	defaults_.video.keyframeIntervalSec = 2;
	defaults_.audio.bitrateKbps = 160;
}

std::string TikTokProvider::rejectedAdvice() const
{
	return tr("Stop.Rejected.Action.TikTok",
		  "TikTok stream keys expire. Set up the LIVE on TikTok again, copy the new server URL and stream key, and try again within an hour.");
}

std::vector<std::string> TikTokProvider::setupNotes() const
{
	return {
		tr("Notes.TikTok.Access",
		   "TikTok gives a server URL and stream key only to accounts with LIVE access for streaming software. TikTok decides who gets it, and the rules differ by region."),
		tr("Notes.TikTok.Key",
		   "Set up the LIVE on TikTok first, then copy the server URL and the stream key from that page. The key expires after a period without use, so copy it less than an hour before you stream."),
		tr("Notes.TikTok.End",
		   "Stopping the stream in RelayDock does not end the LIVE on TikTok. End it on TikTok as well."),
	};
}

} // namespace rd
