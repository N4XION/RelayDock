// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace rd {

enum class LogLevel { Debug = 0, Info, Warning, Error };

// Receives lines that have already passed through the redactor.
using LogSink = std::function<void(LogLevel, const std::string &)>;

// Sets where log lines go. The plugin forwards them to the OBS log. Tests capture them.
// Passing an empty sink drops lines but still records them in the recent-lines buffer.
void setLogSink(LogSink sink);

// Redacts the message, records it in the recent-lines buffer and forwards it to the sink.
// Safe to call from any thread.
//
// Every RelayDock log line goes through this function. Do not call blog() directly, because
// that would bypass redaction.
void logMessage(LogLevel level, std::string_view message);

// The most recent redacted lines, oldest first. The buffer is bounded (kRecentLogCapacity).
std::vector<std::string> recentLogLines(size_t maxLines = 200);
void clearRecentLogLines();

inline constexpr size_t kRecentLogCapacity = 500;

template <class... Args> void logDebug(std::format_string<Args...> fmt, Args &&...args)
{
	logMessage(LogLevel::Debug, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args> void logInfo(std::format_string<Args...> fmt, Args &&...args)
{
	logMessage(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args> void logWarning(std::format_string<Args...> fmt, Args &&...args)
{
	logMessage(LogLevel::Warning, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args> void logError(std::format_string<Args...> fmt, Args &&...args)
{
	logMessage(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace rd
