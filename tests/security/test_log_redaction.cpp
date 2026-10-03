// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
//
// Every key in this file is made up for the test. None of them belongs to a real account.
#include <doctest/doctest.h>

#include "security/redactor.h"
#include "utils/log.h"

#include <string>
#include <vector>

namespace {

struct CapturedLog {
	std::vector<std::string> lines;

	CapturedLog()
	{
		rd::clearRecentLogLines();
		rd::setLogSink([this](rd::LogLevel, const std::string &line) { lines.push_back(line); });
	}

	~CapturedLog()
	{
		rd::setLogSink(nullptr);
		rd::clearRecentLogLines();
		rd::globalRedactor().clearSecrets();
	}

	bool anyContains(const std::string &needle) const
	{
		for (const std::string &line : lines) {
			if (line.find(needle) != std::string::npos)
				return true;
		}
		for (const std::string &line : rd::recentLogLines(rd::kRecentLogCapacity)) {
			if (line.find(needle) != std::string::npos)
				return true;
		}
		return false;
	}
};

} // namespace

TEST_SUITE("security.log")
{
	TEST_CASE("a registered stream key never reaches the sink or the recent-lines buffer")
	{
		CapturedLog capture;
		const std::string key = "unit-test-key-9f8e7d6c5b4a-NOT-REAL";
		rd::globalRedactor().addSecret(key);

		rd::logInfo("Starting with key {}", key);
		rd::logWarning("Server said: bad name '{}'", key);
		rd::logError("{}", key);

		REQUIRE(capture.lines.size() == 3);
		CHECK_FALSE(capture.anyContains(key));
		CHECK(capture.anyContains("[redacted]"));
	}

	TEST_CASE("an unregistered key in an RTMP URL is still removed")
	{
		CapturedLog capture;
		rd::logInfo("Connecting to rtmp://live.example.net/app/never-registered-key-123456");
		CHECK_FALSE(capture.anyContains("never-registered-key-123456"));
		CHECK(capture.anyContains("rtmp://live.example.net/app/[redacted]"));
	}

	TEST_CASE("the recent-lines buffer is bounded")
	{
		CapturedLog capture;
		for (size_t i = 0; i < rd::kRecentLogCapacity + 50; ++i)
			rd::logDebug("line {}", i);
		CHECK(rd::recentLogLines(10000).size() == rd::kRecentLogCapacity);
	}

	TEST_CASE("a percent sign in a message is passed through unchanged")
	{
		CapturedLog capture;
		rd::logInfo("Dropped frames: 5% of total, format %s stays literal");
		REQUIRE(capture.lines.size() == 1);
		CHECK(capture.lines[0] == "Dropped frames: 5% of total, format %s stays literal");
	}
}
