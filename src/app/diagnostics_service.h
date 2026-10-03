// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "diagnostics/preflight.h"
#include "diagnostics/report.h"
#include "network/bandwidth.h"

#include <string>
#include <vector>

namespace rd {

class AppContext;

// Gathers the facts the pure checks need (diagnostics/preflight.h, diagnostics/report.h,
// network/bandwidth.h) from OBS and from RelayDock's own state. Everything here is a read.
//
// Thread ownership: OBS UI thread.

// The upload budget of the enabled destinations plus any other destination that is active.
BandwidthBudget currentBandwidth(AppContext &app);

// The pre-stream check for the given destinations. An empty list means every enabled one.
PreflightInput gatherPreflightInput(AppContext &app, std::vector<std::string> destinationIds = {});
PreflightReport runPreflightNow(AppContext &app, std::vector<std::string> destinationIds = {});

// The text behind "Export diagnostics". Contains no stream key and no password.
std::string buildDiagnosticsNow(AppContext &app);

// The server a destination connects to, in a form that is safe to show: scheme, host, port
// and application name only.
std::string safeServerText(AppContext &app, const DestinationConfig &config);

} // namespace rd
