// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "network/websocket_client.h"

#include "network/http_client.h"
#include "network/winhttp_async.h"

#include <format>
#include <vector>

namespace rd {

using namespace winhttp;

struct WebSocketClient::Impl {
	HINTERNET session = nullptr;
	HINTERNET connection = nullptr;
	HINTERNET socket = nullptr;
	AsyncState *state = nullptr; // Belongs to `socket`
	std::vector<char> buffer = std::vector<char>(16 * 1024);
	std::string partial;         // Fragments of the text message that is still arriving
	bool receiving = false;      // A receive is under way and owns `buffer`

	void close()
	{
		if (socket) {
			// Ends a receive that is under way. WinHTTP's last callback comes before the
			// state and the buffer go.
			closeAndRelease(socket, state);
			socket = nullptr;
			state = nullptr;
		}
		receiving = false;
		partial.clear();
		if (connection) {
			WinHttpCloseHandle(connection);
			connection = nullptr;
		}
		if (session) {
			WinHttpCloseHandle(session);
			session = nullptr;
		}
	}
};

WebSocketClient::WebSocketClient() : impl_(std::make_unique<Impl>()) {}

WebSocketClient::~WebSocketClient()
{
	impl_->close();
}

bool WebSocketClient::connected() const
{
	return impl_->socket != nullptr;
}

void WebSocketClient::close()
{
	impl_->close();
}

bool WebSocketClient::connect(const std::string &address, const std::string &userAgent, int timeoutMs,
			      const std::atomic<bool> &cancel, std::string &error)
{
	impl_->close();

	HttpUrl url;
	if (!parseHttpUrl(address, url)) {
		error = "The address is not a web address RelayDock accepts.";
		return false;
	}
	if (cancel.load()) {
		error = "cancelled";
		return false;
	}
	if (timeoutMs <= 0)
		timeoutMs = 10000;

	impl_->session = WinHttpOpen(widen(userAgent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
				     WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
	if (!impl_->session) {
		error = std::format("Windows could not open an HTTP session (error {}).", GetLastError());
		return false;
	}
	// The receive timeout stays off. A chat can be quiet for a long time, and receive() has its
	// own idle time.
	WinHttpSetTimeouts(impl_->session, timeoutMs, timeoutMs, timeoutMs, 0);

	impl_->connection =
		WinHttpConnect(impl_->session, widen(url.host).c_str(), static_cast<INTERNET_PORT>(url.port), 0);
	if (!impl_->connection) {
		error = std::format("Windows could not prepare the connection (error {}).", GetLastError());
		impl_->close();
		return false;
	}

	const HINTERNET request = WinHttpOpenRequest(impl_->connection, L"GET", widen(url.path).c_str(), nullptr,
						     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
						     url.secure ? WINHTTP_FLAG_SECURE : 0);
	if (!request) {
		error = std::format("Windows could not create the request (error {}).", GetLastError());
		impl_->close();
		return false;
	}

	auto *requestState = new AsyncState();
	if (!attach(request, requestState)) {
		error = std::format("Windows could not watch the request (error {}).", GetLastError());
		WinHttpCloseHandle(request);
		delete requestState;
		impl_->close();
		return false;
	}

	const auto giveUp = [&](StepResult result, DWORD code) {
		if (result == StepResult::Cancelled)
			error = "cancelled";
		else if (result == StepResult::TimedOut)
			error = describeError(ERROR_WINHTTP_TIMEOUT);
		else
			error = describeError(code);
		closeAndRelease(request, requestState);
		impl_->close();
		return false;
	};

	if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0))
		return giveUp(StepResult::Failed, GetLastError());

	DWORD code = 0;
	if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
				reinterpret_cast<DWORD_PTR>(requestState)))
		return giveUp(StepResult::Failed, GetLastError());
	// For an upgrade, WinHTTP reports the send as complete only once the server has answered.
	// The receive timeout is off for this session, so this wait is the limit for the whole
	// handshake: finding the server, connecting, and the server's answer.
	if (const StepResult result = waitStep(*requestState, cancel, 3LL * timeoutMs, code); result != StepResult::Done)
		return giveUp(result, code);

	if (!WinHttpReceiveResponse(request, nullptr))
		return giveUp(StepResult::Failed, GetLastError());
	if (const StepResult result = waitStep(*requestState, cancel, timeoutMs, code); result != StepResult::Done)
		return giveUp(result, code);

	DWORD status = 0;
	DWORD statusSize = sizeof(status);
	if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
				 &status, &statusSize, WINHTTP_NO_HEADER_INDEX) ||
	    status != HTTP_STATUS_SWITCH_PROTOCOLS) {
		error = std::format("The server did not open a WebSocket. It answered with status {}.", status);
		closeAndRelease(request, requestState);
		impl_->close();
		return false;
	}

	auto *socketState = new AsyncState();
	socketState->webSocket = true;
	const HINTERNET socket = WinHttpWebSocketCompleteUpgrade(request, reinterpret_cast<DWORD_PTR>(socketState));
	if (!socket) {
		error = std::format("Windows could not open the WebSocket (error {}).", GetLastError());
		delete socketState;
		closeAndRelease(request, requestState);
		impl_->close();
		return false;
	}
	// The request has done its work. The socket lives on by itself.
	closeAndRelease(request, requestState);

	if (!attach(socket, socketState)) {
		error = std::format("Windows could not watch the WebSocket (error {}).", GetLastError());
		// Whether the socket carries a callback is unknown here, so the state stays allocated.
		WinHttpCloseHandle(socket);
		impl_->close();
		return false;
	}

	impl_->socket = socket;
	impl_->state = socketState;
	return true;
}

WebSocketClient::Receive WebSocketClient::receive(std::string &message, int idleMs, const std::atomic<bool> &cancel,
						  std::string &error)
{
	Impl &impl = *impl_;
	if (!impl.socket) {
		error = "The connection is not open.";
		return Receive::Failed;
	}

	const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(idleMs > 0 ? idleMs : 0);
	for (;;) {
		if (!impl.receiving) {
			const DWORD started = WinHttpWebSocketReceive(impl.socket, impl.buffer.data(),
								      static_cast<DWORD>(impl.buffer.size()), nullptr, nullptr);
			if (started != NO_ERROR) {
				error = describeError(started);
				impl.close();
				return Receive::Failed;
			}
			impl.receiving = true;
		}

		const ULONGLONG now = GetTickCount64();
		DWORD code = 0;
		const StepResult result = waitStep(*impl.state, cancel, now < deadline ? static_cast<long long>(deadline - now) : 0,
						   code);
		if (result == StepResult::Cancelled) {
			error = "cancelled";
			return Receive::Cancelled;
		}
		if (result == StepResult::TimedOut)
			return Receive::Idle; // The receive stays under way for the next call.
		impl.receiving = false;
		if (result == StepResult::Failed) {
			error = describeError(code);
			impl.close();
			return Receive::Failed;
		}

		const DWORD bytes = impl.state->bytes.load();
		const auto type = static_cast<WINHTTP_WEB_SOCKET_BUFFER_TYPE>(impl.state->bufferType.load());
		switch (type) {
		case WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE:
			impl.close();
			return Receive::Closed;
		case WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE:
		case WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE:
			break; // Dropped. RelayDock reads text only.
		case WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE:
		case WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE:
			if (impl.partial.size() + bytes > kMaxMessageBytes) {
				error = "The server sent a message that is larger than RelayDock accepts.";
				impl.close();
				return Receive::Failed;
			}
			impl.partial.append(impl.buffer.data(), bytes);
			if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
				message = std::move(impl.partial);
				impl.partial.clear();
				return Receive::Message;
			}
			break;
		default:
			break;
		}
	}
}

} // namespace rd
