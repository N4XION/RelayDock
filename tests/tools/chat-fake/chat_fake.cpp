// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
//
// rd-chat-fake: stand-ins for Twitch and YouTube on this PC, for the integration tests.
//
// It runs the fake platforms of tests/support and a small control server. A test script reads
// the ports from the file this program writes, points RelayDock at them, and then asks the
// control server to make things happen: a viewer writes, a gift arrives, a connection breaks.
//
//   rd-chat-fake.exe <file to write the ports to>
//
// It runs until the control server gets /quit or until the process is ended.
//
// Every key, token and comment here is made up. Nothing leaves this PC.
#include "support/fake_platforms.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <string>

using rdtest::FakeRequest;
using rdtest::FakeResponse;
using Json = nlohmann::json;

namespace {

int numberOf(const FakeRequest &request, const char *name, int fallback)
{
	const std::string value = request.param(name);
	if (value.empty())
		return fallback;
	return std::atoi(value.c_str());
}

FakeResponse ok(const Json &body = Json::object())
{
	return FakeResponse{200, "application/json", body.dump()};
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: rd-chat-fake <file to write the ports to>\n");
		return 2;
	}

	rdtest::FakeTwitch twitch;
	rdtest::FakeYouTube youtube;
	std::atomic<bool> quit{false};

	rdtest::FakeServer control([&](const FakeRequest &request) -> FakeResponse {
		const std::string &path = request.path;

		if (path == "/twitch/chat") {
			twitch.chat(request.param("id"), request.param("author"), request.param("text"), numberOf(request, "bits", 0));
			return ok();
		}
		if (path == "/twitch/notice") {
			twitch.notice(request.param("id"), request.param("type"), request.param("text"));
			return ok();
		}
		if (path == "/twitch/repeat") {
			twitch.repeatLast();
			return ok();
		}
		if (path == "/twitch/drop") {
			twitch.dropConnection();
			return ok();
		}
		if (path == "/twitch/reconnect") {
			twitch.requestReconnect();
			return ok();
		}
		if (path == "/twitch/revoke") {
			twitch.revoke("authorization_revoked");
			return ok();
		}
		if (path == "/twitch/set") {
			// How the next sign-in goes.
			twitch.pendingPolls = numberOf(request, "pending_polls", twitch.pendingPolls);
			twitch.refuseSignIn = numberOf(request, "refuse", twitch.refuseSignIn ? 1 : 0) != 0;
			return ok();
		}
		if (path == "/twitch/forget") {
			// Twitch no longer accepts the saved sign-in, as after 30 days without use.
			twitch.existingSignIn();
			return ok();
		}

		if (path == "/youtube/comment") {
			youtube.comment(request.param("id"), request.param("author"), request.param("text"));
			return ok();
		}
		if (path == "/youtube/superchat") {
			youtube.superChat(request.param("id"), request.param("author"), request.param("amount"), request.param("text"));
			return ok();
		}
		if (path == "/youtube/fail") {
			youtube.failMessages(numberOf(request, "status", 500), request.param("reason"), numberOf(request, "times", 1));
			return ok();
		}
		if (path == "/youtube/offline") {
			youtube.goOffline();
			return ok();
		}
		if (path == "/youtube/live") {
			youtube.live = numberOf(request, "on", 1) != 0;
			return ok();
		}

		if (path == "/state") {
			Json subscriptions = Json::array();
			for (const Json &subscription : twitch.subscriptions())
				subscriptions.push_back(subscription.value("type", std::string()));
			Json twitchPaths = Json::array();
			bool secretSent = false;
			for (const FakeRequest &seen : twitch.requests()) {
				twitchPaths.push_back(seen.method + " " + seen.path);
				secretSent = secretSent || seen.body.find("client_secret") != std::string::npos;
			}
			Json youtubeTargets = Json::array();
			bool keyInAddress = false;
			for (const FakeRequest &seen : youtube.requests()) {
				youtubeTargets.push_back(seen.path);
				keyInAddress = keyInAddress || seen.target.find(youtube.apiKey) != std::string::npos;
			}
			return ok({{"twitch",
				    {{"connections", twitch.connections()},
				     {"moved_connections", twitch.movedConnections()},
				     {"refreshes", twitch.refreshes()},
				     {"token_polls", twitch.tokenPolls()},
				     {"subscriptions", subscriptions},
				     {"requests", twitchPaths},
				     {"client_secret_sent", secretSent}}},
				   {"youtube",
				    {{"video_requests", youtube.videoRequests()},
				     {"message_requests", youtube.messageRequests()},
				     {"page_tokens", youtube.pageTokens()},
				     {"requests", youtubeTargets},
				     {"key_in_address", keyInAddress}}}});
		}
		if (path == "/quit") {
			quit = true;
			return ok();
		}
		return FakeResponse{404, "application/json", "{}"};
	});

	// What a test needs to point RelayDock here. The sign-in already exists, as if the user had
	// signed in on an earlier day.
	const Json ports = {
		{"control", control.url("")},
		{"twitch",
		 {{"auth", twitch.authUrl()},
		  {"api", twitch.apiUrl()},
		  {"eventsub", twitch.eventSubUrl()},
		  {"client_id", twitch.clientId},
		  {"login", twitch.login},
		  {"user_code", twitch.userCode},
		  {"refresh_token", twitch.existingSignIn()}}},
		{"youtube",
		 {{"api", youtube.apiUrl()},
		  {"api_key", youtube.apiKey},
		  {"video_id", youtube.videoId},
		  {"title", youtube.title}}},
	};
	{
		std::ofstream file(argv[1], std::ios::binary | std::ios::trunc);
		file << ports.dump(2);
	}
	std::printf("rd-chat-fake is running. Control: %s\n", control.url("").c_str());
	std::fflush(stdout);

	while (!quit.load())
		Sleep(50);
	return 0;
}
