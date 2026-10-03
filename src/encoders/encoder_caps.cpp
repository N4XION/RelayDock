// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "encoders/encoder_caps.h"

#include "utils/strings.h"

#include <algorithm>

namespace rd {

namespace {

bool containsValue(const std::vector<std::string> &values, const std::string &value)
{
	return std::any_of(values.begin(), values.end(),
			   [&](const std::string &candidate) { return equalsNoCase(candidate, value); });
}

} // namespace

const char *encoderFamilyName(EncoderFamily family)
{
	switch (family) {
	case EncoderFamily::X264:
		return "x264";
	case EncoderFamily::Nvenc:
		return "nvenc";
	case EncoderFamily::Amf:
		return "amf";
	case EncoderFamily::Qsv:
		return "qsv";
	case EncoderFamily::OtherSoftware:
		return "software";
	case EncoderFamily::OtherHardware:
		return "hardware";
	}
	return "software";
}

EncoderFamily encoderFamilyFromId(const std::string &id)
{
	if (containsNoCase(id, "x264"))
		return EncoderFamily::X264;
	if (containsNoCase(id, "nvenc"))
		return EncoderFamily::Nvenc;
	if (containsNoCase(id, "amf"))
		return EncoderFamily::Amf;
	if (containsNoCase(id, "qsv"))
		return EncoderFamily::Qsv;
	if (containsNoCase(id, "videotoolbox") || containsNoCase(id, "vaapi") || containsNoCase(id, "_hw"))
		return EncoderFamily::OtherHardware;
	return EncoderFamily::OtherSoftware;
}

bool VideoEncoderCaps::supportsRateControl(const std::string &value) const
{
	return containsValue(rateControls, value);
}

bool VideoEncoderCaps::supportsPreset(const std::string &value) const
{
	return containsValue(presets, value);
}

bool VideoEncoderCaps::supportsProfile(const std::string &value) const
{
	return containsValue(profiles, value);
}

const VideoEncoderCaps *StreamContext::findVideoEncoder(const std::string &id) const
{
	for (const VideoEncoderCaps &caps : videoEncoders) {
		if (caps.id == id)
			return &caps;
	}
	return nullptr;
}

const AudioEncoderCaps *StreamContext::findAudioEncoder(const std::string &id) const
{
	for (const AudioEncoderCaps &caps : audioEncoders) {
		if (caps.id == id)
			return &caps;
	}
	return nullptr;
}

} // namespace rd
