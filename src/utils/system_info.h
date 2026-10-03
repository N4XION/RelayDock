// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <string>

namespace rd {

// Facts about the PC for the diagnostics export. Nothing here identifies the user: no computer
// name, no user name, no serial numbers, no network addresses.
struct SystemInfo {
	std::string osVersion; // "Windows 11 24H2 (build 26200)"
	std::string cpuName;
	int cpuCores = 0;
	int cpuThreads = 0;
	int ramMb = 0;
};

SystemInfo querySystemInfo();

} // namespace rd
