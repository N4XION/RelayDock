// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <obs.hpp>

namespace rd {

// Id of the helper output type RelayDock registers with OBS.
inline constexpr const char *kVideoGuardOutputId = "relaydock_video_guard";

// Registers the output type the guard uses. Call once from obs_module_load.
void registerVideoGuardOutput();

// Keeps OBS from changing its video settings while RelayDock holds encoders.
//
// OBS refuses a new canvas size or frame rate while an encoder is capturing, because the
// change frees the video that encoder reads. The encoder of a destination that is still
// connecting, or that waits to reconnect, is not capturing. OBS would accept the change, the
// encoder would point at freed video, and OBS would crash at the next connection attempt.
// The OBS interface only knows about its own stream, so it does not block the change either.
//
// The guard closes that gap. It is a raw video output of RelayDock's own, on a view of 16 by
// 16 pixels that shows nothing. While it runs, OBS counts video as in use: it refuses the
// change and greys out its Video settings, as it does during its own stream. Drawing and
// reading back 16 by 16 pixels costs next to nothing.
//
// Thread ownership: OBS UI thread.
class VideoGuard {
public:
	VideoGuard() = default;
	~VideoGuard();
	VideoGuard(const VideoGuard &) = delete;
	VideoGuard &operator=(const VideoGuard &) = delete;

	// Returns true when the guard is now in the wanted state.
	bool setActive(bool active);
	bool active() const { return static_cast<bool>(output_); }

private:
	void release();

	obs_view_t *view_ = nullptr;
	OBSOutputAutoRelease output_;
};

} // namespace rd
