// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "encoders/encoder_caps.h"
#include "performance/effective_settings.h"
#include "providers/provider_registry.h"
#include "utils/uuid.h"

#include <string>
#include <vector>

// Shared fixtures for unit tests. Encoder descriptions mirror what OBS Studio 32 reports for
// each encoder, so the logic under test sees realistic data without OBS running.
namespace rdtest {

inline rd::VideoEncoderCaps x264Caps()
{
	rd::VideoEncoderCaps caps;
	caps.id = "obs_x264";
	caps.displayName = "x264";
	caps.codec = "h264";
	caps.family = rd::EncoderFamily::X264;
	caps.hardware = false;
	caps.rateControls = {"CBR", "ABR", "VBR", "CRF"};
	caps.presetKey = "preset";
	caps.presets = {"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow", "placebo"};
	caps.profiles = {"", "baseline", "main", "high"};
	caps.hasKeyframeInterval = true;
	caps.maxKeyframeIntervalSec = 20;
	caps.optionsKey = "x264opts";
	caps.dynamicBitrate = true;
	return caps;
}

inline rd::VideoEncoderCaps amfCaps()
{
	rd::VideoEncoderCaps caps;
	caps.id = "h264_texture_amf";
	caps.displayName = "AMD HW H.264 (AVC)";
	caps.codec = "h264";
	caps.family = rd::EncoderFamily::Amf;
	caps.hardware = true;
	caps.rateControls = {"CBR", "CQP", "VBR", "VBR_LAT", "QVBR", "HQVBR", "HQCBR"};
	caps.presetKey = "preset";
	caps.presets = {"quality", "balanced", "speed"};
	caps.profiles = {"high", "main", "baseline"};
	caps.bFramesKey = "bf";
	caps.maxBFrames = 5;
	caps.hasKeyframeInterval = true;
	caps.maxKeyframeIntervalSec = 10;
	caps.optionsKey = "ffmpeg_opts";
	caps.dynamicBitrate = true;
	return caps;
}

inline rd::VideoEncoderCaps nvencCaps()
{
	rd::VideoEncoderCaps caps;
	caps.id = "obs_nvenc_h264_tex";
	caps.displayName = "NVIDIA NVENC H.264";
	caps.codec = "h264";
	caps.family = rd::EncoderFamily::Nvenc;
	caps.hardware = true;
	caps.rateControls = {"CBR", "CQP", "VBR", "CQVBR"};
	caps.presetKey = "preset";
	caps.presets = {"p1", "p2", "p3", "p4", "p5", "p6", "p7"};
	caps.profiles = {"high", "main", "baseline"};
	caps.bFramesKey = "bf";
	caps.maxBFrames = 4;
	caps.hasKeyframeInterval = true;
	caps.maxKeyframeIntervalSec = 10;
	caps.optionsKey = "opts";
	caps.dynamicBitrate = true;
	return caps;
}

inline rd::VideoEncoderCaps hevcAmfCaps()
{
	rd::VideoEncoderCaps caps = amfCaps();
	caps.id = "h265_texture_amf";
	caps.displayName = "AMD HW H.265 (HEVC)";
	caps.codec = "hevc";
	caps.presets = {"quality", "balanced", "speed"};
	return caps;
}

// A 1080p60 OBS with only the software encoder.
inline rd::StreamContext softwareContext(int fps = 60)
{
	rd::StreamContext context;
	context.horizontal = {1920, 1080, 1920, 1080, fps, 1};
	context.vertical = {1080, 1920, 1080, 1920, fps, 1};
	context.videoEncoders = {x264Caps()};
	context.audioEncoders = {{"ffmpeg_aac", "FFmpeg AAC", "aac"}};
	context.outputVideoCodecs = {"h264", "hevc", "av1"};
	return context;
}

// The same OBS on a PC with an AMD graphics chip.
inline rd::StreamContext amdContext(int fps = 60)
{
	rd::StreamContext context = softwareContext(fps);
	context.videoEncoders = {x264Caps(), amfCaps(), hevcAmfCaps()};
	return context;
}

struct Providers {
	rd::ProviderRegistry registry;
	Providers() { rd::registerBuiltInProviders(registry); }

	const rd::IProvider *get(const char *id) const { return registry.find(id); }

	// A new destination of the given provider, ready to resolve.
	rd::ResolveInput input(const char *providerId) const
	{
		rd::ResolveInput in;
		in.provider = registry.find(providerId);
		in.config = in.provider->newDestination();
		in.config.id = rd::generateUuid();
		if (in.provider->info().userSuppliesServer)
			in.config.serverUrl = "rtmp://ingest.example.net/live";
		return in;
	}
};

} // namespace rdtest
