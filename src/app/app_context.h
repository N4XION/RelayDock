// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "encoders/encoder_catalog.h"
#include "performance/effective_settings.h"
#include "providers/provider_registry.h"
#include "security/secret_vault.h"
#include "settings/app_config.h"
#include "settings/config_store.h"
#include "utils/clock.h"

#include <QObject>

#include <obs.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rd {

class OutputManager;

// Owns everything RelayDock keeps alive while OBS runs: settings, providers, the credential
// vault, the encoder catalog and the output manager. The interface talks to this object.
//
// Lifetime: created in obs_module_load, shut down on OBS_FRONTEND_EVENT_EXIT (while libobs is
// still fully alive), destroyed in obs_module_unload.
//
// Thread ownership: OBS UI thread.
class AppContext : public QObject {
	Q_OBJECT

public:
	AppContext();
	~AppContext() override;

	// Loads settings, registers providers and opens the credential store.
	void initialize();
	// OBS finished loading its modules, so the encoder list is complete now.
	void onObsFinishedLoading();
	// OBS is exiting. Stops every output and releases every OBS object.
	void shutdown();
	bool isShutDown() const { return shutDown_; }

	AppConfig &config() { return config_; }
	const AppConfig &config() const { return config_; }
	ProviderRegistry &providers() { return providers_; }
	const ProviderRegistry &providers() const { return providers_; }
	SecretVault &vault() { return *vault_; }
	EncoderCatalog &encoders() { return encoders_; }
	OutputManager &outputs() { return *outputs_; }
	const IClock &clock() const { return clock_; }
	const ConfigStore &configStore() const { return *store_; }

	// Notes from loading the settings: repairs, migrations, recovery from a backup.
	const std::vector<std::string> &loadNotes() const { return loadNotes_; }
	ConfigLoadStatus loadStatus() const { return loadStatus_; }

	// Writes the settings to disk. Logs and returns false on failure.
	bool saveConfig();
	// Saves and tells the interface that something changed.
	void notifyConfigChanged();

	// ---- Destinations -----------------------------------------------------------------------
	DestinationConfig *addDestination(const std::string &providerId);
	DestinationConfig *duplicateDestination(const std::string &id);
	// Stops the destination, deletes its saved secrets and removes it.
	bool removeDestination(const std::string &id);
	bool moveDestination(const std::string &id, int newIndex);

	// Whether the destination's stream key and password are saved.
	ValidationContext secretsPresent(const std::string &id) const;
	// Problems that keep a destination from starting, plus warnings.
	std::vector<ValidationIssue> validateDestination(const std::string &id) const;

	// ---- Streaming --------------------------------------------------------------------------
	StreamContext streamContext() const;

	// Effective settings of every enabled destination. `alsoId` adds one destination that is
	// not enabled, for starting it by hand.
	std::vector<EffectiveDestination> resolveEffective(const std::string &alsoId = {}) const;

	// Reductions the optimiser currently applies to a destination.
	Adjustment adjustmentFor(const std::string &id) const;
	void setAdjustment(const std::string &id, const Adjustment &adjustment);
	void clearAdjustments();

	// The frames an encoder reads: the OBS main video for horizontal destinations, a vertical
	// canvas for vertical ones. Every acquire needs a matching release.
	video_t *acquireVideo(const EffectiveVideo &video, std::string &error);
	void releaseVideo(const EffectiveVideo &video);

Q_SIGNALS:
	// Destinations or settings changed.
	void configChanged();

private:
	SteadyClock clock_;
	AppConfig config_;
	std::unique_ptr<ConfigStore> store_;
	ProviderRegistry providers_;
	std::unique_ptr<SecretVault> vault_;
	EncoderCatalog encoders_;
	std::unique_ptr<OutputManager> outputs_;
	std::map<std::string, Adjustment> adjustments_;
	std::vector<std::string> loadNotes_;
	ConfigLoadStatus loadStatus_ = ConfigLoadStatus::CreatedDefault;
	bool shutDown_ = false;
};

} // namespace rd
