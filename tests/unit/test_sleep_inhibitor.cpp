// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include <doctest/doctest.h>

#include "utils/sleep_inhibitor.h"

#include <windows.h>

#include <powrprof.h>

namespace {

// What Windows as a whole is currently asked to keep awake, by every program together.
// Returns false when Windows does not answer.
bool systemExecutionState(ULONG &state)
{
	state = 0;
	return CallNtPowerInformation(SystemExecutionState, nullptr, 0, &state, sizeof(state)) == 0;
}

// Whether the PC runs on mains power. On battery, the laptop these tests were written on does
// not count a request to keep the system awake: Windows accepts the request and still reports
// that nothing needs the system. So on battery there is nothing to compare with.
bool onMainsPower()
{
	SYSTEM_POWER_STATUS status{};
	return GetSystemPowerStatus(&status) && status.ACLineStatus == 1;
}

} // namespace

TEST_SUITE("sleep inhibitor")
{
	TEST_CASE("starts inactive and switching it off again is harmless")
	{
		rd::SleepInhibitor inhibitor("RelayDock unit test");
		CHECK_FALSE(inhibitor.active());
		CHECK(inhibitor.setActive(false));
		CHECK_FALSE(inhibitor.active());
	}

	TEST_CASE("while active, Windows reports that the system must stay awake")
	{
		rd::SleepInhibitor inhibitor("RelayDock unit test");
		REQUIRE(inhibitor.setActive(true));
		CHECK(inhibitor.active());

		ULONG state = 0;
		REQUIRE(systemExecutionState(state));
		if (onMainsPower())
			CHECK((state & ES_SYSTEM_REQUIRED) != 0);
		else
			WARN((state & ES_SYSTEM_REQUIRED) != 0);
		// A session without a display may not report this one.
		WARN((state & ES_DISPLAY_REQUIRED) != 0);

		// Asking again changes nothing.
		CHECK(inhibitor.setActive(true));
		CHECK(inhibitor.active());

		CHECK(inhibitor.setActive(false));
		CHECK_FALSE(inhibitor.active());
	}

	TEST_CASE("it can be switched on and off many times")
	{
		rd::SleepInhibitor inhibitor("RelayDock unit test");
		for (int i = 0; i < 50; ++i) {
			REQUIRE(inhibitor.setActive(true));
			REQUIRE(inhibitor.setActive(false));
		}
		CHECK_FALSE(inhibitor.active());
	}

	TEST_CASE("two inhibitors do not disturb each other")
	{
		rd::SleepInhibitor first("RelayDock unit test, first");
		rd::SleepInhibitor second("RelayDock unit test, second");
		REQUIRE(first.setActive(true));
		REQUIRE(second.setActive(true));

		// Releasing one leaves the other in force.
		REQUIRE(first.setActive(false));
		ULONG state = 0;
		REQUIRE(systemExecutionState(state));
		if (onMainsPower())
			CHECK((state & ES_SYSTEM_REQUIRED) != 0);
		else
			WARN((state & ES_SYSTEM_REQUIRED) != 0);
		CHECK(second.active());
	}

	TEST_CASE("destroying an active inhibitor does not crash or leak the request")
	{
		for (int i = 0; i < 20; ++i) {
			rd::SleepInhibitor inhibitor("RelayDock unit test");
			REQUIRE(inhibitor.setActive(true));
		}
	}
}
