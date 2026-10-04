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

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rd {

class OutputManager;
class PerformanceMonitor;
class VerticalCanvasManager;

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
	// A frontend event other than "finished loading" and "exit".
	void onFrontendEvent(int event);
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
	VerticalCanvasManager &vertical() { return *vertical_; }
	PerformanceMonitor &performance() { return *performance_; }
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
	// A new destination that is not part of the settings yet, for the editor. Has an id and a
	// name no other destination uses.
	DestinationConfig draftDestination(const std::string &providerId) const;
	// Replaces the destination with the same id, or adds it at the end.
	void upsertDestination(const DestinationConfig &destination);
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
	// The same, with `candidate` standing in for the saved destination of that id, or added
	// when the id is new. The editor uses it to show what unsaved settings would do.
	std::vector<EffectiveDestination> resolveEffectiveWith(const DestinationConfig &candidate) const;

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
	// OBS is closing. Windows close and timers stop before anything is released.
	void shuttingDown();

private:
	SteadyClock clock_;
	AppConfig config_;
	std::unique_ptr<ConfigStore> store_;
	ProviderRegistry providers_;
	std::unique_ptr<SecretVault> vault_;
	EncoderCatalog encoders_;
	std::unique_ptr<OutputManager> outputs_;
	std::unique_ptr<VerticalCanvasManager> vertical_;
	std::unique_ptr<PerformanceMonitor> performance_;
	std::map<std::string, Adjustment> adjustments_;
	std::vector<std::string> loadNotes_;
	ConfigLoadStatus loadStatus_ = ConfigLoadStatus::CreatedDefault;
	bool shutDown_ = false;

	// A source appeared, went away or changed its name. Vertical layouts refer to sources,
	// so they bind again. Calls are folded into one, on the UI thread.
	void queueVerticalRebind();
	static void onSourceListChanged(void *data, calldata_t *params);

	OBSSignal sourceCreateSignal_;
	OBSSignal sourceRemoveSignal_;
	OBSSignal sourceRenameSignal_;
	std::atomic<bool> rebindQueued_{false};
	bool collectionChanging_ = false;
};

} // namespace rd
