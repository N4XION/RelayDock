// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "core/destination_state.h"

namespace rd {

const char *destinationPhaseName(DestinationPhase phase)
{
	switch (phase) {
	case DestinationPhase::Idle:
		return "idle";
	case DestinationPhase::Starting:
		return "starting";
	case DestinationPhase::Live:
		return "live";
	case DestinationPhase::Reconnecting:
		return "reconnecting";
	case DestinationPhase::Stopping:
		return "stopping";
	case DestinationPhase::Failed:
		return "failed";
	}
	return "idle";
}

bool DestinationStateMachine::canStart() const
{
	return runtime_.phase == DestinationPhase::Idle || runtime_.phase == DestinationPhase::Failed;
}

bool DestinationStateMachine::canStop() const
{
	return runtime_.phase == DestinationPhase::Starting || runtime_.phase == DestinationPhase::Live ||
	       runtime_.phase == DestinationPhase::Reconnecting;
}

bool DestinationStateMachine::canReconnect() const
{
	return runtime_.phase == DestinationPhase::Live || runtime_.phase == DestinationPhase::Reconnecting;
}

bool DestinationStateMachine::requestStart(bool privateTest)
{
	if (!canStart())
		return false;
	runtime_ = DestinationRuntime{};
	runtime_.phase = DestinationPhase::Starting;
	runtime_.privateTest = privateTest;
	return true;
}

bool DestinationStateMachine::requestStop()
{
	if (!canStop())
		return false;
	runtime_.phase = DestinationPhase::Stopping;
	runtime_.restartPending = false;
	runtime_.reconnectAttempt = 0;
	runtime_.reconnectInSec = 0;
	return true;
}

bool DestinationStateMachine::requestReconnect()
{
	if (!canReconnect())
		return false;
	runtime_.phase = DestinationPhase::Stopping;
	runtime_.restartPending = true;
	runtime_.reconnectAttempt = 0;
	runtime_.reconnectInSec = 0;
	return true;
}

bool DestinationStateMachine::cancelRestart()
{
	if (runtime_.phase != DestinationPhase::Stopping || !runtime_.restartPending)
		return false;
	runtime_.restartPending = false;
	return true;
}

bool DestinationStateMachine::startFailed(UserMessage error)
{
	if (runtime_.phase != DestinationPhase::Starting)
		return false;
	const bool privateTest = runtime_.privateTest;
	runtime_ = DestinationRuntime{};
	runtime_.phase = DestinationPhase::Failed;
	runtime_.privateTest = privateTest;
	runtime_.error = std::move(error);
	runtime_.lastStop = StopReason::Error;
	return true;
}

bool DestinationStateMachine::outputStarted()
{
	// A "started" that arrives after Stop was pressed must not bring the card back to Live.
	if (runtime_.phase != DestinationPhase::Starting)
		return false;
	runtime_.phase = DestinationPhase::Live;
	runtime_.liveSinceMs = clock_.nowMs();
	runtime_.error = {};
	return true;
}

bool DestinationStateMachine::outputReconnecting(int retryInSec)
{
	if (runtime_.phase != DestinationPhase::Live && runtime_.phase != DestinationPhase::Reconnecting)
		return false;
	runtime_.phase = DestinationPhase::Reconnecting;
	runtime_.reconnectAttempt += 1;
	runtime_.reconnectInSec = retryInSec < 0 ? 0 : retryInSec;
	return true;
}

bool DestinationStateMachine::outputReconnected()
{
	if (runtime_.phase != DestinationPhase::Reconnecting)
		return false;
	runtime_.phase = DestinationPhase::Live;
	runtime_.reconnectAttempt = 0;
	runtime_.reconnectInSec = 0;
	runtime_.reconnectsThisRun += 1;
	return true;
}

DestinationStateMachine::StopOutcome DestinationStateMachine::outputStopped(StopReason reason, UserMessage error)
{
	StopOutcome outcome;
	if (!runtime_.active())
		return outcome;

	const bool userAsked = runtime_.phase == DestinationPhase::Stopping;
	const bool restart = userAsked && runtime_.restartPending;
	const bool privateTest = runtime_.privateTest;

	runtime_ = DestinationRuntime{};
	runtime_.lastStop = reason;
	runtime_.privateTest = privateTest;

	if (userAsked || reason == StopReason::UserStopped) {
		// The user stopped it. Whatever code the output reports on the way down is not a failure.
		runtime_.phase = DestinationPhase::Idle;
	} else {
		runtime_.phase = DestinationPhase::Failed;
		runtime_.error = std::move(error);
	}

	outcome.changed = true;
	outcome.restart = restart;
	return outcome;
}

int64_t DestinationStateMachine::liveSeconds() const
{
	if (runtime_.liveSinceMs < 0 ||
	    (runtime_.phase != DestinationPhase::Live && runtime_.phase != DestinationPhase::Reconnecting))
		return 0;
	const int64_t elapsed = clock_.nowMs() - runtime_.liveSinceMs;
	return elapsed > 0 ? elapsed / 1000 : 0;
}

bool DestinationStateMachine::clearFailure()
{
	if (runtime_.phase != DestinationPhase::Failed)
		return false;
	runtime_ = DestinationRuntime{};
	return true;
}

} // namespace rd
