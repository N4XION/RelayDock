// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "app/update_install.h"

#include "utils/i18n.h"
#include "utils/log.h"
#include "utils/uninstall.h"

#include <QMetaObject>
#include <QTimer>

#include <windows.h>

#include <shellapi.h>

#include <chrono>

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

std::string folderOf(const std::string &file)
{
	const size_t slash = file.find_last_of("\\/");
	return slash == std::string::npos ? std::string() : file.substr(0, slash);
}

UserMessage stepProblem(std::string what)
{
	UserMessage message;
	message.what = std::move(what);
	message.action = loc("UpdateNow.Error.Action",
			     "Nothing was installed. Try again later, or download the installer from the release page.");
	return message;
}

} // namespace

UpdateInstall::UpdateInstall(QObject *parent) : QObject(parent) {}

UpdateInstall::~UpdateInstall()
{
	endDownload();
}

void UpdateInstall::configure(Setup setup)
{
	setup_ = std::move(setup);
	if (setup_.downloadFolder.empty())
		return;
	if (const int removed = removeUpdateLeftovers(setup_.downloadFolder); removed > 0)
		logInfo("Removed {} downloaded installer(s) that an earlier update left behind.", removed);
}

bool UpdateInstall::possible(const ReleaseInfo &offered) const
{
	return !setup_.installFolder.empty() && !setup_.downloadFolder.empty() && canDownloadUpdate(offered, setup_.rules);
}

void UpdateInstall::endDownload()
{
	if (cancel_)
		cancel_->store(true);
	if (thread_.joinable())
		thread_.join();
	cancel_.reset();
}

void UpdateInstall::fail(UserMessage message)
{
	state_ = State::Failed;
	problem_ = std::move(message);
	logWarning("The update did not go ahead. {}", problem_.text());
	Q_EMIT changed();
}

bool UpdateInstall::start(const ReleaseInfo &offered)
{
	if (busy())
		return true;
	endDownload();

	release_ = offered;
	problem_ = {};
	received_ = 0;
	total_ = offered.installerSize;
	requestFile_.clear();
	installerFile_.clear();

	if (!possible(offered)) {
		UserMessage message;
		message.what = loc("UpdateNow.Error.NoInstaller", "This release has no installer that RelayDock can check.");
		message.action = loc("UpdateNow.Error.NoInstaller.Action", "Open the release page and download the installer there.");
		fail(std::move(message));
		return false;
	}

	UpdateDownloadConfig config;
	config.rules = setup_.rules;
	config.userAgent = setup_.userAgent;
	config.folder = newUpdateFolder(setup_.downloadFolder);

	auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
	cancel_ = cancelFlag;
	state_ = State::Downloading;
	logInfo("Update now: downloading RelayDock {} from its release page.", versionOfTag(offered.tag));

	thread_ = std::thread([this, offered, config, cancelFlag] {
		// A report at most every tenth of a second, and one for the last byte.
		auto lastReport = std::chrono::steady_clock::now() - std::chrono::seconds(1);
		const DownloadProgress report = [&](uint64_t got, uint64_t expected) {
			const auto now = std::chrono::steady_clock::now();
			if (got != expected && now - lastReport < std::chrono::milliseconds(100))
				return;
			lastReport = now;
			QMetaObject::invokeMethod(
				this,
				[this, got, expected, cancelFlag] {
					if (cancelFlag->load())
						return;
					received_ = got;
					if (expected > 0)
						total_ = expected;
					Q_EMIT progressed();
				},
				Qt::QueuedConnection);
		};

		UpdateDownload download = downloadUpdate(offered, config, *cancelFlag, report);
		// A cancelled job reports nothing. Its owner has moved on or is being destroyed, and
		// endDownload() is waiting for this thread, so `this` is still valid here.
		if (cancelFlag->load()) {
			discardUpdateDownload(download);
			return;
		}
		QMetaObject::invokeMethod(
			this,
			[this, download, cancelFlag]() mutable {
				if (cancelFlag->load()) {
					discardUpdateDownload(download);
					return;
				}
				finishDownload(std::move(download));
			},
			Qt::QueuedConnection);
	});

	Q_EMIT changed();
	return true;
}

void UpdateInstall::finishDownload(UpdateDownload download)
{
	// The thread has handed over its result and is ending.
	if (thread_.joinable())
		thread_.join();
	cancel_.reset();

	if (download.status == DownloadStatus::Cancelled) {
		state_ = State::Idle;
		Q_EMIT changed();
		return;
	}
	if (download.status == DownloadStatus::Failed) {
		fail(download.problem);
		return;
	}

	// The request file first, so the installer finds it when it starts.
	const std::string request = folderOf(download.file) + "\\" + kUpdateRequestName;
	const HANDLE file = CreateFileW(widen(request).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		const DWORD code = GetLastError();
		discardUpdateDownload(download);
		fail(stepProblem(locf("UpdateNow.Error.Step", "Windows refused a step of the update (error {0}).", code)));
		return;
	}
	CloseHandle(file);

	// ShellExecute, because an install for all users needs administrator rights, and Windows
	// asks for them this way.
	const UpdatePlan plan = planUpdate(download.file, setup_.installFolder);
	const std::wstring program = widen(plan.program);
	const std::wstring arguments = widen(withRequestFile(plan.arguments + setup_.extraArguments, request));
	SHELLEXECUTEINFOW info{};
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
	info.lpVerb = L"open";
	info.lpFile = program.c_str();
	info.lpParameters = arguments.c_str();
	info.nShow = SW_SHOWNORMAL;
	if (!ShellExecuteExW(&info)) {
		const DWORD code = GetLastError();
		DeleteFileW(widen(request).c_str());
		discardUpdateDownload(download);
		fail(stepProblem(locf("UpdateNow.Error.Start", "Windows could not start the installer (error {0}).", code)));
		return;
	}

	// The installer runs now, and Windows keeps a running program's file from being changed.
	download.lock.reset();
	requestFile_ = request;
	installerFile_ = download.file;
	state_ = State::Waiting;
	logInfo("Update now: RelayDock {} is downloaded and checked. Its installer waits for OBS to close.",
		versionOfTag(release_.tag));
	Q_EMIT changed();
}

void UpdateInstall::cancel()
{
	switch (state_) {
	case State::Downloading:
		endDownload();
		state_ = State::Idle;
		logInfo("Update now: the download was cancelled. Nothing was installed.");
		Q_EMIT changed();
		break;
	case State::Waiting: {
		DeleteFileW(widen(requestFile_).c_str());
		// The installer notices within a second and ends. Its file can go after that.
		const std::string installer = installerFile_;
		QTimer::singleShot(3000, this, [installer] {
			DeleteFileW(widen(installer).c_str());
			RemoveDirectoryW(widen(folderOf(installer)).c_str());
		});
		requestFile_.clear();
		installerFile_.clear();
		state_ = State::Idle;
		logInfo("Update now: cancelled. RelayDock stays as it is.");
		Q_EMIT changed();
		break;
	}
	case State::Failed:
		state_ = State::Idle;
		problem_ = {};
		Q_EMIT changed();
		break;
	case State::Idle:
		break;
	}
}

void UpdateInstall::shutdown()
{
	// A download that still runs ends here, and nothing is installed. An installer that waits
	// keeps its request file and goes ahead once OBS has closed.
	if (state_ == State::Downloading) {
		endDownload();
		state_ = State::Idle;
		logInfo("Update now: OBS closes before the download finished. Nothing is installed.");
	} else if (state_ == State::Waiting) {
		logInfo("Update now: OBS closes. The installer of RelayDock {} goes ahead.", versionOfTag(release_.tag));
	}
}

} // namespace rd
