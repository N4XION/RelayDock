// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "core/output_stop.h"
#include "core/types.h"
#include "core/user_message.h"
#include "security/secret_string.h"

#include <string>
#include <string_view>
#include <vector>

namespace rd {

// A streaming platform, or a kind of custom server.
//
// To add a platform, implement IProvider (most providers derive from ProviderBase and only
// fill in data), register it in registerBuiltInProviders(), and add a guide under docs/.
// Nothing else in RelayDock knows the list of platforms. See CONTRIBUTING.md.

// A predefined ingest server the user can pick.
struct ServerOption {
	std::string id;   // Stable id stored in the configuration
	std::string name; // Shown to the user
	std::string url;
};

// How a destination can be checked without going live.
enum class TestSupport {
	// RelayDock can check that the server accepts a network connection. It cannot check
	// the stream key, because RTMP only validates the key when a stream starts.
	Reachability,
	// The platform accepts a real stream that viewers never see, so the key is checked too.
	PrivateStream,
};

// Technical limits a platform publishes. 0 means "the platform does not document a limit".
// docs/research/platform-requirements.md records the source and date of every value.
struct ProviderLimits {
	int maxVideoBitrateKbps = 0;
	int maxAudioBitrateKbps = 0;
	int maxLongEdge = 0;  // Longer side of the frame in pixels
	int maxShortEdge = 0; // Shorter side of the frame in pixels
	int maxFps = 0;

	// What the platform's own guides suggest when they give no hard limit. RelayDock uses
	// these as the ceiling for settings it picks itself. A locked setting may go higher.
	int recommendedVideoBitrateKbps = 0;
	int recommendedFps = 0;

	int keyframeIntervalSec = 2;    // Recommended. 0 means no recommendation.
	int maxKeyframeIntervalSec = 0; // Largest accepted. 0 means not documented.
	std::vector<std::string> videoCodecs = {"h264"};
	bool verticalSupported = true;
	Orientation preferredOrientation = Orientation::Horizontal;
	std::string checkedOn; // Date the limits were last checked, "YYYY-MM-DD"
	std::string source;    // Official page the limits come from
};

struct ProviderInfo {
	std::string id;          // Stable id, lower case, for example "twitch"
	std::string displayName; // "Twitch"
	std::string monogram;    // One or two letters for the badge on the card
	std::string accentColor; // "#RRGGBB", used for the badge

	// The user types the server URL (custom servers, and platforms that hand out a
	// server URL together with each stream key).
	bool userSuppliesServer = false;
	bool tlsRequired = false;  // Only rtmps:// addresses are valid
	bool tlsForbidden = false; // Only rtmp:// addresses are valid
	bool supportsAuth = false; // RTMP user name and password
	bool streamKeyRequired = true;

	TestSupport testSupport = TestSupport::Reachability;

	std::string keyHelpUrl; // Official page that explains where to find the stream key
	std::string guide;      // Guide in this repository, for example "docs/twitch.md"
};

// What the validator knows about secrets without seeing them.
struct ValidationContext {
	bool hasStreamKey = false;
	bool hasPassword = false;
};

// Field ids used in ValidationIssue::field.
namespace field {
inline constexpr const char *kName = "name";
inline constexpr const char *kServer = "server";
inline constexpr const char *kStreamKey = "stream_key";
inline constexpr const char *kUsername = "username";
inline constexpr const char *kPassword = "password";
inline constexpr const char *kResolution = "video.resolution";
inline constexpr const char *kFps = "video.fps";
inline constexpr const char *kVideoBitrate = "video.bitrate";
inline constexpr const char *kKeyframe = "video.keyframe";
inline constexpr const char *kBFrames = "video.bframes";
inline constexpr const char *kOrientation = "video.orientation";
inline constexpr const char *kAudioBitrate = "audio.bitrate";
inline constexpr const char *kAudioTrack = "audio.track";
inline constexpr const char *kReconnect = "connection.reconnect";
inline constexpr const char *kStreamDelay = "connection.delay";
} // namespace field

struct ValidationIssue {
	Severity severity = Severity::Error; // Errors block Start. Warnings do not.
	std::string field;
	UserMessage message;
};

bool hasErrors(const std::vector<ValidationIssue> &issues);

struct Endpoint {
	std::string serverUrl; // The address handed to the OBS RTMP output
	bool tls = false;
};

class IProvider {
public:
	virtual ~IProvider() = default;

	virtual const ProviderInfo &info() const = 0;
	virtual const ProviderLimits &limits() const = 0;

	// Predefined servers. Empty when the user supplies the address.
	virtual std::vector<ServerOption> servers() const = 0;

	// Settings for a new destination of this provider. The caller assigns the id.
	virtual DestinationConfig newDestination() const = 0;

	// Problems with a destination. No Error-severity entries means it can start.
	virtual std::vector<ValidationIssue> validate(const DestinationConfig &config,
						      const ValidationContext &context) const = 0;

	// Where to connect. Only meaningful when validate() reports no errors.
	virtual Endpoint endpoint(const DestinationConfig &config) const = 0;

	// The key to publish with. A provider may adjust it, for example to request a private
	// test stream. `privateTest` is only true when info().testSupport is PrivateStream.
	virtual SecretString publishKey(const SecretString &key, bool privateTest) const = 0;

	// Explains why a stream stopped and what to check. `obsError` is the last error text
	// from the OBS output and may be empty. The result never contains a secret.
	virtual UserMessage describeStop(const std::string &destinationName, StopReason reason,
					 std::string_view obsError) const = 0;

	// Things about this platform a streamer should know before going live, in the order to
	// show them. For example that a stream key works once, or that the platform goes public
	// the moment the encoder connects.
	virtual std::vector<std::string> setupNotes() const = 0;
};

} // namespace rd
