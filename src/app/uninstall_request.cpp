// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "app/uninstall_request.h"

#include "utils/i18n.h"
#include "utils/log.h"
#include "utils/uuid.h"

#include <windows.h>

#include <shellapi.h>

namespace rd {

namespace {

std::wstring widen(const std::string &text)
{
	if (text.empty())
		return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring out(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
	return out;
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

} // namespace

bool UninstallRequest::start(const UninstallPlan &plan, std::string &error)
{
	if (pending())
		return true;
	if (plan.kind != UninstallPlan::Kind::Installer || plan.program.empty()) {
		error = loc("Uninstall.Error.None", "This RelayDock has no uninstaller.");
		return false;
	}

	const std::wstring program = widen(plan.program);
	if (GetFileAttributesW(program.c_str()) == INVALID_FILE_ATTRIBUTES) {
		error = locf("Uninstall.Error.Missing", "The uninstaller is missing: {0}. Uninstall RelayDock from Windows Settings, Apps, Installed apps.",
			     plan.program);
		return false;
	}

	// The request file first, so the uninstaller finds it when it starts.
	wchar_t temp[MAX_PATH + 1] = {};
	const DWORD length = GetTempPathW(MAX_PATH, temp);
	if (length == 0 || length > MAX_PATH) {
		error = locf("Uninstall.Error.Step", "Windows refused a step of the uninstall (error {0}).", GetLastError());
		return false;
	}
	const std::string requestFile = narrow(temp) + "RelayDock-uninstall-" + generateUuid() + ".request";
	const HANDLE file = CreateFileW(widen(requestFile).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		error = locf("Uninstall.Error.Step", "Windows refused a step of the uninstall (error {0}).", GetLastError());
		return false;
	}
	CloseHandle(file);

	// ShellExecute, because an install for all users needs administrator rights to remove, and
	// Windows asks for them this way.
	const std::wstring arguments = widen(withRequestFile(plan.arguments, requestFile));
	SHELLEXECUTEINFOW info{};
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
	info.lpVerb = L"open";
	info.lpFile = program.c_str();
	info.lpParameters = arguments.c_str();
	info.nShow = SW_SHOWNORMAL;
	if (!ShellExecuteExW(&info)) {
		const DWORD code = GetLastError();
		DeleteFileW(widen(requestFile).c_str());
		error = locf("Uninstall.Error.Start", "Windows could not start the uninstaller (error {0}).", code);
		logWarning("The uninstaller did not start. Windows error {}.", code);
		return false;
	}

	requestFile_ = requestFile;
	logInfo("Uninstall requested. The uninstaller waits for OBS to close.");
	return true;
}

void UninstallRequest::cancel()
{
	if (requestFile_.empty())
		return;
	DeleteFileW(widen(requestFile_).c_str());
	requestFile_.clear();
	logInfo("Uninstall cancelled. RelayDock stays.");
}

} // namespace rd
