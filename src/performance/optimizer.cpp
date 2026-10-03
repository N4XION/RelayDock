// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "performance/optimizer.h"

#include <algorithm>
#include <iterator>

namespace rd {

namespace {

constexpr int kBitrateSteps[] = {100, 85, 70, 55, 40};
constexpr const char *kRenderKey = "render";
constexpr const char *kCpuKey = "cpu";

std::string encodeKey(const std::string &groupKey)
{
	return "encode:" + groupKey;
}

std::string dropKey(const std::string &groupKey)
{
	return "drop:" + groupKey;
}

} // namespace

const char *optimizerCauseName(OptimizerCause cause)
{
	switch (cause) {
	case OptimizerCause::NetworkDrops:
		return "network_drops";
	case OptimizerCause::EncoderOverload:
		return "encoder_overload";
	case OptimizerCause::RenderingLag:
		return "rendering_lag";
	case OptimizerCause::HighCpu:
		return "high_cpu";
	case OptimizerCause::Recovery:
		return "recovery";
	}
	return "encoder_overload";
}

const char *optimizerChangeKindName(OptimizerChangeKind kind)
{
	switch (kind) {
	case OptimizerChangeKind::Bitrate:
		return "bitrate";
	case OptimizerChangeKind::Fps:
		return "fps";
	case OptimizerChangeKind::Resolution:
		return "resolution";
	}
	return "bitrate";
}

LockableSetting lockForChange(OptimizerChangeKind kind)
{
	switch (kind) {
	case OptimizerChangeKind::Bitrate:
		return LockableSetting::Bitrate;
	case OptimizerChangeKind::Fps:
		return LockableSetting::Fps;
	case OptimizerChangeKind::Resolution:
		return LockableSetting::Resolution;
	}
	return LockableSetting::Bitrate;
}

int Optimizer::nextLowerBitratePercent(int current)
{
	for (int step : kBitrateSteps) {
		if (step < current)
			return step;
	}
	return current;
}

int Optimizer::nextHigherBitratePercent(int current)
{
	int result = 100;
	for (int step : kBitrateSteps) {
		if (step > current)
			result = step;
	}
	return result;
}

Optimizer::Optimizer(OptimizerTuning tuning) : tuning_(tuning) {}

void Optimizer::Tracker::update(int64_t nowMs, double value, double trigger, double clear)
{
	seen = true;
	if (value < 0.0)
		return; // Not known yet. Keep the current state.

	if (!above) {
		if (value >= trigger) {
			above = true;
			aboveSinceMs = nowMs;
			lastAboveMs = nowMs;
		}
	} else if (value < clear) {
		above = false;
	} else {
		lastAboveMs = nowMs;
	}
}

void Optimizer::reset()
{
	trackers_.clear();
	memory_.clear();
	pending_.clear();
	snoozes_.clear();
	cooldownUntilMs_ = 0;
	lastTroubleMs_ = -1;
	firstUpdateMs_ = -1;
}

std::string Optimizer::memoryKey(const OptimizerGroup &group) const
{
	// The encoder signature changes whenever a change is applied. The set of destinations
	// does not, so memory that must survive a change is keyed by it.
	std::vector<std::string> ids = group.destinationIds;
	std::sort(ids.begin(), ids.end());
	std::string key;
	for (const std::string &id : ids)
		key += id + ";";
	return key;
}

bool Optimizer::snoozed(const std::string &groupKey, OptimizerChangeKind kind, bool recovery, int64_t nowMs) const
{
	return std::any_of(snoozes_.begin(), snoozes_.end(), [&](const Snooze &snooze) {
		return snooze.groupKey == groupKey && snooze.kind == kind && snooze.recovery == recovery &&
		       nowMs < snooze.untilMs;
	});
}

bool Optimizer::hasPending(const std::string &groupKey) const
{
	return std::any_of(pending_.begin(), pending_.end(),
			   [&](const OptimizerChange &change) { return change.groupKey == groupKey; });
}

Optimizer::Build Optimizer::buildReduction(const OptimizerGroup &group, OptimizerCause cause, int64_t nowMs,
					   OptimizerChange &out) const
{
	out = {};
	out.cause = cause;
	out.groupKey = group.key;
	out.destinationIds = group.destinationIds;
	out.from = group.adjustment;
	out.to = group.adjustment;
	out.createdMs = nowMs;

	if (cause == OptimizerCause::NetworkDrops) {
		// A lower frame rate or size at the same bitrate sends just as many bytes. Only the
		// bitrate helps a connection that cannot keep up.
		if (!group.bitrateAdjustable)
			return Build::Nothing;
		const int next = nextLowerBitratePercent(group.adjustment.bitratePercent);
		if (next == group.adjustment.bitratePercent)
			return Build::Nothing;
		if (snoozed(group.key, OptimizerChangeKind::Bitrate, false, nowMs))
			return Build::Snoozed;
		out.kind = OptimizerChangeKind::Bitrate;
		out.to.bitratePercent = next;
		out.needsReconnect = !group.dynamicBitrate;
		return Build::Built;
	}

	// Too much work for the encoder or the graphics chip. Halving the frame rate halves the
	// work and costs the least to look at. A smaller picture comes second.
	bool anySnoozed = false;

	if (group.fpsAdjustable && group.fps > 31) {
		if (snoozed(group.key, OptimizerChangeKind::Fps, false, nowMs)) {
			anySnoozed = true;
		} else {
			out.kind = OptimizerChangeKind::Fps;
			out.to.maxFps = 30;
			out.needsReconnect = true;
			return Build::Built;
		}
	}

	if (group.resolutionAdjustable) {
		int target = 0;
		if (group.lines > 720)
			target = 720;
		else if (group.lines > 540)
			target = 540;
		if (target > 0) {
			if (snoozed(group.key, OptimizerChangeKind::Resolution, false, nowMs)) {
				anySnoozed = true;
			} else {
				out.kind = OptimizerChangeKind::Resolution;
				out.to.maxLines = target;
				out.needsReconnect = true;
				return Build::Built;
			}
		}
	}

	return anySnoozed ? Build::Snoozed : Build::Nothing;
}

bool Optimizer::buildRecovery(const OptimizerGroup &group, int64_t nowMs, OptimizerChange &out) const
{
	out = {};
	out.cause = OptimizerCause::Recovery;
	out.groupKey = group.key;
	out.destinationIds = group.destinationIds;
	out.from = group.adjustment;
	out.to = group.adjustment;
	out.createdMs = nowMs;

	// Undo in the reverse order of the reductions: bitrate first, then size, then frame rate.
	const Adjustment &current = group.adjustment;
	if (current.bitratePercent < 100 && group.bitrateAdjustable &&
	    !snoozed(group.key, OptimizerChangeKind::Bitrate, true, nowMs)) {
		out.kind = OptimizerChangeKind::Bitrate;
		out.to.bitratePercent = nextHigherBitratePercent(current.bitratePercent);
		out.needsReconnect = !group.dynamicBitrate;
		return true;
	}
	if (current.maxLines > 0 && group.resolutionAdjustable &&
	    !snoozed(group.key, OptimizerChangeKind::Resolution, true, nowMs)) {
		out.kind = OptimizerChangeKind::Resolution;
		out.to.maxLines = current.maxLines <= 540 ? 720 : 0;
		out.needsReconnect = true;
		return true;
	}
	if (current.maxFps > 0 && group.fpsAdjustable && !snoozed(group.key, OptimizerChangeKind::Fps, true, nowMs)) {
		out.kind = OptimizerChangeKind::Fps;
		out.to.maxFps = 0;
		out.needsReconnect = true;
		return true;
	}
	return false;
}

OptimizerOutput Optimizer::update(const OptimizerInput &input)
{
	OptimizerOutput output;
	const int64_t now = input.nowMs;

	if (input.config.mode == OptimizationMode::Off) {
		trackers_.clear();
		pending_.clear();
		return output;
	}
	if (firstUpdateMs_ < 0)
		firstUpdateMs_ = now;

	const bool strict = input.config.preventGameLag;
	const int64_t sustainMs = strict ? tuning_.sustainMsStrict : tuning_.sustainMs;
	const int64_t baseRecoverMs = strict ? tuning_.recoverAfterMsStrict : tuning_.recoverAfterMs;
	const double renderTrigger = strict ? tuning_.renderLagTriggerStrict : tuning_.renderLagTrigger;
	const double encodeTrigger = strict ? tuning_.encodeLagTriggerStrict : tuning_.encodeLagTrigger;

	// ---- 1. Measurements against thresholds -------------------------------------------------
	trackers_[kRenderKey].update(now, input.renderLagPercent, renderTrigger, renderTrigger * tuning_.clearRatio);
	if (strict) {
		// CPU load swings less than lag percentages, so it clears ten points below the trigger.
		trackers_[kCpuKey].update(now, input.cpuPercent, tuning_.cpuTriggerStrict, tuning_.cpuTriggerStrict - 10.0);
	} else {
		trackers_.erase(kCpuKey);
	}

	for (const OptimizerGroup &group : input.groups) {
		trackers_[encodeKey(group.key)].update(now, group.encodeLagPercent, encodeTrigger,
						       encodeTrigger * tuning_.clearRatio);
		trackers_[dropKey(group.key)].update(now, group.dropPercent, tuning_.dropTrigger,
						     tuning_.dropTrigger * tuning_.clearRatio);
	}

	// Forget trackers of encoders that no longer run.
	for (auto it = trackers_.begin(); it != trackers_.end();) {
		const std::string &key = it->first;
		bool keep = key == kRenderKey || key == kCpuKey;
		if (!keep) {
			keep = std::any_of(input.groups.begin(), input.groups.end(), [&](const OptimizerGroup &group) {
				return key == encodeKey(group.key) || key == dropKey(group.key);
			});
		}
		it = keep ? std::next(it) : trackers_.erase(it);
	}

	bool anyAbove = false;
	for (const auto &entry : trackers_)
		anyAbove = anyAbove || entry.second.above;
	if (anyAbove)
		lastTroubleMs_ = now;

	auto findTracker = [this](const std::string &key) -> const Tracker * {
		const auto it = trackers_.find(key);
		return it == trackers_.end() ? nullptr : &it->second;
	};
	auto sustained = [&](const std::string &key) {
		const Tracker *tracker = findTracker(key);
		return tracker && tracker->sustained(now, sustainMs);
	};
	auto clearFor = [&](const std::string &key, int64_t durationMs) {
		const Tracker *tracker = findTracker(key);
		return !tracker || (!tracker->above && now - tracker->lastAboveMs >= durationMs);
	};

	// ---- 2. Withdraw suggestions that no longer apply ---------------------------------------
	std::erase_if(snoozes_, [now](const Snooze &snooze) { return now >= snooze.untilMs; });
	std::erase_if(pending_, [&](const OptimizerChange &change) {
		const auto group = std::find_if(input.groups.begin(), input.groups.end(),
						[&](const OptimizerGroup &g) { return g.key == change.groupKey; });
		if (group == input.groups.end())
			return true; // The encoder stopped or its settings changed.

		const bool adjustable = change.kind == OptimizerChangeKind::Bitrate
						? group->bitrateAdjustable
						: change.kind == OptimizerChangeKind::Fps ? group->fpsAdjustable
											  : group->resolutionAdjustable;
		if (!adjustable)
			return true; // The user locked the setting.

		if (change.cause == OptimizerCause::Recovery)
			return anyAbove; // Trouble came back before the user answered.

		// The problem went away by itself and stayed away.
		const std::string key = change.cause == OptimizerCause::NetworkDrops     ? dropKey(change.groupKey)
					: change.cause == OptimizerCause::EncoderOverload ? encodeKey(change.groupKey)
					: change.cause == OptimizerCause::HighCpu         ? std::string(kCpuKey)
											  : std::string(kRenderKey);
		return clearFor(key, tuning_.withdrawAfterClearMs);
	});

	// ---- 3. Reductions ------------------------------------------------------------------------
	std::vector<OptimizerChange> proposed;
	bool renderingHandled = false;

	if (now >= cooldownUntilMs_) {
		for (const OptimizerGroup &group : input.groups) {
			if (hasPending(group.key))
				continue;

			OptimizerCause cause;
			if (sustained(encodeKey(group.key))) {
				cause = OptimizerCause::EncoderOverload;
			} else if (sustained(kRenderKey) && group.hardwareEncoder) {
				cause = OptimizerCause::RenderingLag;
			} else if (sustained(dropKey(group.key))) {
				cause = OptimizerCause::NetworkDrops;
			} else if (strict && sustained(kCpuKey) && !group.hardwareEncoder) {
				cause = OptimizerCause::HighCpu;
			} else {
				continue;
			}

			GroupMemory &memory = memory_[memoryKey(group)];
			OptimizerChange change;
			const Build built = buildReduction(group, cause, now, change);
			if (built == Build::Built) {
				change.id = "change-" + std::to_string(nextId_++);
				proposed.push_back(std::move(change));
				memory.noticeSent = false;
				if (cause == OptimizerCause::RenderingLag)
					renderingHandled = true;
			} else if (built == Build::Nothing && !memory.noticeSent) {
				// Nothing left that RelayDock may lower for this group. Say so once.
				output.notices.push_back({cause, group.destinationIds});
				memory.noticeSent = true;
			}
		}

		// Rendering lag that no hardware encoder of ours can relieve is OBS or the game
		// asking too much of the graphics chip. RelayDock can only point at it.
		const Tracker *render = findTracker(kRenderKey);
		if (render && render->sustained(now, sustainMs) && !renderingHandled) {
			GroupMemory &memory = memory_["<system>"];
			const bool anyHardware = std::any_of(input.groups.begin(), input.groups.end(),
							     [](const OptimizerGroup &g) { return g.hardwareEncoder; });
			if (!memory.noticeSent && (!anyHardware || proposed.empty())) {
				output.notices.push_back({OptimizerCause::RenderingLag, {}});
				memory.noticeSent = true;
			}
		} else if (render && !render->above) {
			memory_["<system>"].noticeSent = false;
		}
	}

	// ---- 4. Recovery --------------------------------------------------------------------------
	if (proposed.empty() && !anyAbove && now >= cooldownUntilMs_) {
		const int64_t quietSince = lastTroubleMs_ >= 0 ? lastTroubleMs_ : firstUpdateMs_;
		for (const OptimizerGroup &group : input.groups) {
			if (group.adjustment.none() || hasPending(group.key))
				continue;

			GroupMemory &memory = memory_[memoryKey(group)];
			if (memory.recoverAfterMs < baseRecoverMs)
				memory.recoverAfterMs = baseRecoverMs;

			const int64_t lastChange = std::max(memory.lastStepDownMs, memory.lastStepUpMs);
			const bool quietLongEnough = now - quietSince >= memory.recoverAfterMs;
			const bool settled = lastChange < 0 || now - lastChange >= memory.recoverAfterMs;
			if (!quietLongEnough || !settled)
				continue;

			OptimizerChange change;
			if (buildRecovery(group, now, change)) {
				change.id = "change-" + std::to_string(nextId_++);
				proposed.push_back(std::move(change));
				break; // One step up at a time, across all groups.
			}
		}
	}

	// ---- 5. Apply now or ask ------------------------------------------------------------------
	for (OptimizerChange &change : proposed) {
		const bool automatic = input.config.mode == OptimizationMode::Automatic &&
				       (!change.needsReconnect || input.config.allowReconnectingChanges);
		if (automatic)
			output.apply.push_back(change);
		else
			pending_.push_back(change);
	}
	if (!proposed.empty())
		cooldownUntilMs_ = now + tuning_.cooldownMs;

	output.suggestions = pending_;
	return output;
}

void Optimizer::notifyApplied(const OptimizerChange &change, int64_t nowMs)
{
	std::erase_if(pending_, [&](const OptimizerChange &candidate) { return candidate.id == change.id; });

	OptimizerGroup group;
	group.destinationIds = change.destinationIds;
	GroupMemory &memory = memory_[memoryKey(group)];

	if (change.cause == OptimizerCause::Recovery) {
		memory.lastStepUpMs = nowMs;
	} else {
		// A reduction soon after a step up means the step up was too early. Wait twice as
		// long before trying again.
		if (memory.lastStepUpMs >= 0 && nowMs - memory.lastStepUpMs <= tuning_.probationMs) {
			const int64_t base = std::max(memory.recoverAfterMs, tuning_.recoverAfterMs);
			memory.recoverAfterMs = std::min(base * 2, tuning_.maxRecoverAfterMs);
		}
		memory.lastStepDownMs = nowMs;
	}

	cooldownUntilMs_ = nowMs + tuning_.cooldownMs;
}

void Optimizer::notifyIgnored(const std::string &changeId, int64_t nowMs)
{
	const auto it = std::find_if(pending_.begin(), pending_.end(),
				     [&](const OptimizerChange &change) { return change.id == changeId; });
	if (it == pending_.end())
		return;
	snoozes_.push_back({it->groupKey, it->kind, it->cause == OptimizerCause::Recovery, nowMs + tuning_.ignoreSnoozeMs});
	pending_.erase(it);
}

} // namespace rd
