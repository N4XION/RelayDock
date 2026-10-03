// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "performance/effective_settings.h"
#include "settings/app_config.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace rd {

// Automatic optimisation. A set of fixed rules, not a model: it watches a few reliable
// measurements and, when one stays bad for a while, proposes the smallest change that
// relieves it.
//
// What it watches
//   Encoding lag    Frames an encoder skipped because it could not keep up.
//   Rendering lag   Frames OBS could not render in time. Usually the graphics chip is busy.
//   Network drops   Frames a destination dropped because the connection could not carry them.
//   CPU load        Only with Prevent Game Lag on, as an early warning.
//
// How it avoids overreacting
//   Windowed values  Every measurement is a percentage over the last ten seconds, so a single
//                    bad second barely moves it.
//   Sustained        A measurement must stay above its threshold for 20 seconds without a break
//                    (12 with Prevent Game Lag) before anything happens.
//   Hysteresis       Once above the threshold, it has to fall to half the threshold to count
//                    as fine again.
//   Cooldown         After a change, nothing else is proposed for 60 seconds.
//   Slow recovery    Settings go back up one step at a time, and only after five minutes with
//                    no problem (ten with Prevent Game Lag).
//   No flapping      If a step back up brings the problem back, the next attempt waits twice
//                    as long, up to 30 minutes.
//
// What it may change
//   Network drops    Bitrate, in steps: 100, 85, 70, 55, 40 percent. Applied while live when
//                    the encoder supports it, with no reconnect.
//   Encoder or rendering overload
//                    Frame rate down to 30 first, then resolution to 720p, then 540p. These
//                    restart the destination's connection.
//   It never changes a setting the user locked, and it leaves a destination alone when its
//   Automatic optimisation switch is off. Destinations that share an encoder change together.
//
// Modes
//   Off        Nothing is proposed.
//   Suggest    Changes appear as suggestions with Apply, Ignore and Lock Setting.
//   Automatic  Changes that need no reconnect are applied. Changes that need one are applied
//              only when "allow changes that reconnect" is on. Otherwise they are suggestions.
//
// Thread ownership: one thread. The plugin calls it from the OBS UI thread.

struct OptimizerTuning {
	int64_t sustainMs = 20000;
	int64_t sustainMsStrict = 12000;
	int64_t cooldownMs = 60000;
	int64_t recoverAfterMs = 300000;
	int64_t recoverAfterMsStrict = 600000;
	int64_t probationMs = 180000;
	int64_t maxRecoverAfterMs = 1800000;
	int64_t ignoreSnoozeMs = 600000;
	int64_t withdrawAfterClearMs = 60000;

	double renderLagTrigger = 3.0;
	double renderLagTriggerStrict = 1.5;
	double encodeLagTrigger = 3.0;
	double encodeLagTriggerStrict = 1.5;
	double dropTrigger = 3.0;
	double cpuTriggerStrict = 88.0;
	double clearRatio = 0.5;
};

enum class OptimizerCause { NetworkDrops, EncoderOverload, RenderingLag, HighCpu, Recovery };
enum class OptimizerChangeKind { Bitrate, Fps, Resolution };

const char *optimizerCauseName(OptimizerCause cause);
const char *optimizerChangeKindName(OptimizerChangeKind kind);
LockableSetting lockForChange(OptimizerChangeKind kind);

// One encoder and the destinations it serves, as it runs right now.
struct OptimizerGroup {
	std::string key;                         // Video signature of the running encoder
	std::vector<std::string> destinationIds;
	bool hardwareEncoder = false;
	bool dynamicBitrate = false;             // Bitrate can change without a restart
	int fps = 0;                             // Current frame rate, rounded
	int lines = 0;                           // Current shorter side in pixels
	Adjustment adjustment;                   // Reductions currently in force for the group

	// False when a member locked the setting or switched automatic optimisation off.
	bool bitrateAdjustable = true;
	bool fpsAdjustable = true;
	bool resolutionAdjustable = true;

	// Measurements over the window. -1 means not known yet.
	double encodeLagPercent = -1.0;
	double dropPercent = -1.0; // Worst member
};

struct OptimizerInput {
	int64_t nowMs = 0;
	OptimizerConfig config;
	double renderLagPercent = -1.0;
	double cpuPercent = -1.0;
	std::vector<OptimizerGroup> groups;
};

// A change the optimiser wants. The same adjustment goes to every destination in the list.
struct OptimizerChange {
	std::string id; // Stable while the change is pending
	OptimizerCause cause = OptimizerCause::EncoderOverload;
	OptimizerChangeKind kind = OptimizerChangeKind::Bitrate;
	std::string groupKey;
	std::vector<std::string> destinationIds;
	Adjustment from;
	Adjustment to;
	bool needsReconnect = false;
	int64_t createdMs = 0;
};

// Something the user should know that the optimiser cannot act on.
struct OptimizerNotice {
	OptimizerCause cause = OptimizerCause::EncoderOverload;
	std::vector<std::string> destinationIds; // Empty for a system-wide notice
};

struct OptimizerOutput {
	std::vector<OptimizerChange> apply;       // Apply now, then call notifyApplied
	std::vector<OptimizerChange> suggestions; // Everything waiting for the user
	std::vector<OptimizerNotice> notices;
};

class Optimizer {
public:
	explicit Optimizer(OptimizerTuning tuning = {});

	OptimizerOutput update(const OptimizerInput &input);

	// The change was applied, by the user or automatically.
	void notifyApplied(const OptimizerChange &change, int64_t nowMs);
	// The user dismissed the suggestion. The same kind of change is not proposed again for
	// this group for a while.
	void notifyIgnored(const std::string &changeId, int64_t nowMs);

	// Every stream stopped. Forgets all measurements and pending suggestions.
	void reset();

	const std::vector<OptimizerChange> &pending() const { return pending_; }
	const OptimizerTuning &tuning() const { return tuning_; }

	// The ladders, exposed for the interface and for tests.
	static int nextLowerBitratePercent(int current);
	static int nextHigherBitratePercent(int current);

private:
	// Tracks one measurement against its threshold over time.
	struct Tracker {
		bool above = false;       // Latched until the value falls below the clear level
		int64_t aboveSinceMs = 0;
		int64_t lastAboveMs = 0;
		bool seen = false;

		void update(int64_t nowMs, double value, double trigger, double clear);
		bool sustained(int64_t nowMs, int64_t sustainMs) const { return above && nowMs - aboveSinceMs >= sustainMs; }
	};

	struct GroupMemory {
		int64_t recoverAfterMs = 0;      // Current wait before a step up. Grows after flapping.
		int64_t lastStepUpMs = -1;
		int64_t lastStepDownMs = -1;
		bool noticeSent = false;
	};

	struct Snooze {
		std::string groupKey;
		OptimizerChangeKind kind;
		bool recovery;
		int64_t untilMs;
	};

	bool snoozed(const std::string &groupKey, OptimizerChangeKind kind, bool recovery, int64_t nowMs) const;
	bool hasPending(const std::string &groupKey) const;
	enum class Build {
		Built,   // A change was produced
		Snoozed, // A lever exists but the user dismissed it recently
		Nothing, // No setting is left that RelayDock may lower
	};
	Build buildReduction(const OptimizerGroup &group, OptimizerCause cause, int64_t nowMs, OptimizerChange &out) const;
	bool buildRecovery(const OptimizerGroup &group, int64_t nowMs, OptimizerChange &out) const;
	std::string memoryKey(const OptimizerGroup &group) const;

	OptimizerTuning tuning_;
	std::map<std::string, Tracker> trackers_;
	std::map<std::string, GroupMemory> memory_; // Keyed by the group's destination ids
	std::vector<OptimizerChange> pending_;
	std::vector<Snooze> snoozes_;
	int64_t cooldownUntilMs_ = 0;
	int64_t lastTroubleMs_ = -1;
	int64_t firstUpdateMs_ = -1;
	unsigned nextId_ = 1;
};

} // namespace rd
