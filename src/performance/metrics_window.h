// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <string>

namespace rd {

// Turns counters that only ever grow into "percent over the last N seconds".
//
// OBS reports frames as running totals: frames rendered and frames that lagged, frames sent
// and frames dropped. A total since the stream started hides what is happening now, so the
// optimiser looks at the change across a sliding window instead.
//
// A counter that goes down means OBS restarted it (a reconnect, a video reset). The window
// for that key starts over instead of producing a negative number.
//
// Thread ownership: one thread. Not synchronised.
class MetricsWindow {
public:
	explicit MetricsWindow(int64_t windowMs = 10000) : windowMs_(windowMs) {}

	// Records the running totals for a key at a point in time.
	void add(const std::string &key, int64_t timeMs, uint64_t total, uint64_t bad);

	// Percent of `bad` among `total` across the window. Returns -1 when there is not enough
	// data yet: fewer than two samples, a window that is not yet half full, or no new frames.
	double percent(const std::string &key) const;

	// Average of plain values (CPU percent, for example) across the window. -1 when empty.
	void addValue(const std::string &key, int64_t timeMs, double value);
	double average(const std::string &key) const;

	// Forgets a key, for example when a destination stops.
	void remove(const std::string &key);
	void clear();

	int64_t windowMs() const { return windowMs_; }

private:
	struct CounterPoint {
		int64_t timeMs;
		uint64_t total;
		uint64_t bad;
	};
	struct ValuePoint {
		int64_t timeMs;
		double value;
	};

	int64_t windowMs_;
	std::map<std::string, std::deque<CounterPoint>> counters_;
	std::map<std::string, std::deque<ValuePoint>> values_;
};

} // namespace rd
