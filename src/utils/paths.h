// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace rd {

// OBS and RelayDock keep paths as UTF-8. std::filesystem on Windows wants UTF-16. These two
// functions convert between them so a user name with non-English letters keeps working.
std::filesystem::path pathFromUtf8(std::string_view utf8);
std::string pathToUtf8(const std::filesystem::path &path);

// Reads a whole file. Returns false when the file is missing, unreadable or larger than
// `maxBytes`.
bool readFileToString(const std::filesystem::path &path, std::string &out, size_t maxBytes);

// Writes `content` to `path` by way of a temporary file in the same folder, so a crash or a
// power cut never leaves a half-written file behind. Returns false and sets `error` on failure.
bool writeFileAtomically(const std::filesystem::path &path, std::string_view content, std::string &error);

} // namespace rd
