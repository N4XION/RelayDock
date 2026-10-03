// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "providers/custom_rtmp/custom_rtmp_provider.h"

namespace rd {

CustomRtmpProvider::CustomRtmpProvider(Transport transport)
{
	const bool tls = transport == Transport::Rtmps;

	info_.id = tls ? kRtmpsId : kRtmpId;
	info_.displayName = tls ? "Custom RTMPS" : "Custom RTMP";
	info_.monogram = tls ? "RS" : "R";
	info_.accentColor = "#6B7280";
	info_.userSuppliesServer = true;
	info_.tlsRequired = tls;
	info_.tlsForbidden = !tls;
	info_.supportsAuth = true;
	// Some servers publish to the application alone and take no stream name.
	info_.streamKeyRequired = false;
	info_.testSupport = TestSupport::Reachability;
	info_.guide = "docs/custom-rtmp.md";

	// A custom server sets its own limits. RelayDock cannot know them.
	limits_ = ProviderLimits{};
	limits_.keyframeIntervalSec = 2;
	limits_.verticalSupported = true;
	limits_.videoCodecs = {"h264", "hevc", "av1"};

	defaults_.video.bitrateKbps = 6000;
	defaults_.video.keyframeIntervalSec = 2;
	defaults_.audio.bitrateKbps = 160;
}

} // namespace rd
