// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "settings/app_config.h"

#include <string>
#include <string_view>
#include <vector>

namespace rd {

// Turns the configuration into JSON text and back.
//
// Reading is forgiving on purpose. A missing key or a value of the wrong type falls back to
// its default, so a hand-edited or partly damaged file still loads. Text that is not JSON at
// all is an error.

struct ConfigParseResult {
	bool ok = false;
	AppConfig config;
	std::string error;              // Set when ok is false
	std::vector<std::string> notes; // Repairs and migrations that were applied
	int fileSchemaVersion = 0;      // The version the file was written with
	bool newerSchema = false;       // Written by a newer RelayDock than this one
};

std::string serializeConfig(const AppConfig &config);
ConfigParseResult parseConfig(std::string_view jsonText);

// One destination as JSON. Used by Duplicate, by tests and by the diagnostic export.
std::string serializeDestination(const DestinationConfig &destination);
bool parseDestination(std::string_view jsonText, DestinationConfig &out);

} // namespace rd
