// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include <doctest/doctest.h>

#include "encoders/encode_plan.h"
#include "encoders/video_math.h"
#include "performance/effective_settings.h"
#include "test_helpers.h"

#include <cmath>

using namespace rd;
using rdtest::Providers;

namespace {

const EffectiveDestination &byId(const std::vector<EffectiveDestination> &all, const std::string &id)
{
	for (const EffectiveDestination &destination : all) {
		if (destination.id == id)
			return destination;
	}
	FAIL("destination not found");
	return all.front();
}

} // namespace

TEST_SUITE("encoders.video_math")
{
	TEST_CASE("scaling keeps the canvas aspect ratio and even dimensions")
	{
		const CanvasInfo wide{1920, 1080, 1920, 1080, 60, 1};
		CHECK(scaleCanvasToLines(wide, 720) == Size{1280, 720});
		CHECK(scaleCanvasToLines(wide, 480) == Size{854, 480});
		CHECK(scaleCanvasToLines(wide, 360) == Size{640, 360});
		CHECK(scaleCanvasToLines(wide, 1080) == Size{1920, 1080});
		CHECK(scaleCanvasToLines(wide, 2160) == Size{1920, 1080}); // never larger than the canvas

		const CanvasInfo tall{1080, 1920, 1080, 1920, 60, 1};
		CHECK(scaleCanvasToLines(tall, 720) == Size{720, 1280});
		CHECK(scaleCanvasToLines(tall, 540) == Size{540, 960});

		const CanvasInfo ultrawide{3440, 1440, 3440, 1440, 60, 1};
		const Size scaled = scaleCanvasToLines(ultrawide, 1080);
		CHECK(scaled.height == 1080);
		CHECK(scaled.width % 2 == 0);
		CHECK(keepsCanvasAspect(ultrawide, scaled));
	}

	TEST_CASE("a size with another aspect ratio is detected, so nothing gets stretched")
	{
		const CanvasInfo wide{1920, 1080, 1920, 1080, 60, 1};
		CHECK(keepsCanvasAspect(wide, {1280, 720}));
		CHECK(keepsCanvasAspect(wide, {854, 480}));
		CHECK(keepsCanvasAspect(wide, {1366, 768}));
		CHECK_FALSE(keepsCanvasAspect(wide, {1080, 1920}));
		CHECK_FALSE(keepsCanvasAspect(wide, {1440, 1080}));
		CHECK_FALSE(keepsCanvasAspect(wide, {1280, 1024}));
		CHECK_FALSE(keepsCanvasAspect(wide, {0, 0}));

		// The 16:9 picture is never squeezed into 9:16.
		CHECK(nearestCanvasSize(wide, {1080, 1920}) == Size{1920, 1080});
		CHECK(nearestCanvasSize(wide, {720, 1280}) == Size{1280, 720});
		CHECK(nearestCanvasSize(wide, {0, 0}) == Size{1920, 1080});
	}

	TEST_CASE("resolution choices are unique, sorted and all keep the aspect ratio")
	{
		const CanvasInfo canvas{1920, 1080, 1280, 720, 60, 1};
		const std::vector<Size> choices = resolutionChoices(canvas);
		REQUIRE(choices.size() >= 4);
		CHECK(choices.front() == Size{1920, 1080});
		for (size_t i = 0; i < choices.size(); ++i) {
			CAPTURE(choices[i].width);
			CHECK(keepsCanvasAspect(canvas, choices[i]));
			CHECK(choices[i].width % 2 == 0);
			CHECK(choices[i].height % 2 == 0);
			if (i > 0) {
				CHECK(static_cast<long long>(choices[i].width) * choices[i].height <
				      static_cast<long long>(choices[i - 1].width) * choices[i - 1].height);
			}
		}
	}

	TEST_CASE("frame rates are whole divisions of the OBS frame rate")
	{
		const CanvasInfo sixty{1920, 1080, 1920, 1080, 60, 1};
		CHECK(fpsChoices(sixty) == std::vector<int>{60, 30, 20, 15, 12, 10});
		CHECK(divisorForTargetFps(sixty, 60) == 1);
		CHECK(divisorForTargetFps(sixty, 30) == 2);
		CHECK(divisorForTargetFps(sixty, 0) == 1);
		CHECK(divisorForTargetFps(sixty, 144) == 1);
		CHECK(divisorForTargetFps(sixty, 25) == 2); // 30 is the closest available rate
		CHECK(divisorForMaxFps(sixty, 30) == 2);
		CHECK(divisorForMaxFps(sixty, 60) == 1);
		CHECK(divisorForMaxFps(sixty, 0) == 1);

		const CanvasInfo thirty{1920, 1080, 1920, 1080, 30, 1};
		CHECK(fpsChoices(thirty) == std::vector<int>{30, 15, 10});
		CHECK(divisorForTargetFps(thirty, 60) == 1); // 60 is not possible on a 30 FPS canvas
		CHECK(divisorForMaxFps(thirty, 60) == 1);

		const CanvasInfo ntsc{1920, 1080, 1920, 1080, 60000, 1001};
		CHECK(fpsChoices(ntsc).front() == 60);
		CHECK(divisorForMaxFps(ntsc, 60) == 1); // 59.94 counts as 60
		CHECK(divisorForMaxFps(ntsc, 30) == 2);
		CHECK(std::abs(fpsForDivisor(ntsc, 2) - 29.97) < 0.01);
	}
}

TEST_SUITE("performance.effective_settings")
{
	TEST_CASE("the spec example: three compatible platforms end up with identical settings")
	{
		Providers p;
		const StreamContext context = rdtest::softwareContext(30);
		const std::vector<ResolveInput> inputs = {p.input("twitch"), p.input("youtube"), p.input("facebook")};

		const auto effective = resolveEffectiveSettings(inputs, context, PerformanceMode::Balanced);
		REQUIRE(effective.size() == 3);
		for (const EffectiveDestination &destination : effective) {
			CHECK(destination.video.width == 1920);
			CHECK(destination.video.height == 1080);
			CHECK(destination.video.fpsDivisor == 1);
			CHECK(destination.video.encoderId == "obs_x264");
			CHECK(destination.video.rateControl == "CBR");
			CHECK(destination.video.keyframeIntervalSec == 2);
			CHECK(destination.usable());
		}
		CHECK(effective[0].video == effective[1].video);
		CHECK(effective[1].video == effective[2].video);

		const EncodePlan plan = planEncoders(effective);
		CHECK(plan.videoEncoderCount() == 1);
		CHECK(plan.encodesSaved() == 2);
		CHECK(plan.groups.front().shared());
	}

	TEST_CASE("Balanced never exceeds the tightest platform limit in a shared group")
	{
		Providers p;
		const StreamContext context = rdtest::softwareContext(60);
		const auto effective = resolveEffectiveSettings({p.input("twitch"), p.input("youtube")}, context,
								PerformanceMode::Balanced);
		// Twitch accepts at most 6000 Kbps, so the shared encode uses 6000.
		CHECK(effective[0].video.bitrateKbps <= 6000);
		CHECK(effective[0].video.bitrateKbps == effective[1].video.bitrateKbps);
	}

	TEST_CASE("Quality lets each platform use its own limit, which splits the encoders")
	{
		Providers p;
		const StreamContext context = rdtest::softwareContext(60);
		const std::vector<ResolveInput> inputs = {p.input("twitch"), p.input("youtube"), p.input("facebook")};
		const auto effective = resolveEffectiveSettings(inputs, context, PerformanceMode::Quality);

		CHECK(effective[0].video.bitrateKbps == 6000); // Twitch limit
		CHECK(effective[1].video.bitrateKbps == 9000); // RelayDock's 1080p60 Quality target
		CHECK(effective[2].video.bitrateKbps == 9000); // Facebook limit equals the target

		const EncodePlan plan = planEncoders(effective);
		CHECK(plan.videoEncoderCount() == 2);
		CHECK(plan.sharedWith(inputs[1].config.id) == std::vector<std::string>{inputs[2].config.id});
		CHECK(plan.sharedWith(inputs[0].config.id).empty());
	}

	TEST_CASE("Potato caps at 720p30, uses the fastest preset and shares one encoder")
	{
		Providers p;
		const StreamContext context = rdtest::softwareContext(60);
		const std::vector<ResolveInput> inputs = {p.input("twitch"), p.input("youtube"), p.input("facebook")};
		const auto effective = resolveEffectiveSettings(inputs, context, PerformanceMode::Potato);

		for (const EffectiveDestination &destination : effective) {
			CHECK(destination.video.width == 1280);
			CHECK(destination.video.height == 720);
			CHECK(destination.video.fpsDivisor == 2);
			CHECK(destination.video.fps == doctest::Approx(30.0));
			CHECK(destination.video.preset == "superfast");
			CHECK(destination.video.bitrateKbps == 2500);
		}
		CHECK(planEncoders(effective).videoEncoderCount() == 1);
	}

	TEST_CASE("a hardware encoder is picked when the PC has one")
	{
		Providers p;
		const StreamContext amd = rdtest::amdContext();
		const auto effective = resolveEffectiveSettings({p.input("twitch")}, amd, PerformanceMode::Balanced);
		CHECK(effective[0].video.encoderId == "h264_texture_amf");
		CHECK(effective[0].video.preset == "balanced");

		StreamContext nvidia = rdtest::softwareContext();
		nvidia.videoEncoders = {rdtest::x264Caps(), rdtest::amfCaps(), rdtest::nvencCaps()};
		CHECK(pickVideoEncoder(nvidia) == "obs_nvenc_h264_tex");

		CHECK(pickVideoEncoder(rdtest::softwareContext()) == "obs_x264");
	}

	TEST_CASE("mode presets exist for every encoder family and mode")
	{
		CHECK(modePreset(rdtest::x264Caps(), PerformanceMode::Potato) == "superfast");
		CHECK(modePreset(rdtest::x264Caps(), PerformanceMode::Balanced) == "veryfast");
		CHECK(modePreset(rdtest::x264Caps(), PerformanceMode::Quality) == "faster");
		CHECK(modePreset(rdtest::amfCaps(), PerformanceMode::Potato) == "speed");
		CHECK(modePreset(rdtest::amfCaps(), PerformanceMode::Quality) == "quality");
		CHECK(modePreset(rdtest::nvencCaps(), PerformanceMode::Potato) == "p3");
		CHECK(modePreset(rdtest::nvencCaps(), PerformanceMode::Balanced) == "p5");
		CHECK(modePreset(rdtest::nvencCaps(), PerformanceMode::Quality) == "p6");

		// An encoder without that preset name gets its own default.
		VideoEncoderCaps odd = rdtest::x264Caps();
		odd.presets = {"turbo"};
		CHECK(modePreset(odd, PerformanceMode::Balanced).empty());
	}

	TEST_CASE("no usable encoder means the destination is not usable")
	{
		Providers p;
		StreamContext context = rdtest::softwareContext();
		context.videoEncoders.clear();
		const auto effective = resolveEffectiveSettings({p.input("twitch")}, context, PerformanceMode::Balanced);
		CHECK(effective[0].video.encoderId.empty());
		CHECK_FALSE(effective[0].usable());
		CHECK(planEncoders(effective).groups.empty());
	}

	TEST_CASE("locked settings are used as saved and are never changed by a mode")
	{
		Providers p;
		const StreamContext context = rdtest::amdContext(60);

		ResolveInput twitch = p.input("twitch");
		twitch.config.locks.resolution = true;
		twitch.config.video.width = 1920;
		twitch.config.video.height = 1080;
		twitch.config.locks.fps = true;
		twitch.config.video.fps = 60;
		twitch.config.locks.bitrate = true;
		twitch.config.video.bitrateKbps = 5800;
		twitch.config.locks.encoder = true;
		twitch.config.video.encoder = "obs_x264";
		twitch.config.video.preset = "medium";
		twitch.config.video.profile = "high";
		twitch.config.video.rateControl = "VBR";

		for (PerformanceMode mode : {PerformanceMode::Potato, PerformanceMode::Balanced, PerformanceMode::Quality,
					     PerformanceMode::Custom}) {
			CAPTURE(performanceModeName(mode));
			const auto effective = resolveEffectiveSettings({twitch, p.input("youtube")}, context, mode);
			const EffectiveVideo &video = effective[0].video;
			CHECK(video.width == 1920);
			CHECK(video.height == 1080);
			CHECK(video.fpsDivisor == 1);
			CHECK(video.bitrateKbps == 5800);
			CHECK(video.encoderId == "obs_x264");
			CHECK(video.preset == "medium");
			CHECK(video.profile == "high");
			CHECK(video.rateControl == "VBR");
		}
	}

	TEST_CASE("the optimiser's reductions apply to unlocked settings only")
	{
		Providers p;
		const StreamContext context = rdtest::softwareContext(60);

		ResolveInput open = p.input("youtube");
		open.adjustment.maxFps = 30;
		open.adjustment.maxLines = 720;
		open.adjustment.bitratePercent = 50;

		ResolveInput locked = open;
		locked.config.id = generateUuid();
		locked.config.locks.resolution = true;
		locked.config.locks.fps = true;
		locked.config.locks.bitrate = true;
		locked.config.video.width = 1920;
		locked.config.video.height = 1080;
		locked.config.video.fps = 60;
		locked.config.video.bitrateKbps = 9000;

		const auto effective = resolveEffectiveSettings({open, locked}, context, PerformanceMode::Quality);

		CHECK(effective[0].video.height == 720);
		CHECK(effective[0].video.fpsDivisor == 2);
		CHECK(effective[0].video.bitrateKbps == 2000); // half of the 720p30 Quality target of 4000

		CHECK(effective[1].video.height == 1080);
		CHECK(effective[1].video.fpsDivisor == 1);
		CHECK(effective[1].video.bitrateKbps == 9000);
	}

	TEST_CASE("Custom mode uses saved values, and the optimiser may still reduce unlocked ones")
	{
		Providers p;
		const StreamContext context = rdtest::softwareContext(60);

		ResolveInput twitch = p.input("twitch");
		twitch.config.video.width = 1280;
		twitch.config.video.height = 720;
		twitch.config.video.fps = 30;
		twitch.config.video.bitrateKbps = 3200;
		twitch.config.video.encoder = "obs_x264";
		twitch.config.video.preset = "fast";

		auto effective = resolveEffectiveSettings({twitch}, context, PerformanceMode::Custom);
		CHECK(effective[0].video.width == 1280);
		CHECK(effective[0].video.height == 720);
		CHECK(effective[0].video.fpsDivisor == 2);
		CHECK(effective[0].video.bitrateKbps == 3200);
		CHECK(effective[0].video.preset == "fast");

		// No saved preset in Custom mode means the encoder's own default.
		twitch.config.video.preset.clear();
		effective = resolveEffectiveSettings({twitch}, context, PerformanceMode::Custom);
		CHECK(effective[0].video.preset.empty());

		twitch.adjustment.bitratePercent = 75;
		effective = resolveEffectiveSettings({twitch}, context, PerformanceMode::Custom);
		CHECK(effective[0].video.bitrateKbps == 2400);

		twitch.config.locks.bitrate = true;
		effective = resolveEffectiveSettings({twitch}, context, PerformanceMode::Custom);
		CHECK(effective[0].video.bitrateKbps == 3200);
	}

	TEST_CASE("a saved size with the wrong aspect ratio is corrected, never stretched")
	{
		Providers p;
		const StreamContext context = rdtest::softwareContext(60);

		ResolveInput twitch = p.input("twitch");
		twitch.config.locks.resolution = true;
		twitch.config.video.width = 1080;
		twitch.config.video.height = 1920; // vertical size on the horizontal canvas

		const auto effective = resolveEffectiveSettings({twitch}, context, PerformanceMode::Balanced);
		CHECK(effective[0].video.width == 1920);
		CHECK(effective[0].video.height == 1080);
		CHECK_FALSE(effective[0].notes.empty());
	}

	TEST_CASE("horizontal Twitch and vertical TikTok run at the same time on separate canvases")
	{
		Providers p;
		const StreamContext context = rdtest::softwareContext(60);
		ResolveInput twitch = p.input("twitch");
		ResolveInput tiktok = p.input("tiktok");
		tiktok.config.verticalLayoutId = generateUuid();

		const auto effective = resolveEffectiveSettings({twitch, tiktok}, context, PerformanceMode::Balanced);
		const EffectiveVideo &horizontal = effective[0].video;
		const EffectiveVideo &vertical = effective[1].video;

		CHECK(horizontal.orientation == Orientation::Horizontal);
		CHECK(horizontal.width == 1920);
		CHECK(horizontal.height == 1080);

		CHECK(vertical.orientation == Orientation::Vertical);
		CHECK(vertical.width == 1080);
		CHECK(vertical.height == 1920);
		CHECK(vertical.verticalLayoutId == tiktok.config.verticalLayoutId);
		CHECK(vertical.fps == doctest::Approx(30.0)); // TikTok's own guides use 30 FPS
		CHECK(vertical.bitrateKbps <= 5400);

		// Different canvases can never share an encoder.
		const EncodePlan plan = planEncoders(effective);
		CHECK(plan.videoEncoderCount() == 2);
		CHECK(plan.encodesSaved() == 0);
	}

	TEST_CASE("with the aspect ratio unlocked the platform's preferred orientation is used")
	{
		Providers p;
		const StreamContext context = rdtest::softwareContext(60);
		ResolveInput tiktok = p.input("tiktok");
		tiktok.config.video.orientation = Orientation::Horizontal;

		tiktok.config.locks.aspectRatio = true;
		auto effective = resolveEffectiveSettings({tiktok}, context, PerformanceMode::Balanced);
		CHECK(effective[0].video.orientation == Orientation::Horizontal);

		tiktok.config.locks.aspectRatio = false;
		effective = resolveEffectiveSettings({tiktok}, context, PerformanceMode::Balanced);
		CHECK(effective[0].video.orientation == Orientation::Vertical);
		CHECK(effective[0].video.height > effective[0].video.width);
	}

	TEST_CASE("a frame rate the canvas cannot produce is explained")
	{
		Providers p;
		const StreamContext context = rdtest::softwareContext(30);
		ResolveInput twitch = p.input("twitch");
		twitch.config.locks.fps = true;
		twitch.config.video.fps = 60;

		const auto effective = resolveEffectiveSettings({twitch}, context, PerformanceMode::Balanced);
		CHECK(effective[0].video.fpsDivisor == 1);
		CHECK(effective[0].video.fps == doctest::Approx(30.0));
		REQUIRE_FALSE(effective[0].notes.empty());
		CHECK(effective[0].notes.front().find("30 FPS") != std::string::npos);
	}

	TEST_CASE("a locked encoder that is missing or uses a rejected codec falls back with a note")
	{
		Providers p;
		const StreamContext context = rdtest::amdContext();

		ResolveInput missing = p.input("twitch");
		missing.config.locks.encoder = true;
		missing.config.video.encoder = "obs_nvenc_h264_tex";
		auto effective = resolveEffectiveSettings({missing}, context, PerformanceMode::Balanced);
		CHECK(effective[0].video.encoderId == "h264_texture_amf");
		CHECK_FALSE(effective[0].notes.empty());

		// Twitch takes H.264 only over RTMP. YouTube also takes HEVC.
		ResolveInput twitchHevc = p.input("twitch");
		twitchHevc.config.locks.encoder = true;
		twitchHevc.config.video.encoder = "h265_texture_amf";
		effective = resolveEffectiveSettings({twitchHevc}, context, PerformanceMode::Balanced);
		CHECK(effective[0].video.encoderId == "h264_texture_amf");
		CHECK_FALSE(effective[0].notes.empty());

		ResolveInput youtubeHevc = p.input("youtube");
		youtubeHevc.config.locks.encoder = true;
		youtubeHevc.config.video.encoder = "h265_texture_amf";
		effective = resolveEffectiveSettings({youtubeHevc}, context, PerformanceMode::Balanced);
		CHECK(effective[0].video.encoderId == "h265_texture_amf");
		CHECK(effective[0].notes.empty());
	}

	TEST_CASE("OBS scaled output is respected: unlocked destinations do not upscale")
	{
		Providers p;
		StreamContext context = rdtest::softwareContext(60);
		context.horizontal.outputWidth = 1280;
		context.horizontal.outputHeight = 720;

		const auto effective = resolveEffectiveSettings({p.input("youtube")}, context, PerformanceMode::Quality);
		CHECK(effective[0].video.width == 1280);
		CHECK(effective[0].video.height == 720);
	}

	TEST_CASE("audio follows the saved settings within the platform limit")
	{
		Providers p;
		const StreamContext context = rdtest::softwareContext();
		ResolveInput twitch = p.input("twitch");
		twitch.config.audio.bitrateKbps = 320;
		twitch.config.audio.track = 2;

		auto effective = resolveEffectiveSettings({twitch}, context, PerformanceMode::Balanced);
		CHECK(effective[0].audio.encoderId == "ffmpeg_aac");
		CHECK(effective[0].audio.bitrateKbps == 160); // Twitch maximum
		CHECK(effective[0].audio.track == 2);
		CHECK_FALSE(effective[0].notes.empty());

		// Custom mode leaves the saved value alone.
		effective = resolveEffectiveSettings({twitch}, context, PerformanceMode::Custom);
		CHECK(effective[0].audio.bitrateKbps == 320);
	}

	TEST_CASE("results do not depend on which other destinations are live, only on the set given")
	{
		Providers p;
		const StreamContext context = rdtest::softwareContext(60);
		const ResolveInput twitch = p.input("twitch");
		const ResolveInput youtube = p.input("youtube");

		const auto together = resolveEffectiveSettings({twitch, youtube}, context, PerformanceMode::Balanced);
		const auto again = resolveEffectiveSettings({youtube, twitch}, context, PerformanceMode::Balanced);
		CHECK(byId(together, twitch.config.id).video == byId(again, twitch.config.id).video);
		CHECK(byId(together, youtube.config.id).video == byId(again, youtube.config.id).video);
	}

	TEST_CASE("the bitrate ladder rises with size, frame rate and mode")
	{
		for (PerformanceMode mode : {PerformanceMode::Potato, PerformanceMode::Balanced, PerformanceMode::Quality}) {
			CAPTURE(performanceModeName(mode));
			CHECK(targetBitrateKbps({1280, 720}, 30, mode) < targetBitrateKbps({1920, 1080}, 30, mode));
			CHECK(targetBitrateKbps({1920, 1080}, 30, mode) < targetBitrateKbps({1920, 1080}, 60, mode));
			CHECK(targetBitrateKbps({1080, 1920}, 30, mode) == targetBitrateKbps({1920, 1080}, 30, mode));
		}
		CHECK(targetBitrateKbps({1920, 1080}, 60, PerformanceMode::Potato) <
		      targetBitrateKbps({1920, 1080}, 60, PerformanceMode::Balanced));
		CHECK(targetBitrateKbps({1920, 1080}, 60, PerformanceMode::Balanced) <
		      targetBitrateKbps({1920, 1080}, 60, PerformanceMode::Quality));
	}
}

TEST_SUITE("encoders.plan")
{
	TEST_CASE("destinations share only when every encoder setting matches")
	{
		Providers p;
		const StreamContext context = rdtest::softwareContext(60);
		ResolveInput a = p.input("youtube");
		ResolveInput b = p.input("youtube");
		auto base = resolveEffectiveSettings({a, b}, context, PerformanceMode::Balanced);
		REQUIRE(planEncoders(base).videoEncoderCount() == 1);

		struct Change {
			const char *name;
			void (*apply)(EffectiveVideo &);
		};
		const Change changes[] = {
			{"width", [](EffectiveVideo &v) { v.width -= 2; }},
			{"height", [](EffectiveVideo &v) { v.height -= 2; }},
			{"fps divisor", [](EffectiveVideo &v) { v.fpsDivisor += 1; }},
			{"encoder", [](EffectiveVideo &v) { v.encoderId = "other"; }},
			{"bitrate", [](EffectiveVideo &v) { v.bitrateKbps += 50; }},
			{"rate control", [](EffectiveVideo &v) { v.rateControl = "VBR"; }},
			{"preset", [](EffectiveVideo &v) { v.preset = "slow"; }},
			{"profile", [](EffectiveVideo &v) { v.profile = "main"; }},
			{"keyframe interval", [](EffectiveVideo &v) { v.keyframeIntervalSec += 1; }},
			{"b-frames", [](EffectiveVideo &v) { v.bFrames = 0; }},
			{"options", [](EffectiveVideo &v) { v.customOptions = "x=1"; }},
			{"orientation", [](EffectiveVideo &v) { v.orientation = Orientation::Vertical; }},
			{"vertical layout", [](EffectiveVideo &v) { v.verticalLayoutId = "layout-b"; }},
		};
		for (const Change &change : changes) {
			CAPTURE(change.name);
			auto changed = base;
			change.apply(changed[1].video);
			CHECK(planEncoders(changed).videoEncoderCount() == 2);
			CHECK(videoSignature(changed[0].video) != videoSignature(changed[1].video));
		}
		CHECK(videoSignature(base[0].video) == videoSignature(base[1].video));
	}

	TEST_CASE("audio encoders are shared inside a video group when audio settings match")
	{
		Providers p;
		const StreamContext context = rdtest::softwareContext(60);
		ResolveInput a = p.input("youtube");
		ResolveInput b = p.input("youtube");
		ResolveInput c = p.input("youtube");
		c.config.audio.bitrateKbps = 192;

		const auto effective = resolveEffectiveSettings({a, b, c}, context, PerformanceMode::Balanced);
		const EncodePlan plan = planEncoders(effective);
		REQUIRE(plan.groups.size() == 1);
		REQUIRE(plan.groups[0].audio.size() == 2);
		CHECK(plan.groups[0].audio[0].destinationIds.size() == 2);
		CHECK(plan.groups[0].audio[1].destinationIds == std::vector<std::string>{c.config.id});
	}

	TEST_CASE("plan lookups")
	{
		Providers p;
		const StreamContext context = rdtest::softwareContext(60);
		ResolveInput a = p.input("twitch");
		ResolveInput b = p.input("youtube");
		const auto effective = resolveEffectiveSettings({a, b}, context, PerformanceMode::Balanced);
		const EncodePlan plan = planEncoders(effective);

		CHECK(plan.destinationCount() == 2);
		REQUIRE(plan.groupOf(a.config.id) != nullptr);
		CHECK(plan.groupOf(a.config.id) == plan.groupOf(b.config.id));
		CHECK(plan.groupOf("missing") == nullptr);
		CHECK(plan.sharedWith("missing").empty());
		CHECK(plan.sharedWith(a.config.id) == std::vector<std::string>{b.config.id});
	}

	TEST_CASE("an empty set of destinations gives an empty plan")
	{
		const EncodePlan plan = planEncoders({});
		CHECK(plan.groups.empty());
		CHECK(plan.encodesSaved() == 0);
		CHECK(plan.destinationCount() == 0);
	}
}
