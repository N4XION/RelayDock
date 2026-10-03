// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "diagnostics/report.h"

#include "network/stream_url.h"
#include "utils/strings.h"

#include <cmath>
#include <format>
#include <regex>

namespace rd {

namespace {

const char *yesNo(bool value)
{
	return value ? "yes" : "no";
}

std::string orDash(const std::string &text)
{
	return text.empty() ? std::string("-") : text;
}

// Scheme, host, port and application name only.
std::string safeServer(const std::string &text)
{
	const StreamUrlResult parsed = parseStreamUrl(text);
	if (parsed.ok())
		return Redactor::redactUrl(parsed.url.displayText());
	// Not a URL: a list entry name such as "Default (RTMPS, encrypted)".
	return Redactor::redactPatterns(text);
}

void line(std::string &out, const std::string &text = {})
{
	out += text;
	out += "\n";
}

void heading(std::string &out, const std::string &title)
{
	line(out);
	line(out, title);
	line(out, std::string(title.size(), '-'));
}

} // namespace

std::string anonymizePaths(std::string text)
{
	// C:\Users\name\ and C:/Users/name/, any drive letter.
	static const std::regex userFolder(R"(([A-Za-z]:[\\/]Users[\\/])[^\\/\r\n"']+)", std::regex::icase);
	return std::regex_replace(text, userFolder, "$1<user>");
}

std::string buildDiagnosticsReport(const DiagnosticsInput &input, const Redactor &redactor)
{
	std::string out;

	line(out, "RelayDock diagnostics");
	line(out, "=====================");
	line(out, "This report contains no stream keys and no passwords. Check it before you share it.");
	line(out);
	line(out, std::format("Generated (UTC):   {}", orDash(input.generatedAtUtc)));
	line(out, std::format("RelayDock:         {} (built {})", orDash(input.relayDockVersion), orDash(input.buildDate)));
	line(out, std::format("OBS Studio:        {}", orDash(input.obsVersion)));
	line(out, std::format("Windows:           {}", orDash(input.osVersion)));

	heading(out, "System");
	line(out, std::format("Processor:         {} ({} cores, {} threads)", orDash(input.cpuName), input.cpuCores, input.cpuThreads));
	line(out, std::format("Memory:            {} MB", input.ramMb));
	if (input.gpus.empty())
		line(out, "Graphics:          -");
	for (const std::string &gpu : input.gpus)
		line(out, std::format("Graphics:          {}", gpu));
	if (input.cpuPercent >= 0.0)
		line(out, std::format("Processor load:    {:.0f} percent", input.cpuPercent));
	if (input.renderLagPercent >= 0.0)
		line(out, std::format("Rendering lag:     {:.1f} percent of frames", input.renderLagPercent));
	if (input.memoryMb >= 0.0)
		line(out, std::format("OBS memory:        {:.0f} MB", input.memoryMb));

	heading(out, "OBS video");
	const CanvasInfo &h = input.context.horizontal;
	const CanvasInfo &v = input.context.vertical;
	line(out, std::format("Canvas:            {}x{}", h.baseWidth, h.baseHeight));
	line(out, std::format("Output:            {}x{}", h.outputWidth, h.outputHeight));
	line(out, std::format("Frame rate:        {:.2f} FPS ({}/{})", h.fps(), h.fpsNum, h.fpsDen));
	line(out, std::format("Vertical canvas:   {}x{}", v.baseWidth, v.baseHeight));

	heading(out, "Encoders OBS offers");
	if (input.context.videoEncoders.empty())
		line(out, "none");
	for (const VideoEncoderCaps &caps : input.context.videoEncoders) {
		line(out, std::format("{:<24} {} ({}, {}){}", caps.id, caps.displayName, caps.codec,
				      encoderFamilyName(caps.family), caps.dynamicBitrate ? ", live bitrate change" : ""));
	}
	line(out, std::format("RTMP output accepts: {}", join(input.context.outputVideoCodecs, ", ")));

	heading(out, "RelayDock settings");
	line(out, std::format("Performance mode:        {}", performanceModeName(input.mode)));
	line(out, std::format("Automatic optimisation:  {}", optimizationModeName(input.optimizer.mode)));
	line(out, std::format("Prevent Game Lag:        {}", yesNo(input.optimizer.preventGameLag)));
	line(out, std::format("Allow reconnecting changes: {}", yesNo(input.optimizer.allowReconnectingChanges)));
	line(out, input.network.uploadKbps > 0
			  ? std::format("Upload speed:            {} (safe share {} percent)", formatBitrate(input.network.uploadKbps),
					input.network.safetyPercent)
			  : std::string("Upload speed:            not set"));
	line(out, std::format("Credential store:        {}", orDash(input.credentialBackend)));
	line(out, std::format("Settings folder:         {}", orDash(input.settingsFolder)));
	for (const std::string &note : input.settingsNotes)
		line(out, std::format("Settings note:           {}", note));

	heading(out, std::format("Destinations ({})", input.destinations.size()));
	for (const DiagnosticsDestination &d : input.destinations) {
		const EffectiveVideo &video = d.effective.video;
		line(out, std::format("[{}] {}", d.providerName, d.config.name));
		line(out, std::format("  Enabled:        {}", yesNo(d.config.enabled)));
		line(out, std::format("  Server:         {}", safeServer(d.serverText)));
		// Not worded "Stream key: ..." on purpose. The redactor replaces whatever follows a
		// label like that, and this line states a fact, not a secret.
		line(out, std::format("  Key saved:      {}", yesNo(d.hasStreamKey)));
		if (d.config.useAuth)
			line(out, std::format("  Authentication: on, user name {}, password {}", d.config.username.empty() ? "not set" : "set",
					      d.hasPassword ? "saved" : "not set"));
		line(out, std::format("  State:          {}{}", orDash(d.phase), d.lastStop.empty() ? "" : " (last stop: " + d.lastStop + ")"));
		if (!d.lastError.empty())
			line(out, std::format("  Last error:     {}", d.lastError));
		line(out, std::format("  Streaming with: {} {}x{} at {:.2f} FPS, {} Kbps {}, preset {}, keyframe {} s", orDash(video.encoderId),
				      video.width, video.height, video.fps, video.bitrateKbps, orDash(video.rateControl),
				      video.preset.empty() ? "default" : video.preset, video.keyframeIntervalSec));
		line(out, std::format("  Canvas:         {}", orientationName(video.orientation)));
		line(out, std::format("  Audio:          {} at {} Kbps, track {}", orDash(d.effective.audio.encoderId),
				      d.effective.audio.bitrateKbps, d.effective.audio.track));
		line(out, std::format("  Locks:          resolution {}, fps {}, bitrate {}, encoder {}, aspect ratio {}",
				      yesNo(d.config.locks.resolution), yesNo(d.config.locks.fps), yesNo(d.config.locks.bitrate),
				      yesNo(d.config.locks.encoder), yesNo(d.config.locks.aspectRatio)));
		line(out, std::format("  Reconnect:      {}, {} attempts, {} s delay", d.config.connection.autoReconnect ? "on" : "off",
				      d.config.connection.reconnectAttempts, d.config.connection.reconnectDelaySec));
		if (!d.videoEncoderName.empty())
			line(out, std::format("  Encoder object: {}{}", d.videoEncoderName,
					      d.sharedWith.empty() ? "" : ", shared with " + join(d.sharedWith, ", ")));
		if (d.totalFrames > 0 || d.liveSeconds > 0) {
			const double dropped = d.totalFrames > 0 ? 100.0 * d.droppedFrames / d.totalFrames : 0.0;
			line(out, std::format("  Live:           {} at {} Kbps, {} of {} frames dropped ({:.1f} percent), {} reconnect(s)",
					      formatDuration(d.liveSeconds), d.bitrateKbps, d.droppedFrames, d.totalFrames, dropped,
					      d.reconnects));
		}
		for (const std::string &note : d.effective.notes)
			line(out, std::format("  Note:           {}", note));
		for (const std::string &issue : d.issues)
			line(out, std::format("  Check:          {}", issue));
		line(out);
	}

	heading(out, std::format("Preflight: {}", toLower(preflightStatusName(input.preflight.status))));
	for (const PreflightItem &item : input.preflight.items)
		line(out, std::format("{:<8} {}: {}", preflightStatusName(item.status), item.title, item.message.text()));

	heading(out, std::format("Recent RelayDock log lines ({})", input.logLines.size()));
	for (const std::string &logLine : input.logLines)
		line(out, logLine);

	// Last line of defence. Every secret RelayDock has handled is removed, whatever field it
	// came in through, and Windows user folders are made anonymous.
	return anonymizePaths(redactor.redact(out));
}

} // namespace rd
