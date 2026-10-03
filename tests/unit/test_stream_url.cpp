// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include <doctest/doctest.h>

#include "network/stream_url.h"

#include <string>

using rd::parseStreamUrl;
using rd::StreamUrlProblem;

TEST_SUITE("network.stream_url")
{
	TEST_CASE("a plain RTMP address parses into its parts")
	{
		const auto result = parseStreamUrl("rtmp://live.example.com/app");
		REQUIRE(result.ok());
		CHECK(result.url.scheme == "rtmp");
		CHECK(result.url.host == "live.example.com");
		CHECK(result.url.port == 0);
		CHECK(result.url.effectivePort() == 1935);
		CHECK(result.url.path == "/app");
		CHECK(result.url.query.empty());
		CHECK_FALSE(result.url.tls());
		CHECK_FALSE(result.url.hasUserInfo);
	}

	TEST_CASE("an RTMPS address with a port, a path and a query")
	{
		const auto result = parseStreamUrl("RTMPS://Live-API.Example.com:443/rtmp/?a=1&b=2");
		REQUIRE(result.ok());
		CHECK(result.url.scheme == "rtmps");
		CHECK(result.url.host == "live-api.example.com");
		CHECK(result.url.port == 443);
		CHECK(result.url.effectivePort() == 443);
		CHECK(result.url.path == "/rtmp/");
		CHECK(result.url.query == "a=1&b=2");
		CHECK(result.url.tls());
	}

	TEST_CASE("RTMPS defaults to port 443")
	{
		const auto result = parseStreamUrl("rtmps://ingest.example.net/app");
		REQUIRE(result.ok());
		CHECK(result.url.effectivePort() == 443);
	}

	TEST_CASE("surrounding whitespace is ignored")
	{
		const auto result = parseStreamUrl("  rtmp://host.example/live \r\n");
		REQUIRE(result.ok());
		CHECK(result.url.host == "host.example");
	}

	TEST_CASE("IPv4 and IPv6 hosts")
	{
		auto v4 = parseStreamUrl("rtmp://192.168.1.20:1935/live");
		REQUIRE(v4.ok());
		CHECK(v4.url.host == "192.168.1.20");
		CHECK(v4.url.port == 1935);

		auto v6 = parseStreamUrl("rtmp://[2001:db8::1]:1936/live");
		REQUIRE(v6.ok());
		CHECK(v6.url.host == "2001:db8::1");
		CHECK(v6.url.port == 1936);
		CHECK(v6.url.displayText() == "rtmp://[2001:db8::1]:1936/live");

		auto local = parseStreamUrl("rtmp://localhost/live");
		REQUIRE(local.ok());
		CHECK(local.url.host == "localhost");
	}

	TEST_CASE("user info is detected and never shown")
	{
		const auto result = parseStreamUrl("rtmp://alice:secret@media.example.org:1935/live?token=abc");
		REQUIRE(result.ok());
		CHECK(result.url.hasUserInfo);
		CHECK(result.url.host == "media.example.org");
		const std::string shown = result.url.displayText();
		CHECK(shown == "rtmp://media.example.org:1935/live");
		CHECK(shown.find("alice") == std::string::npos);
		CHECK(shown.find("secret") == std::string::npos);
		CHECK(shown.find("token") == std::string::npos);
	}

	TEST_CASE("invalid addresses name the problem")
	{
		CHECK(parseStreamUrl("").problem == StreamUrlProblem::Empty);
		CHECK(parseStreamUrl("   ").problem == StreamUrlProblem::Empty);
		CHECK(parseStreamUrl("live.example.com/app").problem == StreamUrlProblem::MissingScheme);
		CHECK(parseStreamUrl("://host/app").problem == StreamUrlProblem::MissingScheme);
		CHECK(parseStreamUrl("http://example.com/live").problem == StreamUrlProblem::UnsupportedScheme);
		CHECK(parseStreamUrl("srt://example.com:9000").problem == StreamUrlProblem::UnsupportedScheme);
		CHECK(parseStreamUrl("rtmp://").problem == StreamUrlProblem::MissingHost);
		CHECK(parseStreamUrl("rtmp:///app").problem == StreamUrlProblem::MissingHost);
		CHECK(parseStreamUrl("rtmp://:1935/app").problem == StreamUrlProblem::MissingHost);
		CHECK(parseStreamUrl("rtmp://bad host/app").problem == StreamUrlProblem::Whitespace);
		CHECK(parseStreamUrl("rtmp://host/app\tx").problem == StreamUrlProblem::Whitespace);
		CHECK(parseStreamUrl("rtmp://ho$t/app").problem == StreamUrlProblem::InvalidHost);
		CHECK(parseStreamUrl("rtmp://a..b/app").problem == StreamUrlProblem::InvalidHost);
		CHECK(parseStreamUrl("rtmp://-host/app").problem == StreamUrlProblem::InvalidHost);
		CHECK(parseStreamUrl("rtmp://[2001:db8::1/app").problem == StreamUrlProblem::InvalidHost);
		CHECK(parseStreamUrl("rtmp://host:0/app").problem == StreamUrlProblem::InvalidPort);
		CHECK(parseStreamUrl("rtmp://host:65536/app").problem == StreamUrlProblem::InvalidPort);
		CHECK(parseStreamUrl("rtmp://host:abc/app").problem == StreamUrlProblem::InvalidPort);
		CHECK(parseStreamUrl("rtmp://host:/app").problem == StreamUrlProblem::InvalidPort);
	}

	TEST_CASE("an address longer than the limit is rejected")
	{
		const std::string longUrl = "rtmp://host.example/" + std::string(rd::kMaxStreamUrlLength, 'a');
		CHECK(parseStreamUrl(longUrl).problem == StreamUrlProblem::TooLong);
	}

	TEST_CASE("a host with no path is valid and reports an empty path")
	{
		const auto result = parseStreamUrl("rtmp://host.example");
		REQUIRE(result.ok());
		CHECK(result.url.path.empty());
	}
}
