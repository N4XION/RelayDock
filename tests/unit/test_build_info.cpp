// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include <doctest/doctest.h>

#include "build_info.h"

#include <nlohmann/json.hpp>

#include <cstring>
#include <fstream>
#include <string>

TEST_SUITE("build_info")
{
	TEST_CASE("version fields are present and consistent")
	{
		const rd::BuildInfo &info = rd::buildInfo();
		CHECK(std::string(info.displayName) == "RelayDock");
		CHECK(std::strlen(info.version) > 0);

		const std::string numeric = std::to_string(info.versionMajor) + "." +
					    std::to_string(info.versionMinor) + "." +
					    std::to_string(info.versionPatch);
		CHECK(numeric == info.versionNumeric);
		CHECK(std::string(info.version).rfind(numeric, 0) == 0);
	}

	TEST_CASE("build date is an ISO date and matches the build year")
	{
		const rd::BuildInfo &info = rd::buildInfo();
		const std::string date = info.buildDate;
		REQUIRE(date.size() == 10);
		CHECK(date[4] == '-');
		CHECK(date[7] == '-');
		CHECK(date.substr(0, 4) == info.buildYear);
	}

	TEST_CASE("build version string carries the build number")
	{
		const rd::BuildInfo &info = rd::buildInfo();
		const std::string full = rd::buildVersionString();
		CHECK(full.rfind(info.version, 0) == 0);
		CHECK(full.find("+" + std::to_string(info.buildNumber)) != std::string::npos);
	}

	TEST_CASE("pre-release versions count as development builds")
	{
		const rd::BuildInfo &info = rd::buildInfo();
		if (std::strchr(info.version, '-') != nullptr)
			CHECK(rd::isDevelopmentBuild());
	}

	TEST_CASE("the build carries what buildspec.json says")
	{
		// A build folder that kept an older value would ship without its update check, or with
		// the wrong version. This reads the file the build was configured from.
		std::ifstream file(std::string(RD_SOURCE_DIR) + "/buildspec.json");
		REQUIRE(file.good());
		const nlohmann::json spec = nlohmann::json::parse(file, nullptr, false);
		REQUIRE(spec.is_object());

		const rd::BuildInfo &info = rd::buildInfo();
		CHECK(std::string(info.repository) == spec.value("repository", std::string("missing")));
		CHECK(std::string(info.twitchClientId) == spec.value("twitchClientId", std::string()));
		CHECK(std::string(info.versionNumeric) == spec.value("version", std::string("missing")));
		CHECK(std::string(info.obsMinimumVersion) == spec["obs"].value("minimumVersion", std::string("missing")));

		const std::string url = rd::repositoryUrl();
		if (std::string(info.repository).empty())
			CHECK(url.empty());
		else
			CHECK(url == "https://github.com/" + std::string(info.repository));
	}

	TEST_CASE("this build targets x64")
	{
		CHECK(std::string(rd::buildInfo().architecture) == "x64");
	}
}
