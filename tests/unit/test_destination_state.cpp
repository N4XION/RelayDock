// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include <doctest/doctest.h>

#include "core/destination_state.h"
#include "encoders/encoder_settings.h"
#include "test_helpers.h"

#include <algorithm>

using namespace rd;

namespace {

UserMessage error(const char *what)
{
	return {what, {}, "Check something."};
}

const EncoderSetting *findSetting(const std::vector<EncoderSetting> &settings, const std::string &key)
{
	const auto it = std::find_if(settings.begin(), settings.end(),
				     [&](const EncoderSetting &setting) { return setting.key == key; });
	return it == settings.end() ? nullptr : &*it;
}

} // namespace

TEST_SUITE("core.destination_state")
{
	TEST_CASE("a normal run: idle, starting, live, stopping, idle")
	{
		FakeClock clock;
		DestinationStateMachine machine(clock);
		CHECK(machine.runtime().phase == DestinationPhase::Idle);
		CHECK(machine.canStart());
		CHECK_FALSE(machine.canStop());
		CHECK_FALSE(machine.canReconnect());

		REQUIRE(machine.requestStart(false));
		CHECK(machine.runtime().phase == DestinationPhase::Starting);
		CHECK_FALSE(machine.canStart());
		CHECK(machine.canStop());

		clock.advanceSeconds(2);
		REQUIRE(machine.outputStarted());
		CHECK(machine.runtime().phase == DestinationPhase::Live);
		CHECK(machine.canReconnect());

		clock.advanceSeconds(75);
		CHECK(machine.liveSeconds() == 75);

		REQUIRE(machine.requestStop());
		CHECK(machine.runtime().phase == DestinationPhase::Stopping);
		CHECK_FALSE(machine.canStop());

		const auto outcome = machine.outputStopped(StopReason::UserStopped, {});
		CHECK(outcome.changed);
		CHECK_FALSE(outcome.restart);
		CHECK(machine.runtime().phase == DestinationPhase::Idle);
		CHECK(machine.runtime().error.empty());
		CHECK(machine.liveSeconds() == 0);
		CHECK(machine.canStart());
	}

	TEST_CASE("a start cannot be requested twice")
	{
		FakeClock clock;
		DestinationStateMachine machine(clock);
		REQUIRE(machine.requestStart(false));
		CHECK_FALSE(machine.requestStart(false));
		machine.outputStarted();
		CHECK_FALSE(machine.requestStart(false));
	}

	TEST_CASE("a rejected connection during start ends in Failed with the message")
	{
		FakeClock clock;
		DestinationStateMachine machine(clock);
		machine.requestStart(false);

		const auto outcome = machine.outputStopped(StopReason::InvalidStream, error("TikTok rejected the connection."));
		CHECK(outcome.changed);
		CHECK(machine.runtime().phase == DestinationPhase::Failed);
		CHECK(machine.runtime().error.what == "TikTok rejected the connection.");
		CHECK(machine.runtime().lastStop == StopReason::InvalidStream);
		CHECK(machine.canStart());
		CHECK_FALSE(machine.canStop());
	}

	TEST_CASE("a start that cannot begin is Failed without ever touching an output")
	{
		FakeClock clock;
		DestinationStateMachine machine(clock);
		machine.requestStart(false);
		REQUIRE(machine.startFailed(error("Twitch has no stream key.")));
		CHECK(machine.runtime().phase == DestinationPhase::Failed);
		CHECK(machine.runtime().error.what == "Twitch has no stream key.");

		// startFailed only applies while starting.
		CHECK_FALSE(machine.startFailed(error("again")));
	}

	TEST_CASE("starting again after a failure clears the old error")
	{
		FakeClock clock;
		DestinationStateMachine machine(clock);
		machine.requestStart(false);
		machine.outputStopped(StopReason::ConnectFailed, error("Could not connect."));
		REQUIRE(machine.requestStart(false));
		CHECK(machine.runtime().error.empty());
		CHECK(machine.runtime().phase == DestinationPhase::Starting);
	}

	TEST_CASE("clearFailure returns a failed destination to idle")
	{
		FakeClock clock;
		DestinationStateMachine machine(clock);
		CHECK_FALSE(machine.clearFailure());
		machine.requestStart(false);
		machine.startFailed(error("x"));
		CHECK(machine.clearFailure());
		CHECK(machine.runtime().phase == DestinationPhase::Idle);
		CHECK(machine.runtime().error.empty());
	}

	TEST_CASE("automatic reconnect: attempts count up, success returns to live and keeps the clock")
	{
		FakeClock clock;
		DestinationStateMachine machine(clock);
		machine.requestStart(false);
		machine.outputStarted();
		clock.advanceSeconds(30);

		REQUIRE(machine.outputReconnecting(2));
		CHECK(machine.runtime().phase == DestinationPhase::Reconnecting);
		CHECK(machine.runtime().reconnectAttempt == 1);
		CHECK(machine.runtime().reconnectInSec == 2);
		CHECK(machine.canStop());

		REQUIRE(machine.outputReconnecting(3));
		CHECK(machine.runtime().reconnectAttempt == 2);
		CHECK(machine.runtime().reconnectInSec == 3);

		clock.advanceSeconds(5);
		REQUIRE(machine.outputReconnected());
		CHECK(machine.runtime().phase == DestinationPhase::Live);
		CHECK(machine.runtime().reconnectAttempt == 0);
		CHECK(machine.runtime().reconnectsThisRun == 1);
		CHECK(machine.liveSeconds() == 35);
	}

	TEST_CASE("running out of reconnect attempts ends in Failed")
	{
		FakeClock clock;
		DestinationStateMachine machine(clock);
		machine.requestStart(false);
		machine.outputStarted();
		machine.outputReconnecting(2);

		const auto outcome = machine.outputStopped(StopReason::Disconnected, error("Twitch lost its connection."));
		CHECK(outcome.changed);
		CHECK(machine.runtime().phase == DestinationPhase::Failed);
		CHECK(machine.runtime().lastStop == StopReason::Disconnected);
	}

	TEST_CASE("stop while reconnecting ends in idle, not failed")
	{
		FakeClock clock;
		DestinationStateMachine machine(clock);
		machine.requestStart(false);
		machine.outputStarted();
		machine.outputReconnecting(10);
		REQUIRE(machine.requestStop());

		// OBS reports the stop with whatever code it had. The user asked, so it is not an error.
		machine.outputStopped(StopReason::Disconnected, error("lost"));
		CHECK(machine.runtime().phase == DestinationPhase::Idle);
		CHECK(machine.runtime().error.empty());
	}

	TEST_CASE("stop while connecting ends in idle")
	{
		FakeClock clock;
		DestinationStateMachine machine(clock);
		machine.requestStart(false);
		REQUIRE(machine.requestStop());
		machine.outputStopped(StopReason::ConnectFailed, error("no"));
		CHECK(machine.runtime().phase == DestinationPhase::Idle);
	}

	TEST_CASE("manual reconnect stops and asks the caller to start again")
	{
		FakeClock clock;
		DestinationStateMachine machine(clock);
		machine.requestStart(true);
		machine.outputStarted();

		REQUIRE(machine.requestReconnect());
		CHECK(machine.runtime().phase == DestinationPhase::Stopping);
		CHECK(machine.runtime().restartPending);

		const auto outcome = machine.outputStopped(StopReason::UserStopped, {});
		CHECK(outcome.changed);
		CHECK(outcome.restart);
		CHECK(machine.runtime().phase == DestinationPhase::Idle);
		CHECK(machine.runtime().privateTest); // The restart keeps the test flag.
		CHECK_FALSE(machine.runtime().restartPending);
	}

	TEST_CASE("pressing stop after asking for a reconnect cancels the restart")
	{
		FakeClock clock;
		DestinationStateMachine machine(clock);
		machine.requestStart(false);
		machine.outputStarted();
		machine.requestReconnect();

		// The output is already stopping, so there is nothing new to stop.
		CHECK_FALSE(machine.requestStop());
		CHECK(machine.runtime().restartPending);

		// The output manager cancels the queued restart when Stop is pressed again.
		CHECK(machine.cancelRestart());
		CHECK_FALSE(machine.cancelRestart());
		const auto outcome = machine.outputStopped(StopReason::UserStopped, {});
		CHECK(outcome.changed);
		CHECK_FALSE(outcome.restart);
		CHECK(machine.runtime().phase == DestinationPhase::Idle);
	}

	TEST_CASE("reconnect is only possible while live or reconnecting")
	{
		FakeClock clock;
		DestinationStateMachine machine(clock);
		CHECK_FALSE(machine.requestReconnect());
		machine.requestStart(false);
		CHECK_FALSE(machine.requestReconnect());
		machine.outputStarted();
		CHECK(machine.requestReconnect());
	}

	TEST_CASE("signals that arrive out of order are ignored")
	{
		FakeClock clock;
		DestinationStateMachine machine(clock);

		// Nothing is running: every output signal is stale.
		CHECK_FALSE(machine.outputStarted());
		CHECK_FALSE(machine.outputReconnecting(1));
		CHECK_FALSE(machine.outputReconnected());
		CHECK_FALSE(machine.outputStopped(StopReason::Error, error("stale")).changed);
		CHECK(machine.runtime().phase == DestinationPhase::Idle);

		// "started" after Stop was pressed must not bring the card back to live.
		machine.requestStart(false);
		machine.requestStop();
		CHECK_FALSE(machine.outputStarted());
		CHECK(machine.runtime().phase == DestinationPhase::Stopping);

		// "reconnected" without a reconnect in progress is ignored.
		machine.outputStopped(StopReason::UserStopped, {});
		machine.requestStart(false);
		machine.outputStarted();
		CHECK_FALSE(machine.outputReconnected());
		CHECK(machine.runtime().phase == DestinationPhase::Live);

		// A second stop signal for the same run changes nothing.
		machine.outputStopped(StopReason::Error, error("first"));
		CHECK(machine.runtime().phase == DestinationPhase::Failed);
		CHECK_FALSE(machine.outputStopped(StopReason::Disconnected, error("second")).changed);
		CHECK(machine.runtime().error.what == "first");
	}

	TEST_CASE("an output error while live ends in Failed")
	{
		FakeClock clock;
		DestinationStateMachine machine(clock);
		machine.requestStart(false);
		machine.outputStarted();
		machine.outputStopped(StopReason::EncodeError, error("The video encoder failed."));
		CHECK(machine.runtime().phase == DestinationPhase::Failed);
		CHECK(machine.runtime().lastStop == StopReason::EncodeError);
	}

	TEST_CASE("active() covers every phase with a running output")
	{
		DestinationRuntime runtime;
		for (DestinationPhase phase : {DestinationPhase::Starting, DestinationPhase::Live,
					       DestinationPhase::Reconnecting, DestinationPhase::Stopping}) {
			runtime.phase = phase;
			CAPTURE(destinationPhaseName(phase));
			CHECK(runtime.active());
		}
		for (DestinationPhase phase : {DestinationPhase::Idle, DestinationPhase::Failed}) {
			runtime.phase = phase;
			CHECK_FALSE(runtime.active());
		}
	}
}

TEST_SUITE("encoders.settings")
{
	TEST_CASE("x264 gets bitrate, rate control, keyframe interval, preset and a fixed-keyframe option")
	{
		EffectiveVideo video;
		video.encoderId = "obs_x264";
		video.bitrateKbps = 4500;
		video.rateControl = "CBR";
		video.preset = "veryfast";
		video.keyframeIntervalSec = 2;

		const auto settings = buildVideoEncoderSettings(video, rdtest::x264Caps());
		REQUIRE(findSetting(settings, "bitrate"));
		CHECK(findSetting(settings, "bitrate")->intValue == 4500);
		CHECK(findSetting(settings, "rate_control")->stringValue == "CBR");
		CHECK(findSetting(settings, "keyint_sec")->intValue == 2);
		CHECK(findSetting(settings, "preset")->stringValue == "veryfast");
		CHECK(findSetting(settings, "x264opts")->stringValue == "scenecut=0");
		CHECK(findSetting(settings, "profile") == nullptr); // empty profile is not written
		CHECK(findSetting(settings, "bf") == nullptr);      // x264 has no "bf" setting
	}

	TEST_CASE("x264 options: user values win and B-frames go through the option string")
	{
		CHECK(mergeX264Options("", -1) == "scenecut=0");
		CHECK(mergeX264Options("", 2) == "scenecut=0 bframes=2");
		CHECK(mergeX264Options("  ref=3  ", 0) == "ref=3 scenecut=0 bframes=0");
		CHECK(mergeX264Options("scenecut=40 bframes=1", 3) == "scenecut=40 bframes=1");
		CHECK(mergeX264Options("ref=3:scenecut=40", -1) == "ref=3:scenecut=40");
		CHECK(mergeX264Options("SCENECUT=0", -1) == "SCENECUT=0");
	}

	TEST_CASE("hardware encoders get their own B-frame key and options only when set")
	{
		EffectiveVideo video;
		video.encoderId = "h264_texture_amf";
		video.bitrateKbps = 6000;
		video.rateControl = "CBR";
		video.preset = "balanced";
		video.profile = "high";
		video.bFrames = 2;
		video.keyframeIntervalSec = 2;

		auto settings = buildVideoEncoderSettings(video, rdtest::amfCaps());
		CHECK(findSetting(settings, "bf")->intValue == 2);
		CHECK(findSetting(settings, "profile")->stringValue == "high");
		CHECK(findSetting(settings, "preset")->stringValue == "balanced");
		CHECK(findSetting(settings, "ffmpeg_opts") == nullptr);
		CHECK(findSetting(settings, "x264opts") == nullptr);
		CHECK(findSetting(settings, "max_bitrate") == nullptr);

		video.bFrames = -1;
		video.customOptions = "enforce_hrd=1";
		settings = buildVideoEncoderSettings(video, rdtest::amfCaps());
		CHECK(findSetting(settings, "bf") == nullptr);
		CHECK(findSetting(settings, "ffmpeg_opts")->stringValue == "enforce_hrd=1");

		settings = buildVideoEncoderSettings(video, rdtest::nvencCaps());
		CHECK(findSetting(settings, "max_bitrate")->intValue == 6000);
		CHECK(findSetting(settings, "opts")->stringValue == "enforce_hrd=1");
	}

	TEST_CASE("only settings the encoder has are written")
	{
		VideoEncoderCaps bare;
		bare.id = "mystery_encoder";
		bare.codec = "h264";
		bare.family = EncoderFamily::OtherSoftware;

		EffectiveVideo video;
		video.encoderId = bare.id;
		video.bitrateKbps = 3000;
		video.rateControl = "CBR";
		video.preset = "fast";
		video.profile = "main";
		video.bFrames = 2;
		video.customOptions = "a=b";

		const auto settings = buildVideoEncoderSettings(video, bare);
		REQUIRE(settings.size() == 1);
		CHECK(settings[0].key == "bitrate");
	}

	TEST_CASE("audio settings carry the bitrate")
	{
		EffectiveAudio audio;
		audio.encoderId = "ffmpeg_aac";
		audio.bitrateKbps = 160;
		const auto settings = buildAudioEncoderSettings(audio);
		REQUIRE(settings.size() == 1);
		CHECK(settings[0].key == "bitrate");
		CHECK(settings[0].intValue == 160);
	}
}
