// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include <doctest/doctest.h>

#include "utils/uninstall.h"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace rd;

namespace {

const char *kInstalledDll = R"(C:\ProgramData\obs-studio\plugins\relaydock\bin\64bit\relaydock.dll)";

std::vector<InstalledCopy> installedByTheInstaller()
{
	return {{R"(C:\ProgramData\obs-studio\plugins\relaydock\unins000.exe)", R"(C:\ProgramData\obs-studio\plugins\relaydock\)"}};
}

} // namespace

TEST_SUITE("utils.uninstall")
{
	TEST_CASE("a RelayDock the installer put there is removed by its uninstaller, which waits for OBS")
	{
		const UninstallPlan plan = planUninstall(kInstalledDll, installedByTheInstaller(), false);
		CHECK(plan.kind == UninstallPlan::Kind::Installer);
		CHECK(plan.program == R"(C:\ProgramData\obs-studio\plugins\relaydock\unins000.exe)");
		// It waits for OBS, shows no question of its own, and keeps settings and keys.
		CHECK(plan.arguments == "/WAITFOROBS=1 /SILENT /REMOVEDATA=0");
		CHECK(plan.paths.empty());
	}

	TEST_CASE("settings and keys go only when the user said so")
	{
		CHECK(planUninstall(kInstalledDll, installedByTheInstaller(), true).arguments == "/WAITFOROBS=1 /SILENT /REMOVEDATA=1");
		CHECK(planUninstall(kInstalledDll, installedByTheInstaller(), false).arguments == "/WAITFOROBS=1 /SILENT /REMOVEDATA=0");
	}

	TEST_CASE("the folder is matched whatever its spelling")
	{
		const std::vector<InstalledCopy> installed = installedByTheInstaller();
		CHECK(planUninstall("c:/programdata/OBS-Studio/plugins/RelayDock/bin/64bit/RelayDock.dll", installed, false).kind ==
		      UninstallPlan::Kind::Installer);
		// Without the backslash at the end that the installer writes.
		const std::vector<InstalledCopy> bare = {
			{R"(C:\ProgramData\obs-studio\plugins\relaydock\unins000.exe)", R"(C:\ProgramData\obs-studio\plugins\relaydock)"}};
		CHECK(planUninstall(kInstalledDll, bare, false).kind == UninstallPlan::Kind::Installer);
	}

	TEST_CASE("another RelayDock on the PC never has its uninstaller started")
	{
		// A portable OBS runs its own copy while an installed RelayDock exists elsewhere.
		const UninstallPlan portable =
			planUninstall(R"(D:\obs-portable\obs-plugins\64bit\relaydock.dll)", installedByTheInstaller(), true);
		CHECK(portable.kind == UninstallPlan::Kind::ByHand);
		CHECK(portable.program.empty());
		CHECK(portable.arguments.empty());

		// A copy made by hand into another plugins folder.
		const UninstallPlan copied =
			planUninstall(R"(D:\somewhere\relaydock\bin\64bit\relaydock.dll)", installedByTheInstaller(), false);
		CHECK(copied.kind == UninstallPlan::Kind::ByHand);

		// A folder whose name only starts the same.
		const UninstallPlan similar = planUninstall(
			R"(C:\ProgramData\obs-studio\plugins\relaydock-old\bin\64bit\relaydock.dll)", installedByTheInstaller(), false);
		CHECK(similar.kind == UninstallPlan::Kind::ByHand);

		// An entry without an uninstaller is no use.
		const std::vector<InstalledCopy> broken = {{"", R"(C:\ProgramData\obs-studio\plugins\relaydock\)"}};
		CHECK(planUninstall(kInstalledDll, broken, false).kind == UninstallPlan::Kind::ByHand);
	}

	TEST_CASE("a copy made by hand: RelayDock names what to delete")
	{
		// The ZIP's folder, copied into the plugins folder.
		const UninstallPlan zip = planUninstall(kInstalledDll, {}, false);
		CHECK(zip.kind == UninstallPlan::Kind::ByHand);
		REQUIRE(zip.paths.size() == 1);
		CHECK(zip.paths[0] == R"(C:\ProgramData\obs-studio\plugins\relaydock)");

		// The two places inside a portable OBS.
		const UninstallPlan portable = planUninstall(R"(D:\obs-portable\obs-plugins\64bit\relaydock.dll)", {}, false);
		REQUIRE(portable.paths.size() == 2);
		CHECK(portable.paths[0] == R"(D:\obs-portable\obs-plugins\64bit\relaydock.dll)");
		CHECK(portable.paths[1] == R"(D:\obs-portable\data\obs-plugins\relaydock)");

		// A build that runs from a developer's folder.
		const UninstallPlan build = planUninstall(R"(C:\build\rundir\RelWithDebInfo\bin\relaydock.dll)", {}, false);
		REQUIRE(build.paths.size() == 1);
		CHECK(build.paths[0] == R"(C:\build\rundir\RelWithDebInfo\bin\relaydock.dll)");
	}

	TEST_CASE("reading Windows' list of installed copies does not fail on a PC that has none or some")
	{
		// What this PC holds is not known to the test. Every entry it returns must be complete.
		for (const bool withTestInstalls : {false, true}) {
			for (const InstalledCopy &copy : readInstalledCopies(withTestInstalls)) {
				CHECK_FALSE(copy.uninstaller.empty());
				CHECK_FALSE(copy.folder.empty());
				CHECK(copy.uninstaller.find('"') == std::string::npos);
			}
		}
	}

	TEST_CASE("the request file is handed to the uninstaller in quotes")
	{
		CHECK(withRequestFile("/WAITFOROBS=1 /SILENT /REMOVEDATA=0", R"(C:\Users\A B\AppData\Local\Temp\RelayDock-uninstall-1.request)") ==
		      R"(/WAITFOROBS=1 /SILENT /REMOVEDATA=0 /REQUESTFILE="C:\Users\A B\AppData\Local\Temp\RelayDock-uninstall-1.request")");
	}

	TEST_CASE("the installer script and RelayDock agree on the switches and on both identities")
	{
		std::ifstream file(std::string(RD_SOURCE_DIR) + "/installer/relaydock.iss", std::ios::binary);
		const std::string script((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		REQUIRE_FALSE(script.empty());

		CHECK(script.find("{param:WAITFOROBS|0}") != std::string::npos);
		CHECK(script.find("{param:REQUESTFILE|}") != std::string::npos);
		CHECK(withRequestFile("", "f").find("/REQUESTFILE=") != std::string::npos);
		CHECK(script.find("{param:REMOVEDATA|}") != std::string::npos);
		// The identity of a real install, and the one of a test build of Setup.
		CHECK(script.find("#define AppId \"{{6D1F3C52-8B0A-4E7D-A3C9-52E0B7F41D68}\"") != std::string::npos);
		CHECK(script.find("#define AppId \"{{6D1F3C52-8B0A-4E7D-A3C9-52E0B7F41D69}\"") != std::string::npos);
	}
}
