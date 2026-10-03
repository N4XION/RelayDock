// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <cstdint>
#include <string>

namespace rd {

// Monotonic time source. Logic that depends on elapsed time takes an IClock so tests can
// drive it with FakeClock instead of sleeping.
class IClock {
public:
	virtual ~IClock() = default;
	virtual int64_t nowMs() const = 0;
};

class SteadyClock final : public IClock {
public:
	int64_t nowMs() const override;
};

class FakeClock final : public IClock {
public:
	int64_t nowMs() const override { return nowMs_; }
	void advanceMs(int64_t ms) { nowMs_ += ms; }
	void advanceSeconds(int64_t seconds) { nowMs_ += seconds * 1000; }
	void setMs(int64_t ms) { nowMs_ = ms; }

private:
	int64_t nowMs_ = 0;
};

// Wall-clock time in UTC, "2026-10-04T10:12:33Z". Used for records such as legal acceptance.
std::string utcTimestampIso8601();

} // namespace rd
