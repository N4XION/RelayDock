// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "providers/provider.h"

namespace rd {

// Shared behaviour for providers. A provider fills in info_, limits_, servers_ and defaults_
// in its constructor and overrides a hook only where the platform differs.
class ProviderBase : public IProvider {
public:
	const ProviderInfo &info() const override { return info_; }
	const ProviderLimits &limits() const override { return limits_; }
	std::vector<ServerOption> servers() const override { return servers_; }

	DestinationConfig newDestination() const override;
	std::vector<ValidationIssue> validate(const DestinationConfig &config,
					      const ValidationContext &context) const override;
	Endpoint endpoint(const DestinationConfig &config) const override;
	SecretString publishKey(const SecretString &key, bool privateTest) const override;
	UserMessage describeStop(const std::string &destinationName, StopReason reason,
				 std::string_view obsError) const override;
	std::vector<std::string> setupNotes() const override { return {}; }

protected:
	// Extra checks for one platform. Called after the shared checks.
	virtual void validateExtra(const DestinationConfig &config, const ValidationContext &context,
				   std::vector<ValidationIssue> &issues) const;

	// What the user should check when the platform refuses the stream key.
	virtual std::string rejectedAdvice() const;

	ProviderInfo info_;
	ProviderLimits limits_;
	std::vector<ServerOption> servers_;
	DestinationConfig defaults_; // Template for newDestination()
};

// Limits RelayDock enforces for every provider.
namespace range {
inline constexpr int kMinVideoBitrateKbps = 200;
inline constexpr int kMaxVideoBitrateKbps = 100000;
inline constexpr int kMinAudioBitrateKbps = 32;
inline constexpr int kMaxAudioBitrateKbps = 512;
inline constexpr int kMinDimension = 128;
inline constexpr int kMaxWidth = 7680;
inline constexpr int kMaxHeight = 7680;
inline constexpr int kMaxFps = 240;
inline constexpr int kMaxKeyframeIntervalSec = 20;
inline constexpr int kMaxBFrames = 16;
inline constexpr int kMaxAudioTrack = 6;
inline constexpr int kMaxReconnectAttempts = 1000;
inline constexpr int kMaxReconnectDelaySec = 300;
inline constexpr int kMaxStreamDelaySec = 1800;
} // namespace range

} // namespace rd
