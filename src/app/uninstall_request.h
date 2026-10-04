// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "utils/uninstall.h"

#include <string>

namespace rd {

// An uninstall the user asked for from inside RelayDock.
//
// start() makes a request file and runs the uninstaller, which waits for OBS to close and then
// removes RelayDock, as long as the request file is still there. cancel() deletes the file. The
// waiting uninstaller then ends without removing anything, whether OBS closes a second later
// or an hour later.
//
// Nothing happens when OBS shuts down: the file stays, and the uninstaller goes ahead.
//
// Thread ownership: OBS UI thread.
class UninstallRequest {
public:
	UninstallRequest() = default;
	UninstallRequest(const UninstallRequest &) = delete;
	UninstallRequest &operator=(const UninstallRequest &) = delete;

	// Returns false with a reason for the user. Nothing is started then.
	bool start(const UninstallPlan &plan, std::string &error);
	void cancel();
	bool pending() const { return !requestFile_.empty(); }
	const std::string &requestFile() const { return requestFile_; }

private:
	std::string requestFile_;
};

} // namespace rd
