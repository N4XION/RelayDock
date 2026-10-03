// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "providers/youtube/youtube_provider.h"

#include "utils/i18n.h"

namespace rd {

YouTubeProvider::YouTubeProvider()
{
	info_.id = kId;
	info_.displayName = "YouTube";
	info_.monogram = "YT";
	info_.accentColor = "#FF0000";
	info_.testSupport = TestSupport::Reachability;
	info_.keyHelpUrl = "https://support.google.com/youtube/answer/2907883";
	info_.guide = "docs/youtube.md";

	// Host names from the OBS Studio service list. YouTube recommends RTMPS.
	servers_.push_back({"primary", "Primary (RTMPS, encrypted)", "rtmps://a.rtmps.youtube.com:443/live2"});
	servers_.push_back({"backup", "Backup (RTMPS, encrypted)", "rtmps://b.rtmps.youtube.com:443/live2?backup=1"});
	servers_.push_back({"primary_rtmp", "Primary (RTMP)", "rtmp://a.rtmp.youtube.com/live2"});
	servers_.push_back({"backup_rtmp", "Backup (RTMP)", "rtmp://b.rtmp.youtube.com/live2?backup=1"});

	// YouTube states recommended bitrates and no hard cap. 51000 Kbps is the cap OBS Studio
	// uses for YouTube and sits just above YouTube's 4K60 recommendation of 50 Mbps.
	limits_.maxVideoBitrateKbps = 51000;
	limits_.maxAudioBitrateKbps = 0;
	limits_.maxLongEdge = 3840;
	limits_.maxShortEdge = 2160;
	limits_.maxFps = 60;
	limits_.keyframeIntervalSec = 2;
	limits_.maxKeyframeIntervalSec = 4; // "Do not exceed 4 seconds"
	limits_.videoCodecs = {"h264", "hevc", "av1"};
	limits_.verticalSupported = true;
	limits_.preferredOrientation = Orientation::Horizontal;
	limits_.checkedOn = "2026-10-04";
	limits_.source = "https://support.google.com/youtube/answer/2853702";

	defaults_.serverId = "primary";
	defaults_.video.bitrateKbps = 6000;
	defaults_.video.keyframeIntervalSec = 2;
	defaults_.audio.bitrateKbps = 128;
}

std::string YouTubeProvider::rejectedAdvice() const
{
	return tr("Stop.Rejected.Action.YouTube",
		  "Check your stream key in YouTube Studio under Go live, Stream. Live streaming must be enabled for your channel, which takes up to 24 hours the first time.");
}

std::vector<std::string> YouTubeProvider::setupNotes() const
{
	return {
		tr("Notes.YouTube.Key",
		   "Find your stream key in YouTube Studio: Create, Go live, Stream tab. The default key stays the same between streams."),
		tr("Notes.YouTube.GoesLive",
		   "With the default settings YouTube goes live and notifies your subscribers as soon as RelayDock connects. To try a stream first, set its visibility to Private or Unlisted in YouTube Studio."),
		tr("Notes.YouTube.Vertical",
		   "For a vertical stream next to a horizontal one, YouTube needs a second stream key. Add a second YouTube destination for it."),
	};
}

} // namespace rd
