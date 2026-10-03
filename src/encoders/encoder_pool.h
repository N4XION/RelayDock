// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "encoders/encoder_caps.h"
#include "performance/effective_settings.h"

#include <obs.hpp>

#include <map>
#include <string>

namespace rd {

// Hands out OBS encoders and makes destinations with identical settings share one.
//
// How sharing works: the pool remembers each encoder it created by the signature of its
// settings, holding only a weak reference. A destination that asks for the same signature
// gets a strong reference to the same OBS encoder, and OBS feeds every output attached to it
// from a single encode. When the last destination releases its reference, OBS destroys the
// encoder and the pool's weak reference expires. Nothing here counts references by hand.
//
// A shared encoder is never reconfigured. A destination whose settings change gets a
// different signature and therefore a different encoder.
//
// Thread ownership: OBS UI thread.
class EncoderPool {
public:
	// Returns an encoder for `video`, creating it when no live encoder has that signature.
	// `frames` is the video source to encode: the OBS main video or a vertical canvas.
	// Returns an empty reference and sets `error` when OBS cannot create the encoder.
	OBSEncoderAutoRelease acquireVideo(const EffectiveVideo &video, const VideoEncoderCaps &caps, video_t *frames,
					   obs_scale_type scaleType, std::string &error);

	// Audio encoders are shared between destinations that share the video encoder and have
	// the same audio settings.
	OBSEncoderAutoRelease acquireAudio(const EffectiveVideo &video, const EffectiveAudio &audio,
					   std::string &error);

	// Changes the bitrate of a running encoder and files it under the signature of its new
	// settings, so a destination that starts later with those settings joins it. `from` and
	// `to` may differ in bitrate only. Every destination on the encoder gets the new bitrate,
	// so the caller must change them together. Returns false when no live encoder matches
	// `from`, or when another encoder already runs with the `to` settings.
	bool retune(const EffectiveVideo &from, const EffectiveVideo &to, const VideoEncoderCaps &caps);

	// How many encoders the pool created that are still alive.
	size_t liveVideoEncoders();
	size_t liveAudioEncoders();

private:
	void prune();

	std::map<std::string, OBSWeakEncoderAutoRelease> video_;
	std::map<std::string, OBSWeakEncoderAutoRelease> audio_;
	unsigned nextNumber_ = 1;
};

} // namespace rd
