// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include <doctest/doctest.h>

#include "performance/metrics_window.h"
#include "performance/optimizer.h"

#include <functional>

using namespace rd;

namespace {

// Feeds the optimiser one sample per second, the way the plugin does.
struct Simulation {
	Optimizer optimizer;
	OptimizerInput input;
	OptimizerOutput last;

	Simulation()
	{
		input.config.mode = OptimizationMode::Suggest;
		input.renderLagPercent = 0.0;
		input.cpuPercent = 30.0;
	}

	OptimizerGroup &addGroup(const std::string &key, std::vector<std::string> ids, int fps = 60, int lines = 1080)
	{
		OptimizerGroup group;
		group.key = key;
		group.destinationIds = std::move(ids);
		group.fps = fps;
		group.lines = lines;
		group.dynamicBitrate = true;
		group.encodeLagPercent = 0.0;
		group.dropPercent = 0.0;
		input.groups.push_back(group);
		return input.groups.back();
	}

	OptimizerGroup &group(size_t index = 0) { return input.groups[index]; }

	// Runs `seconds` one-second steps and returns the output of the last one. Changes the
	// optimiser decides to apply by itself are applied in the same step, as the plugin does.
	const OptimizerOutput &run(int seconds)
	{
		for (int i = 0; i < seconds; ++i) {
			input.nowMs += 1000;
			last = optimizer.update(input);
			noticeCount += last.notices.size();
			for (const OptimizerChange &change : last.apply) {
				collected.push_back(change);
				for (size_t index = 0; index < input.groups.size(); ++index) {
					if (input.groups[index].key == change.groupKey)
						apply(change, index);
				}
			}
		}
		return last;
	}

	// Applies a change the way the plugin would: the group's adjustment and measured values
	// follow, and the optimiser is told.
	void apply(const OptimizerChange &change, size_t groupIndex = 0)
	{
		OptimizerGroup &g = group(groupIndex);
		g.adjustment = change.to;
		if (change.to.maxFps > 0)
			g.fps = std::min(g.fps, change.to.maxFps);
		else if (change.kind == OptimizerChangeKind::Fps)
			g.fps = 60;
		if (change.to.maxLines > 0)
			g.lines = std::min(1080, change.to.maxLines);
		else if (change.kind == OptimizerChangeKind::Resolution)
			g.lines = 1080;
		optimizer.notifyApplied(change, input.nowMs);
	}

	std::vector<OptimizerChange> collected; // Everything returned in `apply`
	size_t noticeCount = 0;
};

} // namespace

TEST_SUITE("performance.metrics_window")
{
	TEST_CASE("percent is the share of bad frames across the window, not since the start")
	{
		MetricsWindow window(10000);
		// 100 healthy seconds: 60 frames per second, none bad.
		for (int s = 0; s <= 100; ++s)
			window.add("render", s * 1000, static_cast<uint64_t>(s) * 60, 0);
		CHECK(window.percent("render") == doctest::Approx(0.0));

		// Then every tenth frame lags for 10 seconds.
		for (int s = 101; s <= 110; ++s)
			window.add("render", s * 1000, static_cast<uint64_t>(s) * 60, static_cast<uint64_t>(s - 100) * 6);
		CHECK(window.percent("render") == doctest::Approx(10.0).epsilon(0.02));
	}

	TEST_CASE("not enough data reports -1")
	{
		MetricsWindow window(10000);
		CHECK(window.percent("missing") == -1.0);
		window.add("a", 0, 0, 0);
		CHECK(window.percent("a") == -1.0);
		window.add("a", 1000, 60, 0);
		CHECK(window.percent("a") == -1.0); // The window is not half full yet.
		window.add("a", 6000, 360, 36);
		CHECK(window.percent("a") == doctest::Approx(10.0));
	}

	TEST_CASE("no new frames reports -1 instead of dividing by zero")
	{
		MetricsWindow window(10000);
		window.add("a", 0, 500, 5);
		window.add("a", 8000, 500, 5);
		CHECK(window.percent("a") == -1.0);
	}

	TEST_CASE("a counter that restarts begins a new window")
	{
		MetricsWindow window(10000);
		for (int s = 0; s <= 20; ++s)
			window.add("drop", s * 1000, static_cast<uint64_t>(s) * 30, static_cast<uint64_t>(s) * 3);
		CHECK(window.percent("drop") == doctest::Approx(10.0));

		// A reconnect: OBS starts the counters again from zero.
		window.add("drop", 21000, 0, 0);
		CHECK(window.percent("drop") == -1.0);
		for (int s = 22; s <= 30; ++s)
			window.add("drop", s * 1000, static_cast<uint64_t>(s - 21) * 30, 0);
		CHECK(window.percent("drop") == doctest::Approx(0.0));
	}

	TEST_CASE("averages cover the window only")
	{
		MetricsWindow window(5000);
		CHECK(window.average("cpu") == -1.0);
		for (int s = 0; s < 10; ++s)
			window.addValue("cpu", s * 1000, 20.0);
		for (int s = 10; s < 15; ++s)
			window.addValue("cpu", s * 1000, 80.0);
		CHECK(window.average("cpu") == doctest::Approx(80.0));

		window.remove("cpu");
		CHECK(window.average("cpu") == -1.0);
	}
}

TEST_SUITE("performance.optimizer")
{
	TEST_CASE("Off proposes nothing, however bad things are")
	{
		Simulation sim;
		sim.input.config.mode = OptimizationMode::Off;
		sim.addGroup("g", {"twitch"}).encodeLagPercent = 40.0;
		sim.group().dropPercent = 40.0;
		sim.input.renderLagPercent = 40.0;
		sim.run(300);
		CHECK(sim.last.suggestions.empty());
		CHECK(sim.collected.empty());
	}

	TEST_CASE("healthy streams produce no suggestions")
	{
		Simulation sim;
		sim.addGroup("g", {"twitch", "youtube"});
		sim.run(900);
		CHECK(sim.last.suggestions.empty());
		CHECK(sim.collected.empty());
		CHECK(sim.noticeCount == 0);
	}

	TEST_CASE("one short spike changes nothing")
	{
		Simulation sim;
		sim.addGroup("g", {"twitch"});
		sim.run(30);

		sim.group().encodeLagPercent = 25.0; // A bad five seconds
		sim.run(5);
		sim.group().encodeLagPercent = 0.0;
		sim.run(120);

		CHECK(sim.last.suggestions.empty());
		CHECK(sim.collected.empty());
	}

	TEST_CASE("repeated short spikes with calm between them change nothing")
	{
		Simulation sim;
		sim.addGroup("g", {"twitch"});
		for (int round = 0; round < 10; ++round) {
			sim.group().encodeLagPercent = 20.0;
			sim.run(10);
			sim.group().encodeLagPercent = 0.0;
			sim.run(10);
		}
		CHECK(sim.last.suggestions.empty());
	}

	TEST_CASE("sustained encoder overload suggests 60 to 30 FPS first")
	{
		Simulation sim;
		sim.addGroup("g", {"tiktok", "facebook"});
		sim.group().encodeLagPercent = 8.0;

		sim.run(19);
		CHECK(sim.last.suggestions.empty()); // Not sustained yet
		sim.run(2);
		REQUIRE(sim.last.suggestions.size() == 1);

		const OptimizerChange &change = sim.last.suggestions.front();
		CHECK(change.cause == OptimizerCause::EncoderOverload);
		CHECK(change.kind == OptimizerChangeKind::Fps);
		CHECK(change.to.maxFps == 30);
		CHECK(change.from.maxFps == 0);
		CHECK(change.needsReconnect);
		CHECK(change.destinationIds == std::vector<std::string>{"tiktok", "facebook"});
		CHECK(sim.collected.empty()); // Suggest mode never applies by itself
	}

	TEST_CASE("the same suggestion is not repeated while it waits for an answer")
	{
		Simulation sim;
		sim.addGroup("g", {"twitch"}).encodeLagPercent = 8.0;
		sim.run(25);
		REQUIRE(sim.last.suggestions.size() == 1);
		const std::string id = sim.last.suggestions.front().id;
		sim.run(600);
		REQUIRE(sim.last.suggestions.size() == 1);
		CHECK(sim.last.suggestions.front().id == id);
	}

	TEST_CASE("the ladder for overload: frame rate, then 720p, then 540p, then a notice")
	{
		Simulation sim;
		sim.addGroup("g", {"twitch"}).encodeLagPercent = 8.0;

		sim.run(25);
		REQUIRE(sim.last.suggestions.size() == 1);
		CHECK(sim.last.suggestions.front().kind == OptimizerChangeKind::Fps);
		sim.apply(sim.last.suggestions.front());
		sim.group().key = "g-30fps"; // New settings mean a new encoder

		sim.run(90);
		REQUIRE(sim.last.suggestions.size() == 1);
		CHECK(sim.last.suggestions.front().kind == OptimizerChangeKind::Resolution);
		CHECK(sim.last.suggestions.front().to.maxLines == 720);
		CHECK(sim.last.suggestions.front().to.maxFps == 30); // Earlier reductions stay
		sim.apply(sim.last.suggestions.front());
		sim.group().key = "g-720p30";

		sim.run(90);
		REQUIRE(sim.last.suggestions.size() == 1);
		CHECK(sim.last.suggestions.front().to.maxLines == 540);
		sim.apply(sim.last.suggestions.front());
		sim.group().key = "g-540p30";

		const size_t noticesBefore = sim.noticeCount;
		sim.run(300);
		CHECK(sim.last.suggestions.empty());
		CHECK(sim.noticeCount == noticesBefore + 1); // Said once, not every second
	}

	TEST_CASE("a change waits out the cooldown before the next one")
	{
		Simulation sim;
		sim.addGroup("g", {"twitch"}).encodeLagPercent = 8.0;
		sim.run(25);
		sim.apply(sim.last.suggestions.front());
		sim.group().key = "g-30fps";

		// Still overloaded. Sustained again after 20 s, but the cooldown is 60 s.
		sim.run(40);
		CHECK(sim.last.suggestions.empty());
		sim.run(30);
		CHECK(sim.last.suggestions.size() == 1);
	}

	TEST_CASE("locked settings are never touched")
	{
		Simulation sim;
		OptimizerGroup &group = sim.addGroup("g", {"twitch"});
		group.encodeLagPercent = 8.0;
		group.fpsAdjustable = false; // FPS locked

		sim.run(25);
		REQUIRE(sim.last.suggestions.size() == 1);
		CHECK(sim.last.suggestions.front().kind == OptimizerChangeKind::Resolution);
		CHECK(sim.last.suggestions.front().to.maxFps == 0);

		Simulation allLocked;
		OptimizerGroup &locked = allLocked.addGroup("g", {"twitch"});
		locked.encodeLagPercent = 8.0;
		locked.dropPercent = 20.0;
		locked.fpsAdjustable = false;
		locked.resolutionAdjustable = false;
		locked.bitrateAdjustable = false;
		allLocked.run(600);
		CHECK(allLocked.last.suggestions.empty());
		CHECK(allLocked.collected.empty());
		CHECK(allLocked.noticeCount == 1);
	}

	TEST_CASE("locking a setting withdraws a suggestion that would change it")
	{
		Simulation sim;
		sim.addGroup("g", {"twitch"}).encodeLagPercent = 8.0;
		sim.run(25);
		REQUIRE(sim.last.suggestions.size() == 1);

		sim.group().fpsAdjustable = false; // The user pressed Lock Setting
		sim.run(1);
		CHECK(sim.last.suggestions.empty());
	}

	TEST_CASE("network drops lower the bitrate one step and need no reconnect")
	{
		Simulation sim;
		sim.addGroup("g", {"facebook"}).dropPercent = 12.0;
		sim.run(25);
		REQUIRE(sim.last.suggestions.size() == 1);

		const OptimizerChange &change = sim.last.suggestions.front();
		CHECK(change.cause == OptimizerCause::NetworkDrops);
		CHECK(change.kind == OptimizerChangeKind::Bitrate);
		CHECK(change.to.bitratePercent == 85);
		CHECK_FALSE(change.needsReconnect);
		CHECK(change.to.maxFps == 0);
		CHECK(change.to.maxLines == 0);
	}

	TEST_CASE("an encoder that cannot change bitrate live needs a reconnect for it")
	{
		Simulation sim;
		OptimizerGroup &group = sim.addGroup("g", {"facebook"});
		group.dropPercent = 12.0;
		group.dynamicBitrate = false;
		sim.run(25);
		REQUIRE(sim.last.suggestions.size() == 1);
		CHECK(sim.last.suggestions.front().needsReconnect);
	}

	TEST_CASE("the bitrate ladder stops at 40 percent")
	{
		CHECK(Optimizer::nextLowerBitratePercent(100) == 85);
		CHECK(Optimizer::nextLowerBitratePercent(85) == 70);
		CHECK(Optimizer::nextLowerBitratePercent(70) == 55);
		CHECK(Optimizer::nextLowerBitratePercent(55) == 40);
		CHECK(Optimizer::nextLowerBitratePercent(40) == 40);
		CHECK(Optimizer::nextHigherBitratePercent(40) == 55);
		CHECK(Optimizer::nextHigherBitratePercent(55) == 70);
		CHECK(Optimizer::nextHigherBitratePercent(85) == 100);
		CHECK(Optimizer::nextHigherBitratePercent(100) == 100);
	}

	TEST_CASE("Automatic applies live changes itself")
	{
		Simulation sim;
		sim.input.config.mode = OptimizationMode::Automatic;
		sim.addGroup("g", {"facebook"}).dropPercent = 12.0;
		sim.run(25);
		REQUIRE(sim.collected.size() == 1);
		CHECK(sim.collected.front().to.bitratePercent == 85);
		CHECK(sim.last.suggestions.empty());
	}

	TEST_CASE("Automatic asks before a change that reconnects, unless that is allowed")
	{
		Simulation asks;
		asks.input.config.mode = OptimizationMode::Automatic;
		asks.input.config.allowReconnectingChanges = false;
		asks.addGroup("g", {"twitch"}).encodeLagPercent = 8.0;
		asks.run(25);
		CHECK(asks.collected.empty());
		REQUIRE(asks.last.suggestions.size() == 1);
		CHECK(asks.last.suggestions.front().needsReconnect);

		Simulation applies;
		applies.input.config.mode = OptimizationMode::Automatic;
		applies.input.config.allowReconnectingChanges = true;
		applies.addGroup("g", {"twitch"}).encodeLagPercent = 8.0;
		applies.run(25);
		REQUIRE(applies.collected.size() == 1);
		CHECK(applies.collected.front().kind == OptimizerChangeKind::Fps);
		CHECK(applies.last.suggestions.empty());
	}

	TEST_CASE("hysteresis: a value between the clear level and the threshold keeps the alarm on")
	{
		Simulation sim;
		sim.addGroup("g", {"twitch"}).encodeLagPercent = 4.0; // Above the 3 percent threshold
		sim.run(10);
		sim.group().encodeLagPercent = 2.0; // Below the threshold, above the 1.5 percent clear level
		sim.run(12);
		CHECK(sim.last.suggestions.size() == 1); // 22 s in the trouble band counts as sustained

		Simulation cleared;
		cleared.addGroup("g", {"twitch"}).encodeLagPercent = 4.0;
		cleared.run(10);
		cleared.group().encodeLagPercent = 1.0; // Below the clear level: the alarm ends
		cleared.run(3);
		cleared.group().encodeLagPercent = 4.0;
		cleared.run(12); // Only 12 s since it started again
		CHECK(cleared.last.suggestions.empty());
	}

	TEST_CASE("an ignored suggestion is not offered again for ten minutes")
	{
		Simulation sim;
		OptimizerGroup &group = sim.addGroup("g", {"twitch"});
		group.encodeLagPercent = 8.0;
		group.resolutionAdjustable = false; // Leave frame rate as the only lever
		sim.run(25);
		REQUIRE(sim.last.suggestions.size() == 1);

		sim.optimizer.notifyIgnored(sim.last.suggestions.front().id, sim.input.nowMs);
		sim.run(590);
		CHECK(sim.last.suggestions.empty());
		sim.run(80);
		CHECK(sim.last.suggestions.size() == 1);
	}

	TEST_CASE("a suggestion is withdrawn when the problem goes away by itself")
	{
		Simulation sim;
		sim.addGroup("g", {"twitch"}).encodeLagPercent = 8.0;
		sim.run(25);
		REQUIRE(sim.last.suggestions.size() == 1);

		sim.group().encodeLagPercent = 0.0;
		sim.run(30);
		CHECK(sim.last.suggestions.size() == 1); // Still there, in case it comes back
		sim.run(40);
		CHECK(sim.last.suggestions.empty());
	}

	TEST_CASE("a suggestion disappears when its encoder stops")
	{
		Simulation sim;
		sim.addGroup("g", {"twitch"}).encodeLagPercent = 8.0;
		sim.run(25);
		REQUIRE(sim.last.suggestions.size() == 1);
		sim.input.groups.clear();
		sim.run(1);
		CHECK(sim.last.suggestions.empty());
	}

	TEST_CASE("recovery: settings return one step at a time after five quiet minutes")
	{
		Simulation sim;
		sim.addGroup("g", {"facebook"}).dropPercent = 12.0;
		sim.run(25);
		sim.apply(sim.last.suggestions.front()); // 85 percent
		sim.run(70);
		REQUIRE(sim.last.suggestions.size() == 1);
		sim.apply(sim.last.suggestions.front()); // 70 percent
		CHECK(sim.group().adjustment.bitratePercent == 70);

		sim.group().dropPercent = 0.0; // The connection is fine again
		sim.run(290);
		CHECK(sim.last.suggestions.empty()); // Not yet five minutes

		sim.run(20);
		REQUIRE(sim.last.suggestions.size() == 1);
		const OptimizerChange &up = sim.last.suggestions.front();
		CHECK(up.cause == OptimizerCause::Recovery);
		CHECK(up.kind == OptimizerChangeKind::Bitrate);
		CHECK(up.to.bitratePercent == 85); // One step, not straight back to 100
	}

	TEST_CASE("recovery undoes reductions in reverse order")
	{
		Simulation sim;
		OptimizerGroup &group = sim.addGroup("g", {"twitch"}, 30, 720);
		group.adjustment.maxFps = 30;
		group.adjustment.maxLines = 720;
		group.adjustment.bitratePercent = 85;

		sim.run(310);
		REQUIRE(sim.last.suggestions.size() == 1);
		CHECK(sim.last.suggestions.front().kind == OptimizerChangeKind::Bitrate);
		sim.apply(sim.last.suggestions.front());

		sim.run(310);
		REQUIRE(sim.last.suggestions.size() == 1);
		CHECK(sim.last.suggestions.front().kind == OptimizerChangeKind::Resolution);
		CHECK(sim.last.suggestions.front().to.maxLines == 0);
		sim.apply(sim.last.suggestions.front());

		sim.run(310);
		REQUIRE(sim.last.suggestions.size() == 1);
		CHECK(sim.last.suggestions.front().kind == OptimizerChangeKind::Fps);
		CHECK(sim.last.suggestions.front().to.maxFps == 0);
		sim.apply(sim.last.suggestions.front());

		sim.run(900);
		CHECK(sim.last.suggestions.empty()); // Nothing left to restore
	}

	TEST_CASE("no flapping: a step up that brings the problem back doubles the next wait")
	{
		Simulation sim;
		sim.input.config.mode = OptimizationMode::Automatic;
		sim.addGroup("g", {"facebook"}).dropPercent = 12.0;

		sim.run(25); // Step down to 85
		REQUIRE(sim.collected.size() == 1);
		CHECK(sim.group().adjustment.bitratePercent == 85);
		sim.group().dropPercent = 0.0;

		sim.run(310); // Quiet for five minutes: step up to 100
		REQUIRE(sim.collected.size() == 2);
		CHECK(sim.collected.back().cause == OptimizerCause::Recovery);
		CHECK(sim.group().adjustment.none());

		sim.group().dropPercent = 12.0; // The problem returns straight away
		sim.run(70);
		REQUIRE(sim.collected.size() == 3);
		CHECK(sim.collected.back().cause == OptimizerCause::NetworkDrops);
		sim.group().dropPercent = 0.0;

		sim.run(400); // Five minutes are no longer enough
		CHECK(sim.collected.size() == 3);
		sim.run(260); // Ten minutes are
		CHECK(sim.collected.size() == 4);
		CHECK(sim.collected.back().cause == OptimizerCause::Recovery);
	}

	TEST_CASE("Prevent Game Lag reacts earlier and to smaller numbers")
	{
		Simulation normal;
		normal.addGroup("g", {"twitch"}).encodeLagPercent = 2.0; // Under the normal 3 percent threshold
		normal.run(120);
		CHECK(normal.last.suggestions.empty());

		Simulation strict;
		strict.input.config.preventGameLag = true;
		strict.addGroup("g", {"twitch"}).encodeLagPercent = 2.0; // Over the strict 1.5 percent threshold
		strict.run(11);
		CHECK(strict.last.suggestions.empty());
		strict.run(2); // Sustained after 12 s instead of 20
		CHECK(strict.last.suggestions.size() == 1);
	}

	TEST_CASE("Prevent Game Lag treats high CPU load as an early warning for software encoders only")
	{
		Simulation normal;
		normal.addGroup("g", {"twitch"});
		normal.input.cpuPercent = 97.0;
		normal.run(300);
		CHECK(normal.last.suggestions.empty()); // Without the option, CPU load alone changes nothing

		Simulation strict;
		strict.input.config.preventGameLag = true;
		strict.addGroup("software", {"twitch"});
		strict.addGroup("hardware", {"youtube"}).hardwareEncoder = true;
		strict.input.cpuPercent = 97.0;
		strict.run(20);
		REQUIRE(strict.last.suggestions.size() == 1);
		CHECK(strict.last.suggestions.front().cause == OptimizerCause::HighCpu);
		CHECK(strict.last.suggestions.front().groupKey == "software");
	}

	TEST_CASE("Prevent Game Lag waits ten quiet minutes before a step up")
	{
		Simulation sim;
		sim.input.config.preventGameLag = true;
		OptimizerGroup &group = sim.addGroup("g", {"twitch"}, 30, 1080);
		group.adjustment.maxFps = 30;
		sim.run(590);
		CHECK(sim.last.suggestions.empty());
		sim.run(20);
		CHECK(sim.last.suggestions.size() == 1);
	}

	TEST_CASE("rendering lag reduces hardware encoders, and is only reported when none runs")
	{
		Simulation hardware;
		hardware.addGroup("hw", {"twitch"}).hardwareEncoder = true;
		hardware.addGroup("sw", {"youtube"});
		hardware.input.renderLagPercent = 9.0;
		hardware.run(25);
		REQUIRE(hardware.last.suggestions.size() == 1);
		CHECK(hardware.last.suggestions.front().cause == OptimizerCause::RenderingLag);
		CHECK(hardware.last.suggestions.front().groupKey == "hw");

		Simulation software;
		software.addGroup("sw", {"youtube"});
		software.input.renderLagPercent = 9.0;
		software.run(600);
		CHECK(software.last.suggestions.empty());
		CHECK(software.noticeCount == 1); // One notice for the whole episode
	}

	TEST_CASE("two overloaded encoders each get a suggestion")
	{
		Simulation sim;
		sim.addGroup("a", {"twitch"}).encodeLagPercent = 8.0;
		sim.addGroup("b", {"tiktok"}).encodeLagPercent = 8.0;
		sim.run(25);
		REQUIRE(sim.last.suggestions.size() == 2);
		CHECK(sim.last.suggestions[0].groupKey != sim.last.suggestions[1].groupKey);
	}

	TEST_CASE("unknown measurements never trigger anything")
	{
		Simulation sim;
		OptimizerGroup &group = sim.addGroup("g", {"twitch"});
		group.encodeLagPercent = -1.0;
		group.dropPercent = -1.0;
		sim.input.renderLagPercent = -1.0;
		sim.input.cpuPercent = -1.0;
		sim.input.config.preventGameLag = true;
		sim.run(600);
		CHECK(sim.last.suggestions.empty());
		CHECK(sim.noticeCount == 0);
	}

	TEST_CASE("reset forgets suggestions and history")
	{
		Simulation sim;
		sim.addGroup("g", {"twitch"}).encodeLagPercent = 8.0;
		sim.run(25);
		REQUIRE(sim.optimizer.pending().size() == 1);
		sim.optimizer.reset();
		CHECK(sim.optimizer.pending().empty());
		sim.run(10);
		CHECK(sim.last.suggestions.empty()); // The 20 s start over
	}

	TEST_CASE("each kind of change maps to the lock that blocks it")
	{
		CHECK(lockForChange(OptimizerChangeKind::Bitrate) == LockableSetting::Bitrate);
		CHECK(lockForChange(OptimizerChangeKind::Fps) == LockableSetting::Fps);
		CHECK(lockForChange(OptimizerChangeKind::Resolution) == LockableSetting::Resolution);
	}
}
