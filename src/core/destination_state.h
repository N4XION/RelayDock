// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "core/output_stop.h"
#include "core/user_message.h"
#include "utils/clock.h"

#include <cstdint>

namespace rd {

// Where a destination is in its life cycle.
//
//   Idle          Not streaming. Start is possible.
//   Starting      Start was requested. The output is connecting.
//   Live          Connected and sending.
//   Reconnecting  The connection dropped. OBS is waiting to retry or retrying.
//   Stopping      Stop was requested. The output is shutting down.
//   Failed        The last attempt ended with an error. Start is possible again.
enum class DestinationPhase { Idle, Starting, Live, Reconnecting, Stopping, Failed };

const char *destinationPhaseName(DestinationPhase phase);

struct DestinationRuntime {
	DestinationPhase phase = DestinationPhase::Idle;
	UserMessage error;          // Why the destination failed. Empty unless phase is Failed.
	StopReason lastStop = StopReason::UserStopped;
	bool privateTest = false;   // This run is a test stream that viewers do not see
	bool restartPending = false; // A manual reconnect is waiting for the stop to finish
	int reconnectAttempt = 0;   // 1 for the first retry. 0 when not reconnecting.
	int reconnectInSec = 0;     // Seconds until the next retry, as reported by OBS
	int reconnectsThisRun = 0;  // Successful reconnects since the stream started
	int64_t liveSinceMs = -1;   // Monotonic time the stream went live. -1 when not live.

	bool active() const
	{
		return phase == DestinationPhase::Starting || phase == DestinationPhase::Live ||
		       phase == DestinationPhase::Reconnecting || phase == DestinationPhase::Stopping;
	}
};

// Tracks one destination's state from the commands the user gives and the signals the OBS
// output sends. OBS raises its signals on its own threads, and they can arrive in an order
// that looks wrong (a "started" after Stop was pressed, for example). The machine ignores an
// event that does not fit the current phase instead of trusting it, and each method returns
// whether it changed anything.
//
// Thread ownership: one machine belongs to one thread. The output manager forwards OBS
// signals to the OBS UI thread before calling in.
class DestinationStateMachine {
public:
	explicit DestinationStateMachine(const IClock &clock) : clock_(clock) {}

	const DestinationRuntime &runtime() const { return runtime_; }

	bool canStart() const;
	bool canStop() const;
	bool canReconnect() const;

	// ---- Commands from the user --------------------------------------------------------
	bool requestStart(bool privateTest);
	bool requestStop();
	// Stop now and start again when the stop finishes. Only while Live or Reconnecting.
	bool requestReconnect();
	// The user pressed Stop while a manual reconnect was stopping the output. Drop the restart.
	bool cancelRestart();

	// The start could not even begin (bad settings, missing key, no encoder).
	bool startFailed(UserMessage error);

	// ---- Signals from the OBS output ----------------------------------------------------
	bool outputStarted();
	bool outputReconnecting(int retryInSec);
	bool outputReconnected();

	struct StopOutcome {
		bool changed = false;
		bool restart = false; // The caller starts the destination again (manual reconnect)
	};
	StopOutcome outputStopped(StopReason reason, UserMessage error);

	// Seconds since the stream went live. 0 when not live.
	int64_t liveSeconds() const;

	// Clears a Failed state without starting, for example after the user edits the settings.
	bool clearFailure();

private:
	const IClock &clock_;
	DestinationRuntime runtime_;
};

} // namespace rd
