// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "performance/system_sampler.h"

#include <windows.h>

#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace rd {

namespace {

uint64_t toTicks(const FILETIME &time)
{
	return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

// Processor load across all cores since the previous call.
class CpuMeter {
public:
	double sample()
	{
		FILETIME idle{}, kernel{}, user{};
		if (!GetSystemTimes(&idle, &kernel, &user))
			return -1.0;

		// Kernel time includes idle time.
		const uint64_t idleNow = toTicks(idle);
		const uint64_t totalNow = toTicks(kernel) + toTicks(user);
		double result = -1.0;
		if (hasPrevious_ && totalNow > total_) {
			const double total = static_cast<double>(totalNow - total_);
			const double idleShare = static_cast<double>(idleNow - idle_) / total;
			result = std::clamp(100.0 * (1.0 - idleShare), 0.0, 100.0);
		}
		idle_ = idleNow;
		total_ = totalNow;
		hasPrevious_ = true;
		return result;
	}

private:
	uint64_t idle_ = 0;
	uint64_t total_ = 0;
	bool hasPrevious_ = false;
};

// Graphics load from the "GPU Engine" counters.
class GpuMeter {
public:
	~GpuMeter() { close(); }

	double sample()
	{
		if (failed_)
			return -1.0;
		if (!query_ && !open())
			return -1.0;

		if (PdhCollectQueryData(query_) != ERROR_SUCCESS)
			return -1.0;
		if (!primed_) {
			// A rate counter needs two collections.
			primed_ = true;
			return -1.0;
		}

		DWORD bytes = 0;
		DWORD count = 0;
		PDH_STATUS status = PdhGetFormattedCounterArrayW(counter_, PDH_FMT_DOUBLE, &bytes, &count, nullptr);
		if (status != static_cast<PDH_STATUS>(PDH_MORE_DATA) || bytes == 0)
			return -1.0;

		buffer_.resize(bytes);
		auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer_.data());
		status = PdhGetFormattedCounterArrayW(counter_, PDH_FMT_DOUBLE, &bytes, &count, items);
		if (status != ERROR_SUCCESS)
			return -1.0;

		// Instance names look like
		//   pid_1234_luid_0x00000000_0x0000ABCD_phys_0_eng_0_engtype_3D
		// Add up every process per adapter and engine type, then take the busiest.
		std::map<std::wstring, double> perEngine;
		for (DWORD i = 0; i < count; ++i) {
			if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA && items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA)
				continue;
			const std::wstring name = items[i].szName ? items[i].szName : L"";
			const size_t luid = name.find(L"luid_");
			const size_t engine = name.find(L"engtype_");
			if (luid == std::wstring::npos || engine == std::wstring::npos)
				continue;
			const size_t phys = name.find(L"_eng_", luid);
			const std::wstring key =
				name.substr(luid, phys == std::wstring::npos ? engine - luid : phys - luid) + L"|" + name.substr(engine);
			perEngine[key] += items[i].FmtValue.doubleValue;
		}
		if (perEngine.empty())
			return -1.0;

		double busiest = 0.0;
		for (const auto &entry : perEngine)
			busiest = std::max(busiest, entry.second);
		return std::clamp(busiest, 0.0, 100.0);
	}

	void close()
	{
		if (query_)
			PdhCloseQuery(query_);
		query_ = nullptr;
		counter_ = nullptr;
		primed_ = false;
	}

private:
	bool open()
	{
		if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) {
			query_ = nullptr;
			failed_ = true;
			return false;
		}
		// The English counter name works on every display language.
		if (PdhAddEnglishCounterW(query_, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter_) != ERROR_SUCCESS) {
			close();
			failed_ = true; // This Windows has no GPU counters. Do not try again.
			return false;
		}
		return true;
	}

	PDH_HQUERY query_ = nullptr;
	PDH_HCOUNTER counter_ = nullptr;
	bool primed_ = false;
	bool failed_ = false;
	std::vector<unsigned char> buffer_;
};

} // namespace

SystemSampler::~SystemSampler()
{
	stop();
}

void SystemSampler::start(int intervalMs, bool sampleGpu)
{
	if (thread_.joinable()) {
		configure(intervalMs, sampleGpu);
		return;
	}
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stopping_ = false;
		intervalMs_ = std::clamp(intervalMs, 250, 60000);
		sampleGpu_ = sampleGpu;
	}
	thread_ = std::thread([this] { run(); });
}

void SystemSampler::configure(int intervalMs, bool sampleGpu)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		intervalMs_ = std::clamp(intervalMs, 250, 60000);
		sampleGpu_ = sampleGpu;
	}
	wake_.notify_all();
}

void SystemSampler::stop()
{
	if (!thread_.joinable())
		return;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stopping_ = true;
	}
	wake_.notify_all();
	thread_.join();
	cpu_.store(-1.0);
	gpu_.store(-1.0);
}

void SystemSampler::run()
{
	CpuMeter cpu;
	GpuMeter gpu;

	for (;;) {
		bool sampleGpu = false;
		int intervalMs = 0;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (stopping_)
				return;
			sampleGpu = sampleGpu_;
			intervalMs = intervalMs_;
		}

		const double cpuNow = cpu.sample();
		if (cpuNow >= 0.0)
			cpu_.store(cpuNow, std::memory_order_relaxed);

		if (sampleGpu) {
			gpu_.store(gpu.sample(), std::memory_order_relaxed);
		} else {
			gpu.close(); // Costs nothing while switched off
			gpu_.store(-1.0, std::memory_order_relaxed);
		}

		std::unique_lock<std::mutex> lock(mutex_);
		// Wakes early for stop() and configure(). A configure() simply starts the next
		// sample sooner, which is harmless.
		wake_.wait_for(lock, std::chrono::milliseconds(intervalMs));
		if (stopping_)
			return;
	}
}

} // namespace rd
