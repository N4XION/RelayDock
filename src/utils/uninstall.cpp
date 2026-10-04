// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "utils/uninstall.h"

#include "utils/strings.h"

#include <windows.h>

namespace rd {

namespace {

// The identities installer/relaydock.iss gives RelayDock in Windows: the real one, and the one
// of a test build of Setup.
constexpr const wchar_t *kUninstallKey =
	L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{6D1F3C52-8B0A-4E7D-A3C9-52E0B7F41D68}_is1";
constexpr const wchar_t *kTestUninstallKey =
	L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{6D1F3C52-8B0A-4E7D-A3C9-52E0B7F41D69}_is1";

// Lower case, backslashes, no backslash at the end. Two spellings of one folder compare equal.
std::string comparable(const std::string &path)
{
	std::string out = toLower(replaceAll(path, "/", "\\"));
	while (out.size() > 3 && out.back() == '\\')
		out.pop_back();
	return out;
}

bool endsWith(const std::string &text, const std::string &suffix)
{
	return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string withoutSuffix(const std::string &path, size_t suffixLength)
{
	return path.substr(0, path.size() - suffixLength);
}

std::string narrow(const std::wstring &text)
{
	if (text.empty())
		return {};
	const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<size_t>(length), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr);
	return out;
}

bool readValue(HKEY key, const wchar_t *name, std::string &out)
{
	wchar_t buffer[1024];
	DWORD size = sizeof(buffer) - sizeof(wchar_t);
	DWORD type = 0;
	if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size) != ERROR_SUCCESS || type != REG_SZ)
		return false;
	buffer[size / sizeof(wchar_t)] = L'\0';
	out = narrow(buffer);
	return true;
}

void readCopy(HKEY root, const wchar_t *subKey, REGSAM view, std::vector<InstalledCopy> &out)
{
	HKEY key = nullptr;
	if (RegOpenKeyExW(root, subKey, 0, KEY_READ | view, &key) != ERROR_SUCCESS)
		return;

	InstalledCopy copy;
	std::string command;
	if (readValue(key, L"UninstallString", command) && readValue(key, L"InstallLocation", copy.folder)) {
		// The command is the program in quotes.
		command = trim(command);
		if (command.size() >= 2 && command.front() == '"') {
			const size_t close = command.find('"', 1);
			if (close != std::string::npos)
				command = command.substr(1, close - 1);
		}
		copy.uninstaller = command;
		if (!copy.uninstaller.empty() && !copy.folder.empty())
			out.push_back(std::move(copy));
	}
	RegCloseKey(key);
}

} // namespace

UninstallPlan planUninstall(const std::string &modulePath, const std::vector<InstalledCopy> &installed, bool removeData)
{
	UninstallPlan plan;
	const std::string module = comparable(modulePath);

	// The layout the installer and the ZIP use: <folder>\bin\64bit\relaydock.dll.
	const std::string standard = "\\bin\\64bit\\relaydock.dll";
	// The layout inside a portable OBS: <obs>\obs-plugins\64bit\relaydock.dll.
	const std::string portable = "\\obs-plugins\\64bit\\relaydock.dll";

	if (endsWith(module, standard)) {
		const std::string folder = withoutSuffix(module, standard.size());
		for (const InstalledCopy &copy : installed) {
			if (comparable(copy.folder) != folder || copy.uninstaller.empty())
				continue;
			plan.kind = UninstallPlan::Kind::Installer;
			plan.program = copy.uninstaller;
			// Silent, because RelayDock has asked already. The uninstaller waits for OBS.
			plan.arguments = std::string("/WAITFOROBS=1 /SILENT /REMOVEDATA=") + (removeData ? "1" : "0");
			return plan;
		}
		// The same spelling the user sees in File Explorer.
		plan.paths = {withoutSuffix(replaceAll(modulePath, "/", "\\"), standard.size())};
		return plan;
	}

	const std::string original = replaceAll(modulePath, "/", "\\");
	if (endsWith(module, portable)) {
		const std::string obs = withoutSuffix(original, portable.size());
		plan.paths = {original, obs + "\\data\\obs-plugins\\relaydock"};
		return plan;
	}

	// A build that runs from somewhere else, such as a developer's build folder.
	plan.paths = {original};
	return plan;
}

std::string withRequestFile(const std::string &arguments, const std::string &requestFile)
{
	return arguments + " /REQUESTFILE=\"" + requestFile + "\"";
}

std::vector<InstalledCopy> readInstalledCopies(bool testInstalls)
{
	std::vector<InstalledCopy> copies;
	for (const wchar_t *subKey : {kUninstallKey, kTestUninstallKey}) {
		if (subKey == kTestUninstallKey && !testInstalls)
			continue;
		// Setup without administrator rights writes to the user's part of the registry. With
		// them it writes to the machine's part.
		readCopy(HKEY_CURRENT_USER, subKey, 0, copies);
		readCopy(HKEY_LOCAL_MACHINE, subKey, KEY_WOW64_64KEY, copies);
		readCopy(HKEY_LOCAL_MACHINE, subKey, KEY_WOW64_32KEY, copies);
	}
	return copies;
}

} // namespace rd
