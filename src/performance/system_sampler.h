// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace rd {

// Measures how busy the whole PC is: processor load, and graphics chip load where Windows
// reports it.
//
// The measurements run on their own thread. Asking Windows for graphics load can take several
// milliseconds, and nothing that slow may run on the OBS interface thread. Readers get the
// latest value from an atomic and never wait.
//
// Graphics load comes from the Windows "GPU Engine" performance counters, the same source
// Task Manager uses. The value is the busiest engine type of the busiest adapter. It is -1 on
// systems that do not have those counters.
//
// Thread ownership: start(), configure() and stop() from one thread. The getters from any.
class SystemSampler {
public:
	SystemSampler() = default;
	~SystemSampler();

	SystemSampler(const SystemSampler &) = delete;
	SystemSampler &operator=(const SystemSampler &) = delete;

	void start(int intervalMs, bool sampleGpu);
	// Changes the pace or switches graphics sampling. Takes effect at the next sample.
	void configure(int intervalMs, bool sampleGpu);
	void stop();
	bool running() const { return thread_.joinable(); }

	// Percent of all processor cores in use. -1 until two samples exist.
	double cpuPercent() const { return cpu_.load(std::memory_order_relaxed); }
	// Percent of the busiest graphics engine. -1 when unknown or switched off.
	double gpuPercent() const { return gpu_.load(std::memory_order_relaxed); }

private:
	void run();

	std::thread thread_;
	std::mutex mutex_;
	std::condition_variable wake_;
	bool stopping_ = false;
	int intervalMs_ = 2000;
	bool sampleGpu_ = true;
	std::atomic<double> cpu_{-1.0};
	std::atomic<double> gpu_{-1.0};
};

} // namespace rd
