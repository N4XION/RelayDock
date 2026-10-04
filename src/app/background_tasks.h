// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "network/reachability.h"
#include "update/update_check.h"

#include <QObject>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace rd {

// Work that waits on the network runs here, on its own thread, so the OBS interface never
// waits with it. Each object runs one job at a time. Destroying the object, or starting a new
// job, cancels the running one and waits for its thread, which returns within about 100 ms.
//
// Thread ownership: create, use and destroy on the OBS UI thread. Results arrive there too.

class ConnectionTester : public QObject {
	Q_OBJECT

public:
	explicit ConnectionTester(QObject *parent = nullptr);
	~ConnectionTester() override;

	// Checks that `host` accepts a TCP connection on `port`.
	void start(const std::string &host, int port, int timeoutMs = 8000);
	void cancel();
	bool running() const { return running_; }

Q_SIGNALS:
	void finished(const rd::ReachResult &result);

private:
	std::thread thread_;
	std::shared_ptr<std::atomic<bool>> cancel_;
	bool running_ = false;
};

class UpdateChecker : public QObject {
	Q_OBJECT

public:
	explicit UpdateChecker(QObject *parent = nullptr);
	~UpdateChecker() override;

	// Asks GitHub for the newest release of the configured project.
	void start(const std::string &repositoryUrl, const std::string &currentVersion);
	void cancel();
	bool running() const { return running_; }

Q_SIGNALS:
	void finished(const rd::UpdateResult &result);

private:
	std::thread thread_;
	std::shared_ptr<std::atomic<bool>> cancel_;
	bool running_ = false;
};

} // namespace rd
