// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "core/destination_state.h"
#include "encoders/encode_plan.h"
#include "encoders/encoder_pool.h"
#include "outputs/video_guard.h"
#include "performance/effective_settings.h"

#include <QObject>
#include <QTimer>

#include <obs.hpp>

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace rd {

class AppContext;

// Numbers for one destination, sampled once per interval while it is live.
struct DestinationStats {
	uint64_t totalBytes = 0;
	int bitrateKbps = 0;            // Sent during the last interval
	int totalFrames = 0;
	int droppedFrames = 0;          // Dropped by the network since the stream started
	double droppedPercent = 0.0;    // Of all frames since the stream started
	double recentDroppedPercent = 0.0; // Of the frames in the last interval
	double congestion = 0.0;        // 0 is clear, 1 is fully congested
	int connectTimeMs = 0;
	int64_t liveSeconds = 0;
};

// What a destination is streaming with right now.
struct DestinationSessionInfo {
	bool hasOutput = false;
	EffectiveDestination effective;
	std::string videoEncoderName; // OBS object name. Equal names mean a shared encoder.
	std::string audioEncoderName;
	std::vector<std::string> sharedWith; // Ids of destinations on the same video encoder
};

// Starts, stops and watches the RTMP outputs, one per destination.
//
// Each destination gets its own OBS output and service, so one destination failing or
// reconnecting never touches another. Video and audio encoders come from the EncoderPool and
// are shared between destinations whose settings match exactly.
//
// Thread ownership
//   - Every method and every member belongs to the OBS UI thread.
//   - OBS raises output signals on its own threads. The static signal handlers copy what they
//     need and post it to the UI thread. They touch nothing else.
//   - Stopping an output can block inside OBS while a connection attempt is in flight, so
//     stops run on short-lived worker threads. shutdown() joins them all.
class OutputManager : public QObject {
	Q_OBJECT

public:
	explicit OutputManager(AppContext &app, QObject *parent = nullptr);
	~OutputManager() override;

	// ---- Commands -------------------------------------------------------------------------
	// Returns false when the destination was not idle. A start that fails later reports
	// through the destination's runtime state.
	bool start(const std::string &id, bool privateTest = false);
	// First call stops gracefully. A second call while stopping forces the output closed.
	void stop(const std::string &id);
	void reconnect(const std::string &id);
	int startAllEnabled();
	void stopAll();

	// What applyEffectiveChanges did with each destination it was given.
	struct ApplyResult {
		std::vector<std::string> retuned;      // Bitrate changed on the running encoder
		std::vector<std::string> reconnecting; // Restarting to pick up the new settings
		std::vector<std::string> pending;      // Keeps its old settings until its next start
	};

	// Brings live destinations in line with their current effective settings, after the
	// optimiser or the user changed something. A change of bitrate alone is applied to the
	// running encoder when the encoder supports it. Anything else needs a new encoder: with
	// `allowReconnect` the destination reconnects, otherwise it keeps its old settings until
	// its next start.
	ApplyResult applyEffectiveChanges(const std::vector<std::string> &ids, bool allowReconnect);

	// Forgets a destination that is being deleted. Stops it first when it is active.
	void forget(const std::string &id);

	// Called when OBS exits. Stops every output, waits for them, and releases every OBS object.
	void shutdown();

	// ---- State ----------------------------------------------------------------------------
	DestinationRuntime runtime(const std::string &id) const;
	DestinationStats stats(const std::string &id) const;
	DestinationSessionInfo sessionInfo(const std::string &id) const;
	bool anyActive() const;
	// Destinations that are starting, live, reconnecting or stopping.
	int activeCount() const;
	int liveCount() const;
	// Sum of the measured bitrates of every live destination.
	int totalBitrateKbps() const;
	EncoderPool &encoderPool() { return pool_; }

	// How often stats are sampled. Potato mode lengthens it.
	void setSampleIntervalMs(int intervalMs);

Q_SIGNALS:
	// A destination's phase, error or session changed.
	void destinationChanged(const QString &id);
	// New stats are available for the live destinations.
	void statsUpdated();
	// The first destination became active, or the last one stopped being active.
	void activeChanged(bool anyActive);

private:
	struct Session;

	Session &session(const std::string &id);
	Session *findSession(const std::string &id);
	const Session *findSession(const std::string &id) const;

	bool failStart(Session &session, UserMessage message);
	void releaseObsObjects(Session &session);
	void stopOutputAsync(Session &session, bool force);
	// Joins workers that have finished. With waitForAll it waits for every worker.
	void joinFinishedStopThreads(bool waitForAll);
	// Waits for the workers of one destination. Its output has reported "stop", so they are
	// about to return.
	void joinStopThreadsFor(const std::string &id);
	void sample();
	// Call after anything that can change whether a destination is active.
	void updateActivity();
	void setActivity(bool active);

	// Run on the UI thread, posted by the signal handlers.
	void handleStarted(const std::string &id, uint64_t generation);
	void handleStopped(const std::string &id, uint64_t generation, int code, const std::string &obsError);
	void handleReconnecting(const std::string &id, uint64_t generation, int retryInSec);
	void handleReconnected(const std::string &id, uint64_t generation);

	static void onOutputStart(void *data, calldata_t *params);
	static void onOutputStop(void *data, calldata_t *params);
	static void onOutputReconnect(void *data, calldata_t *params);
	static void onOutputReconnectSuccess(void *data, calldata_t *params);

	struct StopThread {
		std::string id;
		std::thread thread;
		std::shared_ptr<std::atomic<bool>> done;
	};

	AppContext &app_;
	EncoderPool pool_;
	std::map<std::string, std::unique_ptr<Session>> sessions_;
	std::vector<StopThread> stopThreads_;
	QTimer sampleTimer_;
	uint64_t nextGeneration_ = 1;
	bool shutDown_ = false;

	// On from the first destination that starts until the last one has let go of its
	// encoders. See VideoGuard for why.
	VideoGuard videoGuard_;
	bool activityOn_ = false;
	bool deactivationQueued_ = false;
	bool guardFailureLogged_ = false;
};

} // namespace rd
