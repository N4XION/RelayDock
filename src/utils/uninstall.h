// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <string>
#include <vector>

namespace rd {

// Removing RelayDock from inside RelayDock.
//
// A plugin cannot delete itself while OBS has it loaded. So RelayDock starts the uninstaller
// that the installer left behind, and the uninstaller waits until OBS has closed. RelayDock
// makes a request file for it. The uninstaller removes RelayDock only while that file exists,
// so deleting the file is how the user changes their mind, at any moment before OBS closes.
//
// A RelayDock that was copied by hand has no uninstaller. For that one RelayDock says which
// files to delete.

// What Windows records about one RelayDock that the installer put on this PC.
struct InstalledCopy {
	std::string uninstaller; // Full path of the uninstall program
	std::string folder;      // The folder RelayDock was installed into
};

struct UninstallPlan {
	enum class Kind {
		Installer, // The installer put this RelayDock here, and its uninstaller removes it
		ByHand,    // The files were copied by hand and have to be deleted by hand
	};

	Kind kind = Kind::ByHand;
	std::string program;            // Installer: the uninstall program
	std::string folder;             // Installer: the folder RelayDock was installed into
	// Installer: its command line. The request file is added when the uninstall starts.
	std::string arguments;
	std::vector<std::string> paths; // ByHand: the files and folders to delete, after OBS closed
};

// Decides how the RelayDock that runs from `modulePath` (the full path of relaydock.dll) is
// removed. An entry of `installed` counts only when it is the entry of this very copy, so a
// RelayDock in a portable OBS never starts the uninstaller of another one.
UninstallPlan planUninstall(const std::string &modulePath, const std::vector<InstalledCopy> &installed, bool removeData);

// The command line with the request file added. installer/relaydock.iss reads the same switch.
std::string withRequestFile(const std::string &arguments, const std::string &requestFile);

// What Windows holds under "Installed apps" for RelayDock. With `testInstalls`, also what a
// test build of Setup left there, which only a test build of RelayDock asks for.
std::vector<InstalledCopy> readInstalledCopies(bool testInstalls);

} // namespace rd
