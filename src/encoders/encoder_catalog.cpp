// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "encoders/encoder_catalog.h"

#include "utils/log.h"
#include "utils/strings.h"

#include <obs.h>

namespace rd {

namespace {

constexpr const char *kStreamOutputId = "rtmp_output";

std::vector<std::string> listValues(obs_properties_t *properties, const char *name)
{
	std::vector<std::string> values;
	obs_property_t *property = obs_properties_get(properties, name);
	if (!property || obs_property_get_type(property) != OBS_PROPERTY_LIST ||
	    obs_property_list_format(property) != OBS_COMBO_FORMAT_STRING)
		return values;

	const size_t count = obs_property_list_item_count(property);
	for (size_t i = 0; i < count; ++i) {
		const char *value = obs_property_list_item_string(property, i);
		values.emplace_back(value ? value : "");
	}
	return values;
}

bool intRange(obs_properties_t *properties, const char *name, int &maximum)
{
	obs_property_t *property = obs_properties_get(properties, name);
	if (!property || obs_property_get_type(property) != OBS_PROPERTY_INT)
		return false;
	maximum = obs_property_int_max(property);
	return true;
}

bool hasText(obs_properties_t *properties, const char *name)
{
	obs_property_t *property = obs_properties_get(properties, name);
	return property && obs_property_get_type(property) == OBS_PROPERTY_TEXT;
}

VideoEncoderCaps describeVideoEncoder(const char *id, const char *codec, uint32_t flags)
{
	VideoEncoderCaps caps;
	caps.id = id;
	const char *name = obs_encoder_get_display_name(id);
	caps.displayName = name && *name ? name : id;
	caps.codec = toLower(codec);
	caps.family = encoderFamilyFromId(caps.id);
	caps.hardware = caps.family == EncoderFamily::Nvenc || caps.family == EncoderFamily::Amf ||
			caps.family == EncoderFamily::Qsv || caps.family == EncoderFamily::OtherHardware;
	caps.dynamicBitrate = (flags & OBS_ENCODER_CAP_DYN_BITRATE) != 0;

	obs_properties_t *properties = obs_get_encoder_properties(id);
	if (!properties)
		return caps;

	caps.rateControls = listValues(properties, "rate_control");

	caps.presets = listValues(properties, "preset");
	if (!caps.presets.empty()) {
		caps.presetKey = "preset";
	} else {
		caps.presets = listValues(properties, "target_usage");
		if (!caps.presets.empty())
			caps.presetKey = "target_usage";
	}

	caps.profiles = listValues(properties, "profile");

	int maximum = 0;
	if (intRange(properties, "bf", maximum)) {
		caps.bFramesKey = "bf";
		caps.maxBFrames = maximum;
	} else if (intRange(properties, "bframes", maximum)) {
		caps.bFramesKey = "bframes";
		caps.maxBFrames = maximum;
	}

	if (intRange(properties, "keyint_sec", maximum)) {
		caps.hasKeyframeInterval = true;
		caps.maxKeyframeIntervalSec = maximum;
	}

	for (const char *key : {"x264opts", "opts", "ffmpeg_opts"}) {
		if (hasText(properties, key)) {
			caps.optionsKey = key;
			break;
		}
	}

	obs_properties_destroy(properties);
	return caps;
}

std::vector<std::string> codecList(const char *semicolonList)
{
	std::vector<std::string> codecs;
	if (semicolonList) {
		for (const std::string &codec : split(semicolonList, ';'))
			codecs.push_back(toLower(codec));
	}
	return codecs;
}

} // namespace

void EncoderCatalog::refresh()
{
	video_.clear();
	audio_.clear();

	const char *id = nullptr;
	for (size_t index = 0; obs_enum_encoder_types(index, &id); ++index) {
		const uint32_t flags = obs_get_encoder_caps(id);
		// Deprecated encoders are old ids kept for existing profiles. Internal ones are
		// fallbacks OBS picks by itself. Neither should be offered.
		if (flags & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL))
			continue;

		const char *codec = obs_get_encoder_codec(id);
		if (!codec)
			continue;

		const obs_encoder_type type = obs_get_encoder_type(id);
		if (type == OBS_ENCODER_VIDEO) {
			if (equalsNoCase(codec, "h264") || equalsNoCase(codec, "hevc") || equalsNoCase(codec, "av1"))
				video_.push_back(describeVideoEncoder(id, codec, flags));
		} else if (type == OBS_ENCODER_AUDIO) {
			if (equalsNoCase(codec, "aac") || equalsNoCase(codec, "opus")) {
				AudioEncoderCaps caps;
				caps.id = id;
				const char *name = obs_encoder_get_display_name(id);
				caps.displayName = name && *name ? name : id;
				caps.codec = toLower(codec);
				audio_.push_back(std::move(caps));
			}
		}
	}

	std::string names;
	for (const VideoEncoderCaps &caps : video_)
		names += (names.empty() ? "" : ", ") + caps.id;
	logInfo("Found {} video encoder(s): {}", video_.size(), names.empty() ? "none" : names);
}

StreamContext buildStreamContext(const EncoderCatalog &catalog, const VerticalCanvasConfig &verticalCanvas)
{
	StreamContext context;

	obs_video_info ovi{};
	if (obs_get_video_info(&ovi)) {
		context.horizontal.baseWidth = static_cast<int>(ovi.base_width);
		context.horizontal.baseHeight = static_cast<int>(ovi.base_height);
		context.horizontal.outputWidth = static_cast<int>(ovi.output_width);
		context.horizontal.outputHeight = static_cast<int>(ovi.output_height);
		context.horizontal.fpsNum = static_cast<int>(ovi.fps_num);
		context.horizontal.fpsDen = static_cast<int>(ovi.fps_den ? ovi.fps_den : 1);
	}

	// The vertical canvas renders at its own size and always at the OBS frame rate.
	context.vertical.baseWidth = verticalCanvas.width;
	context.vertical.baseHeight = verticalCanvas.height;
	context.vertical.outputWidth = verticalCanvas.width;
	context.vertical.outputHeight = verticalCanvas.height;
	context.vertical.fpsNum = context.horizontal.fpsNum;
	context.vertical.fpsDen = context.horizontal.fpsDen;

	context.videoEncoders = catalog.video();
	context.audioEncoders = catalog.audio();

	context.outputVideoCodecs = codecList(obs_get_output_supported_video_codecs(kStreamOutputId));
	context.outputAudioCodecs = codecList(obs_get_output_supported_audio_codecs(kStreamOutputId));
	if (context.outputVideoCodecs.empty())
		context.outputVideoCodecs = {"h264"};
	if (context.outputAudioCodecs.empty())
		context.outputAudioCodecs = {"aac"};

	return context;
}

} // namespace rd
