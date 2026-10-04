// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "utils/sleep_inhibitor.h"

#include <windows.h>

#include <iterator>
#include <utility>

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

// What OBS asks for during its own stream: stay awake, keep the display on, and go to away
// mode instead of sleep when someone presses the sleep button. The first one is required.
constexpr POWER_REQUEST_TYPE kRequests[] = {PowerRequestSystemRequired, PowerRequestDisplayRequired,
					    PowerRequestAwayModeRequired};

} // namespace

SleepInhibitor::SleepInhibitor(std::string reason) : reason_(std::move(reason)) {}

SleepInhibitor::~SleepInhibitor()
{
	setActive(false);
	if (request_)
		CloseHandle(static_cast<HANDLE>(request_));
}

bool SleepInhibitor::setActive(bool active)
{
	if (active == active_)
		return true;

	if (!active) {
		for (size_t i = 0; i < std::size(kRequests); ++i) {
			if (held_ & (1u << i))
				PowerClearRequest(static_cast<HANDLE>(request_), kRequests[i]);
		}
		held_ = 0;
		active_ = false;
		return true;
	}

	if (!request_) {
		std::wstring reason = widen(reason_);
		REASON_CONTEXT context{};
		context.Version = POWER_REQUEST_CONTEXT_VERSION;
		context.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
		context.Reason.SimpleReasonString = reason.data();
		const HANDLE handle = PowerCreateRequest(&context);
		if (!handle || handle == INVALID_HANDLE_VALUE)
			return false;
		request_ = handle;
	}

	for (size_t i = 0; i < std::size(kRequests); ++i) {
		if (PowerSetRequest(static_cast<HANDLE>(request_), kRequests[i]))
			held_ |= 1u << i;
	}
	if (!(held_ & 1u)) {
		// Without "stay awake" the request is of no use. Drop whatever else was set.
		for (size_t i = 1; i < std::size(kRequests); ++i) {
			if (held_ & (1u << i))
				PowerClearRequest(static_cast<HANDLE>(request_), kRequests[i]);
		}
		held_ = 0;
		return false;
	}
	active_ = true;
	return true;
}

} // namespace rd
