// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "settings/config_store.h"

#include "settings/config_json.h"
#include "utils/log.h"
#include "utils/paths.h"

#include <chrono>
#include <ctime>
#include <format>
#include <system_error>

namespace rd {

namespace {

std::string fileTimestamp()
{
	const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::tm utc{};
#ifdef _WIN32
	gmtime_s(&utc, &now);
#else
	gmtime_r(&now, &utc);
#endif
	char buffer[32];
	std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &utc);
	return buffer;
}

// Reads and parses one file. Returns false when it is missing, too large or not usable.
bool tryLoad(const std::filesystem::path &path, ConfigParseResult &parsed)
{
	std::string text;
	if (!readFileToString(path, text, ConfigStore::kMaxFileBytes))
		return false;
	parsed = parseConfig(text);
	return parsed.ok;
}

} // namespace

ConfigStore::ConfigStore(std::filesystem::path directory) : directory_(std::move(directory)) {}

ConfigLoadResult ConfigStore::load()
{
	ConfigLoadResult result;
	std::error_code ec;

	const std::filesystem::path main = configPath();
	const std::filesystem::path backup = backupPath();
	const bool mainExists = std::filesystem::exists(main, ec);

	if (mainExists) {
		ConfigParseResult parsed;
		if (tryLoad(main, parsed)) {
			result.status = ConfigLoadStatus::Loaded;
			result.config = std::move(parsed.config);
			result.notes = std::move(parsed.notes);
			result.newerSchema = parsed.newerSchema;
			if (parsed.newerSchema)
				newerSchemaToPreserve_ = parsed.fileSchemaVersion;
			return result;
		}

		// Keep the damaged file. The user or a developer may still get data out of it.
		std::filesystem::path preserved = directory_ / std::format("config.corrupt-{}.json", fileTimestamp());
		std::filesystem::rename(main, preserved, ec);
		if (!ec) {
			result.preservedFile = preserved;
			result.notes.push_back(std::format("config.json could not be read ({}). It was kept as {}.",
							   parsed.error.empty() ? "unreadable file" : parsed.error,
							   pathToUtf8(preserved.filename())));
		} else {
			result.notes.push_back("config.json could not be read and could not be set aside.");
		}
	}

	ConfigParseResult fromBackup;
	if (tryLoad(backup, fromBackup)) {
		result.status = ConfigLoadStatus::RecoveredFromBackup;
		result.config = std::move(fromBackup.config);
		for (std::string &note : fromBackup.notes)
			result.notes.push_back(std::move(note));
		result.notes.push_back("Settings were restored from config.json.bak, the copy from before the last save.");
		result.newerSchema = fromBackup.newerSchema;
		return result;
	}

	result.config = makeDefaultConfig();
	if (mainExists) {
		result.status = ConfigLoadStatus::ResetAfterCorruption;
		result.notes.push_back("No usable backup was found. RelayDock started with default settings.");
	} else {
		result.status = ConfigLoadStatus::CreatedDefault;
	}
	return result;
}

ConfigSaveResult ConfigStore::save(const AppConfig &config)
{
	ConfigSaveResult result;
	std::error_code ec;

	std::filesystem::create_directories(directory_, ec);
	if (ec) {
		result.error = std::format("Could not create the settings folder: {}", ec.message());
		return result;
	}

	const std::filesystem::path main = configPath();

	if (std::filesystem::exists(main, ec)) {
		if (newerSchemaToPreserve_ > 0) {
			// About to write this version's format over a newer one. Keep the original.
			const std::filesystem::path preserved =
				directory_ / std::format("config.newer-v{}.json", newerSchemaToPreserve_);
			std::filesystem::copy_file(main, preserved, std::filesystem::copy_options::overwrite_existing, ec);
			if (ec) {
				result.error = std::format(
					"Could not keep a copy of the newer settings file, so nothing was saved: {}",
					ec.message());
				return result;
			}
			newerSchemaToPreserve_ = 0;
		}

		// Only a readable file may become the backup. A damaged one must not replace a good backup.
		ConfigParseResult current;
		if (tryLoad(main, current)) {
			std::filesystem::copy_file(main, backupPath(), std::filesystem::copy_options::overwrite_existing,
						   ec);
			if (ec)
				logWarning("Could not update config.json.bak: {}", ec.message());
		}
	}

	std::string error;
	if (!writeFileAtomically(main, serializeConfig(config), error)) {
		result.error = error;
		return result;
	}

	result.ok = true;
	return result;
}

} // namespace rd
