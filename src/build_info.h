// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <string>

namespace rd {

// Identity of this build. Values come from buildspec.json and Git at configure time.
struct BuildInfo {
	const char *displayName;        // "RelayDock"
	const char *version;            // "1.0.0-rc.1"
	const char *versionNumeric;     // "1.0.0"
	int versionMajor;
	int versionMinor;
	int versionPatch;
	int buildNumber;                // Commits reachable from HEAD. 0 outside a Git checkout.
	const char *commit;             // Short commit hash. Empty outside a Git checkout.
	bool dirty;                     // Built from a work tree with uncommitted changes.
	const char *buildDate;          // "YYYY-MM-DD", UTC, taken from the commit.
	const char *buildYear;
	const char *obsMinimumVersion;  // Oldest OBS Studio version this build loads in.
	const char *obsTestedVersions;  // OBS Studio versions this release was tested with.
	const char *repository;         // "owner/name" on GitHub. Empty when not configured.
	const char *twitchClientId;     // Id of the Twitch application for chat sign-in. Public. May be empty.
	const char *author;
	const char *architecture;       // "x64"
};

const BuildInfo &buildInfo();

// "1.0.0-rc.1+57.a1b2c3d4e". Adds ".dirty" for uncommitted work trees.
std::string buildVersionString();

// True for pre-release versions and for builds from an uncommitted work tree.
bool isDevelopmentBuild();

// "https://github.com/owner/name". Empty when no repository is configured.
std::string repositoryUrl();

} // namespace rd
