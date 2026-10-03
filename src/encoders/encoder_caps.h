// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <string>
#include <vector>

namespace rd {

// What RelayDock knows about an encoder that OBS offers on this PC.
//
// The plugin fills these in by asking OBS for each encoder's property list (EncoderCatalog).
// Nothing is hard-coded per encoder beyond the family name, so the interface only shows
// controls the encoder really has. Tests build these by hand.

enum class EncoderFamily { X264, Nvenc, Amf, Qsv, OtherSoftware, OtherHardware };

const char *encoderFamilyName(EncoderFamily family);

// Guesses the family from an OBS encoder id such as "obs_x264" or "h264_texture_amf".
EncoderFamily encoderFamilyFromId(const std::string &id);

struct VideoEncoderCaps {
	std::string id;          // OBS encoder id
	std::string displayName; // Name OBS shows, already localised
	std::string codec;       // "h264", "hevc" or "av1"
	EncoderFamily family = EncoderFamily::OtherSoftware;
	bool hardware = false;

	std::vector<std::string> rateControls; // Values of the "rate_control" list. Empty when absent.

	std::string presetKey; // Setting that holds the preset: "preset", "target_usage", or empty
	std::vector<std::string> presets;

	std::vector<std::string> profiles; // Values of the "profile" list. Empty when absent.

	std::string bFramesKey; // "bf", "bframes", or empty when B-frames are not a setting
	int maxBFrames = 0;

	bool hasKeyframeInterval = false; // Has a "keyint_sec" setting
	int maxKeyframeIntervalSec = 0;

	std::string optionsKey; // Free-form options setting: "x264opts", "opts", "ffmpeg_opts", or empty

	bool dynamicBitrate = false; // Bitrate can change while the encoder runs

	bool supportsRateControl(const std::string &value) const;
	bool supportsPreset(const std::string &value) const;
	bool supportsProfile(const std::string &value) const;
};

struct AudioEncoderCaps {
	std::string id;
	std::string displayName;
	std::string codec; // "aac" or "opus"
};

// Video timing and size of a canvas.
struct CanvasInfo {
	int baseWidth = 1920;   // Size the scene is composed at
	int baseHeight = 1080;
	int outputWidth = 1920; // Size OBS scales to by default
	int outputHeight = 1080;
	int fpsNum = 30;
	int fpsDen = 1;

	double fps() const { return fpsDen > 0 ? static_cast<double>(fpsNum) / fpsDen : 0.0; }
};

// Everything the settings resolver needs to know about the running OBS.
struct StreamContext {
	CanvasInfo horizontal; // The OBS main canvas
	CanvasInfo vertical;   // RelayDock's vertical canvas. Shares the OBS frame rate.
	std::vector<VideoEncoderCaps> videoEncoders;
	std::vector<AudioEncoderCaps> audioEncoders;
	std::vector<std::string> outputVideoCodecs = {"h264"}; // Codecs the RTMP output accepts
	std::vector<std::string> outputAudioCodecs = {"aac"};

	const VideoEncoderCaps *findVideoEncoder(const std::string &id) const;
	const AudioEncoderCaps *findAudioEncoder(const std::string &id) const;
};

} // namespace rd
