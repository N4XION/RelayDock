// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "encoders/encoder_caps.h"
#include "performance/effective_settings.h"

#include <string>
#include <vector>

namespace rd {

// One entry of the settings handed to an OBS encoder.
struct EncoderSetting {
	enum class Kind { Int, String, Bool };

	std::string key;
	Kind kind = Kind::Int;
	long long intValue = 0;
	std::string stringValue;
	bool boolValue = false;

	static EncoderSetting integer(std::string key, long long value);
	static EncoderSetting text(std::string key, std::string value);
	static EncoderSetting boolean(std::string key, bool value);
};

// Turns effective settings into the key and value pairs an OBS encoder understands. Only
// keys the encoder has are written, which is why this needs the encoder's capabilities.
std::vector<EncoderSetting> buildVideoEncoderSettings(const EffectiveVideo &video, const VideoEncoderCaps &caps);
std::vector<EncoderSetting> buildAudioEncoderSettings(const EffectiveAudio &audio);

// Builds the x264 option string.
//   - Adds "scenecut=0" so keyframes land exactly on the keyframe interval, which every
//     platform asks for. A scenecut value in `custom` wins.
//   - Adds "bframes=N" when `bFrames` is 0 or more. A bframes value in `custom` wins.
std::string mergeX264Options(const std::string &custom, int bFrames);

} // namespace rd
