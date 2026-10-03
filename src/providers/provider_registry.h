// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "providers/provider.h"

#include <memory>
#include <vector>

namespace rd {

// The list of platforms RelayDock can stream to. The interface asks the registry what to
// offer under Add Platform, so adding a provider here is all it takes to make it appear.
//
// Thread ownership: built once at plugin load on the OBS UI thread and read-only afterwards.
class ProviderRegistry {
public:
	ProviderRegistry() = default;
	ProviderRegistry(const ProviderRegistry &) = delete;
	ProviderRegistry &operator=(const ProviderRegistry &) = delete;

	// Returns false and keeps the existing provider when the id is empty or already taken.
	bool add(std::unique_ptr<IProvider> provider);

	// nullptr when no provider has this id.
	const IProvider *find(const std::string &id) const;

	// In registration order, which is the order shown under Add Platform.
	std::vector<const IProvider *> all() const;

	size_t size() const { return providers_.size(); }

private:
	std::vector<std::unique_ptr<IProvider>> providers_;
};

// Adds Twitch, TikTok, YouTube, Facebook, Custom RTMP and Custom RTMPS.
void registerBuiltInProviders(ProviderRegistry &registry);

} // namespace rd
