// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "performance/optimizer_text.h"

#include "utils/i18n.h"

namespace rd {

namespace {

std::string reasonFor(OptimizerCause cause)
{
	switch (cause) {
	case OptimizerCause::NetworkDrops:
		return loc("Optimizer.Reason.Drops",
			   "The connection dropped frames for a while. A lower bitrate needs less upload.");
	case OptimizerCause::EncoderOverload:
		return loc("Optimizer.Reason.Encoder", "The video encoder could not keep up and skipped frames.");
	case OptimizerCause::RenderingLag:
		return loc("Optimizer.Reason.Render",
			   "OBS could not render every frame in time. The graphics chip is busy.");
	case OptimizerCause::HighCpu:
		return loc("Optimizer.Reason.Cpu",
			   "Processor load stayed high. Prevent Game Lag acts before frames are lost.");
	case OptimizerCause::Recovery:
		return loc("Optimizer.Reason.Recovery", "RelayDock measured no problem for a while.");
	}
	return {};
}

} // namespace

std::string joinNames(const std::vector<std::string> &names)
{
	if (names.empty())
		return loc("Optimizer.Names.None", "your destinations");
	if (names.size() == 1)
		return names.front();

	std::string out;
	for (size_t i = 0; i + 1 < names.size(); ++i) {
		if (i > 0)
			out += ", ";
		out += names[i];
	}
	return locf("Optimizer.Names.And", "{0} and {1}", out, names.back());
}

ChangeText describeChange(const OptimizerChange &change, const std::vector<std::string> &names)
{
	const std::string who = joinNames(names);
	const bool recovery = change.cause == OptimizerCause::Recovery;
	ChangeText text;

	switch (change.kind) {
	case OptimizerChangeKind::Bitrate:
		if (recovery && change.to.bitratePercent >= 100)
			text.title = locf("Optimizer.Title.BitrateFull", "Restore the full bitrate of {0}", who);
		else if (recovery)
			text.title = locf("Optimizer.Title.BitrateUp", "Raise the bitrate of {0} to {1} percent", who,
					  change.to.bitratePercent);
		else
			text.title = locf("Optimizer.Title.BitrateDown", "Lower the bitrate of {0} to {1} percent", who,
					  change.to.bitratePercent);
		break;
	case OptimizerChangeKind::Fps:
		if (change.to.maxFps == 0)
			text.title = locf("Optimizer.Title.FpsFull", "Restore the frame rate of {0}", who);
		else
			text.title = locf("Optimizer.Title.FpsDown", "Limit {0} to {1} FPS", who, change.to.maxFps);
		break;
	case OptimizerChangeKind::Resolution:
		if (change.to.maxLines == 0)
			text.title = locf("Optimizer.Title.ResolutionFull", "Restore the resolution of {0}", who);
		else if (recovery)
			text.title = locf("Optimizer.Title.ResolutionUp", "Raise the resolution of {0} to {1}p", who,
					  change.to.maxLines);
		else
			text.title = locf("Optimizer.Title.ResolutionDown", "Limit {0} to {1}p", who, change.to.maxLines);
		break;
	}

	text.reason = reasonFor(change.cause);
	text.effect = change.needsReconnect
			      ? locf("Optimizer.Effect.Reconnect", "Applying this reconnects {0} for a moment.", who)
			      : loc("Optimizer.Effect.Live", "Applies while you are live. Nothing reconnects.");
	return text;
}

std::string describeAppliedChange(const OptimizerChange &change, const std::vector<std::string> &names)
{
	const std::string who = joinNames(names);
	switch (change.kind) {
	case OptimizerChangeKind::Bitrate:
		return locf("Optimizer.Applied.Bitrate", "Bitrate of {0} set to {1} percent.", who, change.to.bitratePercent);
	case OptimizerChangeKind::Fps:
		return change.to.maxFps == 0 ? locf("Optimizer.Applied.FpsFull", "Frame rate of {0} restored.", who)
					     : locf("Optimizer.Applied.Fps", "{0} limited to {1} FPS.", who, change.to.maxFps);
	case OptimizerChangeKind::Resolution:
		return change.to.maxLines == 0
			       ? locf("Optimizer.Applied.ResolutionFull", "Resolution of {0} restored.", who)
			       : locf("Optimizer.Applied.Resolution", "{0} limited to {1}p.", who, change.to.maxLines);
	}
	return {};
}

std::string describeNotice(const OptimizerNotice &notice, const std::vector<std::string> &names)
{
	const std::string who = joinNames(names);
	switch (notice.cause) {
	case OptimizerCause::NetworkDrops:
		return locf("Optimizer.Notice.Drops",
			    "{0} keeps dropping frames, and RelayDock has no setting left that it may lower. Unlock the bitrate, or check your connection.",
			    who);
	case OptimizerCause::EncoderOverload:
		return locf("Optimizer.Notice.Encoder",
			    "The encoder for {0} cannot keep up, and RelayDock has no setting left that it may lower. Unlock the frame rate or resolution, or pick a faster encoder preset.",
			    who);
	case OptimizerCause::RenderingLag:
		return loc("Optimizer.Notice.Render",
			   "OBS misses frames while rendering, and RelayDock has no setting left that it may lower. Close programs that use the graphics chip, or lower the OBS frame rate.");
	case OptimizerCause::HighCpu:
		return loc("Optimizer.Notice.Cpu",
			   "Processor load stays high, and RelayDock has no setting left that it may lower. Use a hardware encoder or Potato Mode.");
	case OptimizerCause::Recovery:
		break;
	}
	return {};
}

} // namespace rd
