// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "providers/provider_base.h"

namespace rd {

// Facebook Live. Limits come from Meta's Live Video API reference and Business Help pages.
// Sources and dates: docs/research/platform-requirements.md.
class FacebookProvider final : public ProviderBase {
public:
	FacebookProvider();

	static constexpr const char *kId = "facebook";

	std::vector<std::string> setupNotes() const override;

protected:
	std::string rejectedAdvice() const override;
};

} // namespace rd
