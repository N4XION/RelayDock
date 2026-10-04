// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "utils/paths.h"

#include <cstdio>
#include <format>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif

namespace rd {

std::filesystem::path pathFromUtf8(std::string_view utf8)
{
#ifdef _WIN32
	if (utf8.empty())
		return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
	std::wstring wide(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
	return std::filesystem::path(wide);
#else
	return std::filesystem::path(std::string(utf8));
#endif
}

std::string pathToUtf8(const std::filesystem::path &path)
{
#ifdef _WIN32
	const std::wstring &wide = path.native();
	if (wide.empty())
		return {};
	const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
					       nullptr, nullptr);
	std::string utf8(static_cast<size_t>(length), '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), length, nullptr,
			    nullptr);
	return utf8;
#else
	return path.string();
#endif
}

namespace {

FILE *openFile(const std::filesystem::path &path, const wchar_t *wideMode, const char *mode)
{
#ifdef _WIN32
	(void)mode;
	FILE *file = nullptr;
	if (_wfopen_s(&file, path.c_str(), wideMode) != 0)
		return nullptr;
	return file;
#else
	(void)wideMode;
	return std::fopen(path.c_str(), mode);
#endif
}

} // namespace

std::string tempFolderUtf8()
{
	std::error_code error;
	const std::filesystem::path folder = std::filesystem::temp_directory_path(error);
	return error ? std::string() : pathToUtf8(folder);
}

bool readFileToString(const std::filesystem::path &path, std::string &out, size_t maxBytes)
{
	out.clear();

	std::error_code ec;
	const auto size = std::filesystem::file_size(path, ec);
	if (ec || size > maxBytes)
		return false;

	FILE *file = openFile(path, L"rb", "rb");
	if (!file)
		return false;

	out.resize(static_cast<size_t>(size));
	const size_t read = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), file);
	std::fclose(file);

	if (read != out.size()) {
		out.clear();
		return false;
	}
	return true;
}

bool writeFileAtomically(const std::filesystem::path &path, std::string_view content, std::string &error)
{
	error.clear();
	std::error_code ec;

	const std::filesystem::path directory = path.parent_path();
	if (!directory.empty()) {
		std::filesystem::create_directories(directory, ec);
		if (ec) {
			error = std::format("Could not create the folder: {}", ec.message());
			return false;
		}
	}

	std::filesystem::path temporary = path;
	temporary += L".tmp";

	FILE *file = openFile(temporary, L"wb", "wb");
	if (!file) {
		error = "Could not open a temporary file for writing.";
		return false;
	}

	bool ok = content.empty() || std::fwrite(content.data(), 1, content.size(), file) == content.size();
	ok = ok && std::fflush(file) == 0;
#ifdef _WIN32
	// Ask Windows to put the bytes on disk before the rename makes them the real file.
	ok = ok && _commit(_fileno(file)) == 0;
#endif
	ok = (std::fclose(file) == 0) && ok;

	if (!ok) {
		std::filesystem::remove(temporary, ec);
		error = "Could not write the temporary file. The disk may be full.";
		return false;
	}

	std::filesystem::rename(temporary, path, ec);
	if (ec) {
		const std::string reason = ec.message();
		std::filesystem::remove(temporary, ec);
		error = std::format("Could not replace the file: {}", reason);
		return false;
	}
	return true;
}

} // namespace rd
