// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "diagnostics/preflight.h"

#include "utils/i18n.h"
#include "utils/strings.h"

#include <algorithm>
#include <cmath>

namespace rd {

namespace {

int rank(PreflightStatus status)
{
	switch (status) {
	case PreflightStatus::Failed:
		return 0;
	case PreflightStatus::Warning:
		return 1;
	case PreflightStatus::Ready:
		return 2;
	}
	return 2;
}

void add(PreflightReport &report, std::string id, PreflightStatus status, std::string title, UserMessage message,
	 std::string destinationId = {})
{
	PreflightItem item;
	item.id = std::move(id);
	item.status = status;
	item.title = std::move(title);
	item.message = std::move(message);
	item.destinationId = std::move(destinationId);
	report.items.push_back(std::move(item));
}

void addOk(PreflightReport &report, std::string id, std::string title, std::string what, std::string destinationId = {})
{
	add(report, std::move(id), PreflightStatus::Ready, std::move(title), {std::move(what), {}, {}},
	    std::move(destinationId));
}

} // namespace

const char *preflightStatusName(PreflightStatus status)
{
	switch (status) {
	case PreflightStatus::Ready:
		return "ready";
	case PreflightStatus::Warning:
		return "warning";
	case PreflightStatus::Failed:
		return "failed";
	}
	return "ready";
}

size_t PreflightReport::count(PreflightStatus wanted) const
{
	return static_cast<size_t>(
		std::count_if(items.begin(), items.end(), [wanted](const PreflightItem &item) { return item.status == wanted; }));
}

PreflightReport runPreflight(const PreflightInput &input)
{
	PreflightReport report;
	const std::string videoTitle = loc("Preflight.Area.Video", "Video");
	const std::string audioTitle = loc("Preflight.Area.Audio", "Audio");
	const std::string networkTitle = loc("Preflight.Area.Network", "Network");
	const std::string performanceTitle = loc("Preflight.Area.Performance", "Performance");
	const std::string destinationsTitle = loc("Preflight.Area.Destinations", "Destinations");

	// ---- Video source ------------------------------------------------------------------------
	if (!input.obs.videoRunning) {
		add(report, "video.not_running", PreflightStatus::Failed, videoTitle,
		    {loc("Preflight.Video.NotRunning", "OBS video is not running."),
		     loc("Preflight.Video.NotRunning.Detail", "OBS reports no active video output."),
		     loc("Preflight.Video.NotRunning.Action", "Check the OBS video settings, then restart OBS.")});
	} else if (!input.obs.sceneHasVideo) {
		add(report, "video.no_source", PreflightStatus::Warning, videoTitle,
		    {loc("Preflight.Video.NoSource", "Your current scene shows no video source."),
		     loc("Preflight.Video.NoSource.Detail", "Viewers see a black picture."),
		     loc("Preflight.Video.NoSource.Action", "Add a source to the scene, or switch to a scene that has one.")});
	} else {
		addOk(report, "video.ok", videoTitle, loc("Preflight.Video.Ok", "Your scene has video."));
	}

	// ---- Audio source ------------------------------------------------------------------------
	if (!input.obs.hasAudioSource) {
		add(report, "audio.no_source", PreflightStatus::Warning, audioTitle,
		    {loc("Preflight.Audio.NoSource", "OBS has no audio source."),
		     loc("Preflight.Audio.NoSource.Detail", "Your stream is silent."),
		     loc("Preflight.Audio.NoSource.Action",
			 "Add a microphone or desktop audio under OBS Settings, Audio.")});
	} else if (input.obs.allAudioMuted) {
		add(report, "audio.muted", PreflightStatus::Warning, audioTitle,
		    {loc("Preflight.Audio.Muted", "Every audio source in OBS is muted."),
		     loc("Preflight.Audio.Muted.Detail", "Your stream is silent."),
		     loc("Preflight.Audio.Muted.Action", "Unmute a source in the OBS Audio Mixer.")});
	} else {
		addOk(report, "audio.ok", audioTitle, loc("Preflight.Audio.Ok", "OBS has audio."));
	}

	// ---- Encoders ----------------------------------------------------------------------------
	if (input.obs.videoEncoderCount <= 0) {
		add(report, "encoder.none", PreflightStatus::Failed, videoTitle,
		    {loc("Preflight.Encoder.None", "OBS offers no video encoder for streaming."),
		     loc("Preflight.Encoder.None.Detail", "RelayDock found no H.264, HEVC or AV1 encoder."),
		     loc("Preflight.Encoder.None.Action",
			 "Repair or reinstall OBS Studio. The x264 encoder ships with it.")});
	}

	// ---- Destinations --------------------------------------------------------------------------
	if (input.destinations.empty()) {
		add(report, "destinations.none", PreflightStatus::Failed, destinationsTitle,
		    {loc("Preflight.Destinations.None", "No destination is enabled."),
		     {},
		     loc("Preflight.Destinations.None.Action", "Add a platform, or enable one of your destinations.")});
	}

	for (const PreflightDestination &destination : input.destinations) {
		const std::string &name = destination.config.name;
		const std::string &id = destination.config.id;
		bool destinationOk = true;

		if (!destination.provider) {
			add(report, "destination.unknown_provider", PreflightStatus::Failed, name,
			    {locf("Start.UnknownProvider", "{0} uses a platform this RelayDock version does not know.", name),
			     locf("Start.UnknownProvider.Detail", "The platform id is \"{0}\".", destination.config.provider),
			     loc("Start.UnknownProvider.Action", "Update RelayDock, or remove this destination and add it again.")},
			    id);
			continue;
		}

		// Configuration, credentials and URL, straight from the provider's own rules.
		for (const ValidationIssue &issue : destination.issues) {
			const bool error = issue.severity == Severity::Error;
			add(report, "destination." + issue.field, error ? PreflightStatus::Failed : PreflightStatus::Warning,
			    name, issue.message, id);
			destinationOk = false;
		}

		if (destination.keySessionOnly) {
			add(report, "destination.key_session_only", PreflightStatus::Warning, name,
			    {locf("Preflight.Key.SessionOnly", "The stream key for {0} is not saved.", name),
			     loc("Preflight.Key.SessionOnly.Detail",
				 "Windows could not store it, so RelayDock keeps it in memory until OBS closes."),
			     loc("Preflight.Key.SessionOnly.Action", "Enter the key again after you restart OBS.")},
			    id);
			destinationOk = false;
		}

		if (!destination.duplicateKeyOf.empty()) {
			add(report, "destination.duplicate_key", PreflightStatus::Warning, name,
			    {locf("Preflight.Key.Duplicate", "{0} and {1} use the same server and stream key.", name,
				  destination.duplicateKeyOf),
			     loc("Preflight.Key.Duplicate.Detail", "Most platforms accept one connection per key and drop the other."),
			     loc("Preflight.Key.Duplicate.Action", "Disable one of them, or give it its own key.")},
			    id);
			destinationOk = false;
		}

		// Encoder availability
		if (!destination.effective.usable()) {
			add(report, "destination.no_encoder", PreflightStatus::Failed, name,
			    {locf("Start.NoEncoder", "{0} has no video encoder to use.", name),
			     loc("Start.NoEncoder.Detail", "OBS reports no H.264 encoder that the RTMP output accepts."),
			     loc("Start.NoEncoder.Action",
				 "Check that OBS itself can stream with the x264 encoder, then restart OBS.")},
			    id);
			destinationOk = false;
		}

		// Where the effective settings differ from what the user saved.
		for (const std::string &note : destination.effective.notes) {
			add(report, "destination.adjusted", PreflightStatus::Warning, name, {note, {}, {}}, id);
			destinationOk = false;
		}

		if (destination.verticalMissingItems > 0) {
			add(report, "destination.vertical_missing", PreflightStatus::Warning, name,
			    {locf("Preflight.Vertical.Missing", "The vertical layout for {0} refers to sources OBS does not have. Missing: {1}.",
				  name, destination.verticalMissingItems),
			     loc("Preflight.Vertical.Missing.Detail", "Those parts of the layout stay empty."),
			     loc("Preflight.Vertical.Missing.Action", "Open the vertical layout editor and pick the sources again.")},
			    id);
			destinationOk = false;
		}

		if (destinationOk) {
			const EffectiveVideo &video = destination.effective.video;
			addOk(report, "destination.ok", name,
			      locf("Preflight.Destination.Ok", "{0} is ready: {1} at {2} FPS, {3}.", name,
				   formatResolution(video.width, video.height), static_cast<int>(std::lround(video.fps)),
				   formatBitrate(video.bitrateKbps)),
			      id);
		}
	}

	// ---- Known conflicts between destinations --------------------------------------------------
	// Twitch's simulcast rules ask that Twitch viewers get a stream at least as good as the
	// one on other platforms.
	const PreflightDestination *twitch = nullptr;
	for (const PreflightDestination &destination : input.destinations) {
		if (destination.provider && destination.provider->info().id == "twitch" && destination.effective.usable())
			twitch = &destination;
	}
	if (twitch) {
		for (const PreflightDestination &other : input.destinations) {
			if (&other == twitch || !other.provider || other.provider->info().id == "twitch" ||
			    !other.effective.usable())
				continue;
			const EffectiveVideo &a = twitch->effective.video;
			const EffectiveVideo &b = other.effective.video;
			// Compare streams of the same shape only. A vertical stream is a different picture.
			if (a.orientation != b.orientation)
				continue;
			const long long pixelsA = static_cast<long long>(a.width) * a.height;
			const long long pixelsB = static_cast<long long>(b.width) * b.height;
			if (pixelsB > pixelsA || b.fps > a.fps + 0.5) {
				add(report, "conflict.twitch_simulcast", PreflightStatus::Warning, twitch->config.name,
				    {locf("Preflight.Twitch.Simulcast", "{0} gets a better picture than {1}.", other.config.name,
					  twitch->config.name),
				     loc("Preflight.Twitch.Simulcast.Detail",
					 "Twitch's simulcasting rules ask that the Twitch stream is at least as good as the stream on other platforms."),
				     locf("Preflight.Twitch.Simulcast.Action",
					  "Raise the resolution or frame rate of {0}, or lower them for {1}.", twitch->config.name,
					  other.config.name)},
				    twitch->config.id);
				break;
			}
		}
	}

	// ---- Encoder load ----------------------------------------------------------------------------
	const size_t encoders = input.plan.videoEncoderCount();
	if (encoders > 0) {
		size_t software = 0;
		for (const EncoderGroup &group : input.plan.groups) {
			if (encoderFamilyFromId(group.settings.encoderId) == EncoderFamily::X264 ||
			    encoderFamilyFromId(group.settings.encoderId) == EncoderFamily::OtherSoftware)
				++software;
		}

		if (input.mode == PerformanceMode::Potato && encoders > 2) {
			add(report, "performance.potato_encoders", PreflightStatus::Warning, performanceTitle,
			    {locf("Preflight.Potato.Encoders", "Potato Mode runs {0} video encoders for your destinations.", encoders),
			     loc("Preflight.Potato.Encoders.Detail",
				 "Locked settings keep some destinations from sharing an encoder, which costs performance."),
			     loc("Preflight.Potato.Encoders.Action", "Unlock resolution, frame rate and bitrate on destinations that do not need their own.")});
		} else if (software >= 3) {
			add(report, "performance.software_encoders", PreflightStatus::Warning, performanceTitle,
			    {locf("Preflight.Software.Encoders", "{0} software video encoders will run at the same time.", software),
			     loc("Preflight.Software.Encoders.Detail", "Each x264 encode uses a large share of the processor."),
			     loc("Preflight.Software.Encoders.Action",
				 "Use Balanced or Potato mode so destinations share an encoder, or pick a hardware encoder.")});
		} else {
			addOk(report, "performance.encoders", performanceTitle,
			      locf("Preflight.Encoders", "Destinations to start: {0}. Video encoders they need: {1}.",
				   input.plan.destinationCount(), encoders));
		}
	}

	// ---- OBS rendering state -----------------------------------------------------------------------
	if (input.obs.renderLagPercent >= 2.0) {
		add(report, "performance.render_lag", PreflightStatus::Warning, performanceTitle,
		    {locf("Preflight.RenderLag", "OBS already misses {0} percent of its frames.",
			  static_cast<int>(std::lround(input.obs.renderLagPercent))),
		     loc("Preflight.RenderLag.Detail", "The graphics chip is busy. Streaming adds to its load."),
		     loc("Preflight.RenderLag.Action",
			 "Lower the OBS frame rate or canvas size, close other programs that use the graphics chip, or switch to Potato Mode.")});
	}

	// ---- Upload ---------------------------------------------------------------------------------
	if (!input.destinations.empty()) {
		const UserMessage message = bandwidthMessage(input.bandwidth);
		switch (input.bandwidth.status) {
		case BandwidthStatus::Ok:
			addOk(report, "network.ok", networkTitle,
			      locf("Preflight.Network.Ok", "Your destinations need {0} of your {1} upload.",
				   formatBitrate(input.bandwidth.requiredKbps), formatBitrate(input.bandwidth.uploadKbps)));
			break;
		case BandwidthStatus::Unknown:
			// Not knowing the upload speed is not a problem by itself. Say what is needed and
			// how to get the comparison, without raising a warning on every start.
			addOk(report, "network.unknown", networkTitle,
			      locf("Preflight.Network.Unknown",
				   "Your destinations need {0} of upload. Enter your upload speed under Settings, Network to have RelayDock check that it fits.",
				   formatBitrate(input.bandwidth.requiredKbps)));
			break;
		case BandwidthStatus::Tight:
			add(report, "network.tight", PreflightStatus::Warning, networkTitle, message);
			break;
		case BandwidthStatus::Exceeded:
			add(report, "network.exceeded", PreflightStatus::Failed, networkTitle, message);
			break;
		}
	}

	// Failed first, then warnings, then what passed. Order inside each group is kept.
	std::stable_sort(report.items.begin(), report.items.end(),
			 [](const PreflightItem &a, const PreflightItem &b) { return rank(a.status) < rank(b.status); });

	report.status = PreflightStatus::Ready;
	for (const PreflightItem &item : report.items) {
		if (rank(item.status) < rank(report.status))
			report.status = item.status;
	}
	return report;
}

} // namespace rd
