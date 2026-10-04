// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include <doctest/doctest.h>

#include "network/http_client.h"
#include "network/websocket_client.h"
#include "support/fake_server.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace rd;
using rdtest::FakeRequest;
using rdtest::FakeResponse;
using rdtest::FakeServer;
using rdtest::FakeSocket;

namespace {

using Clock = std::chrono::steady_clock;

long long msSince(Clock::time_point start)
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

// Sets a flag after a while, from another thread, the way a closing window cancels a request.
struct CancelAfter {
	std::atomic<bool> flag{false};
	std::thread thread;

	explicit CancelAfter(int ms)
		: thread([this, ms] {
			  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
			  flag = true;
		  })
	{
	}
	~CancelAfter() { thread.join(); }
};

HttpRequest get(const std::string &url, int timeoutMs = 5000)
{
	HttpRequest request;
	request.url = url;
	request.timeoutMs = timeoutMs;
	return request;
}

bool contains(const std::string &text, const std::string &part)
{
	return text.find(part) != std::string::npos;
}

} // namespace

TEST_SUITE("network.http")
{
	TEST_CASE("an address is split into its parts")
	{
		HttpUrl url;
		REQUIRE(parseHttpUrl("https://api.twitch.tv/helix/users?login=a", url));
		CHECK(url.secure);
		CHECK(url.host == "api.twitch.tv");
		CHECK(url.port == 443);
		CHECK(url.path == "/helix/users?login=a");

		REQUIRE(parseHttpUrl("wss://EventSub.wss.twitch.tv/ws", url));
		CHECK(url.secure);
		CHECK(url.host == "eventsub.wss.twitch.tv");
		CHECK(url.path == "/ws");

		REQUIRE(parseHttpUrl("https://example.com", url));
		CHECK(url.path == "/");
		REQUIRE(parseHttpUrl("https://example.com?x=1", url));
		CHECK(url.path == "/?x=1");
		REQUIRE(parseHttpUrl("https://example.com:8443/a#part", url));
		CHECK(url.port == 8443);
		CHECK(url.path == "/a");
	}

	TEST_CASE("a plain address is accepted on this PC and nowhere else")
	{
		HttpUrl url;
		REQUIRE(parseHttpUrl("http://127.0.0.1:8080/x", url));
		CHECK_FALSE(url.secure);
		CHECK(url.port == 8080);
		CHECK(parseHttpUrl("ws://localhost:9000/ws", url));
		CHECK(parseHttpUrl("http://127.0.0.1/", url));
		CHECK(url.port == 80);

		CHECK_FALSE(parseHttpUrl("http://example.com/", url));
		CHECK_FALSE(parseHttpUrl("ws://example.com/ws", url));
		CHECK_FALSE(parseHttpUrl("http://127.0.0.1.example.com/", url));
		CHECK_FALSE(parseHttpUrl("http://192.168.1.10/", url));
	}

	TEST_CASE("an address that could mislead is refused")
	{
		HttpUrl url;
		CHECK_FALSE(parseHttpUrl("", url));
		CHECK_FALSE(parseHttpUrl("example.com/path", url));
		CHECK_FALSE(parseHttpUrl("ftp://example.com/", url));
		CHECK_FALSE(parseHttpUrl("file:///C:/Windows/win.ini", url));
		CHECK_FALSE(parseHttpUrl("https://user:secret@example.com/", url));
		CHECK_FALSE(parseHttpUrl("https://example.com@evil.example/", url));
		CHECK_FALSE(parseHttpUrl("https://[::1]/", url));
		CHECK_FALSE(parseHttpUrl("https:///path", url));
		CHECK_FALSE(parseHttpUrl("https://exa mple.com/", url));
		CHECK_FALSE(parseHttpUrl("https://example.com:0/", url));
		CHECK_FALSE(parseHttpUrl("https://example.com:70000/", url));
		CHECK_FALSE(parseHttpUrl("https://example.com:80x/", url));
		CHECK_FALSE(parseHttpUrl("https://example.com/a b", url));
		CHECK_FALSE(parseHttpUrl("https://example.com/a\r\nHost: evil", url));
	}

	TEST_CASE("a value is encoded for a query or a form")
	{
		CHECK(urlEncode("abc-XYZ_0.9~") == "abc-XYZ_0.9~");
		CHECK(urlEncode("a b&c=d") == "a%20b%26c%3Dd");
		CHECK(urlEncode("user:read:chat") == "user%3Aread%3Achat");
		CHECK(urlEncode("\xC3\xA9") == "%C3%A9");
		CHECK(urlEncode("") == "");
	}

	TEST_CASE("a GET returns the status and the body, and sends its headers")
	{
		FakeServer server([](const FakeRequest &) { return FakeResponse{200, "application/json", R"({"ok":true})"}; });

		HttpRequest request = get(server.url("/v1/thing?a=1&b=two"));
		request.headers = {{"Authorization", "Bearer test-token-123"}, {"Client-Id", "abc"}};
		request.userAgent = "RelayDock/9.9.9";
		const std::atomic<bool> cancel{false};
		const HttpResponse response = httpRequest(request, cancel);

		CAPTURE(response.error);
		REQUIRE(response.ok);
		CHECK(response.status == 200);
		CHECK(response.body == R"({"ok":true})");

		const std::vector<FakeRequest> seen = server.requests();
		REQUIRE(seen.size() == 1);
		CHECK(seen[0].method == "GET");
		CHECK(seen[0].path == "/v1/thing");
		CHECK(seen[0].param("a") == "1");
		CHECK(seen[0].param("b") == "two");
		CHECK(seen[0].header("authorization") == "Bearer test-token-123");
		CHECK(seen[0].header("client-id") == "abc");
		CHECK(seen[0].header("user-agent") == "RelayDock/9.9.9");
	}

	TEST_CASE("a POST sends its body")
	{
		FakeServer server([](const FakeRequest &) { return FakeResponse{202, "application/json", "{}"}; });

		HttpRequest request = get(server.url("/oauth2/device"));
		request.method = "POST";
		request.headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
		request.body = "client_id=abc&scopes=" + urlEncode("user:read:chat");
		const std::atomic<bool> cancel{false};
		const HttpResponse response = httpRequest(request, cancel);

		CAPTURE(response.error);
		REQUIRE(response.ok);
		CHECK(response.status == 202);
		const std::vector<FakeRequest> seen = server.requests();
		REQUIRE(seen.size() == 1);
		CHECK(seen[0].method == "POST");
		CHECK(seen[0].header("content-type") == "application/x-www-form-urlencoded");
		CHECK(seen[0].form()["client_id"] == "abc");
		CHECK(seen[0].form()["scopes"] == "user:read:chat");
	}

	TEST_CASE("an error status is an answer, not a failure")
	{
		FakeServer server([](const FakeRequest &) { return FakeResponse{401, "application/json", R"({"message":"no"})"}; });
		const std::atomic<bool> cancel{false};
		const HttpResponse response = httpRequest(get(server.url("/x")), cancel);
		CHECK(response.ok);
		CHECK(response.status == 401);
		CHECK(response.body == R"({"message":"no"})");
		CHECK(response.error.empty());
	}

	TEST_CASE("a long answer arrives whole")
	{
		const std::string big(300 * 1000, 'x');
		FakeServer server([&](const FakeRequest &) { return FakeResponse{200, "text/plain", big}; });
		const std::atomic<bool> cancel{false};
		const HttpResponse response = httpRequest(get(server.url("/big")), cancel);
		REQUIRE(response.ok);
		CHECK(response.body.size() == big.size());
		CHECK(response.body == big);
	}

	TEST_CASE("an answer that is larger than allowed is discarded")
	{
		const std::string big(200 * 1000, 'x');
		FakeServer server([&](const FakeRequest &) { return FakeResponse{200, "text/plain", big}; });
		HttpRequest request = get(server.url("/big"));
		request.maxBytes = 100 * 1000;
		const std::atomic<bool> cancel{false};
		const HttpResponse response = httpRequest(request, cancel);
		CHECK_FALSE(response.ok);
		CHECK(response.body.empty());
		CHECK(contains(response.error, "larger than expected"));
	}

	TEST_CASE("a server that never answers: the request ends on its timeout")
	{
		FakeServer server([](const FakeRequest &) {
			FakeResponse response;
			response.hang = true;
			return response;
		});
		const std::atomic<bool> cancel{false};
		const auto started = Clock::now();
		const HttpResponse response = httpRequest(get(server.url("/hang"), 700), cancel);
		CHECK_FALSE(response.ok);
		CHECK_FALSE(response.cancelled());
		CHECK(contains(response.error, "did not answer in time"));
		// The request waited, and did not fail at once. Windows does not time this exactly: on
		// a GitHub runner a timeout of 700 ms came after 594 ms.
		CHECK(msSince(started) >= 350);
		CHECK(msSince(started) < 6000);
	}

	TEST_CASE("a cancel ends a request that waits for a server that never answers")
	{
		FakeServer server([](const FakeRequest &) {
			FakeResponse response;
			response.hang = true;
			return response;
		});
		CancelAfter cancel(300);
		const auto started = Clock::now();
		// The timeout is far away. Only the cancel can end this in time.
		const HttpResponse response = httpRequest(get(server.url("/hang"), 30000), cancel.flag);
		const long long took = msSince(started);
		CHECK_FALSE(response.ok);
		CHECK(response.cancelled());
		CHECK(took >= 250);
		CHECK(took < 2000);
		CHECK(server.count("/hang") == 1);
	}

	TEST_CASE("a cancel ends a request to an address that nobody answers")
	{
		// A reserved documentation network. A connection attempt gets no answer at all, or the
		// PC has no route to it and gives up at once. Either way the call must return quickly.
		CancelAfter cancel(300);
		const auto started = Clock::now();
		const HttpResponse response = httpRequest(get("https://192.0.2.1/", 30000), cancel.flag);
		CHECK_FALSE(response.ok);
		CHECK(msSince(started) < 2500);
	}

	TEST_CASE("a request that is cancelled before it starts touches nothing")
	{
		FakeServer server([](const FakeRequest &) { return FakeResponse{}; });
		const std::atomic<bool> cancel{true};
		const HttpResponse response = httpRequest(get(server.url("/x")), cancel);
		CHECK(response.cancelled());
		CHECK(server.requests().empty());
	}

	TEST_CASE("a closed port fails with a reason")
	{
		int port = 0;
		{
			// A port that was free a moment ago.
			FakeServer server([](const FakeRequest &) { return FakeResponse{}; });
			port = server.port();
		}
		const std::atomic<bool> cancel{false};
		const HttpResponse response = httpRequest(get("http://127.0.0.1:" + std::to_string(port) + "/", 3000), cancel);
		CHECK_FALSE(response.ok);
		CHECK_FALSE(response.error.empty());
		CHECK(contains(response.error, "Windows error"));
	}

	TEST_CASE("a request RelayDock would not send is refused before the network is used")
	{
		FakeServer server([](const FakeRequest &) { return FakeResponse{}; });
		const std::atomic<bool> cancel{false};

		HttpRequest injected = get(server.url("/x"));
		injected.headers = {{"X-Test", "a\r\nX-Evil: 1"}};
		CHECK_FALSE(httpRequest(injected, cancel).ok);

		HttpRequest badName = get(server.url("/x"));
		badName.headers = {{"X Test", "a"}};
		CHECK_FALSE(httpRequest(badName, cancel).ok);

		HttpRequest badMethod = get(server.url("/x"));
		badMethod.method = "GET /evil HTTP/1.1\r\nX: y";
		CHECK_FALSE(httpRequest(badMethod, cancel).ok);

		CHECK_FALSE(httpRequest(get("http://example.com/"), cancel).ok);
		CHECK(server.requests().empty());
	}
}

TEST_SUITE("network.websocket")
{
	namespace {

	FakeResponse upgrade()
	{
		FakeResponse response;
		response.webSocket = true;
		return response;
	}

	} // namespace

	TEST_CASE("text messages arrive whole and in order")
	{
		FakeServer server([](const FakeRequest &) { return upgrade(); }, [](FakeSocket &socket, const FakeRequest &) {
			socket.sendText(R"({"n":1})");
			socket.sendText(R"({"n":2})");
			socket.sendText(std::string(40 * 1000, 'y')); // Larger than one read
			socket.waitForClose(5000);
		});

		const std::atomic<bool> cancel{false};
		WebSocketClient client;
		std::string error;
		REQUIRE_MESSAGE(client.connect(server.wsUrl("/ws?x=1"), "RelayDock/test", 5000, cancel, error), error);
		CHECK(client.connected());

		std::string message;
		REQUIRE(client.receive(message, 5000, cancel, error) == WebSocketClient::Receive::Message);
		CHECK(message == R"({"n":1})");
		REQUIRE(client.receive(message, 5000, cancel, error) == WebSocketClient::Receive::Message);
		CHECK(message == R"({"n":2})");
		REQUIRE(client.receive(message, 5000, cancel, error) == WebSocketClient::Receive::Message);
		CHECK(message == std::string(40 * 1000, 'y'));

		const std::vector<FakeRequest> seen = server.requests();
		REQUIRE(seen.size() == 1);
		CHECK(seen[0].path == "/ws");
		CHECK(seen[0].param("x") == "1");
		CHECK(seen[0].header("upgrade") == "websocket");
		client.close();
		CHECK_FALSE(client.connected());
	}

	TEST_CASE("a message sent in pieces arrives as one")
	{
		FakeServer server([](const FakeRequest &) { return upgrade(); }, [](FakeSocket &socket, const FakeRequest &) {
			socket.sendFragments({R"({"type":)", R"("session_)", R"(welcome"})"});
			socket.waitForClose(5000);
		});
		const std::atomic<bool> cancel{false};
		WebSocketClient client;
		std::string error;
		REQUIRE_MESSAGE(client.connect(server.wsUrl("/ws"), "RelayDock/test", 5000, cancel, error), error);
		std::string message;
		REQUIRE(client.receive(message, 5000, cancel, error) == WebSocketClient::Receive::Message);
		CHECK(message == R"({"type":"session_welcome"})");
	}

	TEST_CASE("binary messages are dropped and a ping does not disturb")
	{
		FakeServer server([](const FakeRequest &) { return upgrade(); }, [](FakeSocket &socket, const FakeRequest &) {
			socket.sendBinary(std::string("\x01\x02\x03", 3));
			socket.sendPing();
			socket.sendText("after");
			socket.waitForClose(5000);
		});
		const std::atomic<bool> cancel{false};
		WebSocketClient client;
		std::string error;
		REQUIRE_MESSAGE(client.connect(server.wsUrl("/ws"), "RelayDock/test", 5000, cancel, error), error);
		std::string message;
		REQUIRE(client.receive(message, 5000, cancel, error) == WebSocketClient::Receive::Message);
		CHECK(message == "after");
	}

	TEST_CASE("a quiet connection reports idle and stays usable")
	{
		FakeServer server([](const FakeRequest &) { return upgrade(); }, [](FakeSocket &socket, const FakeRequest &) {
			socket.wait(1500);
			socket.sendText("late");
			socket.waitForClose(5000);
		});
		const std::atomic<bool> cancel{false};
		WebSocketClient client;
		std::string error;
		REQUIRE_MESSAGE(client.connect(server.wsUrl("/ws"), "RelayDock/test", 5000, cancel, error), error);

		std::string message;
		const auto started = Clock::now();
		CHECK(client.receive(message, 200, cancel, error) == WebSocketClient::Receive::Idle);
		CHECK(msSince(started) >= 150);
		CHECK(msSince(started) < 1200);
		CHECK(client.connected());
		// The message that arrives later is not lost.
		REQUIRE(client.receive(message, 5000, cancel, error) == WebSocketClient::Receive::Message);
		CHECK(message == "late");
	}

	TEST_CASE("the server closes the connection")
	{
		FakeServer server([](const FakeRequest &) { return upgrade(); }, [](FakeSocket &socket, const FakeRequest &) {
			socket.sendText("bye");
			socket.sendClose(1000);
			socket.waitForClose(2000);
		});
		const std::atomic<bool> cancel{false};
		WebSocketClient client;
		std::string error;
		REQUIRE_MESSAGE(client.connect(server.wsUrl("/ws"), "RelayDock/test", 5000, cancel, error), error);
		std::string message;
		REQUIRE(client.receive(message, 5000, cancel, error) == WebSocketClient::Receive::Message);
		CHECK(client.receive(message, 5000, cancel, error) == WebSocketClient::Receive::Closed);
		CHECK_FALSE(client.connected());
		// A receive on a closed connection fails instead of waiting.
		CHECK(client.receive(message, 5000, cancel, error) == WebSocketClient::Receive::Failed);
	}

	TEST_CASE("a connection that breaks off is reported as failed")
	{
		FakeServer server([](const FakeRequest &) { return upgrade(); }, [](FakeSocket &socket, const FakeRequest &) {
			socket.sendText("one");
			socket.wait(200);
			// Returning here drops the connection without a close message.
		});
		const std::atomic<bool> cancel{false};
		WebSocketClient client;
		std::string error;
		REQUIRE_MESSAGE(client.connect(server.wsUrl("/ws"), "RelayDock/test", 5000, cancel, error), error);
		std::string message;
		REQUIRE(client.receive(message, 5000, cancel, error) == WebSocketClient::Receive::Message);
		const WebSocketClient::Receive result = client.receive(message, 5000, cancel, error);
		CHECK((result == WebSocketClient::Receive::Failed || result == WebSocketClient::Receive::Closed));
		CHECK_FALSE(client.connected());
	}

	TEST_CASE("a cancel ends a receive that waits")
	{
		FakeServer server([](const FakeRequest &) { return upgrade(); },
				  [](FakeSocket &socket, const FakeRequest &) { socket.waitForClose(10000); });
		const std::atomic<bool> never{false};
		WebSocketClient client;
		std::string error;
		REQUIRE_MESSAGE(client.connect(server.wsUrl("/ws"), "RelayDock/test", 5000, never, error), error);

		CancelAfter cancel(300);
		std::string message;
		const auto started = Clock::now();
		CHECK(client.receive(message, 30000, cancel.flag, error) == WebSocketClient::Receive::Cancelled);
		CHECK(msSince(started) < 1500);
		// Closing with a receive under way must not hang or crash.
		const auto closing = Clock::now();
		client.close();
		CHECK(msSince(closing) < 2000);
	}

	TEST_CASE("a server that does not upgrade: connect fails and says what it got")
	{
		FakeServer server([](const FakeRequest &) { return FakeResponse{404, "text/plain", "nope"}; });
		const std::atomic<bool> cancel{false};
		WebSocketClient client;
		std::string error;
		CHECK_FALSE(client.connect(server.wsUrl("/ws"), "RelayDock/test", 5000, cancel, error));
		CHECK(contains(error, "404"));
		CHECK_FALSE(client.connected());
	}

	TEST_CASE("a cancel ends a connect to a server that never answers")
	{
		FakeServer server([](const FakeRequest &) {
			FakeResponse response;
			response.hang = true;
			return response;
		});
		CancelAfter cancel(300);
		WebSocketClient client;
		std::string error;
		const auto started = Clock::now();
		CHECK_FALSE(client.connect(server.wsUrl("/ws"), "RelayDock/test", 30000, cancel.flag, error));
		CHECK(error == "cancelled");
		CHECK(msSince(started) < 2000);
	}

	TEST_CASE("a connect that gets no answer ends on its timeout")
	{
		FakeServer server([](const FakeRequest &) {
			FakeResponse response;
			response.hang = true;
			return response;
		});
		const std::atomic<bool> cancel{false};
		WebSocketClient client;
		std::string error;
		const auto started = Clock::now();
		CHECK_FALSE(client.connect(server.wsUrl("/ws"), "RelayDock/test", 700, cancel, error));
		CHECK(contains(error, "did not answer in time"));
		CHECK(msSince(started) < 5000);
	}

	TEST_CASE("a message larger than the limit ends the connection")
	{
		FakeServer server([](const FakeRequest &) { return upgrade(); }, [](FakeSocket &socket, const FakeRequest &) {
			socket.sendText(std::string(WebSocketClient::kMaxMessageBytes + 5000, 'z'));
			socket.waitForClose(3000);
		});
		const std::atomic<bool> cancel{false};
		WebSocketClient client;
		std::string error;
		REQUIRE_MESSAGE(client.connect(server.wsUrl("/ws"), "RelayDock/test", 5000, cancel, error), error);
		std::string message;
		CHECK(client.receive(message, 10000, cancel, error) == WebSocketClient::Receive::Failed);
		CHECK(contains(error, "larger than RelayDock accepts"));
		CHECK_FALSE(client.connected());
	}

	TEST_CASE("a plain address on another computer is refused")
	{
		const std::atomic<bool> cancel{false};
		WebSocketClient client;
		std::string error;
		CHECK_FALSE(client.connect("ws://example.com/ws", "RelayDock/test", 5000, cancel, error));
		CHECK_FALSE(error.empty());
	}
}
