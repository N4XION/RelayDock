// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <string>
#include <vector>

namespace rd {

// Which canvas a destination streams from.
//   Horizontal  The OBS main canvas, as shown in the OBS preview.
//   Vertical    RelayDock's own 9:16 canvas, arranged in the vertical layout editor.
enum class Orientation { Horizontal, Vertical };

const char *orientationName(Orientation orientation);
bool orientationFromName(const std::string &name, Orientation &out);

// "auto" lets RelayDock pick the encoder. Any other value is an OBS encoder id.
inline constexpr const char *kAutoEncoder = "auto";

struct VideoSettings {
	Orientation orientation = Orientation::Horizontal;

	// Output size in pixels. 0 x 0 means "the canvas size".
	int width = 0;
	int height = 0;

	// Frames per second. 0 means "the OBS frame rate". OBS can only encode every Nth frame,
	// so other values must divide the OBS frame rate evenly (60 -> 30, 20, 15).
	int fps = 0;

	std::string encoder = kAutoEncoder;
	int bitrateKbps = 6000;
	std::string rateControl = "CBR";
	std::string preset;  // Empty means the encoder default.
	std::string profile; // Empty means the encoder default.
	int keyframeIntervalSec = 2;
	int bFrames = -1; // -1 means the encoder default.

	// Extra encoder options as "name=value name=value". Passed to encoders that accept them.
	std::string customOptions;

	bool operator==(const VideoSettings &other) const = default;
};

struct AudioSettings {
	std::string encoder = kAutoEncoder;
	int bitrateKbps = 160;
	int track = 1; // OBS audio track, 1 to 6.

	bool operator==(const AudioSettings &other) const = default;
};

struct ConnectionSettings {
	bool autoReconnect = true;
	int reconnectAttempts = 20; // How many times to retry after a lost connection.
	int reconnectDelaySec = 2;  // Wait before the first retry. Later waits grow by 1.5x.
	int streamDelaySec = 0;     // 0 turns stream delay off.
	std::string bindIp;         // Empty means the default network interface.

	bool operator==(const ConnectionSettings &other) const = default;
};

// Settings the user can protect from automatic changes.
enum class LockableSetting { Resolution, Fps, Bitrate, Encoder, AspectRatio };

const char *lockableSettingName(LockableSetting setting);

struct SettingLocks {
	bool resolution = false;
	bool fps = false;
	bool bitrate = false;
	bool encoder = false;
	bool aspectRatio = true;

	bool isLocked(LockableSetting setting) const;
	void setLocked(LockableSetting setting, bool locked);

	bool operator==(const SettingLocks &other) const = default;
};

// One configured streaming destination. Holds no secrets. The stream key and the RTMP
// password live in the credential store under the destination id.
struct DestinationConfig {
	std::string id;       // UUID
	std::string provider; // Provider id, for example "twitch" or "custom_rtmp"
	std::string name;     // Label shown on the card
	bool enabled = true;

	std::string serverId;  // Id of a provider server option, or "custom"
	std::string serverUrl; // Used when serverId is "custom" or the provider has no list

	bool useAuth = false; // RTMP user name and password, custom servers only
	std::string username;

	VideoSettings video;
	AudioSettings audio;
	ConnectionSettings connection;
	SettingLocks locks;

	bool autoOptimize = true;     // Automatic optimisation may suggest or apply changes
	std::string verticalLayoutId; // Vertical layout to use when orientation is Vertical

	bool operator==(const DestinationConfig &other) const = default;
};

inline constexpr const char *kCustomServerId = "custom";

// Aspect ratio as a reduced fraction, for display. 1920x1080 -> "16:9".
std::string aspectRatioText(int width, int height);

// True when the two sizes have the same aspect ratio within one pixel of rounding.
bool sameAspectRatio(int widthA, int heightA, int widthB, int heightB);

} // namespace rd
