// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "utils/system_info.h"

#include "utils/strings.h"

#include <windows.h>

#include <format>
#include <vector>

namespace rd {

namespace {

std::string narrow(const std::wstring &text)
{
	if (text.empty())
		return {};
	const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<size_t>(length), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr);
	return out;
}

std::string readRegistryString(HKEY root, const wchar_t *key, const wchar_t *value)
{
	wchar_t buffer[256] = {};
	DWORD bytes = sizeof(buffer) - sizeof(wchar_t);
	if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buffer, &bytes) != ERROR_SUCCESS)
		return {};
	return trim(narrow(buffer));
}

std::string osVersionText()
{
	// GetVersionEx reports what the application manifest claims. RtlGetVersion reports the truth.
	using RtlGetVersionFn = LONG(WINAPI *)(OSVERSIONINFOEXW *);
	OSVERSIONINFOEXW info{};
	info.dwOSVersionInfoSize = sizeof(info);
	const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	const auto rtlGetVersion =
		ntdll ? reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void *>(GetProcAddress(ntdll, "RtlGetVersion"))) : nullptr;
	if (!rtlGetVersion || rtlGetVersion(&info) != 0)
		return "Windows";

	// Windows 11 still calls itself version 10.0. The build number tells them apart.
	const char *name = info.dwMajorVersion == 10 && info.dwBuildNumber >= 22000 ? "Windows 11"
			   : info.dwMajorVersion == 10                             ? "Windows 10"
										   : "Windows";
	const std::string display =
		readRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion");
	return display.empty() ? std::format("{} (build {})", name, info.dwBuildNumber)
			       : std::format("{} {} (build {})", name, display, info.dwBuildNumber);
}

int physicalCores()
{
	DWORD bytes = 0;
	GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
	if (bytes == 0)
		return 0;
	std::vector<unsigned char> buffer(bytes);
	auto *first = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(buffer.data());
	if (!GetLogicalProcessorInformationEx(RelationProcessorCore, first, &bytes))
		return 0;

	int cores = 0;
	for (DWORD offset = 0; offset < bytes;) {
		const auto *entry = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(buffer.data() + offset);
		if (entry->Relationship == RelationProcessorCore)
			++cores;
		if (entry->Size == 0)
			break;
		offset += entry->Size;
	}
	return cores;
}

} // namespace

SystemInfo querySystemInfo()
{
	SystemInfo info;
	info.osVersion = osVersionText();
	info.cpuName = readRegistryString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
					  L"ProcessorNameString");
	info.cpuCores = physicalCores();
	info.cpuThreads = static_cast<int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));

	MEMORYSTATUSEX memory{};
	memory.dwLength = sizeof(memory);
	if (GlobalMemoryStatusEx(&memory))
		info.ramMb = static_cast<int>(memory.ullTotalPhys / (1024ull * 1024ull));
	return info;
}

} // namespace rd
