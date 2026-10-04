// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// A small web server for tests. It listens on 127.0.0.1 only, on a port Windows picks, speaks
// just enough HTTP and WebSocket to stand in for a platform, and answers with whatever the
// test's handler returns. It has no TLS, which is why RelayDock accepts plain addresses on this
// PC and nowhere else.
namespace rdtest {

struct FakeRequest {
	std::string method;
	std::string target; // "/path?name=value"
	std::string path;   // "/path"
	std::map<std::string, std::string> query;   // Decoded
	std::map<std::string, std::string> headers; // Names in lower case
	std::string body;

	std::string header(const std::string &name) const;
	std::string param(const std::string &name) const;
	// The body read as a form (name=value&name=value), decoded.
	std::map<std::string, std::string> form() const;
};

struct FakeResponse {
	int status = 200;
	std::string contentType = "application/json";
	std::string body;
	// Further header lines, such as {"Location", "..."} for a redirect.
	std::vector<std::pair<std::string, std::string>> headers;
	int delayMs = 0;        // Wait this long before answering
	bool hang = false;      // Never answer. The connection stays open until the server stops.
	bool webSocket = false; // Accept the WebSocket upgrade. The socket handler takes over.
};

// The server's end of one WebSocket connection. Used on the connection's own thread.
class FakeSocket {
public:
	FakeSocket(uintptr_t socket, const std::atomic<bool> &stopping) : socket_(socket), stopping_(stopping) {}

	bool sendText(const std::string &text);
	// One text message spread over several frames.
	bool sendFragments(const std::vector<std::string> &parts);
	bool sendBinary(const std::string &bytes);
	bool sendPing();
	bool sendClose(int code = 1000);

	// Sleeps. Returns false when the server stops or the client went away in the meantime.
	bool wait(int ms);
	// Waits until the client goes away. Returns false when the server stops first or `ms` pass.
	bool waitForClose(int ms);

private:
	bool sendFrame(unsigned char opcode, bool final, const std::string &payload);
	// Reads what the client sent. Returns false once the client is gone.
	bool pump(int ms);

	uintptr_t socket_;
	const std::atomic<bool> &stopping_;
	bool gone_ = false;
};

class FakeServer {
public:
	using Handler = std::function<FakeResponse(const FakeRequest &)>;
	using SocketHandler = std::function<void(FakeSocket &, const FakeRequest &)>;

	explicit FakeServer(Handler handler, SocketHandler socketHandler = {});
	~FakeServer();
	FakeServer(const FakeServer &) = delete;
	FakeServer &operator=(const FakeServer &) = delete;

	int port() const { return port_; }
	std::string url(const std::string &path = "/") const;   // http://127.0.0.1:<port>/path
	std::string wsUrl(const std::string &path = "/") const; // ws://127.0.0.1:<port>/path

	// Every request so far, in order of arrival.
	std::vector<FakeRequest> requests() const;
	size_t count(const std::string &path) const;

	// Ends every connection and stops listening. The destructor does the same.
	void stop();

private:
	void acceptLoop();
	void serve(uintptr_t socket);

	Handler handler_;
	SocketHandler socketHandler_;
	uintptr_t listener_ = ~static_cast<uintptr_t>(0);
	int port_ = 0;
	std::atomic<bool> stopping_{false};
	std::thread acceptThread_;
	mutable std::mutex mutex_;
	std::vector<std::thread> connections_;
	std::vector<FakeRequest> requests_;
};

// "a%20b+c" gives "a b c".
std::string urlDecode(const std::string &text);
std::map<std::string, std::string> parseForm(const std::string &text);

} // namespace rdtest
