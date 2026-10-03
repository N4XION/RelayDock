// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "providers/twitch/twitch_provider.h"

#include "security/secure_zero.h"
#include "utils/i18n.h"

namespace rd {

namespace {

struct Ingest {
	const char *id;
	const char *name;
	const char *host;
};

// From https://ingest.twitch.tv/ingests on 2026-10-04. Twitch can change this list. A user
// can always enter another address through the Custom server option.
const Ingest kIngests[] = {
	{"aps20", "Asia Pacific (Sydney)", "aps20.contribute.live-video.net"},
	{"aps10", "Asia Pacific (Singapore)", "aps10.contribute.live-video.net"},
	{"apn10", "Asia Pacific (Tokyo)", "apn10.contribute.live-video.net"},
	{"apn20", "Asia Pacific (Seoul)", "apn20.contribute.live-video.net"},
	{"aps30", "Asia Pacific (Mumbai)", "aps30.contribute.live-video.net"},
	{"sae10", "South America (S\xC3\xA3o Paulo)", "sae10.contribute.live-video.net"},
	{"usw20", "US West (Oregon)", "usw20.contribute.live-video.net"},
	{"use20", "US East (Ohio)", "use20.contribute.live-video.net"},
	{"use10", "US East (N. Virginia)", "use10.contribute.live-video.net"},
	{"eun10", "Europe (Stockholm)", "eun10.contribute.live-video.net"},
	{"euc10", "Europe (Frankfurt)", "euc10.contribute.live-video.net"},
	{"euw10", "Europe (Ireland)", "euw10.contribute.live-video.net"},
	{"euw30", "Europe (Paris)", "euw30.contribute.live-video.net"},
};

} // namespace

TwitchProvider::TwitchProvider()
{
	info_.id = kId;
	info_.displayName = "Twitch";
	info_.monogram = "Tw";
	info_.accentColor = "#9146FF";
	info_.testSupport = TestSupport::PrivateStream;
	info_.keyHelpUrl = "https://help.twitch.tv/s/article/twitch-stream-key-faq";
	info_.guide = "docs/twitch.md";

	// RTMPS first. Over plain RTMP the stream key crosses the network unencrypted.
	servers_.push_back({"auto", "Default (RTMPS, encrypted)", "rtmps://ingest.global-contribute.live-video.net/app"});
	servers_.push_back({"auto_rtmp", "Default (RTMP)", "rtmp://ingest.global-contribute.live-video.net/app"});
	for (const Ingest &ingest : kIngests) {
		servers_.push_back({ingest.id, ingest.name, std::string("rtmps://") + ingest.host + "/app"});
	}

	limits_.maxVideoBitrateKbps = 6000; // "Guide to Broadcast Health": maximum for a standard stream
	limits_.maxAudioBitrateKbps = 160;  // Broadcasting Guidelines
	limits_.maxLongEdge = 1920;
	limits_.maxShortEdge = 1080;
	limits_.maxFps = 60;
	limits_.keyframeIntervalSec = 2;
	limits_.videoCodecs = {"h264"};
	limits_.verticalSupported = true;
	limits_.preferredOrientation = Orientation::Horizontal;
	limits_.checkedOn = "2026-10-04";
	limits_.source = "https://help.twitch.tv/s/article/broadcasting-guidelines";

	defaults_.serverId = "auto";
	defaults_.video.bitrateKbps = 6000;
	defaults_.video.keyframeIntervalSec = 2;
	defaults_.audio.bitrateKbps = 160;
}

SecretString TwitchProvider::publishKey(const SecretString &key, bool privateTest) const
{
	if (!privateTest || key.empty())
		return key.clone();

	std::string value = key.reveal();
	value += value.find('?') == std::string::npos ? "?bandwidthtest=true" : "&bandwidthtest=true";
	SecretString out(value);
	secureZero(value);
	return out;
}

std::string TwitchProvider::rejectedAdvice() const
{
	return tr("Stop.Rejected.Action.Twitch",
		  "Check your stream key under Creator Dashboard, Settings, Stream. Twitch issues a new key when you reset it or change your password.");
}

void TwitchProvider::validateExtra(const DestinationConfig &config, const ValidationContext &,
				   std::vector<ValidationIssue> &issues) const
{
	if (config.video.orientation == Orientation::Vertical) {
		ValidationIssue issue;
		issue.severity = Severity::Warning;
		issue.field = field::kOrientation;
		issue.message.what =
			tr("Validate.Twitch.Vertical", "Twitch plays 16:9 video on the most devices.");
		issue.message.detail = tr("Validate.Twitch.Vertical.Detail",
					  "A vertical stream shows with bars beside it on many screens.");
		issue.message.action =
			tr("Validate.Twitch.Vertical.Action", "Switch this destination to horizontal unless you want that.");
		issues.push_back(std::move(issue));
	}
}

std::vector<std::string> TwitchProvider::setupNotes() const
{
	return {
		tr("Notes.Twitch.Key",
		   "Find your stream key in the Twitch Creator Dashboard under Settings, Stream. The key stays the same until you reset it."),
		tr("Notes.Twitch.Test",
		   "Test stream connects with your real key but your channel does not go live and Twitch sends no notifications."),
		tr("Notes.Twitch.Simulcast",
		   "Twitch allows streaming to other platforms at the same time when your Twitch stream is at least as good as the others, you do not send viewers away from Twitch, and you do not merge other platforms' chat into the Twitch stream."),
	};
}

} // namespace rd
