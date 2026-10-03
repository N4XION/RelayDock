// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "diagnostics/preflight.h"
#include "encoders/encoder_caps.h"
#include "performance/effective_settings.h"
#include "security/redactor.h"
#include "settings/app_config.h"

#include <string>
#include <vector>

namespace rd {

// Builds the text file behind "Export diagnostics". The user asks for it, picks where to save
// it and decides whom to send it to. RelayDock sends it nowhere.
//
// What can never be in it
//   - Stream keys and passwords. The input has no field for them, and the finished text also
//     goes through the redactor, which removes every secret RelayDock has handled.
//   - Full server URLs. Only scheme, host, port and application name are shown.
//   - The Windows user name in file paths.
//   - RTMP user names. The report only says whether one is set.

struct DiagnosticsDestination {
	DestinationConfig config;
	std::string providerName;
	std::string serverText;     // Server as chosen: a list entry name or the typed URL
	bool hasStreamKey = false;
	bool hasPassword = false;

	std::string phase;
	std::string lastStop;
	std::string lastError;
	int reconnects = 0;
	long long liveSeconds = 0;
	int bitrateKbps = 0;
	int totalFrames = 0;
	int droppedFrames = 0;

	EffectiveDestination effective;
	std::string videoEncoderName;
	std::vector<std::string> sharedWith; // Names of destinations on the same encoder
	std::vector<std::string> issues;     // Validation messages
};

struct DiagnosticsInput {
	std::string generatedAtUtc;
	std::string relayDockVersion;
	std::string buildDate;
	std::string obsVersion;
	std::string osVersion;
	std::string cpuName;
	int cpuCores = 0;
	int cpuThreads = 0;
	int ramMb = 0;
	std::vector<std::string> gpus;

	StreamContext context;
	PerformanceMode mode = PerformanceMode::Balanced;
	OptimizerConfig optimizer;
	NetworkConfig network;
	std::string credentialBackend;
	std::string settingsFolder;
	std::vector<std::string> settingsNotes;

	double cpuPercent = -1.0;
	double renderLagPercent = -1.0;
	double memoryMb = -1.0;

	std::vector<DiagnosticsDestination> destinations;
	PreflightReport preflight;
	std::vector<std::string> logLines; // Recent RelayDock log lines, already redacted
};

std::string buildDiagnosticsReport(const DiagnosticsInput &input, const Redactor &redactor);

// Replaces the user folder name in Windows paths: C:\Users\name\... becomes C:\Users\<user>\...
std::string anonymizePaths(std::string text);

} // namespace rd
