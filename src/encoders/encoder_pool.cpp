// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "encoders/encoder_pool.h"

#include "encoders/encode_plan.h"
#include "encoders/encoder_settings.h"
#include "utils/log.h"

#include <format>

namespace rd {

namespace {

OBSDataAutoRelease toObsData(const std::vector<EncoderSetting> &settings)
{
	OBSDataAutoRelease data = obs_data_create();
	for (const EncoderSetting &setting : settings) {
		switch (setting.kind) {
		case EncoderSetting::Kind::Int:
			obs_data_set_int(data, setting.key.c_str(), setting.intValue);
			break;
		case EncoderSetting::Kind::String:
			obs_data_set_string(data, setting.key.c_str(), setting.stringValue.c_str());
			break;
		case EncoderSetting::Kind::Bool:
			obs_data_set_bool(data, setting.key.c_str(), setting.boolValue);
			break;
		}
	}
	return data;
}

template <class Map> size_t countLive(Map &map)
{
	size_t live = 0;
	for (auto &entry : map) {
		OBSEncoderAutoRelease strong = obs_weak_encoder_get_encoder(entry.second);
		if (strong)
			++live;
	}
	return live;
}

} // namespace

void EncoderPool::prune()
{
	for (auto *map : {&video_, &audio_}) {
		for (auto it = map->begin(); it != map->end();) {
			OBSEncoderAutoRelease strong = obs_weak_encoder_get_encoder(it->second);
			if (strong)
				++it;
			else
				it = map->erase(it);
		}
	}
}

OBSEncoderAutoRelease EncoderPool::acquireVideo(const EffectiveVideo &video, const VideoEncoderCaps &caps,
						video_t *frames, obs_scale_type scaleType, std::string &error)
{
	prune();

	const std::string key = videoSignature(video);
	const auto existing = video_.find(key);
	if (existing != video_.end()) {
		obs_encoder_t *strong = obs_weak_encoder_get_encoder(existing->second);
		if (strong)
			return OBSEncoderAutoRelease(strong);
	}

	if (!frames) {
		error = "No video source is available for the encoder.";
		return {};
	}

	OBSDataAutoRelease settings = toObsData(buildVideoEncoderSettings(video, caps));
	const std::string name = std::format("RelayDock video {}", nextNumber_++);
	obs_encoder_t *encoder = obs_video_encoder_create(video.encoderId.c_str(), name.c_str(), settings, nullptr);
	if (!encoder) {
		error = std::format("OBS could not create the video encoder '{}'.", video.encoderId);
		return {};
	}

	obs_encoder_set_video(encoder, frames);

	// Scale on the GPU when the destination wants another size than the canvas outputs.
	// GPU scaling keeps hardware encoders on their fast texture path.
	const video_output_info *info = video_output_get_info(frames);
	if (info && (static_cast<int>(info->width) != video.width || static_cast<int>(info->height) != video.height)) {
		obs_encoder_set_scaled_size(encoder, static_cast<uint32_t>(video.width),
					    static_cast<uint32_t>(video.height));
		obs_encoder_set_gpu_scale_type(encoder, scaleType);
	}

	if (video.fpsDivisor > 1)
		obs_encoder_set_frame_rate_divisor(encoder, static_cast<uint32_t>(video.fpsDivisor));

	logInfo("Created {}: {} {}x{} at {:.2f} FPS, {} Kbps {}, preset '{}', keyframe every {} s", name,
		video.encoderId, video.width, video.height, video.fps, video.bitrateKbps, video.rateControl,
		video.preset.empty() ? "default" : video.preset, video.keyframeIntervalSec);

	video_[key] = obs_encoder_get_weak_encoder(encoder);
	return OBSEncoderAutoRelease(encoder);
}

OBSEncoderAutoRelease EncoderPool::acquireAudio(const EffectiveVideo &video, const EffectiveAudio &audio,
						std::string &error)
{
	prune();

	const std::string key = videoSignature(video) + "\x1e" + audioSignature(audio);
	const auto existing = audio_.find(key);
	if (existing != audio_.end()) {
		obs_encoder_t *strong = obs_weak_encoder_get_encoder(existing->second);
		if (strong)
			return OBSEncoderAutoRelease(strong);
	}

	OBSDataAutoRelease settings = toObsData(buildAudioEncoderSettings(audio));
	const std::string name = std::format("RelayDock audio {}", nextNumber_++);
	const size_t mixerIndex = static_cast<size_t>(audio.track > 0 ? audio.track - 1 : 0);
	obs_encoder_t *encoder =
		obs_audio_encoder_create(audio.encoderId.c_str(), name.c_str(), settings, mixerIndex, nullptr);
	if (!encoder) {
		error = std::format("OBS could not create the audio encoder '{}'.", audio.encoderId);
		return {};
	}

	obs_encoder_set_audio(encoder, obs_get_audio());

	logInfo("Created {}: {} at {} Kbps, track {}", name, audio.encoderId, audio.bitrateKbps, audio.track);

	audio_[key] = obs_encoder_get_weak_encoder(encoder);
	return OBSEncoderAutoRelease(encoder);
}

size_t EncoderPool::liveVideoEncoders()
{
	return countLive(video_);
}

size_t EncoderPool::liveAudioEncoders()
{
	return countLive(audio_);
}

} // namespace rd
