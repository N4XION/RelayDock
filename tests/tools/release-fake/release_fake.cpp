// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
//
// rd-release-fake: a stand-in for the release pages of the project, on this PC, for
// tests/integration/Test-Update.ps1.
//
//   rd-release-fake <file to write the port to> <installer file> <tag> [<owner>/<name>]
//
// It serves two files the way GitHub serves the files of a release:
//
//   /<owner>/<name>/releases/download/<tag>/SHA256SUMS.txt
//       The checksum list. It names the installer with the checksum of the real file.
//   /<owner>/<name>/releases/download/<tag>/<installer name>
//       Answers with a redirect to /files/<installer name>, as GitHub hands a download on to
//       its file servers.
//   /files/<installer name>
//       The installer.
//
// The test steers it over the same port:
//
//   /control/tamper?on=1   Serve the installer with one byte changed. The checksum list stays.
//   /control/slow?ms=N     Wait N milliseconds before the installer is sent.
//   /control/state         How many times each file was asked for, as JSON.
//   /control/quit          End.
//
// It listens on 127.0.0.1 only and has no TLS. RelayDock accepts such an address in a test
// build and nowhere else.
#include "support/fake_server.h"
#include "update/update_download.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

using rdtest::FakeRequest;
using rdtest::FakeResponse;

namespace {

std::string readFile(const std::string &file)
{
	std::ifstream in(file, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string fileName(const std::string &path)
{
	const size_t slash = path.find_last_of("\\/");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

FakeResponse json(const std::string &body)
{
	FakeResponse response;
	response.body = body;
	return response;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 4) {
		std::fprintf(stderr, "usage: rd-release-fake <file to write the port to> <installer file> <tag> [<owner>/<name>]\n");
		return 2;
	}
	const std::string portFile = argv[1];
	const std::string installer = readFile(argv[2]);
	const std::string name = fileName(argv[2]);
	const std::string tag = argv[3];
	if (installer.empty()) {
		std::fprintf(stderr, "rd-release-fake: cannot read %s\n", argv[2]);
		return 2;
	}

	// The project the build under test belongs to, as "owner/name". RelayDock accepts a
	// download only from the release pages of its own project.
	const std::string repository = argc > 4 ? argv[4] : "N4XION/RelayDock";
	const std::string release = "/" + repository + "/releases/download/" + tag + "/";
	const std::string checksums = rd::sha256Hex(installer) + "  " + name + "\n";

	std::atomic<bool> quit{false};
	std::atomic<bool> tamper{false};
	std::atomic<int> slowMs{0};
	std::atomic<int> listAsked{0};
	std::atomic<int> installerAsked{0};
	std::atomic<int> fileSent{0};

	rdtest::FakeServer *self = nullptr;
	rdtest::FakeServer server([&](const FakeRequest &request) -> FakeResponse {
		const std::string &path = request.path;
		FakeResponse response;
		response.contentType = "application/octet-stream";

		if (path == release + "SHA256SUMS.txt") {
			listAsked++;
			response.body = checksums;
			return response;
		}
		if (path == release + name) {
			installerAsked++;
			response.status = 302;
			response.headers = {{"Location", self->url("/files/" + name)}};
			return response;
		}
		if (path == "/files/" + name) {
			fileSent++;
			response.body = installer;
			if (tamper.load())
				response.body[response.body.size() / 2] = static_cast<char>(response.body[response.body.size() / 2] ^ 0x01);
			response.delayMs = slowMs.load();
			return response;
		}

		if (path == "/control/tamper") {
			tamper = request.param("on") == "1";
			return json("{\"ok\":true}");
		}
		if (path == "/control/slow") {
			slowMs = std::atoi(request.param("ms").c_str());
			return json("{\"ok\":true}");
		}
		if (path == "/control/state") {
			return json("{\"list_asked\":" + std::to_string(listAsked.load()) + ",\"installer_asked\":" +
				    std::to_string(installerAsked.load()) + ",\"file_sent\":" + std::to_string(fileSent.load()) + "}");
		}
		if (path == "/control/quit") {
			quit = true;
			return json("{\"ok\":true}");
		}

		response.status = 404;
		response.contentType = "application/json";
		response.body = "{\"error\":\"not found\"}";
		return response;
	});
	self = &server;

	{
		std::ofstream out(portFile, std::ios::trunc);
		out << server.port() << "\n";
	}
	std::printf("rd-release-fake: %s %s on port %d\n", tag.c_str(), name.c_str(), server.port());
	std::fflush(stdout);

	while (!quit.load())
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	// Let the answer to /control/quit go out.
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	return 0;
}
