// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "settings/app_config.h"

#include <filesystem>
#include <string>
#include <vector>

namespace rd {

enum class ConfigLoadStatus {
	Loaded,               // config.json was read
	CreatedDefault,       // No file yet. This is a first start.
	RecoveredFromBackup,  // config.json was missing or damaged and config.json.bak was used
	ResetAfterCorruption, // Both files were unusable. Defaults are in effect.
};

struct ConfigLoadResult {
	ConfigLoadStatus status = ConfigLoadStatus::CreatedDefault;
	AppConfig config;
	std::vector<std::string> notes;       // Repairs, migrations and recovery steps
	std::filesystem::path preservedFile;  // Where a damaged config.json was moved to, if any
	bool newerSchema = false;             // The file came from a newer RelayDock
};

struct ConfigSaveResult {
	bool ok = false;
	std::string error;
};

// Reads and writes RelayDock's configuration file.
//
// Files in the folder:
//   config.json                    The current configuration
//   config.json.bak                The configuration before the last save
//   config.corrupt-<time>.json     A damaged file, kept so nothing is lost silently
//   config.newer-v<N>.json         A file from a newer RelayDock, kept before this version
//                                  writes its own format over it
//
// Saving is atomic: the new content goes to a temporary file that then replaces config.json.
//
// Thread ownership: used from the OBS UI thread only.
class ConfigStore {
public:
	// Larger files are treated as damaged. A real configuration is a few kilobytes.
	static constexpr size_t kMaxFileBytes = 8 * 1024 * 1024;

	explicit ConfigStore(std::filesystem::path directory);

	ConfigLoadResult load();
	ConfigSaveResult save(const AppConfig &config);

	std::filesystem::path configPath() const { return directory_ / "config.json"; }
	std::filesystem::path backupPath() const { return directory_ / "config.json.bak"; }
	const std::filesystem::path &directory() const { return directory_; }

private:
	std::filesystem::path directory_;
	int newerSchemaToPreserve_ = 0; // Non-zero until the newer file has been set aside
};

} // namespace rd
