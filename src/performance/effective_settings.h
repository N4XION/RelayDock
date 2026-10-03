// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "core/types.h"
#include "encoders/encoder_caps.h"
#include "encoders/video_math.h"
#include "providers/provider.h"
#include "settings/app_config.h"

#include <string>
#include <vector>

namespace rd {

// Works out the settings a destination really streams with.
//
// A destination's saved settings are a starting point. What it streams with also depends on:
//   - the performance mode,
//   - which settings the user locked,
//   - the platform's limits,
//   - the encoders this PC has,
//   - the OBS canvas size and frame rate,
//   - reductions the automatic optimiser has applied.
//
// How locks and modes combine:
//   Locked setting   The saved value is used as it is. Modes and the optimiser never touch it.
//   Unlocked setting The performance mode picks the value, within the platform's limits.
//   Custom mode      Every saved value is used as it is. Only the optimiser may still reduce
//                    unlocked settings.
//
// Encoder sharing follows from this. Potato and Balanced give every unlocked destination on
// the same canvas the same resolution, frame rate and bitrate, so their settings match and
// one encoder serves them all. Quality lets each destination use its platform's own limit,
// which can need more encoders.
//
// The result depends on the destinations passed in, not on which ones are live. Pass every
// enabled destination, so a destination started later resolves to the same values and can
// join an encoder that is already running.

// A reduction requested by the automatic optimiser. Applies to unlocked settings only.
struct Adjustment {
	int maxLines = 0;         // Cap on the shorter side of the frame. 0 means no cap.
	int maxFps = 0;           // Cap on the frame rate. 0 means no cap.
	int bitratePercent = 100; // Share of the normal bitrate to use.

	bool operator==(const Adjustment &other) const = default;
	bool none() const { return maxLines == 0 && maxFps == 0 && bitratePercent == 100; }
};

struct EffectiveVideo {
	Orientation orientation = Orientation::Horizontal;
	std::string verticalLayoutId; // Set for vertical destinations

	int width = 0;
	int height = 0;
	int fpsDivisor = 1;
	double fps = 0.0;

	std::string encoderId; // Empty when this PC has no encoder the output accepts
	int bitrateKbps = 0;
	std::string rateControl;
	std::string preset;  // Empty means the encoder default
	std::string profile; // Empty means the encoder default
	int keyframeIntervalSec = 2;
	int bFrames = -1; // -1 means the encoder default
	std::string customOptions;

	bool operator==(const EffectiveVideo &other) const = default;
};

struct EffectiveAudio {
	std::string encoderId;
	int bitrateKbps = 0;
	int track = 1;

	bool operator==(const EffectiveAudio &other) const = default;
};

struct EffectiveDestination {
	std::string id;
	EffectiveVideo video;
	EffectiveAudio audio;

	// Why a value differs from what the user saved. Ready to show.
	std::vector<std::string> notes;

	bool usable() const { return !video.encoderId.empty() && !audio.encoderId.empty(); }
};

struct ResolveInput {
	DestinationConfig config;
	const IProvider *provider = nullptr; // nullptr for an unknown provider: no platform limits
	Adjustment adjustment;
};

std::vector<EffectiveDestination> resolveEffectiveSettings(const std::vector<ResolveInput> &inputs,
							   const StreamContext &context, PerformanceMode mode);

// ---- Building blocks, exposed for the interface and for tests --------------------------------

struct ModeLimits {
	int maxLines = 0; // 0 means no cap
	int maxFps = 0;
};

ModeLimits modeLimits(PerformanceMode mode);

// RelayDock's own bitrate ladder. These are starting points, not platform rules.
int targetBitrateKbps(Size size, double fps, PerformanceMode mode);

// The encoder RelayDock picks when the user leaves the choice to it: a hardware H.264 encoder
// when the PC has one, otherwise x264. Empty when nothing usable exists.
std::string pickVideoEncoder(const StreamContext &context);
std::string pickAudioEncoder(const StreamContext &context);

// The preset a mode uses for an encoder. Empty when the encoder has no matching preset.
std::string modePreset(const VideoEncoderCaps &caps, PerformanceMode mode);

} // namespace rd
