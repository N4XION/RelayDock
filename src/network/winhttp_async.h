// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

// Internal to src/network. The pieces http_client.cpp and websocket_client.cpp share.
//
// WinHTTP runs here in asynchronous mode. A caller starts an operation, WinHTTP reports its end
// through a callback on one of its own threads, and the callback only sets an event. The calling
// thread waits for that event and looks at the cancel flag while it waits. To give up, it closes
// the handle itself. Closing a handle from the thread that uses it is the way Microsoft
// documents for ending a request early. Closing it from another thread is not.

#include <windows.h>

#include <winhttp.h>

#include <atomic>
#include <string>

namespace rd::winhttp {

std::wstring widen(const std::string &text);

// "The server did not answer in time (Windows error 12002)."
std::string describeError(DWORD error);

// What a callback tells the thread that waits.
struct AsyncState {
	AsyncState();
	~AsyncState();
	AsyncState(const AsyncState &) = delete;
	AsyncState &operator=(const AsyncState &) = delete;

	HANDLE step;   // An operation completed or failed. Resets itself when a wait takes it.
	HANDLE closed; // WinHTTP made its last callback for the handle.
	std::atomic<DWORD> error{0};
	std::atomic<DWORD> bytes{0};
	std::atomic<DWORD> bufferType{0}; // WebSocket reads only
	bool webSocket = false;           // Set before the state is attached, never afterwards
};

// Registers the callback on `handle` and makes `state` its context.
bool attach(HINTERNET handle, AsyncState *state);

enum class StepResult { Done, Failed, Cancelled, TimedOut };

// Waits for the operation that was just started. `error` holds the Windows error after Failed.
StepResult waitStep(AsyncState &state, const std::atomic<bool> &cancel, long long timeoutMs, DWORD &error);

// Closes a handle that has a state attached, waits for WinHTTP's last callback and frees the
// state. Afterwards no callback can run for the handle, so the buffers it used may go.
void closeAndRelease(HINTERNET handle, AsyncState *state);

// Owns a session or connection handle, which carry no state.
struct PlainHandle {
	HINTERNET handle = nullptr;
	PlainHandle() = default;
	explicit PlainHandle(HINTERNET h) : handle(h) {}
	~PlainHandle()
	{
		if (handle)
			WinHttpCloseHandle(handle);
	}
	PlainHandle(const PlainHandle &) = delete;
	PlainHandle &operator=(const PlainHandle &) = delete;
	explicit operator bool() const { return handle != nullptr; }
};

} // namespace rd::winhttp
