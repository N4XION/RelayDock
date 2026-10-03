// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "app/app_context.h"

#include "outputs/output_manager.h"
#include "security/redactor.h"
#include "security/windows_credential_store.h"
#include "utils/i18n.h"
#include "utils/log.h"
#include "utils/paths.h"
#include "utils/strings.h"
#include "utils/uuid.h"

#include <obs-module.h>

#include <algorithm>
#include <cstdlib>

namespace rd {

namespace {

bool lookupTranslation(const char *key, const char **translation)
{
	return obs_module_get_string(key, translation);
}

std::string credentialPrefix()
{
#ifdef RELAYDOCK_TEST_HOOKS
	// Integration tests use their own prefix, so test keys never mix with real ones.
	if (const char *prefix = std::getenv("RELAYDOCK_TEST_CREDENTIAL_PREFIX")) {
		if (*prefix)
			return prefix;
	}
#endif
	return WindowsCredentialStore::kDefaultPrefix;
}

} // namespace

AppContext::AppContext() = default;

AppContext::~AppContext()
{
	shutdown();
}

void AppContext::initialize()
{
	setTranslator(lookupTranslation);
	registerBuiltInProviders(providers_);

	char *configDir = obs_module_config_path("");
	store_ = std::make_unique<ConfigStore>(pathFromUtf8(configDir ? configDir : ""));
	bfree(configDir);

	ConfigLoadResult loaded = store_->load();
	config_ = std::move(loaded.config);
	loadNotes_ = std::move(loaded.notes);
	loadStatus_ = loaded.status;
	for (const std::string &note : loadNotes_)
		logWarning("Settings: {}", note);
	logInfo("Settings loaded: {} destination(s), performance mode {}.", config_.destinations.size(),
		performanceModeName(config_.performanceMode));

	vault_ = std::make_unique<SecretVault>(std::make_unique<WindowsCredentialStore>(credentialPrefix()),
						globalRedactor());
	// Register every saved secret with the redactor before anything can log.
	vault_->primeRedactor();

	outputs_ = std::make_unique<OutputManager>(*this);
}

void AppContext::onObsFinishedLoading()
{
	encoders_.refresh();
}

void AppContext::shutdown()
{
	if (shutDown_)
		return;
	shutDown_ = true;

	if (outputs_)
		outputs_->shutdown();
	if (store_)
		saveConfig();
}

bool AppContext::saveConfig()
{
	if (!store_)
		return false;
	const ConfigSaveResult result = store_->save(config_);
	if (!result.ok)
		logError("Could not save the settings. {}", result.error);
	return result.ok;
}

void AppContext::notifyConfigChanged()
{
	saveConfig();
	Q_EMIT configChanged();
}

// ---- Destinations --------------------------------------------------------------------------------

DestinationConfig *AppContext::addDestination(const std::string &providerId)
{
	const IProvider *provider = providers_.find(providerId);
	if (!provider)
		return nullptr;

	DestinationConfig destination = provider->newDestination();
	destination.id = generateUuid();

	// "Twitch", "Twitch 2", "Twitch 3": keep names apart so cards and log lines are clear.
	const std::string baseName = destination.name;
	int suffix = 2;
	auto nameTaken = [&](const std::string &name) {
		return std::any_of(config_.destinations.begin(), config_.destinations.end(),
				   [&](const DestinationConfig &other) { return equalsNoCase(other.name, name); });
	};
	while (nameTaken(destination.name))
		destination.name = baseName + " " + std::to_string(suffix++);

	config_.destinations.push_back(std::move(destination));
	return &config_.destinations.back();
}

DestinationConfig *AppContext::duplicateDestination(const std::string &id)
{
	const DestinationConfig *source = config_.findDestination(id);
	if (!source)
		return nullptr;

	DestinationConfig copy = *source;
	copy.id = generateUuid();
	copy.name = locf("Destination.CopyName", "{0} copy", source->name);

	// Secrets are copied too, so the duplicate works straight away.
	for (CredentialKind kind : {CredentialKind::StreamKey, CredentialKind::Password}) {
		SecretString secret;
		if (vault_->get({source->id, kind}, secret).ok())
			vault_->set({copy.id, kind}, secret);
	}

	const auto position = std::find_if(config_.destinations.begin(), config_.destinations.end(),
					   [&](const DestinationConfig &d) { return d.id == id; });
	const auto inserted = config_.destinations.insert(position + 1, std::move(copy));
	return &*inserted;
}

bool AppContext::removeDestination(const std::string &id)
{
	const auto position = std::find_if(config_.destinations.begin(), config_.destinations.end(),
					   [&](const DestinationConfig &d) { return d.id == id; });
	if (position == config_.destinations.end())
		return false;
	if (outputs_ && outputs_->runtime(id).active())
		return false; // Stop it first.

	if (outputs_)
		outputs_->forget(id);
	vault_->removeDestination(id);
	adjustments_.erase(id);
	config_.destinations.erase(position);
	return true;
}

bool AppContext::moveDestination(const std::string &id, int newIndex)
{
	auto &list = config_.destinations;
	const auto position = std::find_if(list.begin(), list.end(), [&](const DestinationConfig &d) { return d.id == id; });
	if (position == list.end())
		return false;

	const int count = static_cast<int>(list.size());
	const int clamped = std::clamp(newIndex, 0, count - 1);
	const int current = static_cast<int>(position - list.begin());
	if (clamped == current)
		return false;

	DestinationConfig moving = std::move(*position);
	list.erase(position);
	list.insert(list.begin() + clamped, std::move(moving));
	return true;
}

ValidationContext AppContext::secretsPresent(const std::string &id) const
{
	ValidationContext context;
	context.hasStreamKey = vault_->has({id, CredentialKind::StreamKey});
	context.hasPassword = vault_->has({id, CredentialKind::Password});
	return context;
}

std::vector<ValidationIssue> AppContext::validateDestination(const std::string &id) const
{
	const DestinationConfig *config = config_.findDestination(id);
	if (!config)
		return {};
	const IProvider *provider = providers_.find(config->provider);
	if (!provider)
		return {};
	return provider->validate(*config, secretsPresent(id));
}

// ---- Streaming -----------------------------------------------------------------------------------

StreamContext AppContext::streamContext() const
{
	return buildStreamContext(encoders_, config_.verticalCanvas);
}

std::vector<EffectiveDestination> AppContext::resolveEffective(const std::string &alsoId) const
{
	std::vector<ResolveInput> inputs;
	for (const DestinationConfig &destination : config_.destinations) {
		if (!destination.enabled && destination.id != alsoId)
			continue;
		ResolveInput input;
		input.config = destination;
		input.provider = providers_.find(destination.provider);
		input.adjustment = adjustmentFor(destination.id);
		inputs.push_back(std::move(input));
	}
	return resolveEffectiveSettings(inputs, streamContext(), config_.performanceMode);
}

Adjustment AppContext::adjustmentFor(const std::string &id) const
{
	const auto it = adjustments_.find(id);
	return it == adjustments_.end() ? Adjustment{} : it->second;
}

void AppContext::setAdjustment(const std::string &id, const Adjustment &adjustment)
{
	if (adjustment.none())
		adjustments_.erase(id);
	else
		adjustments_[id] = adjustment;
}

void AppContext::clearAdjustments()
{
	adjustments_.clear();
}

video_t *AppContext::acquireVideo(const EffectiveVideo &video, std::string &error)
{
	if (video.orientation == Orientation::Horizontal) {
		video_t *frames = obs_get_video();
		if (!frames)
			error = "OBS video is not running.";
		return frames;
	}

	error = "The vertical canvas is not set up.";
	return nullptr;
}

void AppContext::releaseVideo(const EffectiveVideo &)
{
	// The OBS main video needs no release.
}

} // namespace rd
