// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "build_info.h"

#include "build_info_generated.h"

#include <cstring>

namespace rd {

const BuildInfo &buildInfo()
{
	static const BuildInfo info = {
		RD_GEN_DISPLAY_NAME,
		RD_GEN_VERSION,
		RD_GEN_VERSION_NUMERIC,
		RD_GEN_VERSION_MAJOR,
		RD_GEN_VERSION_MINOR,
		RD_GEN_VERSION_PATCH,
		RD_GEN_BUILD_NUMBER,
		RD_GEN_COMMIT,
		RD_GEN_DIRTY != 0,
		RD_GEN_BUILD_DATE,
		RD_GEN_BUILD_YEAR,
		RD_GEN_OBS_MINIMUM_VERSION,
		RD_GEN_OBS_TESTED_VERSIONS,
		RD_GEN_REPOSITORY,
		RD_GEN_AUTHOR,
#if defined(_M_X64) || defined(__x86_64__)
		"x64",
#elif defined(_M_ARM64) || defined(__aarch64__)
		"arm64",
#else
		"unknown",
#endif
	};
	return info;
}

std::string buildVersionString()
{
	const BuildInfo &info = buildInfo();
	std::string out = info.version;
	out += "+";
	out += std::to_string(info.buildNumber);
	if (info.commit[0] != '\0') {
		out += ".";
		out += info.commit;
	}
	if (info.dirty)
		out += ".dirty";
	return out;
}

bool isDevelopmentBuild()
{
	const BuildInfo &info = buildInfo();
	return info.dirty || std::strchr(info.version, '-') != nullptr;
}

std::string repositoryUrl()
{
	const BuildInfo &info = buildInfo();
	if (info.repository[0] == '\0')
		return {};
	return std::string("https://github.com/") + info.repository;
}

} // namespace rd
