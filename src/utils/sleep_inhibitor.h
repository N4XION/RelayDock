// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <string>

namespace rd {

// Keeps Windows from going to sleep or switching the display off while a stream runs.
//
// OBS does the same for its own stream with SetThreadExecutionState. That state belongs to
// the calling thread, and RelayDock runs on the same thread as OBS, so the two would
// overwrite each other. A power request belongs to a handle instead. OBS ending its own
// stream leaves it alone.
//
// Not thread-safe. Use one object from one thread.
class SleepInhibitor {
public:
	// `reason` is what "powercfg /requests" shows for this request.
	explicit SleepInhibitor(std::string reason);
	~SleepInhibitor();
	SleepInhibitor(const SleepInhibitor &) = delete;
	SleepInhibitor &operator=(const SleepInhibitor &) = delete;

	// Returns true when the request is now in the wanted state. Asking for the state it
	// already has changes nothing.
	bool setActive(bool active);
	bool active() const { return active_; }

private:
	std::string reason_;
	void *request_ = nullptr; // HANDLE from PowerCreateRequest
	unsigned held_ = 0;       // One bit per request type that was set
	bool active_ = false;
};

} // namespace rd
