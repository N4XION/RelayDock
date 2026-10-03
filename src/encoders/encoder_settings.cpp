// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "encoders/encoder_settings.h"

#include "utils/strings.h"

namespace rd {

EncoderSetting EncoderSetting::integer(std::string key, long long value)
{
	EncoderSetting setting;
	setting.key = std::move(key);
	setting.kind = Kind::Int;
	setting.intValue = value;
	return setting;
}

EncoderSetting EncoderSetting::text(std::string key, std::string value)
{
	EncoderSetting setting;
	setting.key = std::move(key);
	setting.kind = Kind::String;
	setting.stringValue = std::move(value);
	return setting;
}

EncoderSetting EncoderSetting::boolean(std::string key, bool value)
{
	EncoderSetting setting;
	setting.key = std::move(key);
	setting.kind = Kind::Bool;
	setting.boolValue = value;
	return setting;
}

namespace {

// True when the option string sets `name`, as in "name=value".
bool hasOption(const std::string &options, const std::string &name)
{
	for (const std::string &token : split(replaceAll(options, ":", " "), ' ')) {
		const size_t equals = token.find('=');
		const std::string key = equals == std::string::npos ? token : token.substr(0, equals);
		if (equalsNoCase(key, name))
			return true;
	}
	return false;
}

} // namespace

std::string mergeX264Options(const std::string &custom, int bFrames)
{
	std::string options = trim(custom);
	auto append = [&options](const std::string &option) {
		if (!options.empty())
			options += " ";
		options += option;
	};

	if (!hasOption(options, "scenecut"))
		append("scenecut=0");
	if (bFrames >= 0 && !hasOption(options, "bframes"))
		append("bframes=" + std::to_string(bFrames));
	return options;
}

std::vector<EncoderSetting> buildVideoEncoderSettings(const EffectiveVideo &video, const VideoEncoderCaps &caps)
{
	std::vector<EncoderSetting> settings;

	settings.push_back(EncoderSetting::integer("bitrate", video.bitrateKbps));

	if (!caps.rateControls.empty() && !video.rateControl.empty())
		settings.push_back(EncoderSetting::text("rate_control", video.rateControl));

	if (caps.hasKeyframeInterval)
		settings.push_back(EncoderSetting::integer("keyint_sec", video.keyframeIntervalSec));

	if (!caps.presetKey.empty() && !video.preset.empty())
		settings.push_back(EncoderSetting::text(caps.presetKey, video.preset));

	if (!caps.profiles.empty() && !video.profile.empty())
		settings.push_back(EncoderSetting::text("profile", video.profile));

	if (!caps.bFramesKey.empty() && video.bFrames >= 0)
		settings.push_back(EncoderSetting::integer(caps.bFramesKey, video.bFrames));

	if (caps.family == EncoderFamily::X264) {
		settings.push_back(EncoderSetting::text("x264opts", mergeX264Options(video.customOptions, video.bFrames)));
	} else if (!caps.optionsKey.empty() && !video.customOptions.empty()) {
		settings.push_back(EncoderSetting::text(caps.optionsKey, video.customOptions));
	}

	// QSV and NVENC take a separate ceiling for VBR. Keep it in step with the target so a
	// bitrate the user sets is the bitrate they get.
	if (caps.family == EncoderFamily::Qsv || caps.family == EncoderFamily::Nvenc)
		settings.push_back(EncoderSetting::integer("max_bitrate", video.bitrateKbps));

	return settings;
}

std::vector<EncoderSetting> buildAudioEncoderSettings(const EffectiveAudio &audio)
{
	return {EncoderSetting::integer("bitrate", audio.bitrateKbps)};
}

} // namespace rd
