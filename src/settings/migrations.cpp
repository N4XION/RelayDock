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
	static const MigrationChain chain = [] {
		MigrationChain steps;

		// 1 to 2. Version 1 had the update check at start-up off by default, and its file
		// cannot tell a choice from that default. From version 2 the check is on unless the
		// user switches it off, so it is switched on once here. The Privacy Policy changed
		// with it, and RelayDock asks the user to review the new text.
		steps.add({1, "The update check when OBS starts is now on. Switch it off under Settings, Updates.",
			   [](nlohmann::json &root) {
				   if (!root.contains("general") || !root["general"].is_object())
					   root["general"] = nlohmann::json::object();
				   root["general"]["check_updates_on_start"] = true;
			   }});

		return steps;
	}();
	return chain;
}

} // namespace rd
