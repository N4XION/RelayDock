// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "app/diagnostics_service.h"

#include "app/app_context.h"
#include "app/performance_monitor.h"
#include "build_info.h"
#include "network/stream_url.h"
#include "outputs/output_manager.h"
#include "outputs/vertical_canvas.h"
#include "security/redactor.h"
#include "utils/clock.h"
#include "utils/log.h"
#include "utils/paths.h"
#include "utils/system_info.h"

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <algorithm>
#include <map>

namespace rd {

namespace {

struct AudioFacts {
	int audible = 0;
	int unmuted = 0;
};

bool sceneItemShowsVideo(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (source && obs_sceneitem_visible(item) && (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) != 0) {
		*static_cast<bool *>(param) = true;
		return false; // One is enough
	}
	return true;
}

bool countAudioSource(void *param, obs_source_t *source)
{
	auto *facts = static_cast<AudioFacts *>(param);
	if ((obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) == 0)
		return true;
	// Only sources that reach the mix right now: global devices and sources in the shown scene.
	if (!obs_source_active(source) || !obs_source_audio_active(source))
		return true;
	++facts->audible;
	if (!obs_source_muted(source))
		++facts->unmuted;
	return true;
}

bool collectAdapter(void *param, const char *name, uint32_t)
{
	if (name && *name)
		static_cast<std::vector<std::string> *>(param)->push_back(name);
	return true;
}

PreflightObsFacts gatherObsFacts(AppContext &app)
{
	PreflightObsFacts facts;

	obs_video_info video{};
	facts.videoRunning = obs_get_video_info(&video) && obs_get_video() != nullptr;

	facts.sceneHasVideo = false;
	OBSSourceAutoRelease sceneSource = obs_frontend_get_current_scene();
	if (obs_scene_t *scene = obs_scene_from_source(sceneSource))
		obs_scene_enum_items(scene, sceneItemShowsVideo, &facts.sceneHasVideo);

	AudioFacts audio;
	obs_enum_sources(countAudioSource, &audio);
	facts.hasAudioSource = audio.audible > 0;
	facts.allAudioMuted = audio.audible > 0 && audio.unmuted == 0;

	facts.renderLagPercent = app.performance().snapshot().renderLagPercent;
	facts.videoEncoderCount = static_cast<int>(app.streamContext().videoEncoders.size());
	return facts;
}

std::vector<std::string> enabledIds(const AppContext &app)
{
	std::vector<std::string> ids;
	for (const DestinationConfig &destination : app.config().destinations) {
		if (destination.enabled)
			ids.push_back(destination.id);
	}
	return ids;
}

const EffectiveDestination *findEffective(const std::vector<EffectiveDestination> &all, const std::string &id)
{
	const auto found =
		std::find_if(all.begin(), all.end(), [&](const EffectiveDestination &destination) { return destination.id == id; });
	return found == all.end() ? nullptr : &*found;
}

// Effective settings of the given destinations. Enabled ones resolve together, so the result
// matches what a start would use.
std::vector<EffectiveDestination> effectiveFor(AppContext &app, const std::vector<std::string> &ids)
{
	std::vector<EffectiveDestination> out;
	const std::vector<EffectiveDestination> enabled = app.resolveEffective();
	for (const std::string &id : ids) {
		if (const EffectiveDestination *found = findEffective(enabled, id)) {
			out.push_back(*found);
			continue;
		}
		const std::vector<EffectiveDestination> withThis = app.resolveEffective(id);
		if (const EffectiveDestination *found = findEffective(withThis, id))
			out.push_back(*found);
	}
	return out;
}

} // namespace

std::string safeServerText(AppContext &app, const DestinationConfig &config)
{
	const IProvider *provider = app.providers().find(config.provider);
	if (!provider)
		return {};
	const std::string url = provider->endpoint(config).serverUrl;
	const StreamUrlResult parsed = parseStreamUrl(url);
	return parsed.ok() ? Redactor::redactUrl(parsed.url.displayText()) : Redactor::redactPatterns(url);
}

BandwidthBudget currentBandwidth(AppContext &app)
{
	std::vector<std::string> ids = enabledIds(app);
	// A destination that streams counts, enabled or not.
	for (const DestinationConfig &destination : app.config().destinations) {
		if (!destination.enabled && app.outputs().runtime(destination.id).active())
			ids.push_back(destination.id);
	}

	std::map<std::string, std::string> names;
	std::map<std::string, int> measured;
	for (const std::string &id : ids) {
		if (const DestinationConfig *config = app.config().findDestination(id))
			names[id] = config->name;
		if (app.outputs().runtime(id).phase == DestinationPhase::Live)
			measured[id] = app.outputs().stats(id).bitrateKbps;
	}
	return computeBandwidth(effectiveFor(app, ids), names, measured, app.config().network);
}

PreflightInput gatherPreflightInput(AppContext &app, std::vector<std::string> destinationIds)
{
	if (destinationIds.empty())
		destinationIds = enabledIds(app);

	PreflightInput input;
	input.obs = gatherObsFacts(app);
	input.mode = app.config().performanceMode;

	const std::vector<EffectiveDestination> effective = effectiveFor(app, destinationIds);
	std::map<std::string, std::string> names;

	// For spotting two destinations that publish to the same place with the same key.
	struct Publish {
		std::string name;
		std::string server;
		SecretString key;
	};
	std::vector<Publish> seen;

	for (const std::string &id : destinationIds) {
		const DestinationConfig *config = app.config().findDestination(id);
		if (!config)
			continue;

		PreflightDestination destination;
		destination.config = *config;
		destination.provider = app.providers().find(config->provider);
		destination.issues = app.validateDestination(id);
		if (const EffectiveDestination *found = findEffective(effective, id))
			destination.effective = *found;
		destination.keySessionOnly = app.vault().sessionOnly({id, CredentialKind::StreamKey});
		names[id] = config->name;

		if (config->video.orientation == Orientation::Vertical) {
			const std::string layoutId = app.vertical().resolveLayoutId(config->verticalLayoutId);
			destination.verticalMissingItems = static_cast<int>(app.vertical().canvasFor(layoutId).missingItems().size());
		}

		if (destination.provider) {
			Publish publish;
			publish.name = config->name;
			publish.server = destination.provider->endpoint(*config).serverUrl;
			if (app.vault().get({id, CredentialKind::StreamKey}, publish.key).ok() && !publish.key.empty()) {
				for (const Publish &other : seen) {
					if (other.server == publish.server && other.key.equals(publish.key)) {
						destination.duplicateKeyOf = other.name;
						break;
					}
				}
				seen.push_back(std::move(publish));
			}
		}

		input.destinations.push_back(std::move(destination));
	}

	input.plan = planEncoders(effective);
	input.bandwidth = computeBandwidth(effective, names, {}, app.config().network);
	return input;
}

PreflightReport runPreflightNow(AppContext &app, std::vector<std::string> destinationIds)
{
	return runPreflight(gatherPreflightInput(app, std::move(destinationIds)));
}

std::string buildDiagnosticsNow(AppContext &app)
{
	DiagnosticsInput input;
	input.generatedAtUtc = utcTimestampIso8601();
	input.relayDockVersion = buildVersionString();
	input.buildDate = buildInfo().buildDate;
	input.obsVersion = obs_get_version_string();

	const SystemInfo system = querySystemInfo();
	input.osVersion = system.osVersion;
	input.cpuName = system.cpuName;
	input.cpuCores = system.cpuCores;
	input.cpuThreads = system.cpuThreads;
	input.ramMb = system.ramMb;

	obs_enter_graphics();
	gs_enum_adapters(collectAdapter, &input.gpus);
	obs_leave_graphics();

	input.context = app.streamContext();
	input.mode = app.config().performanceMode;
	input.optimizer = app.config().optimizer;
	input.network = app.config().network;
	input.credentialBackend = app.vault().backendName();
	input.settingsFolder = pathToUtf8(app.configStore().directory());
	input.settingsNotes = app.loadNotes();

	const PerformanceSnapshot &snapshot = app.performance().snapshot();
	input.cpuPercent = snapshot.obsCpuPercent;
	input.renderLagPercent = snapshot.renderLagPercent;
	input.memoryMb = snapshot.memoryMb;

	std::vector<std::string> allIds;
	for (const DestinationConfig &destination : app.config().destinations)
		allIds.push_back(destination.id);
	const std::vector<EffectiveDestination> effective = effectiveFor(app, allIds);

	for (const DestinationConfig &config : app.config().destinations) {
		DiagnosticsDestination destination;
		destination.config = config;
		const IProvider *provider = app.providers().find(config.provider);
		destination.providerName = provider ? provider->info().displayName : config.provider;
		destination.serverText = provider ? provider->endpoint(config).serverUrl : std::string();
		destination.hasStreamKey = app.vault().has({config.id, CredentialKind::StreamKey});
		destination.hasPassword = app.vault().has({config.id, CredentialKind::Password});

		const DestinationRuntime runtime = app.outputs().runtime(config.id);
		const DestinationStats stats = app.outputs().stats(config.id);
		const DestinationSessionInfo session = app.outputs().sessionInfo(config.id);
		destination.phase = destinationPhaseName(runtime.phase);
		destination.lastStop = stopReasonName(runtime.lastStop);
		destination.lastError = runtime.error.text();
		destination.reconnects = runtime.reconnectsThisRun;
		destination.liveSeconds = stats.liveSeconds;
		destination.bitrateKbps = stats.bitrateKbps;
		destination.totalFrames = stats.totalFrames;
		destination.droppedFrames = stats.droppedFrames;

		// What it streams with now when it has an output, otherwise what a start would use.
		if (session.hasOutput)
			destination.effective = session.effective;
		else if (const EffectiveDestination *found = findEffective(effective, config.id))
			destination.effective = *found;
		destination.videoEncoderName = session.videoEncoderName;
		for (const std::string &otherId : session.sharedWith) {
			const DestinationConfig *other = app.config().findDestination(otherId);
			destination.sharedWith.push_back(other ? other->name : otherId);
		}
		for (const ValidationIssue &issue : app.validateDestination(config.id))
			destination.issues.push_back(issue.message.text());

		input.destinations.push_back(std::move(destination));
	}

	input.preflight = runPreflightNow(app);
	input.logLines = recentLogLines(200);
	return buildDiagnosticsReport(input, globalRedactor());
}

} // namespace rd
