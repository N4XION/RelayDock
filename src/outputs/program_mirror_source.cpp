// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "outputs/program_mirror_source.h"

#include <obs-module.h>

namespace rd {

namespace {

const char *mirrorName(void *)
{
	return "RelayDock Program";
}

void *mirrorCreate(obs_data_t *, obs_source_t *source)
{
	// OBS needs a non-null handle. The source has no state of its own.
	return source;
}

void mirrorDestroy(void *) {}

uint32_t mirrorWidth(void *)
{
	obs_video_info ovi{};
	return obs_get_video_info(&ovi) ? ovi.base_width : 0;
}

uint32_t mirrorHeight(void *)
{
	obs_video_info ovi{};
	return obs_get_video_info(&ovi) ? ovi.base_height : 0;
}

void mirrorRender(void *, gs_effect_t *)
{
	// Draws the main output texture of this frame. OBS renders the main canvas before any
	// extra canvas, so the texture is always the current frame.
	obs_render_main_texture();
}

} // namespace

void registerProgramMirrorSource()
{
	obs_source_info info{};
	info.id = kProgramMirrorSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_CAP_DISABLED |
			    OBS_SOURCE_DO_NOT_DUPLICATE;
	info.get_name = mirrorName;
	info.create = mirrorCreate;
	info.destroy = mirrorDestroy;
	info.get_width = mirrorWidth;
	info.get_height = mirrorHeight;
	info.video_render = mirrorRender;
	obs_register_source(&info);
}

} // namespace rd
