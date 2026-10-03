// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "performance/effective_settings.h"

#include <string>
#include <vector>

namespace rd {

// Decides which destinations can share an encoder.
//
// OBS lets one encoder feed several outputs. That is safe when the outputs want exactly the
// same encoded video: same canvas, size, frame rate, encoder and encoder settings. The plan
// groups destinations by that exact match. It never rounds one destination's settings towards
// another's to force a match. Making settings equal is the performance mode's job, and it
// happens before the plan is built (see effective_settings.h).
//
// Audio encoders are shared inside a video group only. Encoding AAC costs almost nothing, and
// keeping audio encoders paired with one video encoder is the arrangement OBS itself uses.

struct AudioGroup {
	EffectiveAudio settings;
	std::vector<std::string> destinationIds;
};

struct EncoderGroup {
	EffectiveVideo settings;
	std::vector<std::string> destinationIds; // In the order the destinations were given
	std::vector<AudioGroup> audio;

	bool shared() const { return destinationIds.size() > 1; }
};

struct EncodePlan {
	std::vector<EncoderGroup> groups;

	// How many video encoders run.
	size_t videoEncoderCount() const { return groups.size(); }

	// How many video encodes sharing saves compared with one encoder per destination.
	size_t encodesSaved() const;

	size_t destinationCount() const;

	// The group a destination belongs to, or nullptr.
	const EncoderGroup *groupOf(const std::string &destinationId) const;

	// Ids of the other destinations that share this destination's video encoder.
	std::vector<std::string> sharedWith(const std::string &destinationId) const;
};

// Destinations that are not usable (no encoder available) are left out of the plan.
EncodePlan planEncoders(const std::vector<EffectiveDestination> &destinations);

// A stable text key for a video configuration. Equal keys mean the encoder can be shared.
std::string videoSignature(const EffectiveVideo &video);
std::string audioSignature(const EffectiveAudio &audio);

} // namespace rd
