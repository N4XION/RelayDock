// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "providers/provider_base.h"

namespace rd {

// A server the user runs or rents: any RTMP or RTMPS ingest that is not one of the built-in
// platforms. Custom RTMP and Custom RTMPS are separate entries so the card always says
// whether the connection is encrypted.
class CustomRtmpProvider final : public ProviderBase {
public:
	enum class Transport { Rtmp, Rtmps };

	explicit CustomRtmpProvider(Transport transport);

	static constexpr const char *kRtmpId = "custom_rtmp";
	static constexpr const char *kRtmpsId = "custom_rtmps";
};

} // namespace rd
