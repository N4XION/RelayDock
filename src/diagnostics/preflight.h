// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "core/user_message.h"
#include "encoders/encode_plan.h"
#include "network/bandwidth.h"
#include "performance/effective_settings.h"
#include "providers/provider.h"
#include "settings/app_config.h"

#include <string>
#include <vector>

namespace rd {

// The pre-stream check. It looks at everything RelayDock can know without connecting to a
// platform and reports READY, WARNING or FAILED, with a reason and a fix for every finding.
//
// The check itself is pure: the plugin gathers facts from OBS (PreflightInput) and this code
// judges them. That keeps every rule testable without OBS.

enum class PreflightStatus { Ready, Warning, Failed };

const char *preflightStatusName(PreflightStatus status);

struct PreflightItem {
	std::string id;            // Stable id of the rule, for example "video.no_source"
	PreflightStatus status = PreflightStatus::Ready;
	std::string title;         // Short label: a destination name or an area such as "Video"
	UserMessage message;       // What is wrong, what RelayDock knows, what to do
	std::string destinationId; // Empty for findings that concern OBS or every destination
};

struct PreflightReport {
	PreflightStatus status = PreflightStatus::Ready;
	std::vector<PreflightItem> items; // Failed first, then warnings, then what passed

	size_t count(PreflightStatus wanted) const;
};

// What the plugin learned from OBS.
struct PreflightObsFacts {
	bool videoRunning = true;       // OBS has an active video pipeline
	bool sceneHasVideo = true;      // The current scene shows at least one video source
	bool hasAudioSource = true;     // OBS has at least one audio source or device
	bool allAudioMuted = false;     // Every audio source is muted
	double renderLagPercent = -1.0; // Frames OBS missed lately. -1 when not known.
	int videoEncoderCount = 0;      // Encoders OBS offers for streaming
};

struct PreflightDestination {
	DestinationConfig config;
	const IProvider *provider = nullptr;
	std::vector<ValidationIssue> issues; // From the provider's validate()
	EffectiveDestination effective;
	bool keySessionOnly = false;        // The key is not saved in Windows, only held in memory
	int verticalMissingItems = 0;       // Layout items whose source OBS does not have
	std::string duplicateKeyOf;         // Name of another destination with the same server and key
};

struct PreflightInput {
	PreflightObsFacts obs;
	std::vector<PreflightDestination> destinations; // The destinations that are about to start
	EncodePlan plan;
	BandwidthBudget bandwidth;
	PerformanceMode mode = PerformanceMode::Balanced;
};

PreflightReport runPreflight(const PreflightInput &input);

} // namespace rd
