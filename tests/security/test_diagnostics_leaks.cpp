// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
//
// Every key in this file is made up for the test. None of them belongs to a real account.
#include <doctest/doctest.h>

#include "diagnostics/report.h"
#include "security/memory_credential_store.h"
#include "security/secret_vault.h"
#include "test_helpers.h"

#include <memory>
#include <string>

using namespace rd;

namespace {

const char *const kKey = "diag-test-key-4c1d9a7e55b2-NOT-REAL";
const char *const kPassword = "diag-test-password-NOT-REAL";

bool contains(const std::string &text, const std::string &needle)
{
	return text.find(needle) != std::string::npos;
}

// A report input where a secret sits in every free-text field a bug could put it in.
DiagnosticsInput poisonedInput(const std::string &secret)
{
	DiagnosticsInput input;
	input.generatedAtUtc = "2026-10-04T10:00:00Z";
	input.relayDockVersion = "1.0.0 " + secret;
	input.buildDate = secret;
	input.obsVersion = "32.0.4 " + secret;
	input.osVersion = "Windows 11 " + secret;
	input.cpuName = "Test CPU " + secret;
	input.gpus = {"Test GPU " + secret};
	input.context = rdtest::softwareContext(60);
	input.context.videoEncoders[0].displayName = "x264 " + secret;
	input.credentialBackend = "Windows Credential Manager " + secret;
	input.settingsFolder = "C:\\Users\\Morgan Example\\AppData\\Roaming\\obs-studio\\" + secret;
	input.settingsNotes = {"note " + secret};

	DiagnosticsDestination destination;
	destination.config.id = generateUuid();
	destination.config.provider = "custom_rtmp";
	destination.config.name = "My server " + secret;
	destination.config.useAuth = true;
	destination.config.username = "streamer-login-name";
	destination.providerName = "Custom RTMP " + secret;
	destination.serverText = "rtmp://someone:" + secret + "@live.example.net/app/" + secret + "?token=" + secret;
	destination.hasStreamKey = true;
	destination.hasPassword = true;
	destination.phase = "failed " + secret;
	destination.lastStop = "rejected " + secret;
	destination.lastError = "Server said: bad name '" + secret + "'";
	destination.effective.video.encoderId = "obs_x264";
	destination.effective.video.customOptions = secret;
	destination.effective.video.preset = secret;
	destination.effective.video.rateControl = secret;
	destination.effective.audio.encoderId = "ffmpeg_aac";
	destination.effective.notes = {"note " + secret};
	destination.videoEncoderName = "encoder " + secret;
	destination.sharedWith = {"other " + secret};
	destination.issues = {"issue " + secret};
	input.destinations.push_back(destination);

	PreflightItem item;
	item.id = "destination.server";
	item.status = PreflightStatus::Failed;
	item.title = "My server " + secret;
	item.message = {"what " + secret, "detail " + secret, "action " + secret};
	input.preflight.items.push_back(item);
	input.preflight.status = PreflightStatus::Failed;

	input.logLines = {"log line " + secret};
	return input;
}

} // namespace

TEST_SUITE("security.diagnostics")
{
	TEST_CASE("a registered stream key never appears in the report, whatever field carried it")
	{
		Redactor redactor;
		redactor.addSecret(kKey);

		const std::string report = buildDiagnosticsReport(poisonedInput(kKey), redactor);
		CHECK_FALSE(contains(report, kKey));
		CHECK(contains(report, "[redacted]"));
		// The report still has its structure.
		CHECK(contains(report, "RelayDock diagnostics"));
		CHECK(contains(report, "Destinations (1)"));
		CHECK(contains(report, "Key saved:      yes"));
	}

	TEST_CASE("secrets held by the vault are removed from the report")
	{
		Redactor redactor;
		SecretVault vault(std::make_unique<MemoryCredentialStore>(), redactor);
		const std::string id = generateUuid();
		REQUIRE(vault.set({id, CredentialKind::StreamKey}, SecretString(kKey)).result.ok());
		REQUIRE(vault.set({id, CredentialKind::Password}, SecretString(kPassword)).result.ok());

		DiagnosticsInput input = poisonedInput(kKey);
		input.logLines.push_back(std::string("auth failed for password ") + kPassword);
		input.destinations[0].lastError += std::string(" ") + kPassword;

		const std::string report = buildDiagnosticsReport(input, redactor);
		CHECK_FALSE(contains(report, kKey));
		CHECK_FALSE(contains(report, kPassword));
	}

	TEST_CASE("a key that was never registered is still removed from a server address")
	{
		const Redactor redactor; // Knows no secrets
		DiagnosticsInput input;
		DiagnosticsDestination destination;
		destination.config.name = "Custom";
		destination.providerName = "Custom RTMP";
		destination.serverText = "rtmp://user:hunter2-not-real@live.example.net:1935/app/unregistered-key-778899?auth=abc123def456";
		input.destinations.push_back(destination);

		const std::string report = buildDiagnosticsReport(input, redactor);
		CHECK_FALSE(contains(report, "unregistered-key-778899"));
		CHECK_FALSE(contains(report, "hunter2-not-real"));
		CHECK_FALSE(contains(report, "abc123def456"));
		CHECK_FALSE(contains(report, "user:"));
		CHECK(contains(report, "rtmp://live.example.net"));
	}

	TEST_CASE("the report has no field for a secret and never prints the RTMP user name")
	{
		const Redactor redactor;
		DiagnosticsInput input = poisonedInput("harmless-text");
		const std::string report = buildDiagnosticsReport(input, redactor);
		CHECK_FALSE(contains(report, "streamer-login-name"));
		CHECK(contains(report, "user name set"));
		CHECK(contains(report, "password saved"));
	}

	TEST_CASE("the Windows user name is removed from paths")
	{
		CHECK(anonymizePaths("C:\\Users\\Morgan Example\\AppData\\Roaming\\obs-studio") ==
		      "C:\\Users\\<user>\\AppData\\Roaming\\obs-studio");
		CHECK(anonymizePaths("d:/users/someone/Videos/a.png and C:\\Users\\Other\\b.png") ==
		      "d:/users/<user>/Videos/a.png and C:\\Users\\<user>\\b.png");
		CHECK(anonymizePaths("C:\\Program Files\\obs-studio\\bin") == "C:\\Program Files\\obs-studio\\bin");
		CHECK(anonymizePaths("C:\\Users\\sam") == "C:\\Users\\<user>");

		const Redactor redactor;
		const std::string report = buildDiagnosticsReport(poisonedInput("harmless-text"), redactor);
		CHECK_FALSE(contains(report, "Morgan Example"));
		CHECK(contains(report, "C:\\Users\\<user>\\AppData"));
	}

	TEST_CASE("an empty input still produces a readable report")
	{
		const Redactor redactor;
		const std::string report = buildDiagnosticsReport(DiagnosticsInput{}, redactor);
		CHECK(contains(report, "RelayDock diagnostics"));
		CHECK(contains(report, "Destinations (0)"));
		CHECK(contains(report, "Upload speed:            not set"));
	}
}
