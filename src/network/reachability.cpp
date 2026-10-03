// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "network/reachability.h"

#include "utils/i18n.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <chrono>
#include <string>
#include <vector>

namespace rd {

namespace {

using Clock = std::chrono::steady_clock;

int elapsedMs(Clock::time_point since)
{
	return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count());
}

struct WinsockSession {
	bool ok = false;
	WinsockSession()
	{
		WSADATA data{};
		ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
	}
	~WinsockSession()
	{
		if (ok)
			WSACleanup();
	}
};

struct Socket {
	SOCKET handle = INVALID_SOCKET;
	~Socket()
	{
		if (handle != INVALID_SOCKET)
			closesocket(handle);
	}
};

std::wstring widen(const std::string &text)
{
	if (text.empty())
		return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring out(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
	return out;
}

std::string addressText(const sockaddr *address, int length)
{
	char buffer[NI_MAXHOST] = {};
	if (getnameinfo(address, length, buffer, sizeof(buffer), nullptr, 0, NI_NUMERICHOST) != 0)
		return {};
	return buffer;
}

struct Resolved {
	std::vector<std::vector<unsigned char>> addresses; // Raw sockaddr bytes
	std::vector<int> families;
	int error = 0;
	bool cancelled = false;
};

// Resolves the name without blocking past `timeoutMs` and gives up as soon as `cancel` is set.
Resolved resolve(const std::string &host, int port, int timeoutMs, const std::atomic<bool> &cancel)
{
	Resolved out;

	ADDRINFOEXW hints{};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	const std::wstring wideHost = widen(host);
	const std::wstring widePort = std::to_wstring(port);

	OVERLAPPED overlapped{};
	overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!overlapped.hEvent) {
		out.error = static_cast<int>(GetLastError());
		return out;
	}

	PADDRINFOEXW results = nullptr;
	HANDLE cancelHandle = nullptr;
	int status = GetAddrInfoExW(wideHost.c_str(), widePort.c_str(), NS_DNS, nullptr, &hints, &results, nullptr,
				    &overlapped, nullptr, &cancelHandle);

	if (status == WSA_IO_PENDING) {
		const Clock::time_point started = Clock::now();
		for (;;) {
			if (WaitForSingleObject(overlapped.hEvent, 50) == WAIT_OBJECT_0) {
				status = GetAddrInfoExOverlappedResult(&overlapped);
				break;
			}
			const bool timedOut = elapsedMs(started) >= timeoutMs;
			if (cancel.load() || timedOut) {
				GetAddrInfoExCancel(&cancelHandle);
				// The cancel completes the request. Wait for it so the OVERLAPPED stays valid.
				WaitForSingleObject(overlapped.hEvent, INFINITE);
				GetAddrInfoExOverlappedResult(&overlapped);
				out.cancelled = cancel.load();
				status = timedOut ? WSAETIMEDOUT : WSA_E_CANCELLED;
				if (results) {
					FreeAddrInfoExW(results);
					results = nullptr;
				}
				break;
			}
		}
	}
	CloseHandle(overlapped.hEvent);

	if (status != NO_ERROR) {
		out.error = status;
		if (results)
			FreeAddrInfoExW(results);
		return out;
	}

	for (PADDRINFOEXW entry = results; entry; entry = entry->ai_next) {
		if (!entry->ai_addr || (entry->ai_family != AF_INET && entry->ai_family != AF_INET6))
			continue;
		const auto *bytes = reinterpret_cast<const unsigned char *>(entry->ai_addr);
		out.addresses.emplace_back(bytes, bytes + entry->ai_addrlen);
		out.families.push_back(entry->ai_family);
		if (out.addresses.size() >= 4) // Trying a handful is enough
			break;
	}
	if (results)
		FreeAddrInfoExW(results);
	return out;
}

} // namespace

const char *reachStatusName(ReachStatus status)
{
	switch (status) {
	case ReachStatus::Reachable:
		return "reachable";
	case ReachStatus::InvalidAddress:
		return "invalid_address";
	case ReachStatus::DnsFailed:
		return "dns_failed";
	case ReachStatus::Refused:
		return "refused";
	case ReachStatus::TimedOut:
		return "timed_out";
	case ReachStatus::Unreachable:
		return "unreachable";
	case ReachStatus::Cancelled:
		return "cancelled";
	}
	return "unreachable";
}

ReachResult checkReachable(const std::string &host, int port, int timeoutMs, const std::atomic<bool> &cancel)
{
	ReachResult result;
	result.host = host;
	result.port = port;

	if (host.empty() || port < 1 || port > 65535) {
		result.status = ReachStatus::InvalidAddress;
		return result;
	}
	if (cancel.load()) {
		result.status = ReachStatus::Cancelled;
		return result;
	}

	const WinsockSession winsock;
	if (!winsock.ok) {
		result.status = ReachStatus::Unreachable;
		result.systemError = WSAGetLastError();
		return result;
	}

	const Clock::time_point started = Clock::now();
	const Resolved resolved = resolve(host, port, timeoutMs, cancel);
	if (resolved.cancelled || cancel.load()) {
		result.status = ReachStatus::Cancelled;
		return result;
	}
	if (resolved.addresses.empty()) {
		result.status = resolved.error == WSAETIMEDOUT ? ReachStatus::TimedOut : ReachStatus::DnsFailed;
		result.systemError = resolved.error;
		return result;
	}

	// Try each address until one accepts. The overall time limit covers all of them.
	result.status = ReachStatus::Unreachable;
	for (size_t i = 0; i < resolved.addresses.size(); ++i) {
		const auto *address = reinterpret_cast<const sockaddr *>(resolved.addresses[i].data());
		const int addressLength = static_cast<int>(resolved.addresses[i].size());
		result.address = addressText(address, addressLength);

		Socket socket;
		socket.handle = ::socket(resolved.families[i], SOCK_STREAM, IPPROTO_TCP);
		if (socket.handle == INVALID_SOCKET) {
			result.systemError = WSAGetLastError();
			continue;
		}
		u_long nonBlocking = 1;
		ioctlsocket(socket.handle, FIONBIO, &nonBlocking);

		const Clock::time_point attemptStarted = Clock::now();
		if (connect(socket.handle, address, addressLength) == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
			result.systemError = WSAGetLastError();
			result.status = result.systemError == WSAECONNREFUSED ? ReachStatus::Refused : ReachStatus::Unreachable;
			continue;
		}

		bool decided = false;
		while (!decided) {
			if (cancel.load()) {
				result.status = ReachStatus::Cancelled;
				return result;
			}
			if (elapsedMs(started) >= timeoutMs) {
				result.status = ReachStatus::TimedOut;
				result.systemError = WSAETIMEDOUT;
				return result;
			}

			fd_set writable;
			fd_set failed;
			FD_ZERO(&writable);
			FD_ZERO(&failed);
			FD_SET(socket.handle, &writable);
			FD_SET(socket.handle, &failed);
			timeval slice{0, 50 * 1000};
			const int ready = select(0, nullptr, &writable, &failed, &slice);
			if (ready == SOCKET_ERROR) {
				result.systemError = WSAGetLastError();
				result.status = ReachStatus::Unreachable;
				decided = true;
			} else if (ready > 0 && FD_ISSET(socket.handle, &writable)) {
				result.status = ReachStatus::Reachable;
				result.elapsedMs = elapsedMs(attemptStarted);
				result.systemError = 0;
				return result;
			} else if (ready > 0 && FD_ISSET(socket.handle, &failed)) {
				int error = 0;
				int length = sizeof(error);
				getsockopt(socket.handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &length);
				result.systemError = error;
				if (error == WSAECONNREFUSED)
					result.status = ReachStatus::Refused;
				else if (error == WSAETIMEDOUT)
					result.status = ReachStatus::TimedOut;
				else
					result.status = ReachStatus::Unreachable;
				decided = true;
			}
		}
	}
	return result;
}

UserMessage describeReachability(const std::string &destinationName, const ReachResult &result, bool tls)
{
	const std::string server = result.host + ":" + std::to_string(result.port);
	UserMessage message;
	switch (result.status) {
	case ReachStatus::Reachable:
		message.what = locf("Reach.Ok", "The server for {0} answers.", destinationName);
		message.detail = locf("Reach.Ok.Detail", "{0} accepted a connection in {1} ms.", server, result.elapsedMs);
		message.action = tls ? loc("Reach.Ok.ActionTls",
					   "This does not check your stream key or the encrypted handshake. The platform checks both when a stream starts.")
				     : loc("Reach.Ok.Action",
					   "This does not check your stream key. The platform checks it when a stream starts.");
		break;
	case ReachStatus::InvalidAddress:
		message.what = locf("Reach.Invalid", "{0} has no valid server address.", destinationName);
		message.action = loc("Reach.Invalid.Action", "Check the server URL.");
		break;
	case ReachStatus::DnsFailed:
		message.what = locf("Reach.Dns", "RelayDock could not find the server for {0}.", destinationName);
		message.detail = locf("Reach.Dns.Detail", "The name {0} did not resolve to an address.", result.host);
		message.action = loc("Reach.Dns.Action", "Check the server URL for typing mistakes, and check your internet connection.");
		break;
	case ReachStatus::Refused:
		message.what = locf("Reach.Refused", "The server for {0} refused the connection.", destinationName);
		message.detail = locf("Reach.Refused.Detail", "{0} is online, but nothing accepts connections on port {1}.",
				      result.host, result.port);
		message.action = loc("Reach.Refused.Action", "Check the port in the server URL.");
		break;
	case ReachStatus::TimedOut:
		message.what = locf("Reach.Timeout", "The server for {0} did not answer.", destinationName);
		message.detail = locf("Reach.Timeout.Detail", "{0} gave no answer in time.", server);
		message.action = loc("Reach.Timeout.Action",
				     "Check your internet connection. A firewall or your network can block this port.");
		break;
	case ReachStatus::Unreachable:
		message.what = locf("Reach.Unreachable", "RelayDock could not reach the server for {0}.", destinationName);
		message.detail = locf("Reach.Unreachable.Detail", "Windows reported network error {0} for {1}.", result.systemError, server);
		message.action = loc("Reach.Unreachable.Action", "Check your internet connection.");
		break;
	case ReachStatus::Cancelled:
		message.what = loc("Reach.Cancelled", "The connection test was cancelled.");
		break;
	}
	return message;
}

} // namespace rd
