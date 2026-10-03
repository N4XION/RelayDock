// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "performance/effective_settings.h"

#include "utils/i18n.h"
#include "utils/strings.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace rd {

namespace {

// Smallest of the values that are set. 0 means "not set".
int minSet(std::initializer_list<int> values)
{
	int out = 0;
	for (int value : values) {
		if (value > 0 && (out == 0 || value < out))
			out = value;
	}
	return out;
}

int roundBitrate(double kbps)
{
	const int rounded = static_cast<int>(std::lround(kbps / 50.0)) * 50;
	return std::max(rounded, 200);
}

bool codecAccepted(const StreamContext &context, const std::string &codec)
{
	return std::any_of(context.outputVideoCodecs.begin(), context.outputVideoCodecs.end(),
			   [&](const std::string &accepted) { return equalsNoCase(accepted, codec); });
}

bool providerAcceptsCodec(const IProvider *provider, const std::string &codec)
{
	if (!provider)
		return true;
	const std::vector<std::string> &codecs = provider->limits().videoCodecs;
	return codecs.empty() || std::any_of(codecs.begin(), codecs.end(), [&](const std::string &accepted) {
		return equalsNoCase(accepted, codec);
	});
}

int familyRank(EncoderFamily family)
{
	switch (family) {
	case EncoderFamily::Nvenc:
		return 0;
	case EncoderFamily::Amf:
		return 1;
	case EncoderFamily::Qsv:
		return 2;
	case EncoderFamily::OtherHardware:
		return 3;
	case EncoderFamily::X264:
		return 4;
	case EncoderFamily::OtherSoftware:
		return 5;
	}
	return 6;
}

// Per-destination working state while resolving.
struct Working {
	const ResolveInput *input = nullptr;
	ProviderLimits limits;
	const CanvasInfo *canvas = nullptr;
	EffectiveDestination out;

	bool manual = false;
	bool resolutionFixed = false;
	bool fpsFixed = false;
	bool bitrateFixed = false;
	bool encoderFixed = false;

	int lines = 0; // Target for the shorter side, for unlocked resolutions

	std::string groupKey() const
	{
		std::string key = orientationName(out.video.orientation);
		if (out.video.orientation == Orientation::Vertical)
			key += ":" + out.video.verticalLayoutId;
		return key;
	}
};

} // namespace

ModeLimits modeLimits(PerformanceMode mode)
{
	switch (mode) {
	case PerformanceMode::Potato:
		return {720, 30};
	case PerformanceMode::Balanced:
		return {1080, 60};
	case PerformanceMode::Quality:
		return {0, 60};
	case PerformanceMode::Custom:
		return {0, 0};
	}
	return {1080, 60};
}

int targetBitrateKbps(Size size, double fps, PerformanceMode mode)
{
	// Columns: Quality, Balanced, Potato. Custom uses the Balanced column.
	struct Row {
		int minLines;
		bool highFps;
		int quality;
		int balanced;
		int potato;
	};
	static const Row kLadder[] = {
		{2160, true, 35000, 20000, 12000}, {2160, false, 25000, 14000, 9000},
		{1440, true, 13000, 9000, 6000},   {1440, false, 9000, 6500, 4500},
		{1080, true, 9000, 6000, 4500},    {1080, false, 6000, 4500, 3500},
		{720, true, 6000, 4500, 3500},     {720, false, 4000, 3000, 2500},
		{480, true, 3000, 2500, 2000},     {480, false, 2000, 1500, 1200},
		{0, true, 1500, 1200, 900},        {0, false, 1000, 800, 600},
	};

	const bool highFps = fps > 45.0;
	const int lines = size.lines();
	for (const Row &row : kLadder) {
		if (lines >= row.minLines && row.highFps == highFps) {
			switch (mode) {
			case PerformanceMode::Quality:
				return row.quality;
			case PerformanceMode::Potato:
				return row.potato;
			case PerformanceMode::Balanced:
			case PerformanceMode::Custom:
				return row.balanced;
			}
		}
	}
	return 3000;
}

std::string pickVideoEncoder(const StreamContext &context)
{
	const VideoEncoderCaps *best = nullptr;
	for (const VideoEncoderCaps &caps : context.videoEncoders) {
		if (!equalsNoCase(caps.codec, "h264") || !codecAccepted(context, caps.codec))
			continue;
		if (!best || familyRank(caps.family) < familyRank(best->family))
			best = &caps;
	}
	return best ? best->id : std::string();
}

std::string pickAudioEncoder(const StreamContext &context)
{
	// ffmpeg_aac ships with every OBS build, so prefer it for predictable results.
	if (context.findAudioEncoder("ffmpeg_aac"))
		return "ffmpeg_aac";
	for (const AudioEncoderCaps &caps : context.audioEncoders) {
		if (equalsNoCase(caps.codec, "aac"))
			return caps.id;
	}
	return {};
}

std::string modePreset(const VideoEncoderCaps &caps, PerformanceMode mode)
{
	const char *preset = "";
	const bool potato = mode == PerformanceMode::Potato;
	const bool quality = mode == PerformanceMode::Quality;

	switch (caps.family) {
	case EncoderFamily::X264:
		preset = potato ? "superfast" : quality ? "faster" : "veryfast";
		break;
	case EncoderFamily::Nvenc:
		preset = potato ? "p3" : quality ? "p6" : "p5";
		break;
	case EncoderFamily::Amf:
		preset = potato ? "speed" : quality ? "quality" : "balanced";
		break;
	case EncoderFamily::Qsv:
		preset = potato ? "TU7" : quality ? "TU2" : "TU4";
		break;
	case EncoderFamily::OtherSoftware:
	case EncoderFamily::OtherHardware:
		break;
	}

	return caps.supportsPreset(preset) ? std::string(preset) : std::string();
}

std::vector<EffectiveDestination> resolveEffectiveSettings(const std::vector<ResolveInput> &inputs,
							   const StreamContext &context, PerformanceMode mode)
{
	const bool manualMode = mode == PerformanceMode::Custom;
	const bool uniform = mode == PerformanceMode::Potato || mode == PerformanceMode::Balanced;
	const ModeLimits modeCap = modeLimits(mode);

	std::vector<Working> work(inputs.size());

	// ---- Pass 1: per-destination targets ---------------------------------------------------
	for (size_t i = 0; i < inputs.size(); ++i) {
		Working &w = work[i];
		w.input = &inputs[i];
		const DestinationConfig &config = inputs[i].config;
		const Adjustment &adjustment = inputs[i].adjustment;
		if (inputs[i].provider)
			w.limits = inputs[i].provider->limits();

		w.manual = manualMode;
		w.resolutionFixed = manualMode || config.locks.resolution;
		w.fpsFixed = manualMode || config.locks.fps;
		w.bitrateFixed = manualMode || config.locks.bitrate;
		w.encoderFixed = manualMode || config.locks.encoder;

		EffectiveVideo &video = w.out.video;
		w.out.id = config.id;

		// Orientation. With the aspect ratio unlocked, the platform's preference wins.
		video.orientation = config.video.orientation;
		if (!manualMode && !config.locks.aspectRatio && inputs[i].provider)
			video.orientation = w.limits.preferredOrientation;
		if (video.orientation == Orientation::Vertical)
			video.verticalLayoutId = config.verticalLayoutId;
		w.canvas = video.orientation == Orientation::Vertical ? &context.vertical : &context.horizontal;
		const CanvasInfo &canvas = *w.canvas;

		// Resolution
		if (w.resolutionFixed) {
			const Size saved{config.video.width, config.video.height};
			Size size = canvasOutputSize(canvas);
			if (!saved.empty()) {
				const bool fits = saved.width <= canvas.baseWidth && saved.height <= canvas.baseHeight;
				if (keepsCanvasAspect(canvas, saved) && fits) {
					size = saved;
				} else {
					size = nearestCanvasSize(canvas, saved);
					w.out.notes.push_back(locf(
						"Effective.Resolution.Corrected",
						"{0}x{1} does not match the {2} canvas, so this destination uses {3}x{4}. That keeps the picture from being stretched.",
						saved.width, saved.height, aspectRatioText(canvas.baseWidth, canvas.baseHeight),
						size.width, size.height));
				}
			}
			// Custom mode keeps saved values, but the optimiser may still shrink a
			// resolution the user did not lock.
			if (!config.locks.resolution && adjustment.maxLines > 0 && size.lines() > adjustment.maxLines)
				size = scaleCanvasToLines(canvas, adjustment.maxLines);
			video.width = size.width;
			video.height = size.height;
		} else {
			int lines = canvasOutputSize(canvas).lines();
			lines = minSet({lines, modeCap.maxLines, w.limits.maxShortEdge, adjustment.maxLines});
			// A platform's long-edge limit can be tighter than its short-edge limit.
			if (w.limits.maxLongEdge > 0) {
				const Size candidate = scaleCanvasToLines(canvas, lines);
				if (candidate.longEdge() > w.limits.maxLongEdge) {
					lines = static_cast<int>(static_cast<double>(lines) * w.limits.maxLongEdge /
								 candidate.longEdge());
				}
			}
			w.lines = lines;
		}

		// Frame rate
		if (w.fpsFixed) {
			video.fpsDivisor = divisorForTargetFps(canvas, config.video.fps);
			const int wanted = config.video.fps;
			const int actual = static_cast<int>(std::lround(fpsForDivisor(canvas, video.fpsDivisor)));
			if (wanted > 0 && std::abs(actual - wanted) > 1) {
				w.out.notes.push_back(locf(
					"Effective.Fps.Unavailable",
					"OBS runs at {0} FPS, so {1} FPS is not available. This destination streams at {2} FPS.",
					static_cast<int>(std::lround(canvas.fps())), wanted, actual));
			}
			if (!config.locks.fps && adjustment.maxFps > 0) {
				video.fpsDivisor =
					std::max(video.fpsDivisor, divisorForMaxFps(canvas, adjustment.maxFps));
			}
		} else {
			const int cap = minSet({modeCap.maxFps, w.limits.maxFps, w.limits.recommendedFps, adjustment.maxFps});
			video.fpsDivisor = divisorForMaxFps(canvas, cap);
		}

		// Encoder
		std::string encoderId;
		if (w.encoderFixed && config.video.encoder != kAutoEncoder) {
			const VideoEncoderCaps *caps = context.findVideoEncoder(config.video.encoder);
			if (caps && codecAccepted(context, caps->codec) &&
			    providerAcceptsCodec(inputs[i].provider, caps->codec)) {
				encoderId = caps->id;
			} else {
				w.out.notes.push_back(
					caps ? locf("Effective.Encoder.CodecRejected",
						   "The platform does not accept {0} video over RTMP, so RelayDock picks another encoder.",
						   toLower(caps->codec))
					     : locf("Effective.Encoder.Missing",
						   "The encoder \"{0}\" is not available on this PC, so RelayDock picks another one.",
						   config.video.encoder));
			}
		}
		if (encoderId.empty())
			encoderId = pickVideoEncoder(context);
		video.encoderId = encoderId;

		video.keyframeIntervalSec = config.video.keyframeIntervalSec;
	}

	// ---- Pass 2: make unlocked resolution and frame rate uniform per canvas ------------------
	if (uniform) {
		std::map<std::string, int> minLines;
		std::map<std::string, int> maxDivisor;
		for (const Working &w : work) {
			const std::string key = w.groupKey();
			if (!w.resolutionFixed) {
				auto it = minLines.find(key);
				if (it == minLines.end() || w.lines < it->second)
					minLines[key] = w.lines;
			}
			if (!w.fpsFixed) {
				auto it = maxDivisor.find(key);
				if (it == maxDivisor.end() || w.out.video.fpsDivisor > it->second)
					maxDivisor[key] = w.out.video.fpsDivisor;
			}
		}
		for (Working &w : work) {
			const std::string key = w.groupKey();
			if (!w.resolutionFixed)
				w.lines = minLines[key];
			if (!w.fpsFixed)
				w.out.video.fpsDivisor = maxDivisor[key];
		}
	}

	for (Working &w : work) {
		EffectiveVideo &video = w.out.video;
		if (!w.resolutionFixed) {
			const Size output = canvasOutputSize(*w.canvas);
			const Size size = w.lines >= output.lines() ? output : scaleCanvasToLines(*w.canvas, w.lines);
			video.width = size.width;
			video.height = size.height;
		}
		video.fps = fpsForDivisor(*w.canvas, video.fpsDivisor);
	}

	// ---- Pass 3: bitrate --------------------------------------------------------------------
	for (Working &w : work) {
		const DestinationConfig &config = w.input->config;
		EffectiveVideo &video = w.out.video;
		if (w.bitrateFixed) {
			video.bitrateKbps = config.video.bitrateKbps;
			if (!config.locks.bitrate && w.input->adjustment.bitratePercent < 100) {
				video.bitrateKbps = roundBitrate(
					video.bitrateKbps * std::clamp(w.input->adjustment.bitratePercent, 10, 100) / 100.0);
			}
			continue;
		}
		double bitrate = targetBitrateKbps({video.width, video.height}, video.fps, mode);
		const int cap = minSet({w.limits.maxVideoBitrateKbps, w.limits.recommendedVideoBitrateKbps});
		if (cap > 0 && bitrate > cap)
			bitrate = cap;
		bitrate = bitrate * std::clamp(w.input->adjustment.bitratePercent, 10, 100) / 100.0;
		video.bitrateKbps = roundBitrate(bitrate);
	}

	if (uniform) {
		// Destinations that already agree on canvas, size, frame rate and encoder take the
		// lowest bitrate among them. That makes their settings identical, so they share.
		auto shareKey = [](const Working &w) {
			const EffectiveVideo &v = w.out.video;
			return w.groupKey() + "|" + std::to_string(v.width) + "x" + std::to_string(v.height) + "|" +
			       std::to_string(v.fpsDivisor) + "|" + v.encoderId;
		};
		std::map<std::string, int> minBitrate;
		for (const Working &w : work) {
			if (w.bitrateFixed)
				continue;
			const std::string key = shareKey(w);
			auto it = minBitrate.find(key);
			if (it == minBitrate.end() || w.out.video.bitrateKbps < it->second)
				minBitrate[key] = w.out.video.bitrateKbps;
		}
		for (Working &w : work) {
			if (!w.bitrateFixed)
				w.out.video.bitrateKbps = minBitrate[shareKey(w)];
		}
	}

	// ---- Pass 4: encoder details and audio --------------------------------------------------
	for (Working &w : work) {
		const DestinationConfig &config = w.input->config;
		EffectiveVideo &video = w.out.video;
		const VideoEncoderCaps *caps = context.findVideoEncoder(video.encoderId);

		if (caps) {
			const bool useSaved = w.encoderFixed;

			if (useSaved && !config.video.rateControl.empty() && caps->supportsRateControl(config.video.rateControl))
				video.rateControl = config.video.rateControl;
			else if (caps->supportsRateControl("CBR"))
				video.rateControl = "CBR";
			else if (!caps->rateControls.empty())
				video.rateControl = caps->rateControls.front();

			if (useSaved && !config.video.preset.empty() && caps->supportsPreset(config.video.preset))
				video.preset = config.video.preset;
			else if (!w.manual)
				video.preset = modePreset(*caps, mode);

			if (useSaved && !config.video.profile.empty() && caps->supportsProfile(config.video.profile))
				video.profile = config.video.profile;

			if (useSaved && config.video.bFrames >= 0) {
				if (caps->bFramesKey.empty() && caps->family != EncoderFamily::X264)
					video.bFrames = -1;
				else if (caps->maxBFrames > 0)
					video.bFrames = std::min(config.video.bFrames, caps->maxBFrames);
				else
					video.bFrames = config.video.bFrames;
			}

			if (useSaved && !caps->optionsKey.empty())
				video.customOptions = trim(config.video.customOptions);

			if (caps->hasKeyframeInterval && caps->maxKeyframeIntervalSec > 0)
				video.keyframeIntervalSec = std::min(video.keyframeIntervalSec, caps->maxKeyframeIntervalSec);
		}

		EffectiveAudio &audio = w.out.audio;
		if (config.audio.encoder != kAutoEncoder && context.findAudioEncoder(config.audio.encoder))
			audio.encoderId = config.audio.encoder;
		else
			audio.encoderId = pickAudioEncoder(context);
		audio.track = std::clamp(config.audio.track, 1, 6);
		audio.bitrateKbps = config.audio.bitrateKbps;
		if (w.limits.maxAudioBitrateKbps > 0 && audio.bitrateKbps > w.limits.maxAudioBitrateKbps && !w.manual) {
			audio.bitrateKbps = w.limits.maxAudioBitrateKbps;
			w.out.notes.push_back(locf("Effective.Audio.Capped",
						  "The platform accepts up to {0} Kbps of audio, so this destination uses {0} Kbps.",
						  w.limits.maxAudioBitrateKbps));
		}
	}

	std::vector<EffectiveDestination> out;
	out.reserve(work.size());
	for (Working &w : work)
		out.push_back(std::move(w.out));
	return out;
}

} // namespace rd
