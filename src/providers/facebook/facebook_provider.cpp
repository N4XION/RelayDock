// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "providers/facebook/facebook_provider.h"

#include "utils/i18n.h"

namespace rd {

FacebookProvider::FacebookProvider()
{
	info_.id = kId;
	info_.displayName = "Facebook";
	info_.monogram = "Fb";
	info_.accentColor = "#1877F2";
	info_.tlsRequired = true; // Meta: live broadcasts must use RTMPS
	info_.testSupport = TestSupport::Reachability;
	info_.keyHelpUrl = "https://www.facebook.com/help/587160588142067";
	info_.guide = "docs/facebook.md";

	servers_.push_back({"default", "Default (RTMPS, encrypted)", "rtmps://rtmp-api.facebook.com:443/rtmp/"});

	limits_.maxVideoBitrateKbps = 9000; // Top of Meta's 1080p60 range
	limits_.maxAudioBitrateKbps = 256;
	limits_.maxLongEdge = 1920;
	limits_.maxShortEdge = 1080;
	limits_.maxFps = 60;
	limits_.keyframeIntervalSec = 2;
	limits_.maxKeyframeIntervalSec = 4; // "Do not exceed 4 seconds"
	limits_.videoCodecs = {"h264"};
	// Meta makes no statement about 9:16 from an encoder and says to aim for 16:9.
	limits_.verticalSupported = false;
	limits_.preferredOrientation = Orientation::Horizontal;
	limits_.checkedOn = "2026-10-04";
	limits_.source = "https://developers.facebook.com/documentation/live-video-api/reference";

	defaults_.serverId = "default";
	defaults_.video.bitrateKbps = 6000;
	defaults_.video.keyframeIntervalSec = 2;
	defaults_.audio.bitrateKbps = 128;
}

std::string FacebookProvider::rejectedAdvice() const
{
	return tr("Stop.Rejected.Action.Facebook",
		  "Copy the current stream key from Facebook Live Producer. A standard key works for one stream. Turn on Persistent stream key there to reuse a key.");
}

std::vector<std::string> FacebookProvider::setupNotes() const
{
	return {
		tr("Notes.Facebook.Key",
		   "Find the stream key at facebook.com/live/create: Go live, Streaming software. A standard key works for one stream only. Turn on Persistent stream key under Advanced settings to reuse one."),
		tr("Notes.Facebook.Preview",
		   "Facebook shows a preview when RelayDock connects. Viewers see the stream after you click Go live on Facebook."),
		tr("Notes.Facebook.Limits",
		   "Facebook ends a stream after 8 hours. After a lost connection you have 2 to 3 minutes to reconnect before you need a new key."),
	};
}

} // namespace rd
