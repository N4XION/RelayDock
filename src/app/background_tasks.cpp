// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "app/background_tasks.h"

#include <QMetaObject>

namespace rd {

// ---- ConnectionTester ------------------------------------------------------------------------------

ConnectionTester::ConnectionTester(QObject *parent) : QObject(parent) {}

ConnectionTester::~ConnectionTester()
{
	cancel();
}

void ConnectionTester::cancel()
{
	if (cancel_)
		cancel_->store(true);
	if (thread_.joinable())
		thread_.join();
	cancel_.reset();
	running_ = false;
}

void ConnectionTester::start(const std::string &host, int port, int timeoutMs)
{
	cancel();

	auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
	cancel_ = cancelFlag;
	running_ = true;

	thread_ = std::thread([this, host, port, timeoutMs, cancelFlag] {
		const ReachResult result = checkReachable(host, port, timeoutMs, *cancelFlag);
		// A cancelled job reports nothing. Its owner has moved on or is being destroyed, and
		// cancel() is waiting for this thread, so `this` is still valid here.
		if (cancelFlag->load())
			return;
		QMetaObject::invokeMethod(
			this,
			[this, result, cancelFlag] {
				if (cancelFlag->load())
					return;
				running_ = false;
				Q_EMIT finished(result);
			},
			Qt::QueuedConnection);
	});
}

// ---- UpdateChecker ---------------------------------------------------------------------------------

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent) {}

UpdateChecker::~UpdateChecker()
{
	cancel();
}

void UpdateChecker::cancel()
{
	if (cancel_)
		cancel_->store(true);
	if (thread_.joinable())
		thread_.join();
	cancel_.reset();
	running_ = false;
}

void UpdateChecker::start(const std::string &repositoryUrl, const std::string &currentVersion)
{
	cancel();

	auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
	cancel_ = cancelFlag;
	running_ = true;

	thread_ = std::thread([this, repositoryUrl, currentVersion, cancelFlag] {
		const UpdateResult result = checkForUpdate(repositoryUrl, currentVersion, *cancelFlag);
		if (cancelFlag->load())
			return;
		QMetaObject::invokeMethod(
			this,
			[this, result, cancelFlag] {
				if (cancelFlag->load())
					return;
				running_ = false;
				Q_EMIT finished(result);
			},
			Qt::QueuedConnection);
	});
}

} // namespace rd
