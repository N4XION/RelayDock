// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "encoders/encoder_caps.h"
#include "settings/app_config.h"

#include <vector>

namespace rd {

// Asks OBS which encoders this PC has and what each one can be told.
//
// Everything comes from OBS at run time: the encoder list, and for each encoder the property
// list it publishes (rate controls, presets, profiles, B-frame range). RelayDock shows a
// control only when the encoder has the matching property.
//
// Thread ownership: OBS UI thread. Call refresh() after OBS finished loading its modules.
class EncoderCatalog {
public:
	void refresh();

	const std::vector<VideoEncoderCaps> &video() const { return video_; }
	const std::vector<AudioEncoderCaps> &audio() const { return audio_; }
	bool empty() const { return video_.empty(); }

private:
	std::vector<VideoEncoderCaps> video_;
	std::vector<AudioEncoderCaps> audio_;
};

// Canvas sizes, frame rate, encoders and accepted codecs of the running OBS.
StreamContext buildStreamContext(const EncoderCatalog &catalog, const VerticalCanvasConfig &verticalCanvas);

} // namespace rd
