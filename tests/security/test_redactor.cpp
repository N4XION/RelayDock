// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
//
// Every key in this file is made up for the test. None of them belongs to a real account.
#include <doctest/doctest.h>

#include "security/redactor.h"

#include <string>

using rd::Redactor;

namespace {

bool contains(const std::string &haystack, const std::string &needle)
{
	return haystack.find(needle) != std::string::npos;
}

} // namespace

TEST_SUITE("security.redactor")
{
	TEST_CASE("registered secrets are replaced everywhere they appear")
	{
		Redactor redactor;
		const std::string key = "zq81-test-key-NOT-REAL-77";
		redactor.addSecret(key);

		const std::string out = redactor.redact("start " + key + " middle " + key + " end");
		CHECK_FALSE(contains(out, key));
		CHECK(contains(out, "[redacted]"));
		CHECK(contains(out, "start "));
		CHECK(contains(out, " end"));
	}

	TEST_CASE("the percent-encoded form of a secret is replaced too")
	{
		Redactor redactor;
		redactor.addSecret("abc/def+ghi=jk");
		const std::string out = redactor.redact("url has abc%2Fdef%2Bghi%3Djk inside");
		CHECK_FALSE(contains(out, "abc%2Fdef%2Bghi%3Djk"));
	}

	TEST_CASE("a longer secret wins over a shorter secret that is its prefix")
	{
		Redactor redactor;
		redactor.addSecret("prefix-1234");
		redactor.addSecret("prefix-1234-and-the-rest-5678");
		const std::string out = redactor.redact("value prefix-1234-and-the-rest-5678 here");
		CHECK_FALSE(contains(out, "and-the-rest"));
		CHECK_FALSE(contains(out, "5678"));
	}

	TEST_CASE("very short values are not registered")
	{
		Redactor redactor;
		redactor.addSecret("ab");
		CHECK(redactor.secretCount() == 0);
		CHECK(redactor.redact("abacus cab") == "abacus cab");
	}

	TEST_CASE("removed secrets are no longer matched exactly")
	{
		Redactor redactor;
		redactor.addSecret("plainword-secret");
		redactor.removeSecret("plainword-secret");
		CHECK(redactor.secretCount() == 0);
		CHECK(redactor.redact("plainword-secret") == "plainword-secret");
	}

	TEST_CASE("clearSecrets removes every secret")
	{
		Redactor redactor;
		redactor.addSecret("first-secret-value");
		redactor.addSecret("second/secret/value");
		CHECK(redactor.secretCount() >= 2);
		redactor.clearSecrets();
		CHECK(redactor.secretCount() == 0);
	}

	TEST_CASE("URL user info is removed")
	{
		const std::string out = Redactor::redactPatterns("Connecting to rtmp://alice:hunter2pass@example.org/live");
		CHECK_FALSE(contains(out, "alice"));
		CHECK_FALSE(contains(out, "hunter2pass"));
		CHECK(contains(out, "rtmp://[redacted]@example.org/live"));
	}

	TEST_CASE("everything after the RTMP application name is removed")
	{
		CHECK(Redactor::redactPatterns("rtmp://live.example.net/app/my-private-stream-name") ==
		      "rtmp://live.example.net/app/[redacted]");
		CHECK(Redactor::redactPatterns("rtmps://ingest.example.net:443/live2/aaaa/bbbb") ==
		      "rtmps://ingest.example.net:443/live2/[redacted]");
	}

	TEST_CASE("a server URL without a key stays readable")
	{
		CHECK(Redactor::redactPatterns("rtmps://live-api-s.facebook.com:443/rtmp/") ==
		      "rtmps://live-api-s.facebook.com:443/rtmp/");
		CHECK(Redactor::redactPatterns("rtmp://a.rtmp.youtube.com/live2") == "rtmp://a.rtmp.youtube.com/live2");
	}

	TEST_CASE("URL query strings are removed")
	{
		const std::string out =
			Redactor::redactPatterns("rtmps://host.example/rtmp/?s_bl=1&s_token=abcdef123456&a=AbCdEf");
		CHECK_FALSE(contains(out, "abcdef123456"));
		CHECK_FALSE(contains(out, "AbCdEf"));
		CHECK(contains(out, "?[redacted]"));

		const std::string web = Redactor::redactPatterns("GET https://api.example.com/v1/x?access_token=tok_12345678");
		CHECK_FALSE(contains(web, "tok_12345678"));
	}

	TEST_CASE("platform key formats are removed without registration")
	{
		// Twitch format
		CHECK_FALSE(contains(Redactor::redactPatterns("key is live_123456789_AbCdEfGhIjKlMnOpQrStUvWx end"), // NOT-REAL
				     "AbCdEfGhIjKlMnOpQrStUvWx"));
		// YouTube format
		CHECK_FALSE(contains(Redactor::redactPatterns("using ab12-cd34-ef56-gh78-ij90 now"), "cd34-ef56")); // NOT-REAL
		// Facebook format
		CHECK_FALSE(contains(Redactor::redactPatterns("FB-1234567890123456-0-AbCdEfGhIjKlMnOp"), // NOT-REAL
				     "AbCdEfGhIjKlMnOp"));
	}

	TEST_CASE("credential-like name and value pairs are removed")
	{
		CHECK_FALSE(contains(Redactor::redactPatterns("stream key: s3cr3tvalue"), "s3cr3tvalue"));
		CHECK_FALSE(contains(Redactor::redactPatterns("stream_key=s3cr3tvalue&x=1"), "s3cr3tvalue"));
		CHECK_FALSE(contains(Redactor::redactPatterns("{\"key\": \"s3cr3tvalue\"}"), "s3cr3tvalue"));
		CHECK_FALSE(contains(Redactor::redactPatterns("password = hunter2pass"), "hunter2pass"));
		CHECK_FALSE(contains(Redactor::redactPatterns("Authorization: Bearer"), "Bearer"));
		CHECK_FALSE(contains(Redactor::redactPatterns("token=abc.def.ghi"), "abc.def.ghi"));
	}

	TEST_CASE("ordinary text and identifiers survive")
	{
		const std::string uuid = "550e8400-e29b-41d4-a716-446655440000";
		CHECK(Redactor::redactPatterns("destination " + uuid + " started") == "destination " + uuid + " started");
		CHECK(Redactor::redactPatterns("keyint_sec=2 bitrate=6000 preset=veryfast") ==
		      "keyint_sec=2 bitrate=6000 preset=veryfast");
		CHECK(Redactor::redactPatterns("Twitch is live at 1920x1080, 60 FPS") ==
		      "Twitch is live at 1920x1080, 60 FPS");
		CHECK(Redactor::redactPatterns("hotkey: F5") == "hotkey: F5");
	}

	TEST_CASE("redaction is idempotent")
	{
		Redactor redactor;
		redactor.addSecret("zq81-test-key-NOT-REAL-77");
		const std::string once =
			redactor.redact("rtmp://u:p@h.example/app/zq81-test-key-NOT-REAL-77?token=zzzzzz key=abcd1234");
		CHECK(redactor.redact(once) == once);
	}

	TEST_CASE("redactUrl keeps scheme, host, port and application")
	{
		CHECK(Redactor::redactUrl("rtmps://user:pw@ingest.example.com:443/app/secretpath?t=1") ==
		      "rtmps://[redacted]@ingest.example.com:443/app/[redacted]?[redacted]");
	}
}
