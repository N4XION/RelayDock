// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "providers/provider_registry.h"

#include "providers/custom_rtmp/custom_rtmp_provider.h"
#include "providers/facebook/facebook_provider.h"
#include "providers/tiktok/tiktok_provider.h"
#include "providers/twitch/twitch_provider.h"
#include "providers/youtube/youtube_provider.h"

namespace rd {

bool ProviderRegistry::add(std::unique_ptr<IProvider> provider)
{
	if (!provider || provider->info().id.empty() || find(provider->info().id))
		return false;
	providers_.push_back(std::move(provider));
	return true;
}

const IProvider *ProviderRegistry::find(const std::string &id) const
{
	for (const auto &provider : providers_) {
		if (provider->info().id == id)
			return provider.get();
	}
	return nullptr;
}

std::vector<const IProvider *> ProviderRegistry::all() const
{
	std::vector<const IProvider *> out;
	out.reserve(providers_.size());
	for (const auto &provider : providers_)
		out.push_back(provider.get());
	return out;
}

void registerBuiltInProviders(ProviderRegistry &registry)
{
	registry.add(std::make_unique<TwitchProvider>());
	registry.add(std::make_unique<TikTokProvider>());
	registry.add(std::make_unique<YouTubeProvider>());
	registry.add(std::make_unique<FacebookProvider>());
	registry.add(std::make_unique<CustomRtmpProvider>(CustomRtmpProvider::Transport::Rtmp));
	registry.add(std::make_unique<CustomRtmpProvider>(CustomRtmpProvider::Transport::Rtmps));
}

} // namespace rd
