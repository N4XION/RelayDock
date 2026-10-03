// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "app/performance_monitor.h"

#include "app/app_context.h"
#include "encoders/encode_plan.h"
#include "outputs/output_manager.h"
#include "outputs/vertical_canvas.h"
#include "utils/clock.h"
#include "utils/log.h"

#include <obs.h>
#include <util/platform.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace rd {

namespace {

constexpr size_t kHistoryLimit = 50;

std::string encodeKey(const EffectiveVideo &video)
{
	return video.orientation == Orientation::Vertical ? "encode:" + video.verticalLayoutId : std::string("encode:main");
}

} // namespace

PerformanceMonitor::PerformanceMonitor(AppContext &app, QObject *parent) : QObject(parent), app_(app)
{
	connect(&timer_, &QTimer::timeout, this, &PerformanceMonitor::tick);
}

PerformanceMonitor::~PerformanceMonitor()
{
	shutdown();
}

void PerformanceMonitor::start()
{
	if (shutDown_ || timer_.isActive())
		return;
	cpuInfo_ = os_cpu_usage_info_start();
	applySettings();
	timer_.start();
}

void PerformanceMonitor::shutdown()
{
	if (shutDown_)
		return;
	shutDown_ = true;
	timer_.stop();
	sampler_.stop();
	if (cpuInfo_) {
		os_cpu_usage_info_destroy(cpuInfo_);
		cpuInfo_ = nullptr;
	}
}

void PerformanceMonitor::applySettings()
{
	if (shutDown_)
		return;
	// Potato Mode halves the measuring work and leaves the graphics counters alone.
	const bool potato = app_.config().performanceMode == PerformanceMode::Potato;
	const int intervalMs = potato ? 2000 : 1000;
	timer_.setInterval(intervalMs);
	app_.outputs().setSampleIntervalMs(intervalMs);
	sampler_.start(potato ? 4000 : 2000, !potato);
}

double PerformanceMonitor::dropPercent(const std::string &destinationId) const
{
	return window_.percent("drop:" + destinationId);
}

std::vector<std::string> PerformanceMonitor::namesFor(const std::vector<std::string> &ids) const
{
	std::vector<std::string> names;
	for (const std::string &id : ids) {
		const DestinationConfig *config = app_.config().findDestination(id);
		names.push_back(config ? config->name : id);
	}
	return names;
}

// ---- Measuring -----------------------------------------------------------------------------------

void PerformanceMonitor::tick()
{
	if (shutDown_)
		return;

	const int64_t now = app_.clock().nowMs();
	OutputManager &outputs = app_.outputs();

	// Counters OBS keeps anyway. Reading them costs nothing.
	window_.add("render", now, obs_get_total_frames(), obs_get_lagged_frames());
	if (video_t *main = obs_get_video())
		window_.add("encode:main", now, video_output_get_total_frames(main), video_output_get_skipped_frames(main));
	for (const VerticalLayout &layout : app_.vertical().layouts()) {
		const std::string key = "encode:" + layout.id;
		if (video_t *frames = app_.vertical().activeVideo(layout.id))
			window_.add(key, now, video_output_get_total_frames(frames), video_output_get_skipped_frames(frames));
		else
			window_.remove(key);
	}

	double worstDrop = -1.0;
	for (const DestinationConfig &destination : app_.config().destinations) {
		const std::string key = "drop:" + destination.id;
		if (outputs.runtime(destination.id).phase == DestinationPhase::Live) {
			const DestinationStats stats = outputs.stats(destination.id);
			window_.add(key, now, static_cast<uint64_t>(std::max(stats.totalFrames, 0)),
				    static_cast<uint64_t>(std::max(stats.droppedFrames, 0)));
			worstDrop = std::max(worstDrop, window_.percent(key));
		} else {
			window_.remove(key);
		}
	}

	const double systemCpu = sampler_.cpuPercent();
	if (systemCpu >= 0.0)
		window_.addValue("cpu", now, systemCpu);

	PerformanceSnapshot snapshot;
	snapshot.obsCpuPercent = cpuInfo_ ? os_cpu_usage_info_query(cpuInfo_) : -1.0;
	snapshot.systemCpuPercent = systemCpu;
	snapshot.gpuPercent = sampler_.gpuPercent();
	snapshot.memoryMb = static_cast<double>(os_get_proc_resident_size()) / (1024.0 * 1024.0);
	snapshot.renderLagPercent = window_.percent("render");
	snapshot.worstDropPercent = worstDrop;
	snapshot.activeFps = obs_get_active_fps();
	snapshot.frameTimeMs = static_cast<double>(obs_get_average_frame_time_ns()) / 1000000.0;
	snapshot.videoEncoders = static_cast<int>(outputs.encoderPool().liveVideoEncoders());
	snapshot.liveDestinations = outputs.liveCount();
	snapshot.totalBitrateKbps = outputs.totalBitrateKbps();

	// ---- Optimiser ---------------------------------------------------------------------------
	const bool active = outputs.anyActive();
	if (active) {
		OptimizerInput input = buildOptimizerInput(now);
		for (const OptimizerGroup &group : input.groups)
			snapshot.encodeLagPercent = std::max(snapshot.encodeLagPercent, group.encodeLagPercent);

		const OptimizerOutput output = optimizer_.update(input);
		for (const OptimizerChange &change : output.apply)
			applyChange(change, true);
		setSuggestions(output);
	} else if (wasActive_) {
		// Every stream ended. The next session starts at full quality with a clean slate.
		optimizer_.reset();
		app_.clearAdjustments();
		setSuggestions({});
	}
	wasActive_ = active;

	snapshot_ = snapshot;
	Q_EMIT snapshotUpdated();
}

OptimizerInput PerformanceMonitor::buildOptimizerInput(int64_t nowMs)
{
	OptimizerInput input;
	input.nowMs = nowMs;
	input.config = app_.config().optimizer;
	input.renderLagPercent = window_.percent("render");
	input.cpuPercent = window_.average("cpu");

	const StreamContext context = app_.streamContext();
	OutputManager &outputs = app_.outputs();

	// One group per running encoder. The encoder's OBS name tells which destinations share it.
	std::map<std::string, size_t> indexByEncoder;
	for (const DestinationConfig &destination : app_.config().destinations) {
		if (outputs.runtime(destination.id).phase != DestinationPhase::Live)
			continue;
		const DestinationSessionInfo info = outputs.sessionInfo(destination.id);
		if (info.videoEncoderName.empty())
			continue;

		auto found = indexByEncoder.find(info.videoEncoderName);
		if (found == indexByEncoder.end()) {
			OptimizerGroup group;
			const EffectiveVideo &video = info.effective.video;
			group.key = videoSignature(video);
			if (const VideoEncoderCaps *caps = context.findVideoEncoder(video.encoderId)) {
				group.hardwareEncoder = caps->hardware;
				group.dynamicBitrate = caps->dynamicBitrate;
			}
			group.fps = static_cast<int>(std::lround(video.fps));
			group.lines = std::min(video.width, video.height);
			group.adjustment = app_.adjustmentFor(destination.id);
			group.encodeLagPercent = window_.percent(encodeKey(video));
			input.groups.push_back(std::move(group));
			found = indexByEncoder.emplace(info.videoEncoderName, input.groups.size() - 1).first;
		}

		OptimizerGroup &group = input.groups[found->second];
		group.destinationIds.push_back(destination.id);
		// One member that locked a setting, or opted out, keeps the whole group from changing
		// it, because a shared encoder cannot serve two different settings.
		const bool automatic = destination.autoOptimize;
		group.bitrateAdjustable = group.bitrateAdjustable && automatic && !destination.locks.bitrate;
		group.fpsAdjustable = group.fpsAdjustable && automatic && !destination.locks.fps;
		group.resolutionAdjustable = group.resolutionAdjustable && automatic && !destination.locks.resolution;
		group.dropPercent = std::max(group.dropPercent, window_.percent("drop:" + destination.id));
	}
	return input;
}

// ---- Acting --------------------------------------------------------------------------------------

void PerformanceMonitor::applyChange(const OptimizerChange &change, bool automatic)
{
	for (const std::string &id : change.destinationIds)
		app_.setAdjustment(id, change.to);

	app_.outputs().applyEffectiveChanges(change.destinationIds, true);
	optimizer_.notifyApplied(change, app_.clock().nowMs());

	const std::string text = describeAppliedChange(change, namesFor(change.destinationIds));
	logInfo("Optimiser, {}: {} Cause: {}.", automatic ? "automatic" : "suggestion accepted", text,
		optimizerCauseName(change.cause));

	history_.push_back({utcTimestampIso8601(), text, automatic});
	if (history_.size() > kHistoryLimit)
		history_.erase(history_.begin(), history_.begin() + static_cast<long long>(history_.size() - kHistoryLimit));
}

void PerformanceMonitor::setSuggestions(const OptimizerOutput &output)
{
	std::vector<Suggestion> suggestions;
	for (const OptimizerChange &change : output.suggestions) {
		Suggestion suggestion;
		suggestion.change = change;
		suggestion.names = namesFor(change.destinationIds);
		suggestion.text = describeChange(change, suggestion.names);
		suggestions.push_back(std::move(suggestion));
	}
	std::vector<std::string> notices;
	for (const OptimizerNotice &notice : output.notices) {
		std::string text = describeNotice(notice, namesFor(notice.destinationIds));
		if (!text.empty())
			notices.push_back(std::move(text));
	}

	bool changed = notices != notices_ || suggestions.size() != suggestions_.size();
	for (size_t i = 0; !changed && i < suggestions.size(); ++i)
		changed = suggestions[i].change.id != suggestions_[i].change.id;

	suggestions_ = std::move(suggestions);
	notices_ = std::move(notices);
	if (changed)
		Q_EMIT suggestionsChanged();
}

bool PerformanceMonitor::applySuggestion(const std::string &changeId)
{
	const auto found = std::find_if(suggestions_.begin(), suggestions_.end(),
					[&](const Suggestion &suggestion) { return suggestion.change.id == changeId; });
	if (found == suggestions_.end())
		return false;

	const OptimizerChange change = found->change;
	suggestions_.erase(found);
	applyChange(change, false);
	Q_EMIT suggestionsChanged();
	return true;
}

void PerformanceMonitor::ignoreSuggestion(const std::string &changeId)
{
	optimizer_.notifyIgnored(changeId, app_.clock().nowMs());
	const size_t removed =
		std::erase_if(suggestions_, [&](const Suggestion &suggestion) { return suggestion.change.id == changeId; });
	if (removed > 0)
		Q_EMIT suggestionsChanged();
}

bool PerformanceMonitor::lockSuggestion(const std::string &changeId)
{
	const auto found = std::find_if(suggestions_.begin(), suggestions_.end(),
					[&](const Suggestion &suggestion) { return suggestion.change.id == changeId; });
	if (found == suggestions_.end())
		return false;

	const OptimizerChangeKind kind = found->change.kind;
	const std::vector<std::string> ids = found->change.destinationIds;
	const LockableSetting setting = lockForChange(kind);
	for (const std::string &id : ids) {
		if (DestinationConfig *config = app_.config().findDestination(id))
			config->locks.setLocked(setting, true);

		// A locked setting uses the saved value, so an earlier reduction of it ends here.
		Adjustment adjustment = app_.adjustmentFor(id);
		switch (kind) {
		case OptimizerChangeKind::Bitrate:
			adjustment.bitratePercent = 100;
			break;
		case OptimizerChangeKind::Fps:
			adjustment.maxFps = 0;
			break;
		case OptimizerChangeKind::Resolution:
			adjustment.maxLines = 0;
			break;
		}
		app_.setAdjustment(id, adjustment);
	}
	logInfo("{} locked for {} on request. The optimiser leaves it alone.", lockableSettingName(setting),
		joinNames(found->names));

	ignoreSuggestion(changeId);
	app_.notifyConfigChanged();
	// A bitrate goes back to the saved value at once. A frame rate or resolution would need a
	// reconnect, so it returns at the destination's next start.
	app_.outputs().applyEffectiveChanges(ids, false);
	return true;
}

#ifdef RELAYDOCK_TEST_HOOKS
void PerformanceMonitor::setTuningForTest(const OptimizerTuning &tuning)
{
	optimizer_ = Optimizer(tuning);
}
#endif

} // namespace rd
