// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "chat/chat_worker.h"

#include <chrono>

namespace rd {

ChatWorker::ChatWorker(Callbacks callbacks) : callbacks_(std::move(callbacks)) {}

ChatWorker::~ChatWorker()
{
	// A derived class has stopped already. This covers a worker that was never started.
	cancel_ = true;
	if (thread_.joinable())
		thread_.join();
}

void ChatWorker::stop()
{
	const bool wasRunning = thread_.joinable();
	cancel_ = true;
	if (thread_.joinable())
		thread_.join();
	running_ = false;
	if (wasRunning)
		report(ChatState::Off);
}

ChatStatus ChatWorker::status() const
{
	const std::lock_guard lock(mutex_);
	return status_;
}

void ChatWorker::launch(std::function<void()> body)
{
	cancel_ = true;
	if (thread_.joinable())
		thread_.join();
	cancel_ = false;
	running_ = true;
	thread_ = std::thread([this, body = std::move(body)] {
		body();
		running_ = false;
	});
}

bool ChatWorker::sleepFor(long long ms)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
	while (!cancel_.load()) {
		const auto now = std::chrono::steady_clock::now();
		if (now >= deadline)
			return true;
		const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
		std::this_thread::sleep_for(left < std::chrono::milliseconds(20) ? left : std::chrono::milliseconds(20));
	}
	return false;
}

void ChatWorker::report(ChatState state, const UserMessage &message, const std::string &account)
{
	ChatStatus status;
	status.state = state;
	status.message = message;
	status.account = account;
	{
		const std::lock_guard lock(mutex_);
		if (status == status_)
			return;
		status_ = status;
	}
	if (callbacks_.status)
		callbacks_.status(status);
}

void ChatWorker::deliver(std::vector<ChatEvent> events)
{
	if (!events.empty() && callbacks_.events)
		callbacks_.events(std::move(events));
}

} // namespace rd
