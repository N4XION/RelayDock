// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "utils/uuid.h"

#include <array>
#include <mutex>
#include <random>

namespace rd {

std::string generateUuid()
{
	static std::mutex mutex;
	static std::mt19937_64 engine{std::random_device{}()};

	std::array<unsigned char, 16> bytes{};
	{
		std::lock_guard<std::mutex> lock(mutex);
		for (size_t i = 0; i < bytes.size(); i += 8) {
			const uint64_t value = engine();
			for (size_t j = 0; j < 8; ++j)
				bytes[i + j] = static_cast<unsigned char>((value >> (j * 8)) & 0xFF);
		}
	}

	bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40); // version 4
	bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80); // RFC 4122 variant

	static const char *hex = "0123456789abcdef";
	std::string out;
	out.reserve(36);
	for (size_t i = 0; i < bytes.size(); ++i) {
		if (i == 4 || i == 6 || i == 8 || i == 10)
			out.push_back('-');
		out.push_back(hex[bytes[i] >> 4]);
		out.push_back(hex[bytes[i] & 0x0F]);
	}
	return out;
}

bool isUuid(std::string_view text)
{
	if (text.size() != 36)
		return false;
	for (size_t i = 0; i < text.size(); ++i) {
		const char c = text[i];
		if (i == 8 || i == 13 || i == 18 || i == 23) {
			if (c != '-')
				return false;
		} else {
			const bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
			if (!isHex)
				return false;
		}
	}
	return true;
}

} // namespace rd
