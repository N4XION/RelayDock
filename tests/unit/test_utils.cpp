// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include <doctest/doctest.h>

#include "core/types.h"
#include "utils/clock.h"
#include "utils/i18n.h"
#include "utils/strings.h"
#include "utils/uuid.h"

#include <set>
#include <string>

TEST_SUITE("utils")
{
	TEST_CASE("trim removes surrounding whitespace only")
	{
		CHECK(rd::trim("  a b \t\r\n") == "a b");
		CHECK(rd::trim("") == "");
		CHECK(rd::trim("   ") == "");
		CHECK(rd::trim("x") == "x");
	}

	TEST_CASE("case-insensitive helpers")
	{
		CHECK(rd::toLower("RTMPS://Host") == "rtmps://host");
		CHECK(rd::equalsNoCase("Twitch", "tWITCH"));
		CHECK_FALSE(rd::equalsNoCase("Twitch", "Twitch "));
		CHECK(rd::startsWithNoCase("RTMPS://x", "rtmps://"));
		CHECK_FALSE(rd::startsWithNoCase("rtmp", "rtmps://"));
		CHECK(rd::containsNoCase("Connection REFUSED by host", "refused"));
		CHECK_FALSE(rd::containsNoCase("abc", "abcd"));
	}

	TEST_CASE("split and join")
	{
		CHECK(rd::split("a,b,,c", ',') == std::vector<std::string>{"a", "b", "c"});
		CHECK(rd::split("a,b,,c", ',', true) == std::vector<std::string>{"a", "b", "", "c"});
		CHECK(rd::split("", ',').empty());
		CHECK(rd::join({"a", "b", "c"}, ", ") == "a, b, c");
		CHECK(rd::join({}, ", ") == "");
	}

	TEST_CASE("replaceAll replaces every occurrence and survives overlap")
	{
		CHECK(rd::replaceAll("aaa", "a", "aa") == "aaaaaa");
		CHECK(rd::replaceAll("x-y-z", "-", "") == "xyz");
		CHECK(rd::replaceAll("abc", "", "q") == "abc");
	}

	TEST_CASE("isPrintableNoSpace rejects spaces, control and non-ASCII bytes")
	{
		CHECK(rd::isPrintableNoSpace("live_123_abcDEF-._~"));
		CHECK_FALSE(rd::isPrintableNoSpace("has space"));
		CHECK_FALSE(rd::isPrintableNoSpace("tab\there"));
		CHECK_FALSE(rd::isPrintableNoSpace("caf\xC3\xA9"));
	}

	TEST_CASE("number formatting for the interface")
	{
		CHECK(rd::formatResolution(1920, 1080) == "1920x1080");
		CHECK(rd::formatBitrate(6000) == "6000 Kbps");
		CHECK(rd::formatBitrate(12000) == "12 Mbps");
		CHECK(rd::formatBitrate(12500) == "12.5 Mbps");
		CHECK(rd::formatDuration(754) == "12:34");
		CHECK(rd::formatDuration(3723) == "1:02:03");
		CHECK(rd::formatDuration(-5) == "0:00");
	}

	TEST_CASE("generated UUIDs are valid, version 4 and unique")
	{
		std::set<std::string> seen;
		for (int i = 0; i < 2000; ++i) {
			const std::string id = rd::generateUuid();
			REQUIRE(rd::isUuid(id));
			CHECK(id[14] == '4');
			CHECK((id[19] == '8' || id[19] == '9' || id[19] == 'a' || id[19] == 'b'));
			seen.insert(id);
		}
		CHECK(seen.size() == 2000);
	}

	TEST_CASE("isUuid rejects malformed text")
	{
		CHECK(rd::isUuid("550e8400-e29b-41d4-a716-446655440000"));
		CHECK(rd::isUuid("550E8400-E29B-41D4-A716-446655440000"));
		CHECK_FALSE(rd::isUuid(""));
		CHECK_FALSE(rd::isUuid("550e8400e29b41d4a716446655440000"));
		CHECK_FALSE(rd::isUuid("550e8400-e29b-41d4-a716-44665544000g"));
		CHECK_FALSE(rd::isUuid("550e8400-e29b-41d4-a716-4466554400000"));
		CHECK_FALSE(rd::isUuid("../../etc/passwd-aaaa-bbbb-cccc-dddddddd"));
	}

	TEST_CASE("fake clock advances only when told")
	{
		rd::FakeClock clock;
		CHECK(clock.nowMs() == 0);
		clock.advanceMs(250);
		clock.advanceSeconds(2);
		CHECK(clock.nowMs() == 2250);
	}

	TEST_CASE("steady clock never goes backwards")
	{
		rd::SteadyClock clock;
		const int64_t a = clock.nowMs();
		const int64_t b = clock.nowMs();
		CHECK(b >= a);
	}

	TEST_CASE("UTC timestamp has ISO 8601 shape")
	{
		const std::string stamp = rd::utcTimestampIso8601();
		REQUIRE(stamp.size() == 20);
		CHECK(stamp[4] == '-');
		CHECK(stamp[10] == 'T');
		CHECK(stamp[19] == 'Z');
	}
}

namespace {

// A stand-in for the OBS locale lookup.
bool testTranslator(const char *key, const char **translation)
{
	const std::string name = key;
	if (name == "Test.Hello") {
		*translation = "Hallo";
		return true;
	}
	if (name == "Test.Count") {
		*translation = "{0} von {1}";
		return true;
	}
	if (name == "Test.Broken") {
		*translation = "{0} {1} {2} {";
		return true;
	}
	return false;
}

struct TranslatorGuard {
	TranslatorGuard() { rd::setTranslator(testTranslator); }
	~TranslatorGuard() { rd::setTranslator(nullptr); }
};

} // namespace

TEST_SUITE("utils.i18n")
{
	TEST_CASE("without a translator the English text is used")
	{
		CHECK(rd::loc("Test.Hello", "Hello") == "Hello");
		CHECK(rd::locf("Test.Count", "{0} of {1}", 2, 5) == "2 of 5");
	}

	TEST_CASE("a translation replaces the English text")
	{
		const TranslatorGuard guard;
		CHECK(rd::loc("Test.Hello", "Hello") == "Hallo");
		CHECK(rd::locf("Test.Count", "{0} of {1}", 2, 5) == "2 von 5");
		CHECK(rd::loc("Test.Unknown", "English stays") == "English stays");
	}

	TEST_CASE("a translation with broken placeholders falls back to English instead of failing")
	{
		const TranslatorGuard guard;
		CHECK(rd::locf("Test.Broken", "{0} and {1}", 1, 2) == "1 and 2");
	}

	TEST_CASE("singular and plural wordings are picked by the count")
	{
		CHECK(rd::locn(1, "Test.One", "One point", "Test.Many", "{0} points", 1) == "One point");
		CHECK(rd::locn(3, "Test.One", "One point", "Test.Many", "{0} points", 3) == "3 points");
		CHECK(rd::locn(0, "Test.One", "One point", "Test.Many", "{0} points", 0) == "0 points");
	}
}

TEST_SUITE("core.types")
{
	TEST_CASE("orientation names round-trip")
	{
		rd::Orientation orientation = rd::Orientation::Horizontal;
		CHECK(rd::orientationFromName(rd::orientationName(rd::Orientation::Vertical), orientation));
		CHECK(orientation == rd::Orientation::Vertical);
		CHECK(rd::orientationFromName("horizontal", orientation));
		CHECK(orientation == rd::Orientation::Horizontal);
		CHECK_FALSE(rd::orientationFromName("diagonal", orientation));
	}

	TEST_CASE("aspect ratio text names common sizes the way people expect")
	{
		CHECK(rd::aspectRatioText(1920, 1080) == "16:9");
		CHECK(rd::aspectRatioText(1280, 720) == "16:9");
		CHECK(rd::aspectRatioText(1366, 768) == "16:9");
		CHECK(rd::aspectRatioText(854, 480) == "16:9");
		CHECK(rd::aspectRatioText(1080, 1920) == "9:16");
		CHECK(rd::aspectRatioText(720, 1280) == "9:16");
		CHECK(rd::aspectRatioText(1440, 1080) == "4:3");
		CHECK(rd::aspectRatioText(1080, 1080) == "1:1");
		CHECK(rd::aspectRatioText(1000, 300) == "10:3");
		CHECK(rd::aspectRatioText(0, 1080) == "");
	}

	TEST_CASE("sameAspectRatio separates horizontal from vertical")
	{
		CHECK(rd::sameAspectRatio(1920, 1080, 1280, 720));
		CHECK(rd::sameAspectRatio(1080, 1920, 720, 1280));
		CHECK_FALSE(rd::sameAspectRatio(1920, 1080, 1080, 1920));
		CHECK_FALSE(rd::sameAspectRatio(1920, 1080, 1440, 1080));
		CHECK_FALSE(rd::sameAspectRatio(0, 0, 16, 9));
	}

	TEST_CASE("setting locks: aspect ratio is locked by default, the rest is not")
	{
		rd::SettingLocks locks;
		CHECK(locks.isLocked(rd::LockableSetting::AspectRatio));
		CHECK_FALSE(locks.isLocked(rd::LockableSetting::Resolution));
		CHECK_FALSE(locks.isLocked(rd::LockableSetting::Fps));
		CHECK_FALSE(locks.isLocked(rd::LockableSetting::Bitrate));
		CHECK_FALSE(locks.isLocked(rd::LockableSetting::Encoder));

		locks.setLocked(rd::LockableSetting::Bitrate, true);
		locks.setLocked(rd::LockableSetting::AspectRatio, false);
		CHECK(locks.isLocked(rd::LockableSetting::Bitrate));
		CHECK_FALSE(locks.isLocked(rd::LockableSetting::AspectRatio));
	}
}
