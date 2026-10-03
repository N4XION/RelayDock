// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "utils/log.h"

#include "security/redactor.h"

#include <chrono>
#include <ctime>
#include <deque>
#include <mutex>

namespace rd {

namespace {

struct LogState {
	std::mutex mutex;
	LogSink sink;
	std::deque<std::string> recent;
};

LogState &state()
{
	static LogState instance;
	return instance;
}

const char *levelName(LogLevel level)
{
	switch (level) {
	case LogLevel::Debug:
		return "debug";
	case LogLevel::Info:
		return "info";
	case LogLevel::Warning:
		return "warning";
	case LogLevel::Error:
		return "error";
	}
	return "info";
}

std::string timestamp()
{
	const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::tm local{};
#ifdef _WIN32
	localtime_s(&local, &now);
#else
	localtime_r(&now, &local);
#endif
	char buffer[16];
	std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
	return buffer;
}

} // namespace

void setLogSink(LogSink sink)
{
	LogState &s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	s.sink = std::move(sink);
}

void logMessage(LogLevel level, std::string_view message)
{
	const std::string redacted = globalRedactor().redact(message);

	LogSink sink;
	{
		LogState &s = state();
		std::lock_guard<std::mutex> lock(s.mutex);
		s.recent.push_back(std::format("{} {:<7} {}", timestamp(), levelName(level), redacted));
		while (s.recent.size() > kRecentLogCapacity)
			s.recent.pop_front();
		sink = s.sink;
	}

	// Called outside the lock so a sink may log without deadlocking.
	if (sink)
		sink(level, redacted);
}

std::vector<std::string> recentLogLines(size_t maxLines)
{
	LogState &s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	const size_t count = std::min(maxLines, s.recent.size());
	return std::vector<std::string>(s.recent.end() - static_cast<std::ptrdiff_t>(count), s.recent.end());
}

void clearRecentLogLines()
{
	LogState &s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	s.recent.clear();
}

} // namespace rd
