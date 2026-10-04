// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
//
// Every token and key in this file is made up for the test. None belongs to a real account.
#include <doctest/doctest.h>

#include "chat/twitch_chat.h"
#include "chat/youtube_chat.h"
#include "security/redactor.h"
#include "support/fake_platforms.h"
#include "utils/log.h"

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace rd;
using rdtest::FakeRequest;
using rdtest::FakeTwitch;
using rdtest::FakeYouTube;

namespace {

using Clock = std::chrono::steady_clock;

long long msSince(Clock::time_point start)
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

template <class Predicate> bool waitUntil(Predicate predicate, int ms = 6000)
{
	const auto deadline = Clock::now() + std::chrono::milliseconds(ms);
	while (Clock::now() < deadline) {
		if (predicate())
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return predicate();
}

bool contains(const std::string &text, const std::string &part)
{
	return text.find(part) != std::string::npos;
}

// Collects what a reader reports, the way the interface does.
struct Recorder {
	mutable std::mutex mutex;
	std::vector<ChatEvent> events;
	std::vector<ChatStatus> statuses;

	ChatWorker::Callbacks callbacks()
	{
		ChatWorker::Callbacks out;
		out.events = [this](std::vector<ChatEvent> batch) {
			const std::lock_guard lock(mutex);
			events.insert(events.end(), batch.begin(), batch.end());
		};
		out.status = [this](const ChatStatus &status) {
			const std::lock_guard lock(mutex);
			statuses.push_back(status);
		};
		return out;
	}

	size_t eventCount() const
	{
		const std::lock_guard lock(mutex);
		return events.size();
	}
	std::vector<ChatEvent> allEvents() const
	{
		const std::lock_guard lock(mutex);
		return events;
	}
	int count(ChatState state) const
	{
		const std::lock_guard lock(mutex);
		int n = 0;
		for (const ChatStatus &status : statuses)
			n += status.state == state ? 1 : 0;
		return n;
	}
	ChatStatus last() const
	{
		const std::lock_guard lock(mutex);
		return statuses.empty() ? ChatStatus{} : statuses.back();
	}
	bool waitFor(ChatState state, int times = 1, int ms = 6000) const
	{
		return waitUntil([&] { return count(state) >= times; }, ms);
	}
	bool waitForEvents(size_t n, int ms = 6000) const
	{
		return waitUntil([&] { return eventCount() >= n; }, ms);
	}
	// Every word RelayDock would show the user, in one string.
	std::string allText() const
	{
		const std::lock_guard lock(mutex);
		std::string out;
		for (const ChatStatus &status : statuses)
			out += status.message.text() + " " + status.account + "\n";
		return out;
	}
};

// The saved sign-in, in memory.
struct MemoryStore {
	mutable std::mutex mutex;
	std::string token;
	int saves = 0;
	int clears = 0;

	TwitchTokenStore store()
	{
		TwitchTokenStore out;
		out.load = [this] {
			const std::lock_guard lock(mutex);
			return token;
		};
		out.save = [this](const std::string &value) {
			const std::lock_guard lock(mutex);
			token = value;
			++saves;
		};
		out.clear = [this] {
			const std::lock_guard lock(mutex);
			token.clear();
			++clears;
		};
		return out;
	}
	std::string current() const
	{
		const std::lock_guard lock(mutex);
		return token;
	}
};

TwitchChatConfig twitchConfig(const FakeTwitch &fake)
{
	TwitchChatConfig config;
	config.endpoints.auth = fake.authUrl();
	config.endpoints.api = fake.apiUrl();
	config.endpoints.eventSub = fake.eventSubUrl();
	config.clientId = fake.clientId;
	config.userAgent = "RelayDock/test";
	config.timeoutMs = 3000;
	config.retryFirstMs = 50;
	config.retryMaxMs = 200;
	config.signInPollMs = 30;
	config.keepaliveGraceMs = 500;
	return config;
}

YouTubeChatConfig youtubeConfig(const FakeYouTube &fake)
{
	YouTubeChatConfig config;
	config.endpoints.api = fake.apiUrl();
	config.apiKey = fake.apiKey;
	config.videoId = fake.videoId;
	config.userAgent = "RelayDock/test";
	config.timeoutMs = 3000;
	config.minPollMs = 20;
	config.notLiveRetryMs = 80;
	config.retryFirstMs = 50;
	config.retryMaxMs = 200;
	return config;
}

// What the sign-in reports.
struct SignInResult {
	std::mutex mutex;
	bool hasCode = false;
	twitch::DeviceCode code;
	bool finished = false;
	bool ok = false;
	std::string login;
	UserMessage problem;

	TwitchSignIn::Callbacks callbacks()
	{
		TwitchSignIn::Callbacks out;
		out.code = [this](const twitch::DeviceCode &value) {
			const std::lock_guard lock(mutex);
			code = value;
			hasCode = true;
		};
		out.finished = [this](bool success, const std::string &name, const UserMessage &message) {
			const std::lock_guard lock(mutex);
			finished = true;
			ok = success;
			login = name;
			problem = message;
		};
		return out;
	}
	bool isFinished()
	{
		const std::lock_guard lock(mutex);
		return finished;
	}
	bool gotCode()
	{
		const std::lock_guard lock(mutex);
		return hasCode;
	}
};

struct CapturedLog {
	std::mutex mutex;
	std::vector<std::string> lines;

	CapturedLog()
	{
		clearRecentLogLines();
		setLogSink([this](LogLevel, const std::string &line) {
			const std::lock_guard lock(mutex);
			lines.push_back(line);
		});
	}
	~CapturedLog()
	{
		setLogSink(nullptr);
		clearRecentLogLines();
		globalRedactor().clearSecrets();
	}
	bool anyContains(const std::string &needle)
	{
		const std::lock_guard lock(mutex);
		for (const std::string &line : lines) {
			if (line.find(needle) != std::string::npos)
				return true;
		}
		return false;
	}
	size_t size()
	{
		const std::lock_guard lock(mutex);
		return lines.size();
	}
};

} // namespace

TEST_SUITE("chat.twitch_sign_in")
{
	TEST_CASE("the sign-in shows Twitch's code, waits for the user and ends with their name")
	{
		FakeTwitch twitch;
		twitch.pendingPolls = 2;
		MemoryStore store;
		SignInResult result;
		TwitchSignIn signIn(twitchConfig(twitch), store.store(), result.callbacks());
		signIn.start();

		REQUIRE(waitUntil([&] { return result.isFinished(); }));
		CHECK(result.hasCode);
		CHECK(result.code.userCode == "ABCDEFGH");
		CHECK(result.code.verificationUri == "https://www.twitch.tv/activate?public=true&device-code=ABCDEFGH");
		CHECK(result.ok);
		CHECK(result.login == "teststreamer");
		CHECK(result.problem.empty());
		// Two "not yet", then the grant.
		CHECK(twitch.tokenPolls() == 3);
		// What is saved is the token that renews the sign-in, and nothing else.
		CHECK(store.saves == 1);
		CHECK(store.current() == twitch.currentRefreshToken());
		CHECK_FALSE(store.current().empty());
		CHECK(waitUntil([&] { return !signIn.running(); }));
	}

	TEST_CASE("a sign-in Twitch refuses ends with a reason and saves nothing")
	{
		FakeTwitch twitch;
		twitch.refuseSignIn = true;
		MemoryStore store;
		SignInResult result;
		TwitchSignIn signIn(twitchConfig(twitch), store.store(), result.callbacks());
		signIn.start();

		REQUIRE(waitUntil([&] { return result.isFinished(); }));
		CHECK_FALSE(result.ok);
		CHECK_FALSE(result.problem.what.empty());
		CHECK_FALSE(result.problem.action.empty());
		CHECK(contains(result.problem.detail, "invalid device code"));
		CHECK(store.saves == 0);
		CHECK(store.current().empty());
	}

	TEST_CASE("a code nobody confirms runs out")
	{
		FakeTwitch twitch;
		twitch.pendingPolls = 100000;
		twitch.codeExpiresInSec = 1;
		MemoryStore store;
		SignInResult result;
		TwitchSignIn signIn(twitchConfig(twitch), store.store(), result.callbacks());
		const auto started = Clock::now();
		signIn.start();

		REQUIRE(waitUntil([&] { return result.isFinished(); }));
		CHECK_FALSE(result.ok);
		CHECK(contains(result.problem.what, "expired"));
		CHECK(msSince(started) >= 900);
		CHECK(store.saves == 0);
	}

	TEST_CASE("a cancel ends a sign-in that waits for the user, and nothing is reported")
	{
		FakeTwitch twitch;
		twitch.pendingPolls = 100000;
		MemoryStore store;
		SignInResult result;
		TwitchSignIn signIn(twitchConfig(twitch), store.store(), result.callbacks());
		signIn.start();
		REQUIRE(waitUntil([&] { return result.gotCode(); }));

		const auto started = Clock::now();
		signIn.cancel();
		CHECK(msSince(started) < 1000);
		CHECK_FALSE(signIn.running());
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
		CHECK_FALSE(result.isFinished());
		CHECK(store.saves == 0);
	}

	TEST_CASE("without an application id there is nothing to sign in with")
	{
		FakeTwitch twitch;
		TwitchChatConfig config = twitchConfig(twitch);
		config.clientId.clear();
		MemoryStore store;
		SignInResult result;
		TwitchSignIn signIn(config, store.store(), result.callbacks());
		signIn.start();

		REQUIRE(waitUntil([&] { return result.isFinished(); }));
		CHECK_FALSE(result.ok);
		CHECK(contains(result.problem.what, "application id"));
		CHECK(twitch.requests().empty());
	}

	TEST_CASE("Twitch gives no code: the sign-in ends and says what Twitch answered")
	{
		FakeTwitch twitch;
		twitch.deviceCodeStatus = 400;
		MemoryStore store;
		SignInResult result;
		TwitchSignIn signIn(twitchConfig(twitch), store.store(), result.callbacks());
		signIn.start();

		REQUIRE(waitUntil([&] { return result.isFinished(); }));
		CHECK_FALSE(result.ok);
		CHECK_FALSE(result.hasCode);
		CHECK(contains(result.problem.detail, "400"));
	}
}

TEST_SUITE("chat.twitch_reader")
{
	TEST_CASE("nobody is signed in: nothing is sent")
	{
		FakeTwitch twitch;
		MemoryStore store;
		Recorder recorder;
		TwitchChat chat(twitchConfig(twitch), store.store(), recorder.callbacks());
		chat.start();

		REQUIRE(recorder.waitFor(ChatState::NotSetUp));
		CHECK(contains(recorder.last().message.what, "sign-in"));
		CHECK(waitUntil([&] { return !chat.running(); }));
		CHECK(twitch.requests().empty());
	}

	TEST_CASE("comments arrive, and the renewed sign-in is saved")
	{
		FakeTwitch twitch;
		MemoryStore store;
		store.token = twitch.existingSignIn();
		const std::string before = store.token;
		Recorder recorder;
		TwitchChat chat(twitchConfig(twitch), store.store(), recorder.callbacks());
		chat.start();

		REQUIRE(recorder.waitFor(ChatState::Connected));
		CHECK(recorder.last().account == "teststreamer");
		CHECK(recorder.last().message.empty());
		// Twitch replaced the saved token, and RelayDock kept the new one.
		CHECK(twitch.refreshes() == 1);
		CHECK(store.current() == twitch.currentRefreshToken());
		CHECK(store.current() != before);

		// One subscription for comments, one for what Twitch announces in chat, both for the
		// user's own channel and this session.
		const auto subscriptions = twitch.subscriptions();
		REQUIRE(subscriptions.size() == 2);
		CHECK(subscriptions[0]["type"] == "channel.chat.message");
		CHECK(subscriptions[1]["type"] == "channel.chat.notification");
		for (const auto &subscription : subscriptions) {
			CHECK(subscription["condition"]["broadcaster_user_id"] == "1971641");
			CHECK(subscription["condition"]["user_id"] == "1971641");
			CHECK(subscription["transport"]["session_id"] == "session-1");
		}

		twitch.chat("m1", "viewer_one", "hello from Twitch");
		REQUIRE(recorder.waitForEvents(1));
		const ChatEvent event = recorder.allEvents()[0];
		CHECK(event.platform == ChatPlatform::Twitch);
		CHECK(event.kind == ChatEventKind::Message);
		CHECK(event.id == "m1");
		CHECK(event.author == "viewer_one");
		CHECK(event.text == "hello from Twitch");
	}

	TEST_CASE("Bits, subscriptions and raids arrive as highlights")
	{
		FakeTwitch twitch;
		MemoryStore store;
		store.token = twitch.existingSignIn();
		Recorder recorder;
		TwitchChat chat(twitchConfig(twitch), store.store(), recorder.callbacks());
		chat.start();
		REQUIRE(recorder.waitFor(ChatState::Connected));

		twitch.chat("b1", "viewer_one", "Cheer100 nice", 100);
		twitch.notice("s1", "sub", "viewer subscribed at Tier 1.");
		twitch.notice("g1", "community_sub_gift", "viewer is gifting 5 Tier 1 Subs to teststreamer's community!");
		twitch.notice("r1", "raid", "15 raiders from otherchannel have joined!");
		REQUIRE(recorder.waitForEvents(4));

		const std::vector<ChatEvent> events = recorder.allEvents();
		CHECK(events[0].kind == ChatEventKind::Paid);
		CHECK(events[0].headline == "100 Bits");
		CHECK(events[1].kind == ChatEventKind::Membership);
		CHECK(events[1].headline == "viewer subscribed at Tier 1.");
		CHECK(events[2].kind == ChatEventKind::Membership);
		CHECK(contains(events[2].headline, "gifting 5"));
		CHECK(events[3].kind == ChatEventKind::Raid);
		for (const ChatEvent &event : events)
			CHECK(isHighlight(event));
	}

	TEST_CASE("a message Twitch sends twice is delivered once")
	{
		FakeTwitch twitch;
		MemoryStore store;
		store.token = twitch.existingSignIn();
		Recorder recorder;
		TwitchChat chat(twitchConfig(twitch), store.store(), recorder.callbacks());
		chat.start();
		REQUIRE(recorder.waitFor(ChatState::Connected));

		twitch.chat("m1", "viewer", "once");
		twitch.repeatLast();
		twitch.chat("m2", "viewer", "second");
		REQUIRE(recorder.waitForEvents(2));
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
		const std::vector<ChatEvent> events = recorder.allEvents();
		REQUIRE(events.size() == 2);
		CHECK(events[0].id == "m1");
		CHECK(events[1].id == "m2");
	}

	TEST_CASE("text that is no EventSub message is ignored and the reading goes on")
	{
		FakeTwitch twitch;
		MemoryStore store;
		store.token = twitch.existingSignIn();
		Recorder recorder;
		TwitchChat chat(twitchConfig(twitch), store.store(), recorder.callbacks());
		chat.start();
		REQUIRE(recorder.waitFor(ChatState::Connected));

		twitch.sendRaw("not json at all");
		twitch.sendRaw(R"({"metadata":{"message_type":"notification","message_id":"x1","subscription_type":"channel.chat.message"},"payload":{"event":"not an object"}})");
		twitch.chat("m1", "viewer", "still here");
		REQUIRE(recorder.waitForEvents(1));
		CHECK(recorder.allEvents()[0].text == "still here");
		CHECK(twitch.connections() == 1);
	}

	TEST_CASE("a sign-in Twitch no longer accepts is forgotten, and the user is told to sign in again")
	{
		FakeTwitch twitch;
		twitch.existingSignIn();
		MemoryStore store;
		store.token = "a-token-twitch-does-not-know";
		Recorder recorder;
		TwitchChat chat(twitchConfig(twitch), store.store(), recorder.callbacks());
		chat.start();

		REQUIRE(recorder.waitFor(ChatState::Stopped));
		CHECK(store.clears == 1);
		CHECK(store.current().empty());
		CHECK(contains(recorder.last().message.what, "no longer accepts"));
		CHECK(contains(recorder.last().message.action, "Sign in again"));
		CHECK(twitch.connections() == 0);
		CHECK(waitUntil([&] { return !chat.running(); }));
	}

	TEST_CASE("a renewal that fails for a while is tried again, and the sign-in is kept")
	{
		FakeTwitch twitch;
		twitch.failRefreshes = 2;
		MemoryStore store;
		store.token = twitch.existingSignIn();
		Recorder recorder;
		TwitchChat chat(twitchConfig(twitch), store.store(), recorder.callbacks());
		chat.start();

		REQUIRE(recorder.waitFor(ChatState::Connected));
		CHECK(recorder.count(ChatState::Waiting) == 2);
		CHECK(twitch.refreshes() == 3);
		// A server error says nothing about the sign-in. It must not be thrown away.
		CHECK(store.clears == 0);
		CHECK(contains(recorder.allText(), "tries again in"));
	}

	TEST_CASE("a connection that breaks is opened again and subscribed again")
	{
		FakeTwitch twitch;
		MemoryStore store;
		store.token = twitch.existingSignIn();
		Recorder recorder;
		TwitchChat chat(twitchConfig(twitch), store.store(), recorder.callbacks());
		chat.start();
		REQUIRE(recorder.waitFor(ChatState::Connected));

		twitch.dropConnection();
		REQUIRE(recorder.waitFor(ChatState::Connected, 2));
		CHECK(recorder.count(ChatState::Waiting) >= 1);
		CHECK(twitch.connections() == 2);
		// A new session has no subscriptions, so both are made again.
		const auto subscriptions = twitch.subscriptions();
		REQUIRE(subscriptions.size() == 4);
		CHECK(subscriptions[2]["transport"]["session_id"] == "session-2");
		// The token was still good, so the sign-in was not renewed a second time.
		CHECK(twitch.refreshes() == 1);

		twitch.chat("after", "viewer", "back again");
		REQUIRE(recorder.waitForEvents(1));
		CHECK(recorder.allEvents()[0].id == "after");
	}

	TEST_CASE("the server says goodbye: the connection is opened again")
	{
		FakeTwitch twitch;
		MemoryStore store;
		store.token = twitch.existingSignIn();
		Recorder recorder;
		TwitchChat chat(twitchConfig(twitch), store.store(), recorder.callbacks());
		chat.start();
		REQUIRE(recorder.waitFor(ChatState::Connected));

		twitch.closeConnection();
		REQUIRE(recorder.waitFor(ChatState::Connected, 2));
		CHECK(twitch.connections() == 2);
	}

	TEST_CASE("Twitch moves the session: RelayDock follows and does not subscribe again")
	{
		FakeTwitch twitch;
		MemoryStore store;
		store.token = twitch.existingSignIn();
		Recorder recorder;
		TwitchChat chat(twitchConfig(twitch), store.store(), recorder.callbacks());
		chat.start();
		REQUIRE(recorder.waitFor(ChatState::Connected));

		twitch.requestReconnect();
		REQUIRE(waitUntil([&] { return twitch.movedConnections() == 1; }));
		REQUIRE(recorder.waitFor(ChatState::Connected, 2));
		CHECK(twitch.connections() == 2);
		CHECK(twitch.subscriptions().size() == 2);
		// A move is not a failure.
		CHECK(recorder.count(ChatState::Waiting) == 0);

		twitch.chat("moved", "viewer", "on the new connection");
		REQUIRE(recorder.waitForEvents(1));
		CHECK(recorder.allEvents()[0].id == "moved");
	}

	TEST_CASE("Twitch ends the permission: the reading stops and says so")
	{
		FakeTwitch twitch;
		MemoryStore store;
		store.token = twitch.existingSignIn();
		Recorder recorder;
		TwitchChat chat(twitchConfig(twitch), store.store(), recorder.callbacks());
		chat.start();
		REQUIRE(recorder.waitFor(ChatState::Connected));

		twitch.revoke("authorization_revoked");
		REQUIRE(recorder.waitFor(ChatState::Stopped));
		CHECK(contains(recorder.last().message.what, "permission"));
		CHECK(recorder.last().message.detail == "authorization_revoked");
		CHECK(waitUntil([&] { return !chat.running(); }));
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		CHECK(twitch.connections() == 1);
	}

	TEST_CASE("a token that ran out is renewed at once, without the retry wait")
	{
		FakeTwitch twitch;
		twitch.unauthorizedSubscribes = 1;
		MemoryStore store;
		store.token = twitch.existingSignIn();
		Recorder recorder;
		TwitchChat chat(twitchConfig(twitch), store.store(), recorder.callbacks());
		chat.start();

		REQUIRE(recorder.waitFor(ChatState::Connected));
		CHECK(twitch.refreshes() == 2);
		CHECK(recorder.count(ChatState::Waiting) == 0);
		CHECK(store.current() == twitch.currentRefreshToken());
	}

	TEST_CASE("Twitch refuses the subscription: the reading stops instead of asking forever")
	{
		FakeTwitch twitch;
		twitch.subscribeStatus = 403;
		MemoryStore store;
		store.token = twitch.existingSignIn();
		Recorder recorder;
		TwitchChat chat(twitchConfig(twitch), store.store(), recorder.callbacks());
		chat.start();

		REQUIRE(recorder.waitFor(ChatState::Stopped));
		CHECK(contains(recorder.last().message.what, "does not let RelayDock read"));
		CHECK(recorder.count(ChatState::Connected) == 0);
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		CHECK(twitch.connections() == 1);
	}

	TEST_CASE("a connection that goes silent is noticed and opened again")
	{
		FakeTwitch twitch;
		twitch.keepaliveEveryMs = 0; // The welcome promises a sign of life every second. None comes.
		MemoryStore store;
		store.token = twitch.existingSignIn();
		Recorder recorder;
		TwitchChat chat(twitchConfig(twitch), store.store(), recorder.callbacks());
		chat.start();
		REQUIRE(recorder.waitFor(ChatState::Connected));

		REQUIRE(waitUntil([&] { return twitch.connections() >= 2; }, 8000));
		CHECK(recorder.count(ChatState::Waiting) >= 1);
		CHECK(contains(recorder.allText(), "stopped sending"));
	}

	TEST_CASE("a server that opens the connection and never welcomes is given up on and tried again")
	{
		FakeTwitch twitch;
		twitch.sendWelcome = false;
		MemoryStore store;
		store.token = twitch.existingSignIn();
		TwitchChatConfig config = twitchConfig(twitch);
		config.timeoutMs = 400;
		Recorder recorder;
		TwitchChat chat(config, store.store(), recorder.callbacks());
		chat.start();

		REQUIRE(waitUntil([&] { return twitch.connections() >= 2; }, 8000));
		CHECK(recorder.count(ChatState::Connected) == 0);
		CHECK(contains(recorder.allText(), "did not open the session"));
	}

	TEST_CASE("stop ends the reading at once and reports off")
	{
		FakeTwitch twitch;
		MemoryStore store;
		store.token = twitch.existingSignIn();
		Recorder recorder;
		TwitchChat chat(twitchConfig(twitch), store.store(), recorder.callbacks());
		chat.start();
		REQUIRE(recorder.waitFor(ChatState::Connected));

		const auto started = Clock::now();
		chat.stop();
		CHECK(msSince(started) < 1000);
		CHECK_FALSE(chat.running());
		CHECK(recorder.last().state == ChatState::Off);
		CHECK(chat.status().state == ChatState::Off);

		// It can be started again.
		chat.start();
		REQUIRE(recorder.waitFor(ChatState::Connected, 2));
	}

	TEST_CASE("destroying a reader that waits for a retry does not hang")
	{
		FakeTwitch twitch;
		twitch.failRefreshes = 100000;
		MemoryStore store;
		store.token = twitch.existingSignIn();
		TwitchChatConfig config = twitchConfig(twitch);
		config.retryFirstMs = 60000;
		config.retryMaxMs = 60000;
		Recorder recorder;
		const auto started = Clock::now();
		{
			TwitchChat chat(config, store.store(), recorder.callbacks());
			chat.start();
			REQUIRE(recorder.waitFor(ChatState::Waiting));
		}
		CHECK(msSince(started) < 3000);
	}
}

TEST_SUITE("security.chat")
{
	TEST_CASE("Twitch tokens never reach a log line or a message for the user")
	{
		CapturedLog log;
		FakeTwitch twitch;
		twitch.pendingPolls = 1;
		MemoryStore store;

		// A sign-in, then a reading with a broken connection in between, so every path logs.
		SignInResult result;
		{
			TwitchSignIn signIn(twitchConfig(twitch), store.store(), result.callbacks());
			signIn.start();
			REQUIRE(waitUntil([&] { return result.isFinished(); }));
		}
		REQUIRE(result.ok);
		const std::string firstRefresh = store.current();
		const std::string firstAccess = twitch.currentAccessToken();

		Recorder recorder;
		{
			TwitchChat chat(twitchConfig(twitch), store.store(), recorder.callbacks());
			chat.start();
			REQUIRE(recorder.waitFor(ChatState::Connected));
			twitch.dropConnection();
			REQUIRE(recorder.waitFor(ChatState::Connected, 2));
		}

		CHECK(log.size() > 0);
		for (const std::string &secret : {firstRefresh, firstAccess, twitch.currentRefreshToken(),
						   twitch.currentAccessToken(), std::string("device-code-0123456789abcdef")}) {
			REQUIRE_FALSE(secret.empty());
			CHECK_FALSE(log.anyContains(secret));
			CHECK_FALSE(contains(recorder.allText(), secret));
			CHECK_FALSE(contains(result.problem.text(), secret));
		}
		// Even a line that tries to print one comes out without it.
		logInfo("token {}", twitch.currentAccessToken());
		CHECK_FALSE(log.anyContains(twitch.currentAccessToken()));
	}

	TEST_CASE("the YouTube key never reaches an address, a log line or a message for the user")
	{
		CapturedLog log;
		FakeYouTube youtube;
		Recorder recorder;
		{
			YouTubeChat chat(recorder.callbacks());
			chat.start(youtubeConfig(youtube));
			REQUIRE(recorder.waitFor(ChatState::Connected));
			youtube.comment("y1", "Viewer", "hi");
			REQUIRE(recorder.waitForEvents(1));
			youtube.failMessages(500, "backendError", 1);
			REQUIRE(recorder.waitFor(ChatState::Waiting));
		}

		const std::vector<FakeRequest> requests = youtube.requests();
		REQUIRE(requests.size() >= 2);
		for (const FakeRequest &request : requests) {
			CHECK_FALSE(contains(request.target, youtube.apiKey));
			CHECK(request.header("x-goog-api-key") == youtube.apiKey);
		}
		CHECK(log.size() > 0);
		CHECK_FALSE(log.anyContains(youtube.apiKey));
		CHECK_FALSE(contains(recorder.allText(), youtube.apiKey));
		logInfo("key {}", youtube.apiKey);
		CHECK_FALSE(log.anyContains(youtube.apiKey));
	}
}

TEST_SUITE("chat.youtube_reader")
{
	TEST_CASE("without a key or a stream nothing is sent")
	{
		FakeYouTube youtube;
		Recorder recorder;
		YouTubeChat chat(recorder.callbacks());

		YouTubeChatConfig noKey = youtubeConfig(youtube);
		noKey.apiKey.clear();
		chat.start(noKey);
		REQUIRE(recorder.waitFor(ChatState::NotSetUp));
		CHECK(contains(recorder.last().message.what, "API key"));

		YouTubeChatConfig noVideo = youtubeConfig(youtube);
		noVideo.videoId.clear();
		chat.start(noVideo);
		REQUIRE(recorder.waitFor(ChatState::NotSetUp, 2));
		CHECK(contains(recorder.last().message.what, "link to your stream"));

		// A sentence pasted into the key field is not sent anywhere either.
		YouTubeChatConfig pasted = youtubeConfig(youtube);
		pasted.apiKey = "this is not a key, it is a sentence";
		chat.start(pasted);
		REQUIRE(recorder.waitFor(ChatState::NotSetUp, 3));

		CHECK(youtube.requests().empty());
		CHECK(chat.requests() == 0);
	}

	TEST_CASE("comments and a Super Chat arrive, page after page")
	{
		FakeYouTube youtube;
		youtube.comment("y1", "Viewer One", "first");
		Recorder recorder;
		YouTubeChat chat(recorder.callbacks());
		chat.start(youtubeConfig(youtube));

		REQUIRE(recorder.waitFor(ChatState::Connected));
		CHECK(recorder.last().account == "Test stream");
		REQUIRE(recorder.waitForEvents(1));

		youtube.superChat("y2", "Viewer Two", "NZ$5.00", "great stream");
		youtube.comment("y3", "Viewer One", "third");
		REQUIRE(recorder.waitForEvents(3));

		const std::vector<ChatEvent> events = recorder.allEvents();
		CHECK(events[0].platform == ChatPlatform::YouTube);
		CHECK(events[0].id == "y1");
		CHECK(events[0].author == "Viewer One");
		CHECK(events[0].text == "first");
		CHECK(events[1].kind == ChatEventKind::Paid);
		CHECK(events[1].headline == "NZ$5.00 Super Chat");
		CHECK(events[1].text == "great stream");
		CHECK(events[2].id == "y3");

		// The first request has no page token. Every later one carries the token of the page
		// before it, which is how nothing is read twice.
		REQUIRE(waitUntil([&] { return youtube.messageRequests() >= 3; }));
		const std::vector<std::string> tokens = youtube.pageTokens();
		REQUIRE(tokens.size() >= 3);
		CHECK(tokens[0].empty());
		CHECK(tokens[1] == "page-1");
		CHECK(tokens[2] == "page-2");
		CHECK(youtube.videoRequests() == 1);
		CHECK(chat.requests() >= 4);
	}

	TEST_CASE("a key Google does not accept stops the reading and says so")
	{
		FakeYouTube youtube;
		YouTubeChatConfig config = youtubeConfig(youtube);
		config.apiKey = "test-key-NOT-REAL-another-9876543210-zyx";
		Recorder recorder;
		YouTubeChat chat(recorder.callbacks());
		chat.start(config);

		REQUIRE(recorder.waitFor(ChatState::Stopped));
		CHECK(contains(recorder.last().message.what, "did not accept your API key"));
		CHECK(waitUntil([&] { return !chat.running(); }));
		CHECK(youtube.messageRequests() == 0);
		CHECK(chat.requests() == 1);
	}

	TEST_CASE("a link to no video stops the reading")
	{
		FakeYouTube youtube;
		YouTubeChatConfig config = youtubeConfig(youtube);
		config.videoId = "aaaaaaaaaaa";
		Recorder recorder;
		YouTubeChat chat(recorder.callbacks());
		chat.start(config);

		REQUIRE(recorder.waitFor(ChatState::Stopped));
		CHECK(contains(recorder.last().message.what, "found no stream"));
	}

	TEST_CASE("a stream that has not started is looked at again until it is live")
	{
		FakeYouTube youtube;
		youtube.live = false;
		Recorder recorder;
		YouTubeChat chat(recorder.callbacks());
		chat.start(youtubeConfig(youtube));

		REQUIRE(recorder.waitFor(ChatState::Waiting));
		CHECK(contains(recorder.last().message.what, "no live chat right now"));
		CHECK(recorder.last().account == "Test stream");
		REQUIRE(waitUntil([&] { return youtube.videoRequests() >= 3; }));
		CHECK(youtube.messageRequests() == 0);

		youtube.live = true;
		REQUIRE(recorder.waitFor(ChatState::Connected));
		youtube.comment("y1", "Viewer", "we are live");
		REQUIRE(recorder.waitForEvents(1));
	}

	TEST_CASE("a stream that is over stops the reading")
	{
		FakeYouTube youtube;
		youtube.ended = true;
		Recorder recorder;
		YouTubeChat chat(recorder.callbacks());
		chat.start(youtubeConfig(youtube));

		REQUIRE(recorder.waitFor(ChatState::Stopped));
		CHECK(contains(recorder.last().message.what, "has ended"));
		CHECK(youtube.videoRequests() == 1);
	}

	TEST_CASE("the daily quota running out stops the reading and says when it comes back")
	{
		FakeYouTube youtube;
		Recorder recorder;
		YouTubeChat chat(recorder.callbacks());
		chat.start(youtubeConfig(youtube));
		REQUIRE(recorder.waitFor(ChatState::Connected));

		youtube.failMessages(403, "quotaExceeded", 1000);
		REQUIRE(recorder.waitFor(ChatState::Stopped));
		CHECK(contains(recorder.last().message.what, "used its requests for today"));
		CHECK(contains(recorder.last().message.action, "midnight Pacific Time"));
		// It does not keep asking, which would be pointless and would look like abuse.
		const int asked = youtube.messageRequests();
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		CHECK(youtube.messageRequests() == asked);
	}

	TEST_CASE("the stream going offline stops the reading after its last messages")
	{
		FakeYouTube youtube;
		Recorder recorder;
		YouTubeChat chat(recorder.callbacks());
		chat.start(youtubeConfig(youtube));
		REQUIRE(recorder.waitFor(ChatState::Connected));

		youtube.comment("last", "Viewer", "bye");
		youtube.goOffline();
		REQUIRE(recorder.waitFor(ChatState::Stopped));
		CHECK(contains(recorder.last().message.what, "has ended"));
		REQUIRE(recorder.eventCount() == 1);
		CHECK(recorder.allEvents()[0].id == "last");
	}

	TEST_CASE("a request that fails is tried again, and nothing is lost or read twice")
	{
		FakeYouTube youtube;
		Recorder recorder;
		YouTubeChat chat(recorder.callbacks());
		chat.start(youtubeConfig(youtube));
		REQUIRE(recorder.waitFor(ChatState::Connected));
		youtube.comment("y1", "Viewer", "before");
		REQUIRE(recorder.waitForEvents(1));

		youtube.failMessages(500, "backendError", 2);
		youtube.comment("y2", "Viewer", "during");
		REQUIRE(recorder.waitFor(ChatState::Waiting));
		REQUIRE(recorder.waitForEvents(2));
		CHECK(recorder.last().state == ChatState::Connected);
		youtube.comment("y3", "Viewer", "after");
		REQUIRE(recorder.waitForEvents(3));

		const std::vector<ChatEvent> events = recorder.allEvents();
		REQUIRE(events.size() == 3);
		CHECK(events[0].id == "y1");
		CHECK(events[1].id == "y2");
		CHECK(events[2].id == "y3");
		// The stream was found once. A failed page does not start over.
		CHECK(youtube.videoRequests() == 1);
	}

	TEST_CASE("the pause between requests is never shorter than the one that was set")
	{
		FakeYouTube youtube;
		youtube.pollingIntervalMs = 10; // YouTube would allow a request every 10 ms
		YouTubeChatConfig config = youtubeConfig(youtube);
		config.minPollMs = 250;
		Recorder recorder;
		YouTubeChat chat(recorder.callbacks());
		chat.start(config);
		REQUIRE(recorder.waitFor(ChatState::Connected));

		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		// At most one request at the start and one every 250 ms after it.
		CHECK(youtube.messageRequests() <= 6);
		CHECK(youtube.messageRequests() >= 2);
	}

	TEST_CASE("YouTube's own pause is kept when it is the longer one")
	{
		FakeYouTube youtube;
		youtube.pollingIntervalMs = 400;
		YouTubeChatConfig config = youtubeConfig(youtube);
		config.minPollMs = 20;
		Recorder recorder;
		YouTubeChat chat(recorder.callbacks());
		chat.start(config);
		REQUIRE(recorder.waitFor(ChatState::Connected));

		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		CHECK(youtube.messageRequests() <= 4);
	}

	TEST_CASE("stop ends the reading at once, also in the middle of a pause")
	{
		FakeYouTube youtube;
		youtube.pollingIntervalMs = 60000;
		Recorder recorder;
		YouTubeChat chat(recorder.callbacks());
		chat.start(youtubeConfig(youtube));
		REQUIRE(recorder.waitFor(ChatState::Connected));
		REQUIRE(waitUntil([&] { return youtube.messageRequests() >= 1; }));

		const auto started = Clock::now();
		chat.stop();
		CHECK(msSince(started) < 1000);
		CHECK(recorder.last().state == ChatState::Off);
		CHECK_FALSE(chat.running());
	}
}
