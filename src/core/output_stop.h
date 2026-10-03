// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

namespace rd {

// Why an output stopped. Mirrors the OBS_OUTPUT_* codes from libobs (obs-defs.h) so the
// logic that turns a stop into a message can be tested without OBS.
enum class StopReason {
	UserStopped,   // OBS_OUTPUT_SUCCESS (0)
	BadPath,       // OBS_OUTPUT_BAD_PATH (-1)
	ConnectFailed, // OBS_OUTPUT_CONNECT_FAILED (-2)
	InvalidStream, // OBS_OUTPUT_INVALID_STREAM (-3)
	Error,         // OBS_OUTPUT_ERROR (-4)
	Disconnected,  // OBS_OUTPUT_DISCONNECTED (-5)
	Unsupported,   // OBS_OUTPUT_UNSUPPORTED (-6)
	NoSpace,       // OBS_OUTPUT_NO_SPACE (-7)
	EncodeError,   // OBS_OUTPUT_ENCODE_ERROR (-8)
	HdrDisabled,   // OBS_OUTPUT_HDR_DISABLED (-9)
	Unknown,
};

constexpr StopReason stopReasonFromObsCode(int code)
{
	switch (code) {
	case 0:
		return StopReason::UserStopped;
	case -1:
		return StopReason::BadPath;
	case -2:
		return StopReason::ConnectFailed;
	case -3:
		return StopReason::InvalidStream;
	case -4:
		return StopReason::Error;
	case -5:
		return StopReason::Disconnected;
	case -6:
		return StopReason::Unsupported;
	case -7:
		return StopReason::NoSpace;
	case -8:
		return StopReason::EncodeError;
	case -9:
		return StopReason::HdrDisabled;
	default:
		return StopReason::Unknown;
	}
}

constexpr const char *stopReasonName(StopReason reason)
{
	switch (reason) {
	case StopReason::UserStopped:
		return "user_stopped";
	case StopReason::BadPath:
		return "bad_path";
	case StopReason::ConnectFailed:
		return "connect_failed";
	case StopReason::InvalidStream:
		return "invalid_stream";
	case StopReason::Error:
		return "error";
	case StopReason::Disconnected:
		return "disconnected";
	case StopReason::Unsupported:
		return "unsupported";
	case StopReason::NoSpace:
		return "no_space";
	case StopReason::EncodeError:
		return "encode_error";
	case StopReason::HdrDisabled:
		return "hdr_disabled";
	case StopReason::Unknown:
		return "unknown";
	}
	return "unknown";
}

} // namespace rd
