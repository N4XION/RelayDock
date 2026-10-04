// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "support/fake_server.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <cctype>
#include <format>

namespace rdtest {

namespace {

struct Winsock {
	Winsock()
	{
		WSADATA data;
		WSAStartup(MAKEWORD(2, 2), &data);
	}
	~Winsock() { WSACleanup(); }
};

void ensureWinsock()
{
	static Winsock winsock;
}

bool sendAll(SOCKET socket, const char *data, size_t size)
{
	while (size > 0) {
		const int sent = send(socket, data, static_cast<int>(std::min<size_t>(size, 64 * 1024)), 0);
		if (sent <= 0)
			return false;
		data += sent;
		size -= static_cast<size_t>(sent);
	}
	return true;
}

// 1 when there is something to read, 0 after `ms` with nothing, negative on an error.
int waitReadable(SOCKET socket, int ms)
{
	fd_set set;
	FD_ZERO(&set);
	FD_SET(socket, &set);
	timeval wait{ms / 1000, (ms % 1000) * 1000};
	return select(0, &set, nullptr, nullptr, &wait);
}

std::string lower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

std::string trimmed(const std::string &text)
{
	const size_t first = text.find_first_not_of(" \t");
	if (first == std::string::npos)
		return {};
	const size_t last = text.find_last_not_of(" \t");
	return text.substr(first, last - first + 1);
}

// The value a WebSocket server answers the client's key with.
std::string acceptKey(const std::string &clientKey)
{
	const std::string text = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	UCHAR hash[20] = {};
	BCryptHash(BCRYPT_SHA1_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char *>(text.data())),
		   static_cast<ULONG>(text.size()), hash, sizeof(hash));
	char out[64] = {};
	DWORD length = sizeof(out);
	CryptBinaryToStringA(hash, sizeof(hash), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out, &length);
	return std::string(out, length);
}

const char *reason(int status)
{
	switch (status) {
	case 200:
		return "OK";
	case 202:
		return "Accepted";
	case 204:
		return "No Content";
	case 301:
		return "Moved Permanently";
	case 302:
		return "Found";
	case 307:
		return "Temporary Redirect";
	case 400:
		return "Bad Request";
	case 401:
		return "Unauthorized";
	case 403:
		return "Forbidden";
	case 404:
		return "Not Found";
	case 429:
		return "Too Many Requests";
	case 500:
		return "Internal Server Error";
	default:
		return "Status";
	}
}

} // namespace

std::string urlDecode(const std::string &text)
{
	std::string out;
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '+') {
			out += ' ';
		} else if (text[i] == '%' && i + 2 < text.size() + 0 && i + 2 <= text.size() - 1 + 0 &&
			   std::isxdigit(static_cast<unsigned char>(text[i + 1])) &&
			   std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
			out += static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16));
			i += 2;
		} else {
			out += text[i];
		}
	}
	return out;
}

std::map<std::string, std::string> parseForm(const std::string &text)
{
	std::map<std::string, std::string> out;
	size_t start = 0;
	while (start <= text.size()) {
		size_t end = text.find('&', start);
		if (end == std::string::npos)
			end = text.size();
		const std::string pair = text.substr(start, end - start);
		if (!pair.empty()) {
			const size_t equals = pair.find('=');
			if (equals == std::string::npos)
				out[urlDecode(pair)] = "";
			else
				out[urlDecode(pair.substr(0, equals))] = urlDecode(pair.substr(equals + 1));
		}
		start = end + 1;
	}
	return out;
}

// ---- FakeRequest -----------------------------------------------------------------------------

std::string FakeRequest::header(const std::string &name) const
{
	const auto it = headers.find(lower(name));
	return it == headers.end() ? std::string() : it->second;
}

std::string FakeRequest::param(const std::string &name) const
{
	const auto it = query.find(name);
	return it == query.end() ? std::string() : it->second;
}

std::map<std::string, std::string> FakeRequest::form() const
{
	return parseForm(body);
}

// ---- FakeSocket ------------------------------------------------------------------------------

bool FakeSocket::sendFrame(unsigned char opcode, bool final, const std::string &payload)
{
	std::string frame;
	frame += static_cast<char>((final ? 0x80 : 0x00) | opcode);
	const size_t size = payload.size();
	if (size < 126) {
		frame += static_cast<char>(size);
	} else if (size < 65536) {
		frame += static_cast<char>(126);
		frame += static_cast<char>((size >> 8) & 0xff);
		frame += static_cast<char>(size & 0xff);
	} else {
		frame += static_cast<char>(127);
		for (int shift = 56; shift >= 0; shift -= 8)
			frame += static_cast<char>((static_cast<unsigned long long>(size) >> shift) & 0xff);
	}
	frame += payload;
	if (!sendAll(static_cast<SOCKET>(socket_), frame.data(), frame.size())) {
		gone_ = true;
		return false;
	}
	return true;
}

bool FakeSocket::sendText(const std::string &text)
{
	return sendFrame(0x1, true, text);
}

bool FakeSocket::sendFragments(const std::vector<std::string> &parts)
{
	for (size_t i = 0; i < parts.size(); ++i) {
		const bool last = i + 1 == parts.size();
		if (!sendFrame(i == 0 ? 0x1 : 0x0, last, parts[i]))
			return false;
	}
	return true;
}

bool FakeSocket::sendBinary(const std::string &bytes)
{
	return sendFrame(0x2, true, bytes);
}

bool FakeSocket::sendPing()
{
	return sendFrame(0x9, true, "ping");
}

bool FakeSocket::sendClose(int code)
{
	std::string payload;
	payload += static_cast<char>((code >> 8) & 0xff);
	payload += static_cast<char>(code & 0xff);
	return sendFrame(0x8, true, payload);
}

bool FakeSocket::pump(int ms)
{
	if (gone_)
		return false;
	const int ready = waitReadable(static_cast<SOCKET>(socket_), ms);
	if (ready < 0) {
		gone_ = true;
		return false;
	}
	if (ready == 0)
		return true;

	char buffer[2048];
	const int received = recv(static_cast<SOCKET>(socket_), buffer, sizeof(buffer), 0);
	if (received <= 0) {
		gone_ = true;
		return false;
	}
	// A close frame from the client ends the connection as well. Its first byte carries
	// opcode 8. The client sends small control frames only, one per packet.
	if ((static_cast<unsigned char>(buffer[0]) & 0x0f) == 0x8) {
		gone_ = true;
		return false;
	}
	return true;
}

bool FakeSocket::wait(int ms)
{
	const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(ms);
	while (GetTickCount64() < deadline) {
		if (stopping_.load() || !pump(20))
			return false;
	}
	return !stopping_.load() && !gone_;
}

bool FakeSocket::waitForClose(int ms)
{
	const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(ms);
	while (GetTickCount64() < deadline) {
		if (stopping_.load())
			return false;
		if (!pump(20))
			return true;
	}
	return false;
}

// ---- FakeServer ------------------------------------------------------------------------------

FakeServer::FakeServer(Handler handler, SocketHandler socketHandler)
	: handler_(std::move(handler)),
	  socketHandler_(std::move(socketHandler))
{
	ensureWinsock();

	const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = 0;
	bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address));
	listen(listener, 16);

	int length = sizeof(address);
	getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length);
	port_ = ntohs(address.sin_port);
	listener_ = static_cast<uintptr_t>(listener);

	acceptThread_ = std::thread([this] { acceptLoop(); });
}

FakeServer::~FakeServer()
{
	stop();
}

void FakeServer::stop()
{
	if (stopping_.exchange(true))
		return;
	if (acceptThread_.joinable())
		acceptThread_.join();
	closesocket(static_cast<SOCKET>(listener_));

	std::vector<std::thread> connections;
	{
		const std::lock_guard lock(mutex_);
		connections.swap(connections_);
	}
	for (std::thread &thread : connections) {
		if (thread.joinable())
			thread.join();
	}
}

std::string FakeServer::url(const std::string &path) const
{
	return std::format("http://127.0.0.1:{}{}", port_, path);
}

std::string FakeServer::wsUrl(const std::string &path) const
{
	return std::format("ws://127.0.0.1:{}{}", port_, path);
}

std::vector<FakeRequest> FakeServer::requests() const
{
	const std::lock_guard lock(mutex_);
	return requests_;
}

size_t FakeServer::count(const std::string &path) const
{
	const std::lock_guard lock(mutex_);
	return static_cast<size_t>(
		std::count_if(requests_.begin(), requests_.end(), [&](const FakeRequest &r) { return r.path == path; }));
}

void FakeServer::acceptLoop()
{
	const SOCKET listener = static_cast<SOCKET>(listener_);
	while (!stopping_.load()) {
		if (waitReadable(listener, 50) <= 0)
			continue;
		const SOCKET client = accept(listener, nullptr, nullptr);
		if (client == INVALID_SOCKET)
			continue;
		const std::lock_guard lock(mutex_);
		connections_.emplace_back([this, client] { serve(static_cast<uintptr_t>(client)); });
	}
}

void FakeServer::serve(uintptr_t handle)
{
	const SOCKET client = static_cast<SOCKET>(handle);
	const auto finish = [&] {
		// Lets the client read everything before the connection goes.
		shutdown(client, SD_SEND);
		for (int i = 0; i < 20 && !stopping_.load(); ++i) {
			if (waitReadable(client, 50) > 0) {
				char rest[512];
				if (recv(client, rest, sizeof(rest), 0) <= 0)
					break;
			}
		}
		closesocket(client);
	};

	// The head of the request.
	std::string data;
	size_t headEnd = std::string::npos;
	while (headEnd == std::string::npos) {
		if (stopping_.load() || data.size() > 256 * 1024) {
			closesocket(client);
			return;
		}
		if (waitReadable(client, 50) <= 0)
			continue;
		char buffer[4096];
		const int received = recv(client, buffer, sizeof(buffer), 0);
		if (received <= 0) {
			closesocket(client);
			return;
		}
		data.append(buffer, static_cast<size_t>(received));
		headEnd = data.find("\r\n\r\n");
	}

	FakeRequest request;
	{
		const std::string head = data.substr(0, headEnd);
		const size_t lineEnd = head.find("\r\n");
		const std::string first = head.substr(0, lineEnd);
		const size_t space1 = first.find(' ');
		const size_t space2 = first.find(' ', space1 + 1);
		if (space1 == std::string::npos || space2 == std::string::npos) {
			closesocket(client);
			return;
		}
		request.method = first.substr(0, space1);
		request.target = first.substr(space1 + 1, space2 - space1 - 1);
		const size_t question = request.target.find('?');
		request.path = request.target.substr(0, question);
		if (question != std::string::npos)
			request.query = parseForm(request.target.substr(question + 1));

		size_t start = lineEnd == std::string::npos ? head.size() : lineEnd + 2;
		while (start < head.size()) {
			size_t end = head.find("\r\n", start);
			if (end == std::string::npos)
				end = head.size();
			const std::string line = head.substr(start, end - start);
			const size_t colon = line.find(':');
			if (colon != std::string::npos)
				request.headers[lower(line.substr(0, colon))] = trimmed(line.substr(colon + 1));
			start = end + 2;
		}
	}

	// The body.
	size_t wanted = 0;
	if (const std::string length = request.header("content-length"); !length.empty())
		wanted = static_cast<size_t>(std::strtoull(length.c_str(), nullptr, 10));
	request.body = data.substr(headEnd + 4);
	while (request.body.size() < wanted) {
		if (stopping_.load()) {
			closesocket(client);
			return;
		}
		if (waitReadable(client, 50) <= 0)
			continue;
		char buffer[4096];
		const int received = recv(client, buffer, sizeof(buffer), 0);
		if (received <= 0) {
			closesocket(client);
			return;
		}
		request.body.append(buffer, static_cast<size_t>(received));
	}

	FakeResponse response;
	{
		const std::lock_guard lock(mutex_);
		requests_.push_back(request);
	}
	{
		// One handler at a time, so a test's handler needs no locking of its own.
		static std::mutex handlerMutex;
		const std::lock_guard lock(handlerMutex);
		response = handler_ ? handler_(request) : FakeResponse{404, "text/plain", "no handler"};
	}

	if (response.delayMs > 0) {
		const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(response.delayMs);
		while (GetTickCount64() < deadline && !stopping_.load())
			Sleep(10);
	}

	if (response.hang) {
		while (!stopping_.load()) {
			if (waitReadable(client, 50) > 0) {
				char buffer[256];
				if (recv(client, buffer, sizeof(buffer), 0) <= 0)
					break;
			}
		}
		closesocket(client);
		return;
	}

	if (response.webSocket) {
		const std::string head = std::format("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
						     "Connection: Upgrade\r\nSec-WebSocket-Accept: {}\r\n\r\n",
						     acceptKey(request.header("sec-websocket-key")));
		if (sendAll(client, head.data(), head.size())) {
			FakeSocket socket(handle, stopping_);
			if (socketHandler_)
				socketHandler_(socket, request);
		}
		closesocket(client);
		return;
	}

	std::string extra;
	for (const auto &[name, value] : response.headers)
		extra += name + ": " + value + "\r\n";
	const std::string head = std::format("HTTP/1.1 {} {}\r\nContent-Type: {}\r\nContent-Length: {}\r\n{}Connection: close\r\n\r\n",
					     response.status, reason(response.status), response.contentType,
					     response.body.size(), extra);
	if (sendAll(client, head.data(), head.size()) && sendAll(client, response.body.data(), response.body.size()))
		finish();
	else
		closesocket(client);
}

} // namespace rdtest
