// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "chat/chat_types.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rd {

// The thread a chat reader runs on, and how it reports. Reading chat waits on the network all
// the time, so it never runs on the OBS UI thread.
//
// Both callbacks run on the worker thread. The owner hands the results over to its own thread.
//
// Thread ownership: one thread creates, starts, stops and destroys the object. stop() returns
// within about a tenth of a second, whatever the network is doing.
class ChatWorker {
public:
	struct Callbacks {
		std::function<void(std::vector<ChatEvent>)> events;
		std::function<void(const ChatStatus &)> status;
	};

	explicit ChatWorker(Callbacks callbacks);
	virtual ~ChatWorker();
	ChatWorker(const ChatWorker &) = delete;
	ChatWorker &operator=(const ChatWorker &) = delete;

	// Ends the reading and waits for the thread. Reports Off.
	void stop();
	bool running() const { return running_.load(); }

	// The last status that was reported.
	ChatStatus status() const;

protected:
	// Stops what runs and starts `body` on a new thread. A class that derives from this one
	// calls stop() in its own destructor, before its members go.
	void launch(std::function<void()> body);

	// Waits. Returns false when the worker is being stopped.
	bool sleepFor(long long ms);
	bool cancelled() const { return cancel_.load(); }

	void report(ChatState state, const UserMessage &message = {}, const std::string &account = {});
	void deliver(std::vector<ChatEvent> events);

	std::atomic<bool> cancel_{false};

private:
	Callbacks callbacks_;
	std::thread thread_;
	std::atomic<bool> running_{false};
	mutable std::mutex mutex_;
	ChatStatus status_;
};

// How long to wait before the next try: the first wait, doubled after every failure, up to a
// limit. A connection that worked starts over.
class RetryDelay {
public:
	RetryDelay(long long firstMs, long long maxMs) : firstMs_(firstMs), maxMs_(maxMs), nextMs_(firstMs) {}

	long long next()
	{
		const long long now = nextMs_;
		nextMs_ = nextMs_ * 2 > maxMs_ ? maxMs_ : nextMs_ * 2;
		return now;
	}
	void reset() { nextMs_ = firstMs_; }

private:
	long long firstMs_;
	long long maxMs_;
	long long nextMs_;
};

} // namespace rd
