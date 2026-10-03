// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <nlohmann/json_fwd.hpp>

#include <functional>
#include <string>
#include <vector>

namespace rd {

// Upgrades a configuration file written by an older RelayDock, one schema version at a time.
//
// To change the file format:
//   1. Raise kConfigSchemaVersion in app_config.h.
//   2. Add a step here that turns version N into version N + 1.
//   3. Add a test with a version N file in tests/unit/test_settings.cpp.
// Steps work on the raw JSON, before it is read into AppConfig, so they can rename and move
// keys freely. A step never deletes data it does not understand.

struct MigrationStep {
	int fromVersion = 0; // Applies to files with this version and produces fromVersion + 1
	std::string description;
	std::function<void(nlohmann::json &root)> apply;
};

class MigrationChain {
public:
	void add(MigrationStep step);

	struct Result {
		bool ok = true;
		int finalVersion = 0;
		std::string error;
		std::vector<std::string> applied; // Descriptions of the steps that ran
	};

	// Runs every step from `fromVersion` up to `targetVersion`. Fails when a step is missing,
	// because guessing at a file format would risk the user's settings.
	Result run(nlohmann::json &root, int fromVersion, int targetVersion) const;

	size_t size() const { return steps_.size(); }

private:
	std::vector<MigrationStep> steps_;
};

// The steps for RelayDock's own configuration file.
const MigrationChain &configMigrations();

} // namespace rd
