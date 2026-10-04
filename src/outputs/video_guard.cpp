// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "outputs/video_guard.h"

#include <obs-module.h>
#include <util/platform.h>

namespace rd {

namespace {

constexpr uint32_t kGuardSize = 16;

const char *guardName(void *)
{
	return "RelayDock video guard";
}

void *guardCreate(obs_data_t *, obs_output_t *output)
{
	// OBS needs a non-null handle. The output has no state of its own.
	return output;
}

void guardDestroy(void *) {}

bool guardStart(void *data)
{
	auto *output = static_cast<obs_output_t *>(data);
	if (!obs_output_can_begin_data_capture(output, 0))
		return false;
	return obs_output_begin_data_capture(output, 0);
}

void guardStop(void *data, uint64_t)
{
	obs_output_end_data_capture(static_cast<obs_output_t *>(data));
}

void guardRawVideo(void *, video_data *)
{
	// Nothing reads the frames. Receiving them is what makes OBS count video as in use.
}

} // namespace

void registerVideoGuardOutput()
{
	obs_output_info info{};
	info.id = kVideoGuardOutputId;
	info.flags = OBS_OUTPUT_VIDEO;
	info.get_name = guardName;
	info.create = guardCreate;
	info.destroy = guardDestroy;
	info.start = guardStart;
	info.stop = guardStop;
	info.raw_video = guardRawVideo;
	obs_register_output(&info);
}

VideoGuard::~VideoGuard()
{
	release();
}

bool VideoGuard::setActive(bool active)
{
	if (active == this->active())
		return true;
	if (!active) {
		release();
		return true;
	}

	obs_video_info ovi{};
	if (!obs_get_video_info(&ovi))
		return false;
	// The frame rate and format of OBS. Only the size differs.
	ovi.base_width = kGuardSize;
	ovi.base_height = kGuardSize;
	ovi.output_width = kGuardSize;
	ovi.output_height = kGuardSize;

	view_ = obs_view_create();
	video_t *video = view_ ? obs_view_add2(view_, &ovi) : nullptr;
	if (!video) {
		release();
		return false;
	}

	output_ = obs_output_create(kVideoGuardOutputId, "RelayDock video guard", nullptr, nullptr);
	if (!output_) {
		release();
		return false;
	}
	obs_output_set_media(output_, video, obs_get_audio());
	if (!obs_output_start(output_)) {
		release();
		return false;
	}
	return true;
}

void VideoGuard::release()
{
	if (output_) {
		obs_output_stop(output_);
		// OBS ends the capture on a short thread of its own. The view must outlive it.
		for (int i = 0; i < 400 && obs_output_active(output_); ++i)
			os_sleep_ms(5);
		output_ = nullptr;
	}
	if (view_) {
		obs_view_remove(view_);
		obs_view_destroy(view_);
		view_ = nullptr;
	}
}

} // namespace rd
