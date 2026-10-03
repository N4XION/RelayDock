// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "performance/metrics_window.h"
#include "performance/optimizer.h"
#include "performance/optimizer_text.h"
#include "performance/system_sampler.h"

#include <QObject>
#include <QTimer>

#include <string>
#include <vector>

struct os_cpu_usage_info;

namespace rd {

class AppContext;

// What RelayDock measures, refreshed once per tick. -1 means not known yet.
struct PerformanceSnapshot {
	double obsCpuPercent = -1.0;    // OBS and everything in it, as a share of all cores
	double systemCpuPercent = -1.0; // The whole PC
	double gpuPercent = -1.0;       // Busiest graphics engine. -1 where Windows does not report it.
	double memoryMb = -1.0;         // Memory OBS uses
	double renderLagPercent = -1.0; // Frames OBS rendered late, over the last ten seconds
	double encodeLagPercent = -1.0; // Frames encoders skipped, worst canvas, last ten seconds
	double worstDropPercent = -1.0; // Frames dropped by the network, worst destination, last ten seconds
	double activeFps = 0.0;
	double frameTimeMs = 0.0;
	int videoEncoders = 0;
	int liveDestinations = 0;
	int totalBitrateKbps = 0;
};

struct Suggestion {
	OptimizerChange change;
	std::vector<std::string> names; // Display names of the destinations it affects
	ChangeText text;
};

struct AppliedChange {
	std::string timeUtc;
	std::string text;
	bool automatic = false;
};

// Measures performance while OBS runs and connects the measurements to the optimiser.
//
// Cost: one tick per second (every two seconds in Potato Mode) that reads a handful of
// counters OBS already keeps. Nothing here renders, allocates large buffers or blocks.
// Processor and graphics load of the whole PC come from SystemSampler on its own thread.
//
// Thread ownership: OBS UI thread.
class PerformanceMonitor : public QObject {
	Q_OBJECT

public:
	explicit PerformanceMonitor(AppContext &app, QObject *parent = nullptr);
	~PerformanceMonitor() override;

	void start();
	void shutdown();

	// Call when the performance mode or the optimiser settings change.
	void applySettings();

	const PerformanceSnapshot &snapshot() const { return snapshot_; }
	const std::vector<Suggestion> &suggestions() const { return suggestions_; }
	const std::vector<std::string> &notices() const { return notices_; }
	const std::vector<AppliedChange> &history() const { return history_; }

	// Frames a destination dropped over the last ten seconds, in percent. -1 when unknown.
	double dropPercent(const std::string &destinationId) const;

	// The user's answers to a suggestion.
	bool applySuggestion(const std::string &changeId);
	void ignoreSuggestion(const std::string &changeId);
	// Locks the setting on every destination the suggestion names, then dismisses it.
	bool lockSuggestion(const std::string &changeId);

	int tickIntervalMs() const { return timer_.interval(); }

#ifdef RELAYDOCK_TEST_HOOKS
	// Lets an integration test shorten the optimiser's timers. Test builds only.
	void setTuningForTest(const OptimizerTuning &tuning);
#endif

Q_SIGNALS:
	void snapshotUpdated();
	void suggestionsChanged();

private:
	void tick();
	OptimizerInput buildOptimizerInput(int64_t nowMs);
	void applyChange(const OptimizerChange &change, bool automatic);
	std::vector<std::string> namesFor(const std::vector<std::string> &ids) const;
	void setSuggestions(const OptimizerOutput &output);

	AppContext &app_;
	QTimer timer_;
	MetricsWindow window_;
	Optimizer optimizer_;
	SystemSampler sampler_;
	os_cpu_usage_info *cpuInfo_ = nullptr;

	PerformanceSnapshot snapshot_;
	std::vector<Suggestion> suggestions_;
	std::vector<std::string> notices_;
	std::vector<AppliedChange> history_;
	bool wasActive_ = false;
	bool shutDown_ = false;
};

} // namespace rd
