// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "encoders/encode_plan.h"

#include <format>

namespace rd {

std::string videoSignature(const EffectiveVideo &v)
{
	// Every field that changes the encoded bitstream is part of the key. The \x1f separator
	// cannot appear in any field, so two different settings can never produce the same key.
	return std::format("{}\x1f{}\x1f{}x{}\x1f/{}\x1f{}\x1f{}\x1f{}\x1f{}\x1f{}\x1f{}\x1f{}\x1f{}",
			   orientationName(v.orientation), v.verticalLayoutId, v.width, v.height, v.fpsDivisor,
			   v.encoderId, v.bitrateKbps, v.rateControl, v.preset, v.profile, v.keyframeIntervalSec,
			   v.bFrames, v.customOptions);
}

std::string audioSignature(const EffectiveAudio &a)
{
	return std::format("{}\x1f{}\x1f{}", a.encoderId, a.bitrateKbps, a.track);
}

EncodePlan planEncoders(const std::vector<EffectiveDestination> &destinations)
{
	EncodePlan plan;

	for (const EffectiveDestination &destination : destinations) {
		if (!destination.usable())
			continue;

		EncoderGroup *group = nullptr;
		for (EncoderGroup &candidate : plan.groups) {
			if (candidate.settings == destination.video) {
				group = &candidate;
				break;
			}
		}
		if (!group) {
			plan.groups.push_back({});
			group = &plan.groups.back();
			group->settings = destination.video;
		}
		group->destinationIds.push_back(destination.id);

		AudioGroup *audio = nullptr;
		for (AudioGroup &candidate : group->audio) {
			if (candidate.settings == destination.audio) {
				audio = &candidate;
				break;
			}
		}
		if (!audio) {
			group->audio.push_back({});
			audio = &group->audio.back();
			audio->settings = destination.audio;
		}
		audio->destinationIds.push_back(destination.id);
	}

	return plan;
}

size_t EncodePlan::destinationCount() const
{
	size_t count = 0;
	for (const EncoderGroup &group : groups)
		count += group.destinationIds.size();
	return count;
}

size_t EncodePlan::encodesSaved() const
{
	return destinationCount() - groups.size();
}

const EncoderGroup *EncodePlan::groupOf(const std::string &destinationId) const
{
	for (const EncoderGroup &group : groups) {
		for (const std::string &id : group.destinationIds) {
			if (id == destinationId)
				return &group;
		}
	}
	return nullptr;
}

std::vector<std::string> EncodePlan::sharedWith(const std::string &destinationId) const
{
	std::vector<std::string> out;
	if (const EncoderGroup *group = groupOf(destinationId)) {
		for (const std::string &id : group->destinationIds) {
			if (id != destinationId)
				out.push_back(id);
		}
	}
	return out;
}

} // namespace rd
