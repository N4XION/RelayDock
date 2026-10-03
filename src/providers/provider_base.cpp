// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "providers/provider_base.h"

#include "network/stream_url.h"
#include "security/redactor.h"
#include "utils/i18n.h"
#include "utils/strings.h"

#include <algorithm>

namespace rd {

namespace {

void addIssue(std::vector<ValidationIssue> &issues, Severity severity, const char *fieldId, std::string what,
	      std::string detail, std::string action)
{
	ValidationIssue issue;
	issue.severity = severity;
	issue.field = fieldId;
	issue.message.what = std::move(what);
	issue.message.detail = std::move(detail);
	issue.message.action = std::move(action);
	issues.push_back(std::move(issue));
}

void addError(std::vector<ValidationIssue> &issues, const char *fieldId, std::string what, std::string action,
	      std::string detail = {})
{
	addIssue(issues, Severity::Error, fieldId, std::move(what), std::move(detail), std::move(action));
}

void addWarning(std::vector<ValidationIssue> &issues, const char *fieldId, std::string what, std::string action,
		std::string detail = {})
{
	addIssue(issues, Severity::Warning, fieldId, std::move(what), std::move(detail), std::move(action));
}

} // namespace

bool hasErrors(const std::vector<ValidationIssue> &issues)
{
	return std::any_of(issues.begin(), issues.end(),
			   [](const ValidationIssue &issue) { return issue.severity == Severity::Error; });
}

DestinationConfig ProviderBase::newDestination() const
{
	DestinationConfig config = defaults_;
	config.provider = info_.id;
	config.name = info_.displayName;
	config.enabled = true;
	if (info_.userSuppliesServer) {
		config.serverId = kCustomServerId;
	} else if (config.serverId.empty() && !servers_.empty()) {
		config.serverId = servers_.front().id;
	}
	config.video.orientation = limits_.preferredOrientation;
	return config;
}

Endpoint ProviderBase::endpoint(const DestinationConfig &config) const
{
	Endpoint out;
	if (!info_.userSuppliesServer && config.serverId != kCustomServerId) {
		for (const ServerOption &server : servers_) {
			if (server.id == config.serverId) {
				out.serverUrl = server.url;
				break;
			}
		}
	}
	if (out.serverUrl.empty())
		out.serverUrl = trim(config.serverUrl);
	out.tls = startsWithNoCase(out.serverUrl, "rtmps://");
	return out;
}

SecretString ProviderBase::publishKey(const SecretString &key, bool /*privateTest*/) const
{
	return key.clone();
}

std::string ProviderBase::rejectedAdvice() const
{
	return tr("Stop.Rejected.Action", "Check your server URL and stream key.");
}

void ProviderBase::validateExtra(const DestinationConfig &, const ValidationContext &,
				 std::vector<ValidationIssue> &) const
{
}

std::vector<ValidationIssue> ProviderBase::validate(const DestinationConfig &config,
						    const ValidationContext &context) const
{
	std::vector<ValidationIssue> issues;
	const std::string &provider = info_.displayName;

	if (trim(config.name).empty()) {
		addError(issues, field::kName, tr("Validate.Name.Empty", "This destination has no name."),
			 tr("Validate.Name.Empty.Action", "Enter a name so you can tell your destinations apart."));
	}

	// ---- Server ----------------------------------------------------------------------------
	const bool usesListedServer = !info_.userSuppliesServer && config.serverId != kCustomServerId;
	if (usesListedServer) {
		const bool known = std::any_of(servers_.begin(), servers_.end(), [&](const ServerOption &server) {
			return server.id == config.serverId;
		});
		if (!known) {
			addError(issues, field::kServer,
				 trf("Validate.Server.NotPicked", "{0} has no server selected.", provider),
				 tr("Validate.Server.NotPicked.Action", "Pick a server from the list."));
		}
	} else {
		const StreamUrlResult parsed = parseStreamUrl(config.serverUrl);
		switch (parsed.problem) {
		case StreamUrlProblem::None:
			break;
		case StreamUrlProblem::Empty:
			addError(issues, field::kServer,
				 trf("Validate.Server.Empty", "{0} has no server URL.", provider),
				 info_.tlsRequired ? tr("Validate.Server.Empty.ActionTls",
							"Enter the server URL. It starts with rtmps://.")
						   : tr("Validate.Server.Empty.Action",
							"Enter the server URL. It starts with rtmp:// or rtmps://."));
			break;
		case StreamUrlProblem::TooLong:
			addError(issues, field::kServer,
				 tr("Validate.Server.TooLong", "The server URL is longer than 2048 characters."),
				 tr("Validate.Server.TooLong.Action", "Copy the URL again from your streaming service."));
			break;
		case StreamUrlProblem::Whitespace:
			addError(issues, field::kServer,
				 tr("Validate.Server.Whitespace", "The server URL contains a space or a line break."),
				 tr("Validate.Server.Whitespace.Action",
				    "Remove it. Copy the URL again when you are unsure."));
			break;
		case StreamUrlProblem::MissingScheme:
			addError(issues, field::kServer,
				 tr("Validate.Server.NoScheme", "The server URL does not say which protocol to use."),
				 tr("Validate.Server.NoScheme.Action",
				    "Start the URL with rtmp:// or rtmps://."));
			break;
		case StreamUrlProblem::UnsupportedScheme:
			addError(issues, field::kServer,
				 trf("Validate.Server.BadScheme", "RelayDock cannot stream to {0}:// addresses.",
				     parsed.url.scheme),
				 tr("Validate.Server.BadScheme.Action",
				    "RelayDock streams over RTMP and RTMPS. Use an rtmp:// or rtmps:// address."));
			break;
		case StreamUrlProblem::MissingHost:
		case StreamUrlProblem::InvalidHost:
			addError(issues, field::kServer,
				 tr("Validate.Server.BadHost", "The server URL has no valid host name."),
				 tr("Validate.Server.BadHost.Action",
				    "Check the part right after rtmp:// or rtmps://."));
			break;
		case StreamUrlProblem::InvalidPort:
			addError(issues, field::kServer,
				 tr("Validate.Server.BadPort", "The port in the server URL is not valid."),
				 tr("Validate.Server.BadPort.Action", "Use a number from 1 to 65535."));
			break;
		}

		if (parsed.ok()) {
			if (info_.tlsRequired && !parsed.url.tls()) {
				addError(issues, field::kServer,
					 trf("Validate.Server.NeedsTls", "{0} needs an encrypted RTMPS address.",
					     provider),
					 info_.supportsAuth
						 ? tr("Validate.Server.NeedsTls.ActionCustom",
						      "Use an address that starts with rtmps://. For a plain rtmp:// server, add a Custom RTMP destination instead.")
						 : tr("Validate.Server.NeedsTls.Action",
						      "Use an address that starts with rtmps://."));
			}
			if (info_.tlsForbidden && parsed.url.tls()) {
				addError(issues, field::kServer,
					 tr("Validate.Server.IsTls", "This address uses RTMPS."),
					 tr("Validate.Server.IsTls.Action",
					    "Add it as a Custom RTMPS destination, so the card shows that the connection is encrypted."));
			}
			if (parsed.url.hasUserInfo) {
				addError(issues, field::kServer,
					 tr("Validate.Server.UserInfo",
					    "The server URL contains a user name or password."),
					 info_.supportsAuth
						 ? tr("Validate.Server.UserInfo.ActionAuth",
						      "Remove them from the URL and enter them under Authentication.")
						 : tr("Validate.Server.UserInfo.Action", "Remove them from the URL."),
					 tr("Validate.Server.UserInfo.Detail",
					    "OBS writes the server URL to its log file."));
			}
			if (!parsed.url.query.empty()) {
				addWarning(issues, field::kServer,
					   tr("Validate.Server.Query", "The server URL contains a question mark."),
					   tr("Validate.Server.Query.Action",
					      "Move secret values to the Stream key field."),
					   tr("Validate.Server.Query.Detail",
					      "OBS writes the full server URL to its log file, including everything after the question mark."));
			}
			if (parsed.url.path.empty() || parsed.url.path == "/") {
				addWarning(issues, field::kServer,
					   tr("Validate.Server.NoApp", "The server URL has no application name."),
					   tr("Validate.Server.NoApp.Action",
					      "Most servers expect one after the host, for example rtmp://example.com/live."));
			}
		}
	}

	// ---- Credentials -----------------------------------------------------------------------
	if (!context.hasStreamKey) {
		if (info_.streamKeyRequired) {
			addError(issues, field::kStreamKey,
				 trf("Validate.Key.Missing", "{0} has no stream key.", config.name.empty() ? provider : config.name),
				 trf("Validate.Key.Missing.Action", "Enter the stream key from {0}.", provider));
		} else {
			addWarning(issues, field::kStreamKey, tr("Validate.Key.Optional", "No stream key is set."),
				   tr("Validate.Key.Optional.Action",
				      "Most servers need one. Leave it empty only when your server says so."));
		}
	}

	if (config.useAuth) {
		if (!info_.supportsAuth) {
			addError(issues, field::kUsername,
				 trf("Validate.Auth.Unsupported", "{0} does not use a user name and password.",
				     provider),
				 tr("Validate.Auth.Unsupported.Action", "Turn Authentication off."));
		} else {
			if (trim(config.username).empty()) {
				addError(issues, field::kUsername,
					 tr("Validate.Auth.NoUser", "Authentication is on but the user name is empty."),
					 tr("Validate.Auth.NoUser.Action",
					    "Enter the user name or turn Authentication off."));
			}
			if (!context.hasPassword) {
				addError(issues, field::kPassword,
					 tr("Validate.Auth.NoPassword", "Authentication is on but no password is saved."),
					 tr("Validate.Auth.NoPassword.Action",
					    "Enter the password or turn Authentication off."));
			}
		}
	}

	// ---- Video -----------------------------------------------------------------------------
	const VideoSettings &video = config.video;

	if (video.bitrateKbps < range::kMinVideoBitrateKbps || video.bitrateKbps > range::kMaxVideoBitrateKbps) {
		addError(issues, field::kVideoBitrate,
			 trf("Validate.VideoBitrate.Range", "Video bitrate {0} Kbps is out of range.", video.bitrateKbps),
			 trf("Validate.VideoBitrate.Range.Action", "Use a value from {0} to {1} Kbps.",
			     range::kMinVideoBitrateKbps, range::kMaxVideoBitrateKbps));
	} else if (limits_.maxVideoBitrateKbps > 0 && video.bitrateKbps > limits_.maxVideoBitrateKbps) {
		addWarning(issues, field::kVideoBitrate,
			   trf("Validate.VideoBitrate.Limit", "{0} accepts up to {1} Kbps of video.", provider,
			       limits_.maxVideoBitrateKbps),
			   trf("Validate.VideoBitrate.Limit.Action", "Lower the video bitrate to {0} Kbps or less.",
			       limits_.maxVideoBitrateKbps),
			   trf("Validate.VideoBitrate.Limit.Detail", "This destination is set to {0} Kbps.",
			       video.bitrateKbps));
	}

	const bool usesCanvasSize = video.width == 0 && video.height == 0;
	const bool halfResolution = (video.width == 0) != (video.height == 0);
	if (halfResolution) {
		addError(issues, field::kResolution,
			 tr("Validate.Resolution.Half", "The resolution has only a width or only a height."),
			 tr("Validate.Resolution.Half.Action", "Enter both, or pick Same as canvas."));
	} else if (!usesCanvasSize) {
		const bool inRange = video.width >= range::kMinDimension && video.width <= range::kMaxWidth &&
				     video.height >= range::kMinDimension && video.height <= range::kMaxHeight;
		if (!inRange) {
			addError(issues, field::kResolution,
				 trf("Validate.Resolution.Range", "Resolution {0}x{1} is out of range.", video.width,
				     video.height),
				 trf("Validate.Resolution.Range.Action",
				     "Use a width and height from {0} to {1} pixels.", range::kMinDimension,
				     range::kMaxWidth));
		} else if (video.width % 2 != 0 || video.height % 2 != 0) {
			addError(issues, field::kResolution,
				 trf("Validate.Resolution.Odd", "Resolution {0}x{1} has an odd width or height.",
				     video.width, video.height),
				 tr("Validate.Resolution.Odd.Action",
				    "Video encoders need even numbers. Change the size by one pixel."));
		} else {
			const int longEdge = std::max(video.width, video.height);
			const int shortEdge = std::min(video.width, video.height);
			if ((limits_.maxLongEdge > 0 && longEdge > limits_.maxLongEdge) ||
			    (limits_.maxShortEdge > 0 && shortEdge > limits_.maxShortEdge)) {
				addWarning(issues, field::kResolution,
					   trf("Validate.Resolution.Limit", "{0} accepts up to {1}x{2}.", provider,
					       limits_.maxLongEdge, limits_.maxShortEdge),
					   tr("Validate.Resolution.Limit.Action", "Pick a smaller resolution."),
					   trf("Validate.Resolution.Limit.Detail",
					       "This destination is set to {0}x{1}.", video.width, video.height));
			}

			const bool isVertical = video.height > video.width;
			if (video.orientation == Orientation::Vertical && !isVertical) {
				addError(issues, field::kOrientation,
					 tr("Validate.Orientation.NotVertical",
					    "This destination uses the vertical canvas but its resolution is wider than it is tall."),
					 tr("Validate.Orientation.NotVertical.Action",
					    "Pick a vertical resolution such as 1080x1920, or switch the destination to horizontal."));
			} else if (video.orientation == Orientation::Horizontal && isVertical) {
				addError(issues, field::kOrientation,
					 tr("Validate.Orientation.NotHorizontal",
					    "This destination uses the horizontal canvas but its resolution is taller than it is wide."),
					 tr("Validate.Orientation.NotHorizontal.Action",
					    "Pick a horizontal resolution such as 1920x1080, or switch the destination to vertical."));
			}
		}
	}

	if (video.orientation == Orientation::Vertical && !limits_.verticalSupported) {
		addWarning(issues, field::kOrientation,
			   trf("Validate.Orientation.Unsupported", "{0} does not document support for vertical video.",
			       provider),
			   tr("Validate.Orientation.Unsupported.Action",
			      "Switch this destination to horizontal unless you know the platform accepts it."));
	}

	if (video.fps < 0 || video.fps > range::kMaxFps) {
		addError(issues, field::kFps, trf("Validate.Fps.Range", "Frame rate {0} is out of range.", video.fps),
			 trf("Validate.Fps.Range.Action", "Use a value from 1 to {0}, or pick Same as OBS.",
			     range::kMaxFps));
	} else if (limits_.maxFps > 0 && video.fps > limits_.maxFps) {
		addWarning(issues, field::kFps,
			   trf("Validate.Fps.Limit", "{0} accepts up to {1} FPS.", provider, limits_.maxFps),
			   trf("Validate.Fps.Limit.Action", "Lower the frame rate to {0} FPS or less.", limits_.maxFps),
			   trf("Validate.Fps.Limit.Detail", "This destination is set to {0} FPS.", video.fps));
	}

	if (video.keyframeIntervalSec < 0 || video.keyframeIntervalSec > range::kMaxKeyframeIntervalSec) {
		addError(issues, field::kKeyframe,
			 trf("Validate.Keyframe.Range", "Keyframe interval {0} s is out of range.",
			     video.keyframeIntervalSec),
			 trf("Validate.Keyframe.Range.Action", "Use a value from 0 to {0} seconds. 0 lets the encoder decide.",
			     range::kMaxKeyframeIntervalSec));
	} else if (limits_.maxKeyframeIntervalSec > 0 &&
		   (video.keyframeIntervalSec == 0 || video.keyframeIntervalSec > limits_.maxKeyframeIntervalSec)) {
		addWarning(issues, field::kKeyframe,
			   trf("Validate.Keyframe.Limit", "{0} needs a keyframe at least every {1} seconds.", provider,
			       limits_.maxKeyframeIntervalSec),
			   trf("Validate.Keyframe.Limit.Action", "Set the keyframe interval to {0} seconds.",
			       limits_.keyframeIntervalSec > 0 ? limits_.keyframeIntervalSec
							       : limits_.maxKeyframeIntervalSec));
	} else if (limits_.keyframeIntervalSec > 0 && limits_.maxKeyframeIntervalSec == 0 &&
		   !info_.userSuppliesServer && video.keyframeIntervalSec != limits_.keyframeIntervalSec) {
		addWarning(issues, field::kKeyframe,
			   trf("Validate.Keyframe.Recommended", "{0} recommends a keyframe every {1} seconds.", provider,
			       limits_.keyframeIntervalSec),
			   trf("Validate.Keyframe.Recommended.Action", "Set the keyframe interval to {0} seconds.",
			       limits_.keyframeIntervalSec));
	}

	if (video.bFrames < -1 || video.bFrames > range::kMaxBFrames) {
		addError(issues, field::kBFrames,
			 trf("Validate.BFrames.Range", "B-frames value {0} is out of range.", video.bFrames),
			 trf("Validate.BFrames.Range.Action", "Use a value from 0 to {0}, or pick Encoder default.",
			     range::kMaxBFrames));
	}

	// ---- Audio -----------------------------------------------------------------------------
	const AudioSettings &audio = config.audio;
	if (audio.bitrateKbps < range::kMinAudioBitrateKbps || audio.bitrateKbps > range::kMaxAudioBitrateKbps) {
		addError(issues, field::kAudioBitrate,
			 trf("Validate.AudioBitrate.Range", "Audio bitrate {0} Kbps is out of range.", audio.bitrateKbps),
			 trf("Validate.AudioBitrate.Range.Action", "Use a value from {0} to {1} Kbps.",
			     range::kMinAudioBitrateKbps, range::kMaxAudioBitrateKbps));
	} else if (limits_.maxAudioBitrateKbps > 0 && audio.bitrateKbps > limits_.maxAudioBitrateKbps) {
		addWarning(issues, field::kAudioBitrate,
			   trf("Validate.AudioBitrate.Limit", "{0} accepts up to {1} Kbps of audio.", provider,
			       limits_.maxAudioBitrateKbps),
			   trf("Validate.AudioBitrate.Limit.Action", "Lower the audio bitrate to {0} Kbps or less.",
			       limits_.maxAudioBitrateKbps));
	}
	if (audio.track < 1 || audio.track > range::kMaxAudioTrack) {
		addError(issues, field::kAudioTrack,
			 trf("Validate.AudioTrack.Range", "Audio track {0} does not exist.", audio.track),
			 trf("Validate.AudioTrack.Range.Action", "Pick a track from 1 to {0}.", range::kMaxAudioTrack));
	}

	// ---- Connection ------------------------------------------------------------------------
	const ConnectionSettings &connection = config.connection;
	if (connection.reconnectAttempts < 1 || connection.reconnectAttempts > range::kMaxReconnectAttempts ||
	    connection.reconnectDelaySec < 1 || connection.reconnectDelaySec > range::kMaxReconnectDelaySec) {
		addError(issues, field::kReconnect,
			 tr("Validate.Reconnect.Range", "The reconnect settings are out of range."),
			 trf("Validate.Reconnect.Range.Action",
			     "Use 1 to {0} attempts and a delay from 1 to {1} seconds.", range::kMaxReconnectAttempts,
			     range::kMaxReconnectDelaySec));
	}
	if (connection.streamDelaySec < 0 || connection.streamDelaySec > range::kMaxStreamDelaySec) {
		addError(issues, field::kStreamDelay,
			 trf("Validate.Delay.Range", "Stream delay {0} s is out of range.", connection.streamDelaySec),
			 trf("Validate.Delay.Range.Action", "Use a value from 0 to {0} seconds.",
			     range::kMaxStreamDelaySec));
	}

	validateExtra(config, context, issues);
	return issues;
}

UserMessage ProviderBase::describeStop(const std::string &destinationName, StopReason reason,
				       std::string_view obsError) const
{
	UserMessage message;
	const std::string &name = destinationName.empty() ? info_.displayName : destinationName;

	// OBS error text can quote the server address. Never trust it to be free of secrets.
	const std::string detail = globalRedactor().redact(trim(obsError));

	switch (reason) {
	case StopReason::UserStopped:
		return message;
	case StopReason::BadPath:
		message.what = trf("Stop.BadPath", "{0} has an invalid server URL.", name);
		message.action = tr("Stop.BadPath.Action", "Check the server URL in the destination settings.");
		break;
	case StopReason::ConnectFailed:
		message.what = trf("Stop.ConnectFailed", "{0} could not connect to the server.", name);
		message.action = tr("Stop.ConnectFailed.Action",
				    "Check your internet connection and the server URL. A firewall or VPN can block RTMP.");
		break;
	case StopReason::InvalidStream:
		message.what = trf("Stop.Rejected", "{0} rejected the connection.", name);
		message.action = rejectedAdvice();
		break;
	case StopReason::Disconnected:
		message.what = trf("Stop.Disconnected", "{0} lost its connection.", name);
		message.action = tr("Stop.Disconnected.Action",
				    "Check your internet connection, then start this destination again.");
		break;
	case StopReason::EncodeError:
		message.what = trf("Stop.EncodeError", "{0} stopped because the video encoder failed.", name);
		message.action = tr("Stop.EncodeError.Action",
				    "Pick a different encoder for this destination, or update your graphics driver.");
		break;
	case StopReason::Unsupported:
		message.what = trf("Stop.Unsupported", "{0} cannot use the selected encoder settings.", name);
		message.action = tr("Stop.Unsupported.Action", "Pick a different encoder or a lower resolution.");
		break;
	case StopReason::HdrDisabled:
		message.what = trf("Stop.Hdr", "{0} cannot stream the HDR color format OBS is set to.", name);
		message.action = tr("Stop.Hdr.Action",
				    "Pick an encoder that supports HDR, or set an SDR color format in OBS under Settings, Advanced.");
		break;
	case StopReason::NoSpace:
	case StopReason::Error:
	case StopReason::Unknown:
		message.what = trf("Stop.Error", "{0} stopped because of an output error.", name);
		message.action = tr("Stop.Error.Action", "Open the OBS log under Help, Log Files for details.");
		break;
	}

	message.detail = detail;
	return message;
}

} // namespace rd
