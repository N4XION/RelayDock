// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "settings/migrations.h"

#include <nlohmann/json.hpp>

#include <format>

namespace rd {

void MigrationChain::add(MigrationStep step)
{
	steps_.push_back(std::move(step));
}

MigrationChain::Result MigrationChain::run(nlohmann::json &root, int fromVersion, int targetVersion) const
{
	Result result;
	result.finalVersion = fromVersion;

	while (result.finalVersion < targetVersion) {
		const MigrationStep *next = nullptr;
		for (const MigrationStep &step : steps_) {
			if (step.fromVersion == result.finalVersion) {
				next = &step;
				break;
			}
		}
		if (!next) {
			result.ok = false;
			result.error = std::format("No migration from configuration version {} to {}.",
						   result.finalVersion, result.finalVersion + 1);
			return result;
		}

		try {
			next->apply(root);
		} catch (const std::exception &error) {
			result.ok = false;
			result.error = std::format("Migration from configuration version {} failed: {}",
						   result.finalVersion, error.what());
			return result;
		}

		result.applied.push_back(next->description);
		++result.finalVersion;
		root["schema_version"] = result.finalVersion;
	}

	return result;
}

const MigrationChain &configMigrations()
{
	// Version 1 is the first released format, so there is nothing to migrate from yet.
	static const MigrationChain chain;
	return chain;
}

} // namespace rd
